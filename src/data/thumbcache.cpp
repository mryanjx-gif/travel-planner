#include "thumbcache.h"

#include "atomicfile.h"
#include "exif.h"
#include "trdata.h"
#include "wicimage.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QThread>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QTransform>

#include <algorithm>

namespace tp::data {

QImage applyOrientation(const QImage &img, int o)
{
    if (img.isNull() || o <= 1 || o > 8) return img;
    const QTransform r90 = QTransform().rotate(90), r180 = QTransform().rotate(180), r270 = QTransform().rotate(270);
    switch (o) {
    case 2: return img.flipped(Qt::Horizontal);
    case 3: return img.transformed(r180);
    case 4: return img.flipped(Qt::Vertical);
    case 5: return img.transformed(r90).flipped(Qt::Horizontal);   // 沿左上–右下对角线翻转
    case 6: return img.transformed(r90);                            // 顺时针 90°
    case 7: return img.transformed(r90).flipped(Qt::Vertical);     // 沿右上–左下对角线翻转
    case 8: return img.transformed(r270);                           // 逆时针 90°
    default: return img;
    }
}

QImage loadOriented(const QString &path, int maxSide, bool toSrgb, QString *error)
{
    const exif::Info info = exif::readFile(path);
    QImage img;
    if (info.container == exif::Container::Heif) {   // 0.11.1：Qt 读不了 HEIC，用 Windows 自带的解码（WIC 自己摆正方向）
        QString why;
        img = platform::wic::decode(path, maxSide, &why);
        if (img.isNull()) {
            if (error) *error = platform::wic::heifReady() ? trd("读不出这张照片：%1").arg(why) : trd("这台电脑没装 HEIF / HEVC 扩展，HEIC 照片显示不了");
            return {};
        }
    } else {
        QImageReader r(path);
        r.setAutoTransform(false);
        r.setAllocationLimit(1024);   // MB；默认 256 MB，大全景照片会被拒绝
        const QSize stored = r.size();
        if (maxSide > 0 && stored.isValid()) {
            const int longSide = std::max(stored.width(), stored.height());
            if (longSide > maxSide) {
                const double k = double(maxSide) / longSide;
                r.setScaledSize(QSize(std::max(1, int(std::lround(stored.width() * k))), std::max(1, int(std::lround(stored.height() * k)))));
            }
        }
        img = r.read();
        if (img.isNull()) {
            if (error) *error = trd("读不出这张照片：%1").arg(r.errorString());
            return {};
        }
        img = applyOrientation(img, info.orientation);   // 按 EXIF 方向摆正
    }
    if (toSrgb && img.colorSpace().isValid() && img.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
        if (img.colorSpace().colorModel() == QColorSpace::ColorModel::Rgb) {
            if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32)
                img.convertTo(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
            img.convertToColorSpace(QColorSpace::SRgb);
        }
    }
    if (maxSide > 0 && std::max(img.width(), img.height()) > maxSide)   // 不支持缩小解码的格式在这里缩
        img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

ThumbCache::ThumbCache(const QString &root) : m_root(root) {}

int ThumbCache::bucket(int requested)
{
    if (requested <= 160) return 160;   // 0.11.1 地图上的照片标记
    if (requested <= 320) return 320;
    if (requested <= 640) return 640;
    return 1280;
}

QString ThumbCache::pathFor(const QString &photoAbs, int width) const
{
    const QFileInfo fi(photoAbs);
    if (!fi.isFile()) return {};
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(QDir::cleanPath(fi.absoluteFilePath()).toLower())
                            .arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(bucket(width));
    const QString h = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
    return m_root + u'/' + h.left(2) + u'/' + h + QStringLiteral(".jpg");
}

QString ThumbCache::ensure(const QString &photoAbs, int width, QString *error)
{
    const QString out = pathFor(photoAbs, width);
    if (out.isEmpty()) { if (error) *error = trd("照片文件找不到"); return {}; }
    {
        QMutexLocker lk(&m_mutex);
        while (m_busy.contains(out)) m_done.wait(&m_mutex);
        if (QFileInfo::exists(out)) return out;
        m_busy.insert(out);
    }
    auto finish = [this, &out] { QMutexLocker lk(&m_mutex); m_busy.remove(out); m_done.wakeAll(); };

    QString err;
    QImage img = loadOriented(photoAbs, bucket(width), true, &err);
    if (img.isNull()) { finish(); if (error) *error = err; return {}; }
    if (img.hasAlphaChannel()) {   // JPEG 没有透明：铺白底
        QImage bg(img.size(), QImage::Format_RGB32);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, img);
        p.end();
        img = bg;
    }
    // 子文件夹（0.8.6）：几个线程同时建同一个文件夹时 mkpath 偶尔返回失败、文件夹却没建好（验证时 1 / 120 张
    // "系统找不到指定的路径（错误码 3）"，原因推测是这个，不确定）。没建好就稍等再建一次
    const QString dir = QFileInfo(out).absolutePath();
    for (int i = 0; i < 3 && !QDir().mkpath(dir) && !QFileInfo::exists(dir); ++i) QThread::msleep(5);
    const QString tmp = out + QStringLiteral(".tmp-") + QString::number(QDateTime::currentMSecsSinceEpoch());
    // 先编码到内存，再写临时文件、改名（0.7.1：以前直接 QImageWriter(文件)，写完文件还开着，
    // Windows 上改名失败——"缩略图写不进缓存：Unknown error"；Linux 上能改名，云端没发现）
    QByteArray bytes;
    {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        QImageWriter w(&buf, "jpeg");
        w.setQuality(kQuality);
        if (!w.write(img)) {
            finish();
            if (error) *error = trd("缩略图写不进缓存：%1").arg(w.errorString());
            return {};
        }
    }
    platform::WriteResult wr = platform::writeViaTemp(out, tmp, bytes);
    if (!wr.ok && !QFileInfo::exists(dir)) {   // 文件夹被别处删了或刚才没建成：再建一次、再写一次
        QDir().mkpath(dir);
        wr = platform::writeViaTemp(out, tmp, bytes);
    }
    const bool ok = wr.ok;
    if (!ok && error) *error = trd("缩略图写不进缓存：%1").arg(wr.error);
    finish();
    return ok ? out : QString();
}

namespace {
struct CacheFile { QString path; qint64 size; qint64 mtime; };
QList<CacheFile> listCache(const QString &root)
{
    QList<CacheFile> out;
    QDirIterator it(root, { QStringLiteral("*.jpg") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        out.append({ fi.absoluteFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch() });
    }
    return out;
}
} // namespace

qint64 ThumbCache::totalBytes() const
{
    qint64 n = 0;
    for (const CacheFile &f : listCache(m_root)) n += f.size;
    return n;
}

int ThumbCache::fileCount() const { return int(listCache(m_root).size()); }

int ThumbCache::prune(qint64 maxBytes, qint64 targetBytes)
{
    QList<CacheFile> files = listCache(m_root);
    qint64 total = 0;
    for (const CacheFile &f : files) total += f.size;
    if (total <= maxBytes) return 0;
    std::sort(files.begin(), files.end(), [](const CacheFile &a, const CacheFile &b) { return a.mtime < b.mtime; });
    int n = 0;
    for (const CacheFile &f : files) {
        if (total <= targetBytes) break;
        QMutexLocker lk(&m_mutex);
        if (m_busy.contains(f.path)) continue;
        if (QFile::remove(f.path)) { total -= f.size; ++n; }
    }
    return n;
}

bool ThumbCache::clearToTrash(QString *pathInTrash)
{
    if (!QFileInfo::exists(m_root)) return true;
    QFile d(m_root);
    const bool ok = d.moveToTrash();   // 进不了回收站时失败，不会永久删除（photolib.h）
    if (ok && pathInTrash) *pathInTrash = d.fileName();
    QDir().mkpath(m_root);
    return ok;
}

} // namespace tp::data
