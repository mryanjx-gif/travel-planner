#include "datafile.h"

#include "trdata.h"

#include <QStringView>

namespace tp::data {

namespace {

const char kPrefix[] = "window.TRAVEL_DATA";

// 跳过空白和 JS 注释（/* … */、// …）。返回 false = 注释没有结束
bool skipWsAndComments(QStringView s, int &i, bool *sawComment)
{
    const int n = int(s.size());
    while (i < n) {
        const QChar c = s[i];
        if (c.isSpace() || c == QChar(0xFEFF)) { ++i; continue; }
        if (c == u'/' && i + 1 < n && s[i + 1] == u'*') {
            const int end = int(s.indexOf(QLatin1String("*/"), i + 2));
            if (end < 0) return false;
            i = end + 2;
            if (sawComment) *sawComment = true;
            continue;
        }
        if (c == u'/' && i + 1 < n && s[i + 1] == u'/') {
            while (i < n && s[i] != u'\n') ++i;
            if (sawComment) *sawComment = true;
            continue;
        }
        break;
    }
    return true;
}

QString lineInfo(QStringView s, int offset)
{
    int line = 1;
    for (int k = 0; k < offset && k < s.size(); ++k)
        if (s[k] == u'\n') ++line;
    return tp::data::trd("第 %1 行").arg(line);
}

} // namespace

ParsedDataFile parseDataFile(const QByteArray &bytes)
{
    ParsedDataFile r;
    const QString text = QString::fromUtf8(bytes);   // 无效的 UTF-8 字节变成 U+FFFD（浏览器按 UTF-8 读脚本也是这样）
    QStringView s(text);
    int i = 0;
    if (!skipWsAndComments(s, i, &r.hasHeader)) { r.error = tp::data::trd("开头的注释没有结束（缺少 */）"); return r; }
    const QLatin1String prefix(kPrefix);
    if (!s.mid(i).startsWith(prefix)) {
        r.error = tp::data::trd("找不到 window.TRAVEL_DATA =（%1）").arg(lineInfo(s, i));
        return r;
    }
    i += int(prefix.size());
    while (i < s.size() && s[i].isSpace()) ++i;
    if (i >= s.size() || s[i] != u'=') { r.error = tp::data::trd("window.TRAVEL_DATA 后面应该是 =（%1）").arg(lineInfo(s, i)); return r; }
    ++i;
    json::ParseError err;
    int consumed = 0;
    json::Value v = json::parse(s.mid(i), &err, &consumed);
    if (!err.ok) {
        r.error = tp::data::trd("JSON 格式错误：%1（%2）").arg(err.message, lineInfo(s, i + err.offset));
        return r;
    }
    i += consumed;
    if (!skipWsAndComments(s, i, nullptr)) { r.error = tp::data::trd("结尾的注释没有结束"); return r; }
    if (i < s.size() && s[i] == u';') ++i;
    if (!skipWsAndComments(s, i, nullptr)) { r.error = tp::data::trd("结尾的注释没有结束"); return r; }
    if (i < s.size()) {
        r.error = tp::data::trd("数据后面还有别的内容（%1）").arg(lineInfo(s, i));
        return r;
    }
    r.ok = true;
    r.data = std::move(v);
    return r;
}

QString zhCnTimestamp(const QDateTime &t)
{
    const QDate d = t.date();
    const QTime tm = t.time();
    return QStringLiteral("%1/%2/%3 %4:%5:%6")
        .arg(d.year()).arg(d.month()).arg(d.day())
        .arg(tm.hour(), 2, 10, QChar(u'0'))
        .arg(tm.minute(), 2, 10, QChar(u'0'))
        .arg(tm.second(), 2, 10, QChar(u'0'));
}

QByteArray serializeDataFile(const json::Value &data, const QDateTime &savedAt)
{
    // 和 Electron map-app.js serializeData 的注释头逐字一致（第 3 行的"默认 D:/旅行计划数据"也照抄）
    QString out;
    out.reserve(1 << 20);
    out += QStringLiteral(
        "/*\n"
        " * 旅行计划数据文件 —— 由网页自动生成，一般不需要手动改。\n"
        " * 位置：数据文件夹（默认 D:/旅行计划数据）/data.js\n"
        " * 由「旅行计划」桌面 app 读取和保存。\n"
        " *\n"
        " * 照片路径写成 photos/xxx.jpg（相对于本文件所在的资源文件夹），\n"
        " * 不要写成 旅行计划数据/photos/xxx.jpg。\n"
        " *\n"
        " * 最后保存：");
    out += zhCnTimestamp(savedAt);
    out += QStringLiteral("\n */\nwindow.TRAVEL_DATA = ");
    out += json::stringify(data, 2);
    out += QStringLiteral(";\n");
    return out.toUtf8();
}

QByteArray normalizeSavedAt(const QByteArray &bytes)
{
    static const QByteArray tag = QStringLiteral(" * 最后保存：").toUtf8();
    const qsizetype at = bytes.indexOf(tag);
    if (at < 0 || at > 2048) return bytes;   // 只看注释头
    const qsizetype eol = bytes.indexOf('\n', at);
    if (eol < 0) return bytes;
    QByteArray copy = bytes;
    copy.replace(at + tag.size(), eol - at - tag.size(), "<时间>");
    return copy;
}

} // namespace tp::data
