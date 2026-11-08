#include "aistream.h"

#include "translator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace tp::ai {

QStringList SseParser::feed(const QByteArray &bytes)
{
    QStringList out;
    m_buf += bytes;
    m_buf.replace('\r', QByteArray());
    qsizetype i;
    while ((i = m_buf.indexOf("\n\n")) >= 0) {
        const QByteArray chunk = m_buf.left(i);
        m_buf.remove(0, i + 2);
        QByteArray data;
        for (const QByteArray &line : chunk.split('\n'))
            if (line.startsWith("data:")) data += line.mid(5).trimmed();
        if (!data.isEmpty()) out << QString::fromUtf8(data);
    }
    return out;
}

namespace {

QString normStop(const QString &s)
{
    if (s == QLatin1String("end_turn") || s == QLatin1String("stop") || s == QLatin1String("stop_sequence")) return QStringLiteral("end");
    if (s == QLatin1String("length")) return QStringLiteral("max_tokens");
    if (s == QLatin1String("tool_calls")) return QStringLiteral("tool_use");
    return s;
}

// Anthropic 的 usage：message_start 给输入，message_delta 给累计的输出（有的字段也会再给一遍）；有值就覆盖
void mergeAnthropicUsage(Usage &u, const QJsonObject &o)
{
    auto take = [&](const char *k, qint64 &dst) { if (o.contains(QLatin1String(k))) dst = o.value(QLatin1String(k)).toInteger(); };
    take("input_tokens", u.input);
    take("output_tokens", u.output);
    take("cache_read_input_tokens", u.cacheRead);
    take("cache_creation_input_tokens", u.cacheWrite);
    const QJsonObject st = o.value(QStringLiteral("server_tool_use")).toObject();
    if (st.contains(QStringLiteral("web_search_requests"))) u.searches = st.value(QStringLiteral("web_search_requests")).toInt();
}

QString errMessage(const QJsonValue &e)
{
    if (e.isString()) return e.toString();
    const QJsonObject o = e.toObject();
    return o.value(QStringLiteral("message")).toString(o.value(QStringLiteral("type")).toString());
}

} // namespace

