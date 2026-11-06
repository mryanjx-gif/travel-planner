#include "jsonio.h"

#include "trdata.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>

namespace tp::json {

namespace {

constexpr int kMaxDepth = 2000;   // 嵌套层数上限（防止异常文件把栈用完；真实数据不到 10 层）

class Parser
{
public:
    explicit Parser(QStringView t) : s(t), n(int(t.size())) {}

    QStringView s;
    int n;
    int i = 0;
    QString error;
    int errorAt = -1;

    bool fail(const QString &msg, int at = -1)
    {
        if (errorAt < 0) { error = msg; errorAt = at < 0 ? i : at; }
        return false;
    }

    void ws()
    {
        while (i < n) {
            const char16_t c = s[i].unicode();
            if (c == u' ' || c == u'\t' || c == u'\n' || c == u'\r') ++i;
            else break;
        }
    }

    bool value(Value &out, int depth)
    {
        if (depth > kMaxDepth) return fail(tp::data::trd("嵌套层数太多（超过 %1 层）").arg(kMaxDepth));
        ws();
        if (i >= n) return fail(tp::data::trd("文本意外结束，缺少值"));
        const char16_t c = s[i].unicode();
        switch (c) {
        case u'{': return object(out, depth);
        case u'[': return array(out, depth);
        case u'"': { QString str; if (!string(str)) return false; out = Value(str); return true; }
        case u't': return literal(u"true", Value(true), out);
        case u'f': return literal(u"false", Value(false), out);
        case u'n': return literal(u"null", Value(), out);
        default:
            if (c == u'-' || (c >= u'0' && c <= u'9')) return number(out);
            return fail(tp::data::trd("这里不应该出现字符 “%1”").arg(QChar(c)));
        }
    }

    bool literal(const char16_t *word, Value v, Value &out)
    {
        const int len = int(std::char_traits<char16_t>::length(word));
        if (i + len > n || s.mid(i, len) != QStringView(word, len))
            return fail(tp::data::trd("无法识别的内容（应为 true / false / null）"));
        i += len;
        out = std::move(v);
        return true;
    }

    bool object(Value &out, int depth)
    {
        ++i;   // {
        Object obj;
        ws();
        if (i < n && s[i] == u'}') { ++i; out = Value(std::move(obj)); return true; }
        while (true) {
            ws();
            if (i >= n || s[i] != u'"') return fail(tp::data::trd("这里应该是用双引号括起来的键名"));
            QString key;
            if (!string(key)) return false;
            ws();
            if (i >= n || s[i] != u':') return fail(tp::data::trd("键名后面应该是冒号"));
            ++i;
            Value v;
            if (!value(v, depth + 1)) return false;
            obj.set(key, std::move(v));   // 重复的键：取最后一个值、位置不变（和 JS 一样）
            ws();
            if (i < n && s[i] == u',') { ++i; continue; }
            if (i < n && s[i] == u'}') { ++i; break; }
            return fail(tp::data::trd("这里应该是逗号或 }"));
        }
        out = Value(std::move(obj));
        return true;
    }

    bool array(Value &out, int depth)
    {
        ++i;   // [
        Array arr;
        ws();
        if (i < n && s[i] == u']') { ++i; out = Value(std::move(arr)); return true; }
        while (true) {
            Value v;
            if (!value(v, depth + 1)) return false;
            arr.push_back(std::move(v));
            ws();
            if (i < n && s[i] == u',') { ++i; continue; }
            if (i < n && s[i] == u']') { ++i; break; }
            return fail(tp::data::trd("这里应该是逗号或 ]"));
        }
        out = Value(std::move(arr));
        return true;
    }

    static int hexVal(char16_t c)
    {
        if (c >= u'0' && c <= u'9') return c - u'0';
        if (c >= u'a' && c <= u'f') return c - u'a' + 10;
        if (c >= u'A' && c <= u'F') return c - u'A' + 10;
        return -1;
    }

