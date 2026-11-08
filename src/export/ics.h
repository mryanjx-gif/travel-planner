// 日历文件 .ics（0.14.0，E-04；照 Electron travelkit.js icsEsc / fold / ics）。
// 计划全天、交通（浮动时间，没写结束按 +1 小时）、住宿全天、每天的日程；RFC 5545 转义、每行 75 字节折行、CRLF、UID `<id>@travelplanner`。
// 不加提醒（PO 定）。固定文字（类型、"订单号："）按界面语言（tp::tr），用户内容原样。
#pragma once

#include "jsonvalue.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace tp::exporter {

QString icsEscape(const QString &s);   // \ ; , 换行
QString icsFold(const QString &line);  // 超过 75 个 UTF-8 字节时折行，续行以空格开头（按码点切，不切开一个字）

struct IcsResult {
    QByteArray text;   // UTF-8，CRLF
    int count = 0;     // 事件数（0 = 没东西可导出）
};
// nowUtc：DTSTAMP 用（测试传固定值）
IcsResult ics(const json::Value &plan, const QDateTime &nowUtc);

} // namespace tp::exporter