bool StreamState::feedAnthropic(const QString &data)
{
    const QJsonObject ev = QJsonDocument::fromJson(data.toUtf8()).object();
    if (ev.isEmpty()) return false;
    const QString type = ev.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("message_start")) {
        mergeAnthropicUsage(usage, ev.value(QStringLiteral("message")).toObject().value(QStringLiteral("usage")).toObject());
    } else if (type == QLatin1String("content_block_start")) {
        const int idx = ev.value(QStringLiteral("index")).toInt();
        const QJsonObject b = ev.value(QStringLiteral("content_block")).toObject();
        const QString bt = b.value(QStringLiteral("type")).toString();
        while (blocks.size() <= idx) blocks.append(QJsonObject());
        blocks.replace(idx, b);
        if (bt == QLatin1String("text")) text += b.value(QStringLiteral("text")).toString();
        else if (bt == QLatin1String("tool_use") || bt == QLatin1String("server_tool_use")) toolJson.insert(idx, QString());
    } else if (type == QLatin1String("content_block_delta")) {
        const int idx = ev.value(QStringLiteral("index")).toInt();
        const QJsonObject d = ev.value(QStringLiteral("delta")).toObject();
        const QString dt = d.value(QStringLiteral("type")).toString();
        QJsonObject b = idx < blocks.size() ? blocks.at(idx).toObject() : QJsonObject();
        auto add = [&b](const char *k, const QString &v) { b.insert(QLatin1String(k), b.value(QLatin1String(k)).toString() + v); };
        if (dt == QLatin1String("text_delta")) { text += d.value(QStringLiteral("text")).toString(); add("text", d.value(QStringLiteral("text")).toString()); }
        else if (dt == QLatin1String("thinking_delta")) { thinking += d.value(QStringLiteral("thinking")).toString(); add("thinking", d.value(QStringLiteral("thinking")).toString()); }
        else if (dt == QLatin1String("signature_delta")) { signature = d.value(QStringLiteral("signature")).toString(); b.insert(QStringLiteral("signature"), signature); }
        else if (dt == QLatin1String("input_json_delta")) toolJson[idx] += d.value(QStringLiteral("partial_json")).toString();
        else if (dt == QLatin1String("citations_delta")) {
            const QJsonObject cit = d.value(QStringLiteral("citation")).toObject();
            const QString url = cit.value(QStringLiteral("url")).toString();
            if (!url.isEmpty() && !citations.contains(url)) citations << url;
            QJsonArray cs = b.value(QStringLiteral("citations")).toArray();
            cs.append(cit);
            b.insert(QStringLiteral("citations"), cs);
        }
        if (idx < blocks.size()) blocks.replace(idx, b);
    } else if (type == QLatin1String("content_block_stop")) {
        const int idx = ev.value(QStringLiteral("index")).toInt();
        if (toolJson.contains(idx) && idx < blocks.size()) {
            QJsonObject b = blocks.at(idx).toObject();
            const QString raw = toolJson.value(idx).trimmed();
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson((raw.isEmpty() ? QStringLiteral("{}") : raw).toUtf8(), &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) badTools.insert(idx);
            b.insert(QStringLiteral("input"), doc.object());
            blocks.replace(idx, b);
            if (b.value(QStringLiteral("type")).toString() == QLatin1String("tool_use")) doneTools << idx;
        }
    } else if (type == QLatin1String("message_delta")) {
        mergeAnthropicUsage(usage, ev.value(QStringLiteral("usage")).toObject());
        const QString s = ev.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString();
        if (!s.isEmpty()) stop = normStop(s);
    } else if (type == QLatin1String("message_stop")) {
        done = true;
    } else if (type == QLatin1String("error")) {
        error = errMessage(ev.value(QStringLiteral("error")));
        if (error.isEmpty()) error = tp::tr("出错了");
    }
    return true;
}

bool StreamState::feedOpenAi(const QString &data)
{
    if (data == QLatin1String("[DONE]")) { done = true; return true; }
    const QJsonObject ev = QJsonDocument::fromJson(data.toUtf8()).object();
    if (ev.isEmpty()) return false;
    if (ev.contains(QStringLiteral("error"))) {
        error = errMessage(ev.value(QStringLiteral("error")));
        if (error.isEmpty()) error = tp::tr("出错了");
        return true;
    }
    const QJsonArray choices = ev.value(QStringLiteral("choices")).toArray();
    QJsonObject us = ev.value(QStringLiteral("usage")).toObject();
    if (!choices.isEmpty()) {
        const QJsonObject c = choices.at(0).toObject();
        const QJsonObject d = c.value(QStringLiteral("delta")).toObject();
        text += d.value(QStringLiteral("content")).toString();
        thinking += d.value(QStringLiteral("reasoning_content")).toString();
        for (const auto &tc : d.value(QStringLiteral("tool_calls")).toArray()) {
            const QJsonObject t = tc.toObject();
            const int idx = t.value(QStringLiteral("index")).toInt();
            const QJsonObject fn = t.value(QStringLiteral("function")).toObject();
            toolJson[idx] += fn.value(QStringLiteral("arguments")).toString();
            if (!t.value(QStringLiteral("id")).toString().isEmpty()) callIds[idx] = t.value(QStringLiteral("id")).toString();
            if (!fn.value(QStringLiteral("name")).toString().isEmpty()) callNames[idx] = fn.value(QStringLiteral("name")).toString();
        }
        const QString fr = c.value(QStringLiteral("finish_reason")).toString();
        if (!fr.isEmpty()) stop = normStop(fr);
        if (us.isEmpty()) us = c.value(QStringLiteral("usage")).toObject();   // Kimi 把 usage 放在 choices[0] 里
    }
    if (!us.isEmpty()) {
        const qint64 prompt = us.value(QStringLiteral("prompt_tokens")).toInteger();
        qint64 hit = us.value(QStringLiteral("prompt_cache_hit_tokens")).toInteger();   // DeepSeek
        if (!hit) hit = us.value(QStringLiteral("prompt_tokens_details")).toObject().value(QStringLiteral("cached_tokens")).toInteger();
        if (!hit) hit = us.value(QStringLiteral("cached_tokens")).toInteger();          // Kimi
        usage.cacheRead = hit;
        usage.input = qMax<qint64>(0, prompt - hit);
        usage.output = us.value(QStringLiteral("completion_tokens")).toInteger();
    }
    return true;
}

