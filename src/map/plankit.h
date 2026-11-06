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
// 0.11.2 游记页（J-01–J-04）。filter：all / none（没关联计划）/ 计划 id。
// { total, plans: [{ id, name }]（有游记的计划）, hasNone, filterGone, rows: [{ id, title, date, plan, planName, text（110 字）, file }] }，新的在前
QVariantMap journalPage(const json::Value &root, const QString &filter);
// 一篇：{ id, title, date, plan, stop, body, planName, stopName }。没有这篇时为空；计划或站点已经不在时 planName、stopName 是空的
QVariantMap journalEntry(const json::Value &root, const QString &id);
QVariantList journalPlanOptions(const json::Value &root);                     // [{ key, text }]，comparePlans 的顺序
QVariantList journalStopOptions(const json::Value &root, const QString &planId);   // 有效站点 [{ key, text }]
// 新建或修改：只动 title、date、plan、stop、body，别的键留着；plan、stop 空时去掉键。没有这篇、标题和正文都是空的时不新建，返回 false
bool putJournal(json::Value &root, const QString &id, const QVariantMap &fields);
bool removeJournal(json::Value &root, const QString &id);
int dropEmptyJournal(json::Value &root, const QString &keepId);               // 删掉标题和正文都是空的（keepId 除外），返回删了几篇

// ---- 攻略 ----
// { blocks: [{ type: h1 / h2 / p / li / task / quote / img, html, line（task：原文第几行）, done, id, file, caption, width, height }],
//   heads: [{ block, level, text }] }。html 已转义，可以直接给 Text.StyledText。photo:<id> 找不到时 img 的 file 是空的
QVariantMap guideBlocks(const json::Value &root, const QString &text);
QString toggleTaskLine(const QString &text, int line, bool on);               // Electron mdToggleTask
void setGuide(json::Value &plan, const QString &text);                        // 全是空白时去掉 guide
// 这个计划的照片（站点顺序，同一站按库里的先后）：[{ id, file, caption, stop（站名）, stopId, stopNo（第几站，从 1 起）, route, fav, takenAt }]
QVariantList planPhotos(const json::Value &root, const QString &planId);
// 0.11.1 未归类的照片归到计划（A-03，文件不移动）。plan、stop 都是照片记录已有的字段
bool assignPhotos(json::Value &root, const QStringList &ids, const QString &planId, const QString &stopId);
// 照片标签（0.10.5，P-14）。找不到照片 / 计划时返回 false。移除只去掉归属（变成未归类，是封面就连封面去掉），不删文件、不删记录
bool setPhotoFav(json::Value &root, const QString &id, bool on);
bool setPhotoCaption(json::Value &root, const QString &id, const QString &caption);
bool removePhotoFromPlan(json::Value &root, const QString &planId, const QString &id);
bool setPlanCover(json::Value &root, const QString &planId, const QString &id);   // id 空 = 去掉封面

} // namespace tp::map::plans
