// data.js 文件格式：注释头 + `window.TRAVEL_DATA = ` + JSON + `;\n`（阶段 ①，0.3.0）。
// 和 Electron 版 map-app.js `serializeData` 完全一致；读的时候宽松一些（Electron 是把它当脚本执行的）。
#pragma once

#include "jsonio.h"
#include "jsonvalue.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace tp::data {

struct ParsedDataFile
{
    bool ok = false;
    QString error;          // 读不出来时的中文说明（含行号）
    json::Value data;       // window.TRAVEL_DATA 的值（可能不是对象，例如 null）
    bool hasHeader = false; // 开头有没有注释
};

// 读：UTF-8（有 BOM 也接受）；允许前后有注释和空白；`window.TRAVEL_DATA` `=` 一个 JSON 值，可选的 `;`。
// 其他任何写法都当作读不出来（宁可只读，也不冒险覆盖）。
ParsedDataFile parseDataFile(const QByteArray &bytes);

// 写：注释头（最后保存时间用本地时间，格式同 JS toLocaleString('zh-CN')）+ JSON.stringify(data, null, 2)；UTF-8 无 BOM、LF
QByteArray serializeDataFile(const json::Value &data, const QDateTime &savedAt);

// JS 的 new Date().toLocaleString('zh-CN')：2026/1/2 08:05:09（年月日不补零，时分秒补零，24 小时制）
QString zhCnTimestamp(const QDateTime &t);

// 把文件里"最后保存："那一行的时间替换成固定文本，用于比较两份文件除保存时间外是否一致
QByteArray normalizeSavedAt(const QByteArray &bytes);

} // namespace tp::data