QString httpError(int status, const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QString m = errMessage(o.value(QStringLiteral("error")));
    if (m.isEmpty()) m = o.value(QStringLiteral("message")).toString();
    if (status == 401 || status == 403) return tp::tr("钥匙不对或已经失效（%1）。在设置 → AI 里重新填。").arg(status) + (m.isEmpty() ? QString() : QStringLiteral("\n") + m);
    if (status == 429) return tp::tr("请求太多或余额不足（429），等一会儿再试，或者去服务商的网站看看账户。") + (m.isEmpty() ? QString() : QStringLiteral("\n") + m);
    if (status == 0) return tp::tr("连不上服务器，检查一下网络。");
    return QStringLiteral("HTTP %1").arg(status) + (m.isEmpty() ? QString() : QStringLiteral("：") + m);
}

QString mdLite(const QString &s)
{
    // 模型偶尔还是会写 Markdown：标题变成粗体段落，分隔线、表格线去掉；支持 - / • / * / 1. 列表和 **粗体**
    static const QRegularExpression hr(QStringLiteral("^\\s*(-{3,}|\\*{3,}|_{3,})\\s*$"));
    static const QRegularExpression tableSep(QStringLiteral("^\\s*\\|?\\s*:?-{2,}"));
    static const QRegularExpression head(QStringLiteral("^\\s*#{1,6}\\s+(.*)$"));
    static const QRegularExpression row(QStringLiteral("^\\s*\\|.*\\|\\s*$"));
    static const QRegularExpression ul(QStringLiteral("^\\s*[-•*]\\s+(.*)$"));
    static const QRegularExpression ol(QStringLiteral("^\\s*\\d+[.、)]\\s+(.*)$"));
    static const QRegularExpression bold(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    QString out, list;
    auto close = [&] { if (!list.isEmpty()) { out += QStringLiteral("</%1>").arg(list); list.clear(); } };
    QString src = s.toHtmlEscaped();
    src.remove(u'\r');
    for (QString l : src.split(u'\n')) {
        if (hr.match(l).hasMatch()) l.clear();
        else if (tableSep.match(l).hasMatch() && l.contains(u'|')) l.clear();
        else if (auto m = head.match(l); m.hasMatch()) l = QStringLiteral("<b>%1</b>").arg(m.captured(1));
        else if (row.match(l).hasMatch()) {
            QStringList cells;
            for (const QString &c : l.trimmed().split(u'|')) if (!c.trimmed().isEmpty()) cells << c.trimmed();
            l = cells.join(QStringLiteral(" · "));
        }
        const auto mu = ul.match(l), mo = ol.match(l);
        if (mu.hasMatch() || mo.hasMatch()) {
            const QString k = mu.hasMatch() ? QStringLiteral("ul") : QStringLiteral("ol");
            if (list != k) { close(); out += QStringLiteral("<%1>").arg(k); list = k; }
            out += QStringLiteral("<li>%1</li>").arg((mu.hasMatch() ? mu : mo).captured(1));
            continue;
        }
        close();
        if (!l.trimmed().isEmpty()) out += QStringLiteral("<p>%1</p>").arg(l);
    }
    close();
    return out.replace(bold, QStringLiteral("<b>\\1</b>"));
}

} // namespace tp::ai
