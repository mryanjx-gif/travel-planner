// JSON 读写，规则和 JavaScript 的 JSON.parse / JSON.stringify 一致（阶段 ①，0.3.0）。
// 测试基准是 Node 的 JSON.stringify(obj, null, 2)，见 tests/ 和 tools/claude-kit/json-bench/。
#pragma once

#include "jsonvalue.h"

#include <QString>
#include <QStringView>

namespace tp::json {

struct ParseError
{
    bool ok = true;
    QString message;   // 中文说明
    int offset = -1;   // 出错位置（UTF-16 下标，相对于传入的文本）
    int line = 0;      // 1 起
    int column = 0;    // 1 起
};

// 解析一个 JSON 值。consumed 返回解析结束的位置（值后面的空白不算），供 data.js 包装层继续检查后面的内容。
// 和 JS 一致：重复的键取最后一个值、位置在第一次出现处；数字转成 double（溢出 → ±Infinity，写出时变 null）；
// 接受孤立代理字符的 \u 转义；只认 JSON 的四种空白（空格、\t、\n、\r）。
Value parse(QStringView text, ParseError *err = nullptr, int *consumed = nullptr);

// ECMAScript Number::toString（JSON.stringify 用的写法）：最短往返表示；1e21 及以上、1e-7 及以下用指数；-0 写 0。
// 非有限值（NaN、±Infinity）返回 "null"（JSON.stringify 的行为）。
QString numberToString(double d);

// JSON.stringify(value, null, indent)：indent = 0 时是紧凑格式（无空格），否则按空格数缩进
QString stringify(const Value &v, int indent = 2);

// 只把字符串按 JSON.stringify 的规则加引号和转义（测试用）
QString quote(QStringView s);

} // namespace tp::json
