// 行李与待办、紧急信息、游记列表、攻略（0.10.4，P-09、P-10、P-15、P-16、P-17）。只依赖数据层，单元测试 tst_map 共用。
// 照 Electron pages-plan.js（checkList、onSubmit、tpl-add、tpl-save、journalHtml）、travelkit.js（editLocal、editEmergency）、store.js（md、mdHeads、mdToggleTask）。
// 修改只动界面上有的键，别的键原样保留；界面文字由 QML 翻译。
#pragma once

#include "jsonvalue.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace tp::map::plans {

// ---- 行李与待办 ----
// { packing, todos: [{ id, text, done, due, dueState }]（M1：没勾的在前，只是显示顺序）, packDone, todoDone, doneAll, totalAll,
//   dueSoon, dueOver（M2：3 天内到期、已过期的条数，只算计划中的计划）, tplCount, local, emergency }
// dueState：over / soon / 空
QVariantMap kitInfo(const json::Value &root, const json::Value &plan, const QString &today);
// M4：粘贴的多行拆成一条条：去掉空行、行首的 - * • 1. [ ]
QStringList splitLines(const QString &text);
int addChecks(json::Value &plan, const QString &key, const QString &text);    // key = packing / todos；返回加了几条
bool setCheckDone(json::Value &plan, const QString &key, const QString &id, bool done);
bool setCheckDue(json::Value &plan, const QString &id, const QString &due);   // 空 = 去掉；格式不对返回 false
bool removeCheck(json::Value &plan, const QString &key, const QString &id);
const QStringList &defaultPacking();                                          // 默认 11 项
int addFromTemplate(json::Value &root, const QString &planId);                // 加了几条（去重）
int saveTemplate(json::Value &root, const QString &planId);                   // 模板现在有几项；清单是空的时 0、不改

// ---- 紧急信息 ----
// 当地电话：key = 报警 / 急救 / 消防 / 使领馆 / 领事保护 / 其他（存的就是这几个中文词）。别的 label（旧数据导入的一段文字）留着
void setLocalEmergency(json::Value &plan, const QVariantMap &phones);
// { insurer, policyNo, insurerPhone, notes, n0, r0, p0, n1, … }：前 3 个联系人；第 4 个以后和不认识的键留着
void setEmergency(json::Value &root, const QVariantMap &fields);

// ---- 游记 ----
// [{ id, title, date, text（去掉记号的前 120 字）, file（首图的文件，没有时空） }]，新的在前
QVariantList journalList(const json::Value &root, const QString &planId);

// ---- 攻略 ----
// { blocks: [{ type: h1 / h2 / p / li / task / quote / img, html, line（task：原文第几行）, done, file, caption, width, height }],
//   heads: [{ block, level, text }] }。html 已转义，可以直接给 Text.StyledText。photo:<id> 找不到时 img 的 file 是空的
QVariantMap guideBlocks(const json::Value &root, const QString &text);
QString toggleTaskLine(const QString &text, int line, bool on);               // Electron mdToggleTask
void setGuide(json::Value &plan, const QString &text);                        // 全是空白时去掉 guide
// 这个计划的照片（站点顺序，同一站按库里的先后）：[{ id, file, caption, stop }]（插入照片用）
QVariantList planPhotos(const json::Value &root, const QString &planId);

} // namespace tp::map::plans
