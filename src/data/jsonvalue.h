// data.js 用的 JSON 值：保持键顺序，行为按 JavaScript 来（阶段 ①，0.3.0）。
//
// 为什么不用 QJsonDocument / QJsonObject：QJsonObject 按键名排序，写回会打乱顺序（技术选型.md §7）。
// 为什么不用 nlohmann::ordered_json：重复键保留第一个（JS 保留最后一个）、大整数按 int64 存（JS 舍入成 double）、
// 不接受孤立代理字符（JS 接受）、像整数的键不排到前面（JS 会）。见 claude/v0.3.0记录.md §2。
//
// 和 JS 一致的规则：
//   - 数字一律是 double；
//   - 字符串是 UTF-16（QString），能放孤立代理字符；
//   - 对象的键顺序 = JS 属性顺序：像数组下标的键（"0"…"4294967294" 的规范十进制写法）按数值升序排在最前，
//     其余按第一次出现的顺序；给已有的键赋值不改变位置。
//
// 容器是写时复制（共享指针 + 修改前分离），复制一整棵树很便宜（以后的撤销 / 重做靠它）。
#pragma once

#include <QString>
#include <memory>
#include <utility>
#include <vector>

namespace tp::json {

class Value;
using Array = std::vector<Value>;

class Object
{
public:
    using Member = std::pair<QString, Value>;

    int size() const { return int(m_members.size()); }
    bool isEmpty() const { return m_members.empty(); }
    bool contains(const QString &key) const { return indexOf(key) >= 0; }
    int indexOf(const QString &key) const;
    const Value *find(const QString &key) const;
    Value *find(const QString &key);
    // JS 的 obj[key] = v：已有的键原位替换；新键按 JS 属性顺序放（像下标的键排前面，其余追加到末尾）
    void set(const QString &key, Value v);
    bool remove(const QString &key);
    const std::vector<Member> &members() const { return m_members; }
    std::vector<Member> &members() { return m_members; }

    static bool isArrayIndexKey(const QString &key, quint32 *index = nullptr);

private:
    std::vector<Member> m_members;
};

class Value
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;                       // null
    Value(std::nullptr_t) {}
    Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    Value(double d) : m_type(Type::Number), m_num(d) {}
    Value(int i) : m_type(Type::Number), m_num(i) {}
    Value(qint64 i) : m_type(Type::Number), m_num(double(i)) {}
    Value(const QString &s) : m_type(Type::String), m_str(s) {}
    Value(const char *s) : m_type(Type::String), m_str(QString::fromUtf8(s)) {}
    Value(Array a) : m_type(Type::Array), m_arr(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : m_type(Type::Object), m_obj(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool toBool() const { return m_type == Type::Bool && m_bool; }
    double toNumber() const { return m_type == Type::Number ? m_num : 0.0; }
    QString toString() const { return m_type == Type::String ? m_str : QString(); }
    // JS 的 ToNumber（只用于 `version >= 4` 这类比较）：字符串按数字解析，null→0，布尔→0/1，其余 NaN
    double jsToNumber() const;

    // 只读访问（类型不对时返回空的静态对象）
    const Array &asArray() const;
    const Object &asObject() const;
    // 可写访问：先分离（写时复制）；类型不对时先变成空数组 / 空对象
    Array &mutArray();
    Object &mutObject();

    // 方便写法：对象里取某个键（没有时返回 null）
    const Value &operator[](const QString &key) const;

    // 深比较（数字按 ==；对象按顺序和内容）
    bool operator==(const Value &o) const;
    bool operator!=(const Value &o) const { return !(*this == o); }

    // 两个值是否共享同一个容器（测试写时复制用）
    bool sharesWith(const Value &o) const;

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_num = 0;
    QString m_str;
    std::shared_ptr<Array> m_arr;
    std::shared_ptr<Object> m_obj;
};

} // namespace tp::json
