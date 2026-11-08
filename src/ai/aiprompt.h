// AI 的系统提示词（0.13.0）：放一处，各家共用。0.13.1 加工具说明和计划上下文（I-10）。
// 说话方式照 Electron assistant.js RULES；改计划的部分要等 0.13.1 有了工具再加回来。
#pragma once

#include <QDateTime>
#include <QString>

namespace tp::ai {

QString systemPrompt(const QDateTime &now);

} // namespace tp::ai
