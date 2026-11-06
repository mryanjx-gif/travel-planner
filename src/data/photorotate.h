// 旋转照片 90°（0.7.0，功能清单 A-08；PO 2026-11-03：只许 jpg / jpeg / png / webp，JPEG 只改方向标记）。
//
// - 第一次旋转某张照片前，原图原样复制到 photos\.originals\（子路径和照片一样）；以后再转不再覆盖原图。
// - JPEG：只改 EXIF 的方向值（2 个字节），画质无损、立刻完成；XMP 里有 tiff:Orientation 的一起改。
//   没有 EXIF 的 JPEG 在文件开头补一段只含方向的 EXIF；有 EXIF 但没有方向标签的，重新压缩（质量 95），原 EXIF 原样写回、
//   去掉 EXIF 里那张没转的嵌入小图。
// - PNG / WebP：Qt 读 WebP 不处理方向标记，所以重新压缩：PNG 无损；WebP 原来是无损的保持无损，有损的质量 95。
//   原来的 EXIF 写回新文件（方向改成 1，去掉嵌入小图）。色彩配置保留。
// - 写入都先写 <名>.tmp-<毫秒>，再改名覆盖。
#pragma once

#include <QByteArray>
#include <QString>

namespace tp::data {

struct RotateResult
{
    bool ok = false;
    QString error;
    bool backedUp = false;      // 这次第一次备份了原图
    bool lossless = false;      // 只改了方向标记 / 无损重写
    int width = 0, height = 0;  // 转完以后（摆正后）的尺寸
};

// rel = 照片库里的相对路径（photos/…）。turnsCw = 顺时针转几个 90°（1–3）
RotateResult rotatePhoto(const QString &dataDir, const QString &rel, int turnsCw = 1);

// 方向值顺时针再转 turns 个 90° 以后是几（1–8 → 1–8；0 当 1）
int rotateOrientation(int orientation, int turnsCw);

// 下面几个是内部步骤，单独测试用
QByteArray buildMinimalExifApp1(int orientation);                 // FF E1 … "Exif\0\0" + 只有方向的 TIFF
QByteArray pngWithExif(const QByteArray &png, const QByteArray &tiff);    // 在第一个 IDAT 前插入 eXIf 块
QByteArray webpWithExif(const QByteArray &webp, const QByteArray &tiff, int width, int height);  // 加 EXIF 块（简单格式先改成 VP8X）
QByteArray jpegWithApp1(const QByteArray &jpeg, const QByteArray &app1);  // 去掉原来的 Exif APP1，在 SOI / APP0 后插入 app1
// 复制一段 TIFF：方向改成 1、"下一个 IFD"指针清零（去掉嵌入小图）
QByteArray tiffForRewrite(const QByteArray &tiff, qint64 orientationOff, qint64 nextIfdOff, bool littleEndian);

} // namespace tp::data