    bool string(QString &out)
    {
        const int start = i;
        ++i;   // "
        // 快速路径：没有转义时整段复制
        int j = i;
        while (j < n) {
            const char16_t c = s[j].unicode();
            if (c == u'"' || c == u'\\' || c < 0x20) break;
            ++j;
        }
        if (j < n && s[j] == u'"') { out = s.mid(i, j - i).toString(); i = j + 1; return true; }
        out.reserve(j - i + 16);
        out.append(s.mid(i, j - i));
        i = j;
        while (true) {
            if (i >= n) return fail(tp::data::trd("字符串没有结束（缺少右引号）"), start);
            const char16_t c = s[i].unicode();
            if (c == u'"') { ++i; return true; }
            if (c < 0x20) return fail(tp::data::trd("字符串里有未转义的控制字符（U+%1）").arg(int(c), 4, 16, QChar(u'0')));
            if (c != u'\\') { out.append(QChar(c)); ++i; continue; }
            if (i + 1 >= n) return fail(tp::data::trd("字符串在转义符处意外结束"));
            const char16_t e = s[i + 1].unicode();
            i += 2;
            switch (e) {
            case u'"': out.append(u'"'); break;
            case u'\\': out.append(u'\\'); break;
            case u'/': out.append(u'/'); break;
            case u'b': out.append(QChar(0x08)); break;
            case u'f': out.append(QChar(0x0C)); break;
            case u'n': out.append(QChar(0x0A)); break;
            case u'r': out.append(QChar(0x0D)); break;
            case u't': out.append(QChar(0x09)); break;
            case u'u': {
                if (i + 4 > n) return fail(tp::data::trd("\\u 后面应该有 4 位十六进制数"));
                int v = 0;
                for (int k = 0; k < 4; ++k) {
                    const int h = hexVal(s[i + k].unicode());
                    if (h < 0) return fail(tp::data::trd("\\u 后面应该有 4 位十六进制数"));
                    v = v * 16 + h;
                }
                i += 4;
                out.append(QChar(char16_t(v)));   // 孤立代理字符也原样放进去（JS 也是这样）
                break;
            }
            default:
                return fail(tp::data::trd("无效的转义符 \\%1").arg(QChar(e)), i - 2);
            }
        }
    }

    bool number(Value &out)
    {
        const int start = i;
        auto digit = [&](int k) { return k < n && s[k] >= u'0' && s[k] <= u'9'; };
        if (s[i] == u'-') ++i;
        if (!digit(i)) return fail(tp::data::trd("负号后面应该是数字"));
        if (s[i] == u'0') ++i;
        else while (digit(i)) ++i;
        if (i < n && s[i] == u'.') {
            ++i;
            if (!digit(i)) return fail(tp::data::trd("小数点后面应该是数字"));
            while (digit(i)) ++i;
        }
        if (i < n && (s[i] == u'e' || s[i] == u'E')) {
            ++i;
            if (i < n && (s[i] == u'+' || s[i] == u'-')) ++i;
            if (!digit(i)) return fail(tp::data::trd("指数部分应该是数字"));
            while (digit(i)) ++i;
        }
        // 数字只含 ASCII，转成窄字符给 from_chars（正确舍入，不受语言区域影响）
        const int len = i - start;
        char small[64];
        std::string big;
        char *buf = small;
        if (len >= int(sizeof small)) { big.resize(size_t(len)); buf = big.data(); }
        for (int k = 0; k < len; ++k) buf[k] = char(s[start + k].unicode());
        double d = 0;
        const char *b = buf, *e = buf + len;
        const bool neg = *b == '-';
        // from_chars 不接受开头的 '+'，但接受 '-'
        auto r = std::from_chars(b, e, d, std::chars_format::general);
        if (r.ec == std::errc::result_out_of_range) {
            d = overflowValue(b, e, neg);
        } else if (r.ec != std::errc() || r.ptr != e) {
            return fail(tp::data::trd("无法解析的数字"), start);
        }
        out = Value(d);
        return true;
    }

