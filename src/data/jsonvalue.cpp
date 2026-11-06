#include "jsonvalue.h"

#include <cmath>
#include <limits>

namespace tp::json {

// ---------- Object ----------

bool Object::isArrayIndexKey(const QString &key, quint32 *index)
{
    // ECMAScript 的"数组下标"：CanonicalNumericString(key) 是 0 … 2^32−2 之间的整数
    const int n = int(key.size());
    if (n == 0 || n > 10) return false;
    if (n > 1 && key.at(0) == u'0') return false;   // "01" 不是规范写法
    quint64 v = 0;
    for (QChar c : key) {
        if (c < u'0' || c > u'9') return false;
        v = v * 10 + (c.unicode() - u'0');
    }
    if (v > 4294967294ULL) return false;
    if (index) *index = quint32(v);
    return true;
}

int Object::indexOf(const QString &key) const
{
    for (size_t i = 0; i < m_members.size(); ++i)
        if (m_members[i].first == key) return int(i);
    return -1;
}

const Value *Object::find(const QString &key) const
{
    const int i = indexOf(key);
    return i < 0 ? nullptr : &m_members[size_t(i)].second;
}

Value *Object::find(const QString &key)
{
    const int i = indexOf(key);
    return i < 0 ? nullptr : &m_members[size_t(i)].second;
}

void Object::set(const QString &key, Value v)
{
    if (Value *p = find(key)) { *p = std::move(v); return; }
    quint32 idx = 0;
    if (isArrayIndexKey(key, &idx)) {
        // 像下标的键：放在前面那段下标键里，按数值升序
        size_t pos = 0;
        quint32 other = 0;
        while (pos < m_members.size() && isArrayIndexKey(m_members[pos].first, &other) && other < idx) ++pos;
        m_members.insert(m_members.begin() + std::ptrdiff_t(pos), Member(key, std::move(v)));
        return;
    }
    m_members.emplace_back(key, std::move(v));
}

bool Object::remove(const QString &key)
{
    const int i = indexOf(key);
    if (i < 0) return false;
    m_members.erase(m_members.begin() + i);
    return true;
}

// ---------- Value ----------

double Value::jsToNumber() const
{
    switch (m_type) {
    case Type::Null: return 0;
    case Type::Bool: return m_bool ? 1 : 0;
    case Type::Number: return m_num;
    case Type::String: {
        const QString t = m_str.trimmed();
        if (t.isEmpty()) return 0;
        bool ok = false;
        const double d = t.toDouble(&ok);   // QString::toDouble 用 C 语言区域，"4" / "4.5" / "1e3" 都能解析
        return ok ? d : std::numeric_limits<double>::quiet_NaN();
    }
    default: return std::numeric_limits<double>::quiet_NaN();
    }
}

const Array &Value::asArray() const
{
    static const Array empty;
    return (m_type == Type::Array && m_arr) ? *m_arr : empty;
}

const Object &Value::asObject() const
{
    static const Object empty;
    return (m_type == Type::Object && m_obj) ? *m_obj : empty;
}

Array &Value::mutArray()
{
    if (m_type != Type::Array || !m_arr) { *this = Value(Array{}); }
    else if (m_arr.use_count() > 1) m_arr = std::make_shared<Array>(*m_arr);
    return *m_arr;
}

Object &Value::mutObject()
{
    if (m_type != Type::Object || !m_obj) { *this = Value(Object{}); }
    else if (m_obj.use_count() > 1) m_obj = std::make_shared<Object>(*m_obj);
    return *m_obj;
}

const Value &Value::operator[](const QString &key) const
{
    static const Value null;
    if (m_type != Type::Object || !m_obj) return null;
    const Value *p = m_obj->find(key);
    return p ? *p : null;
}

bool Value::operator==(const Value &o) const
{
    if (m_type != o.m_type) return false;
    switch (m_type) {
    case Type::Null: return true;
    case Type::Bool: return m_bool == o.m_bool;
    case Type::Number: return m_num == o.m_num || (std::isnan(m_num) && std::isnan(o.m_num));
    case Type::String: return m_str == o.m_str;
    case Type::Array: return m_arr == o.m_arr || asArray() == o.asArray();
    case Type::Object: {
        if (m_obj == o.m_obj) return true;
        const auto &a = asObject().members(), &b = o.asObject().members();
        return a == b;
    }
    }
    return false;
}

bool Value::sharesWith(const Value &o) const
{
    if (m_type == Type::Array) return m_arr && m_arr == o.m_arr;
    if (m_type == Type::Object) return m_obj && m_obj == o.m_obj;
    return false;
}

} // namespace tp::json
