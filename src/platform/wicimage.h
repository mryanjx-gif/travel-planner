// 用 Windows 自带的图像解码（WIC）读 HEIC / HEIF（0.11.1，PO 选 6A）。不带任何库：解码器是系统的，
// 要这台电脑装了 Microsoft Store 里的"HEIF 图像扩展"和"HEVC 视频扩展"；原文件不动。
// WIC 自己按文件里的 irot 摆正（实测，见 v0.11.1记录.md），所以这里不再转方向。
#pragma once

#include <QImage>
#include <QString>

namespace tp::platform::wic {

// 解码。maxSide > 0 时长边缩到不超过它。读不出时返回空图，error 写 WIC 的错误码
QImage decode(const QString &path, int maxSide, QString *error = nullptr);
// 这台电脑能不能解 HEIC：解一张内置的小样本（HEIF 和 HEVC 两个扩展都要有才行），结果记住
bool heifReady();
// 自检用：假装没装扩展（开发机装了，要测提示这条路）
void setTestOff(bool off);

} // namespace tp::platform::wic