    // from_chars 报超出范围时，判断是上溢（±Infinity）还是下溢（±0），和 JS 一样
    static double overflowValue(const char *b, const char *e, bool neg)
    {
        const char *p = b + (neg ? 1 : 0);
        long long mag = 0;   // 第一个非零数字的十进制位置（个位 = 0）
        int intDigits = 0;
        bool seenNonZero = false;
        long long fracLead = 0;
        while (p < e && *p >= '0' && *p <= '9') {
            if (*p != '0' || seenNonZero) { seenNonZero = true; ++intDigits; }
            ++p;
        }
        if (p < e && *p == '.') {
            ++p;
            while (p < e && *p >= '0' && *p <= '9') {
                if (!seenNonZero) { ++fracLead; if (*p != '0') seenNonZero = true; }
                ++p;
            }
        }
        long long ex = 0;
        if (p < e && (*p == 'e' || *p == 'E')) {
            ++p;
            bool eneg = false;
            if (p < e && (*p == '+' || *p == '-')) { eneg = *p == '-'; ++p; }
            while (p < e && *p >= '0' && *p <= '9') { if (ex < 100000000) ex = ex * 10 + (*p - '0'); ++p; }
            if (eneg) ex = -ex;
        }
        mag = (intDigits > 0 ? intDigits - 1 : -fracLead) + ex;
        const double v = mag > 0 ? std::numeric_limits<double>::infinity() : 0.0;
        return neg ? -v : v;
    }
};

void appendHex4(QString &out, char16_t c)
{
    static const char *hex = "0123456789abcdef";
    out.append(u'\\');
    out.append(u'u');
    out.append(QChar(hex[(c >> 12) & 15]));
    out.append(QChar(hex[(c >> 8) & 15]));
    out.append(QChar(hex[(c >> 4) & 15]));
    out.append(QChar(hex[c & 15]));
}

void appendQuoted(QString &out, QStringView s)
{
    out.append(u'"');
    const int n = int(s.size());
    int runStart = 0;
    for (int k = 0; k < n; ++k) {
        const char16_t c = s[k].unicode();
        const bool plain = c >= 0x20 && c != u'"' && c != u'\\' && (c < 0xD800 || c > 0xDFFF);
        if (plain) continue;
        if (c >= 0xD800 && c <= 0xDBFF && k + 1 < n && s[k + 1].unicode() >= 0xDC00 && s[k + 1].unicode() <= 0xDFFF) {
            ++k;   // 成对的代理字符原样输出
            continue;
        }
        out.append(s.mid(runStart, k - runStart));
        runStart = k + 1;
        switch (c) {
        case 0x08: out.append(QLatin1String("\\b")); break;
        case 0x09: out.append(QLatin1String("\\t")); break;
        case 0x0A: out.append(QLatin1String("\\n")); break;
        case 0x0C: out.append(QLatin1String("\\f")); break;
        case 0x0D: out.append(QLatin1String("\\r")); break;
        case u'"': out.append(QLatin1String("\\\"")); break;
        case u'\\': out.append(QLatin1String("\\\\")); break;
        default: appendHex4(out, c); break;   // 其余控制字符、孤立代理字符：\u 小写
        }
    }
    out.append(s.mid(runStart));
    out.append(u'"');
}

class Writer
{
public:
    explicit Writer(int indent) : gap(indent) {}
    int gap;
    QString out;

    void newline(int depth)
    {
        out.append(u'\n');
        for (int k = 0; k < depth * gap; ++k) out.append(u' ');
    }

