// 照片的 EXIF 和尺寸（0.7.0，自己写，不用第三方库；PO 2026-11-03 同意，理由见 claude/v0.7.0记录.md §2）。
//
// 认得的文件：JPEG（APP1 "Exif"）、PNG（eXIf 块）、WebP（RIFF 的 EXIF 块）、HEIC / HEIF / AVIF（ISOBMFF 里 item_type = Exif 的项目）。
// 按文件头判断格式，不看扩展名。只读文件开头和用得到的几块，不读整张照片。
//
// 读出来的（都可能没有）：
//   - 拍摄时间：DateTimeOriginal → DateTimeDigitized → IFD0 的 DateTime（和 Electron store.js readExif 一样的顺序；年份 0000 当没有）
//   - 时区：OffsetTimeOriginal → OffsetTime → OffsetTimeDigitized；都没有时用 GPS 的 UTC 时间推算（见 tzFromGps）
//   - 位置：纬度、经度（0,0 当没有，GPSStatus = V 当没有；保留 6 位小数，和 Electron 一样）、海拔、拍摄方向
//   - 相机（Make + Model）、镜头（LensModel，没有时 LensMake）、光圈、快门、ISO、焦距、等效焦距、曝光补偿
//   - 方向（Orientation 1–8）
//   - 像素尺寸：从文件本身读（JPEG 的 SOF、PNG 的 IHDR、WebP 的 VP8X / VP8 / VP8L、HEIF 的 ispe + irot），不用 EXIF 里记的数
// 另外记下改方向要用的位置（旋转 JPEG 时只改这 2 个字节，见 photorotate.h）。
//
// 所有读取都先检查范围；坏数据当作没有，不会越界。
#pragma once

#include "jsonvalue.h"

#include <QByteArray>
#include <QString>
#include <cmath>
#include <limits>

class QIODevice;

namespace tp::data::exif {

enum class Container { Unknown, Jpeg, Png, Webp, Heif, Gif, Bmp };

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Info
{
    Container container = Container::Unknown;
    bool hasExif = false;

    int orientation = 0;              // 1–8；0 = 没写
    QString dateTime;                 // "YYYY:MM:DD HH:MM:SS"（原样，按上面的顺序取到的那个）
    QString offsetTime;               // "+09:00"（原样）
    QString gpsDate;                  // "YYYY:MM:DD"（UTC）
    double gpsH = kNaN, gpsM = kNaN, gpsS = kNaN;   // GPS 时间（UTC）
    double lat = kNaN, lng = kNaN, alt = kNaN, dir = kNaN;
    QString make, model, lens;
    double fnumber = kNaN, exposure = kNaN, focal = kNaN, ev = kNaN;
    int focal35 = 0, iso = 0;

    int width = 0, height = 0;        // 文件里存的像素尺寸（还没按方向摆正）
    int exifWidth = 0, exifHeight = 0;   // EXIF 里记的 PixelXDimension / PixelYDimension（文件本身读不到尺寸时才用）
    bool sizeRotated = false;         // HEIF：irot 转了 90° / 270°（width / height 已经换过，下面 displayWidth 不再按 orientation 换）

    // 改方向用（JPEG）：方向值那 2 个字节在文件里的位置、字节序；没有方向标签时为 -1
    qint64 orientationPos = -1;
    bool littleEndian = true;
    // IFD0 末尾"下一个 IFD"指针（指向 IFD1 = 嵌入的小缩略图）在文件里的位置；没有时为 -1
    qint64 nextIfdPos = -1;
    qint64 tiffPos = -1;              // TIFF 头在文件里的位置
    qint64 tiffLen = 0;               // 这段 TIFF 的长度
    qint64 exifSegmentPos = -1;       // JPEG：APP1 段（FF E1）在文件里的位置
    int exifSegmentLen = 0;           // JPEG：整段长度（含 FF E1 和 2 字节长度）

    bool hasGps() const { return std::isfinite(lat) && std::isfinite(lng); }
    // 方向 5–8 是转了 90°：摆正后宽高互换
    bool swapsAxes() const { return !sizeRotated && orientation >= 5 && orientation <= 8; }
    int displayWidth() const { return swapsAxes() ? height : width; }
    int displayHeight() const { return swapsAxes() ? width : height; }
};

// 读文件。读不了 / 不认识的格式返回 container = Unknown（其余字段为空）
Info readFile(const QString &path);
Info readDevice(QIODevice *dev);
Info readBytes(const QByteArray &bytes);   // 测试用

// 只按文件头判断格式（前 16 字节就够）
Container sniff(const QByteArray &head);
QString containerName(Container c);   // "jpeg" / "png" / "webp" / "heif" / "gif" / "bmp" / ""

// 解析一段 TIFF（"II*\0" / "MM\0*" 开头），结果填进 info；tiffPos = 这段在文件里的位置（记位置用）
void parseTiff(const QByteArray &tiff, qint64 tiffPos, Info &info);

// 当地拍摄时间 "YYYY-MM-DDTHH:MM"（照片记录的 takenAt）；没有返回空
QString takenAt(const Info &info);
// 时区 "+09:00"：EXIF 里有就用（规范成 ±HH:MM），否则用 GPS 的 UTC 时间推算；没有返回空
QString timeZone(const Info &info);
// 用拍摄时间（当地）和 GPS 时间（UTC）推算时区：差值取最近的 15 分钟，超过 ±14 小时不算。不能推算返回空
QString tzFromGps(const QString &dateTime, const QString &gpsDate, double h, double m, double s);
// 相机名：Make + Model，型号里已经带品牌名时不重复（"Apple" + "iPhone 15 Pro" → "Apple iPhone 15 Pro"；
// "Canon" + "Canon EOS R5" → "Canon EOS R5"；"NIKON CORPORATION" + "NIKON Z 6" → "NIKON Z 6"）
QString cameraName(const QString &make, const QString &model);

// 照片记录里的 EXIF 字段（claude/数据格式.md §3 照片），只放有的：
//   takenAt, tz, lat, lng, alt, dir, width, height, camera, lens, exposure{ f, t, iso, fl, fl35, ev }
// width / height 是摆正以后的尺寸
json::Object photoFields(const Info &info);

} // namespace tp::data::exif
