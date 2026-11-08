// AI 的系统提示词（0.13.0；0.13.1 加工具说明和计划上下文，I-10）：放一处，各家共用。照 Electron assistant.js RULES / systemBlocks / contextText。
// 分两段：head = 规则 + 所有计划的简表（不常变，Claude 打缓存标记）；tail = 现在的情况 + 当前计划的完整内容（常变）。
#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace tp::ai {

QString systemHead(const QString &plansSummaryJson);
// page：用户正在看的页面（中文）；planName / planId：当前计划（没有 = 空）；live：在途的一句（没有 = 空）；
// web：这次能不能联网（canWeb = 这家有联网能力）；planJson：当前计划的完整内容（空 = 没选中）
QString systemTail(const QDateTime &now, const QString &page, const QString &planName, const QString &planId, const QString &live,
                   bool web, bool canWeb, const QString &planJson);

// 0.13.2 引导式规划（整页向导）：表单 → 发给 AI 的一条消息（text，中文，含怎么做）+ 对话里显示的（shown，界面语言）。
// form：from、to、start、end、days、people、budget、pace（slow / normal / fast）、interests（文字列表）、must、avoid；空的 = 跳过了。cur = 主币种
struct WizardMsg { QString text, shown; };
WizardMsg wizardMessage(const QVariantMap &form, const QString &cur);
// 0.13.2 从订单生成（I-12）：system 后半段加的任务说明，和用户消息的开头
QString orderTask(const QString &planId);
QString orderMessage(const QString &pasted);

} // namespace tp::ai