    void write(const Value &v, int depth)
    {
        switch (v.type()) {
        case Value::Type::Null: out.append(QLatin1String("null")); break;
        case Value::Type::Bool: out.append(v.toBool() ? QLatin1String("true") : QLatin1String("false")); break;
        case Value::Type::Number: out.append(numberToString(v.toNumber())); break;
        case Value::Type::String: appendQuoted(out, v.toString()); break;
        case Value::Type::Array: {
            const Array &a = v.asArray();
            if (a.empty()) { out.append(QLatin1String("[]")); break; }
            out.append(u'[');
            for (size_t k = 0; k < a.size(); ++k) {
                if (k) out.append(u',');
                if (gap) newline(depth + 1);
                write(a[k], depth + 1);
            }
            if (gap) newline(depth);
            out.append(u']');
            break;
        }
        case Value::Type::Object: {
            const auto &m = v.asObject().members();
            if (m.empty()) { out.append(QLatin1String("{}")); break; }
            out.append(u'{');
            for (size_t k = 0; k < m.size(); ++k) {
                if (k) out.append(u',');
                if (gap) newline(depth + 1);
                appendQuoted(out, m[k].first);
                out.append(gap ? QLatin1String(": ") : QLatin1String(":"));
                write(m[k].second, depth + 1);
            }
            if (gap) newline(depth);
            out.append(u'}');
            break;
        }
        }
    }
};

} // namespace

Value parse(QStringView text, ParseError *err, int *consumed)
{
    Parser p(text);
    Value v;
    const bool ok = p.value(v, 0);
    if (err) {
        *err = ParseError{};
        if (!ok) {
            err->ok = false;
            err->message = p.error;
            err->offset = p.errorAt;
            int line = 1, col = 1;
            for (int k = 0; k < p.errorAt && k < p.n; ++k) {
                if (text[k] == u'\n') { ++line; col = 1; } else ++col;
            }
            err->line = line;
            err->column = col;
        }
    }
    if (consumed) *consumed = ok ? p.i : p.errorAt;
    return ok ? v : Value();
}

QString numberToString(double d)
{
    if (!std::isfinite(d)) return QStringLiteral("null");
    if (d == 0) return QStringLiteral("0");   // 也包括 -0
    // 最短往返表示的有效数字和指数（科学计数法形式 "d.ddde±x"）
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);
    *r.ptr = 0;
    const char *p = buf;
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    char digits[32];
    int k = 0;
    while (*p && *p != 'e') { if (*p != '.') digits[k++] = *p; ++p; }
    int e10 = 0;
    if (*p == 'e') e10 = std::atoi(p + 1);
    // 去掉尾部的 0（to_chars 的最短表示不会有，保险起见）
    while (k > 1 && digits[k - 1] == '0') --k;
    const int nExp = e10 + 1;   // ECMAScript 规范里的 n：值 = 0.d1d2…dk × 10^n
    QString out;
    out.reserve(32);
    if (neg) out.append(u'-');
    auto appendDigits = [&](int from, int to) { for (int q = from; q < to; ++q) out.append(QChar(digits[q])); };
    if (k <= nExp && nExp <= 21) {
        appendDigits(0, k);
        for (int q = 0; q < nExp - k; ++q) out.append(u'0');
    } else if (0 < nExp && nExp <= 21) {
        appendDigits(0, nExp);
        out.append(u'.');
        appendDigits(nExp, k);
    } else if (-6 < nExp && nExp <= 0) {
        out.append(QLatin1String("0."));
        for (int q = 0; q < -nExp; ++q) out.append(u'0');
        appendDigits(0, k);
    } else {
        appendDigits(0, 1);
        if (k > 1) { out.append(u'.'); appendDigits(1, k); }
        out.append(u'e');
        const int ex = nExp - 1;
        out.append(ex < 0 ? u'-' : u'+');
        out.append(QString::number(ex < 0 ? -ex : ex));
    }
    return out;
}

QString stringify(const Value &v, int indent)
{
    Writer w(indent < 0 ? 0 : (indent > 10 ? 10 : indent));   // JS 的缩进最多 10 个空格
    w.out.reserve(1 << 16);
    w.write(v, 0);
    return w.out;
}

QString quote(QStringView s)
{
    QString out;
    appendQuoted(out, s);
    return out;
}

} // namespace tp::json
