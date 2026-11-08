// AI 的系统提示词（0.13.0；0.13.1 加工具说明和计划上下文，I-10）：放一处，各家共用。照 Electron assistant.js RULES / systemBlocks / contextText。
// 分两段：head = 规则 + 所有计划的简表（不常变，Claude 打缓存标记）；tail = 现在的情况 + 当前计划的完整内容（常变）。
#pragma once

#include <QDateTime>
#include <QString>

namespace tp::ai {

QString systemHead(const QString &plansSummaryJson);
// page：用户正在看的页面（中文）；planName / planId：当前计划（没有 = 空）；live：在途的一句（没有 = 空）；
// web：这次能不能联网（canWeb = 这家有联网能力）；planJson：当前计划的完整内容（空 = 没选中）
QString systemTail(const QDateTime &now, const QString &page, const QString &planName, const QString &planId, const QString &live,
                   bool web, bool canWeb, const QString &planJson);

} // namespace tp::ai
