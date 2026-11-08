// AI 改计划的工具（0.13.1，I-03 / I-04 / I-10；Electron assistant.js 的 READ_TOOL / RESEARCH_TOOL / TOOL、planForAi、prepareProposal、locate、applyProposal）。
// 工具定义只写一处（Anthropic 的形状），两种接法各自转换；联网搜索各家接法不同（技术选型.md §11）：
//   Claude 服务器工具 web_search（max_uses 3）、千问 enable_search（次数由服务器定）、Kimi 内置 $web_search（每次请求最多回传 3 次）；
//   DeepSeek、豆包（Chat Completions 里联网插件和函数调用不能同时用）、自定义没有 → 回复下标"没有联网核实"。
// 修改预览（P）是 QJsonObject，存进对话历史：{ toolId, summary, state, applied, changes: [{ c, on, bad, key, isNew, stop, item, toStop, after, obj, geo }], gaps, gapState }。
// state：open / applied / undone / skipped / expired / gapsonly / empty / truncated。
// 数据按原生格式（claude/数据格式.md）：站点顺序 = 数组顺序（没有 order，所以不用重排）、交通方式在起点站点的 leg{ mode, path, dist, time, key }、
// 照片在照片库里（plan + stop）。只写格式里已有的字段。都是纯计算，tst_ai 测。
#pragma once

#include "jsonvalue.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

namespace tp::ai::tools {

constexpr int kMaxSearch = 3;   // 每次请求最多搜几次（PO 选 6）
constexpr int kMaxAuto = 12;    // 一次提问里自动续轮最多几轮（Electron MAX_AUTO）

// 这家能不能联网搜索（claude / qwen / kimi）
bool canSearch(const QString &providerId);
QJsonArray defs();                                         // get_plans、research_status、propose_changes
QJsonArray anthropicTools(bool web);                       // + web_search 服务器工具
QJsonArray openAiTools(const QString &providerId, bool web);   // function 形状；Kimi 联网时 + $web_search
// 0.13.2 从订单生成（I-12）：只给 propose_changes；能强制的家强制调用它。
// Claude {type: tool}；DeepSeek / 千问 / 豆包 {type: function}（千问要先关思考，调用方负责）；
// Kimi（一直思考，强制不确定）和自定义（接法不明）返回空 = 不强制，靠提示词要求，没调用时对话里说明
QJsonArray orderTools(bool anthropic, const QString &providerId);
QJsonValue orderChoice(const QString &providerId);

// ---- 上下文（I-10）----
// 发给 AI 的计划：去掉照片、折线、样式、localEmergency；noMoney 时去掉消费、预算、订单号和金额。紧凑 JSON
QString planForAi(const json::Value &root, const json::Value &plan, bool noMoney);
QString plansSummary(const json::Value &root);
// get_plans 的回答；names = 读到的计划名
QString readPlans(const json::Value &root, const QJsonObject &input, bool noMoney, QStringList *names);
// 正在旅行中的一句（"下次出发"那趟今天在日期里）；不在路上返回空。planId 写回在途的计划
QString liveLine(const json::Value &root, const QString &today, QString *planId);

// ---- 预览（I-04）----
bool isDel(const QString &op);
QString opLabel(const QString &op);
// propose_changes 的输入 → 预览；认领每一项的目标，找不到的写 bad、不勾。truncated = 参数没写完（JSON 断了）
QJsonObject prepare(const json::Value &root, const QJsonObject &input, const QString &defPlan, const QString &toolId, bool truncated);
// 给 QML 看的样子（分组、每行的说明、坐标核对、计数）
QVariantMap view(const json::Value &root, const QJsonObject &P);
// add_stop 还在核对坐标的（应用按钮要等）
bool geoBusy(const QJsonObject &P);

struct Applied {
    int n = 0;
    QJsonObject created;      // new_id → 真的 id
    bool needRoute = false;   // 有自驾 / 徒步段要重新算路
    QStringList deleted;      // 删掉的计划 id
};
// 应用勾选的项。顺序照 Electron：先新建计划 → 其余按 AI 给的顺序 → 删除计划放最后。newId(prefix) 生成 id
Applied apply(json::Value &root, const QJsonObject &P, const std::function<QString(const QString &)> &newId);
// 回传给 AI 的结果
QString resultText(const QJsonObject &P, const Applied &a);
// Electron uid：prefix_ + 毫秒的 36 进制 + 5 位随机
QString uid(const QString &prefix);

} // namespace tp::ai::tools
