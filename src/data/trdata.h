// 数据层的界面文字（0.5.0）：出错说明等会显示给用户的话，用 tp::data::trd("中文")。
// 和 tp::tr 一样走 QCoreApplication::translate("TP", …)，由 DictTranslator 按 i18n/tr/*.txt 换成英文；
// 数据层只依赖 Qt Core，所以单独放一个头文件，不引用 src/app。
// 缺英文检查（tests/tst_app.cpp allStringsTranslated）也扫描 trd(。
// 注意：写进 data.js 的内容（注释头等）不能用它。
#pragma once

#include <QCoreApplication>
#include <QString>

namespace tp::data {

inline QString trd(const char *zh) { return QCoreApplication::translate("TP", zh); }

} // namespace tp::data
