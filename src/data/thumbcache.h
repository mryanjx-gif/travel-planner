// 缩略图缓存（0.7.0，功能清单 A-06；需要 Qt Gui 读写图片）。
//
// - 三档宽度 320 / 640 / 1280（长边缩放，不放大），JPEG 质量 82（和 Electron 一样）。
// - 放在缓存目录 thumbs\ab\<sha1>.jpg（不放进数据文件夹）。文件名由"照片绝对路径 + 大小 + 修改时间 + 宽度"算出，
//   照片换了（旋转、被别的程序改过）自动重做；旧的留着，超过上限时按最旧删。
// - 按我们自己读的 EXIF 方向摆正（Qt 读 WebP 时不处理方向，统一自己做）；带色彩配置（如 iPhone 的 Display P3）的转成 sRGB；
//   有透明的铺在白底上。JPEG 读的时候直接缩小解码（QImageReader::setScaledSize）。
// - 缓存上限：超过 1 GB 时删最旧的（按文件修改时间），删到 800 MB（Claude 建议，PO 2026-11-03 同意）。
// - 线程安全：同一张同一档同时只做一次，别的请求等它做完。
#pragma once

#include <QImage>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QWaitCondition>

namespace tp::data {

// 读一张照片，按 EXIF 方向摆正。maxSide > 0 时长边缩到 maxSide 以内（不放大）。
// toSrgb：带色彩配置的转成 sRGB（缩略图要；旋转后写回原格式时不要，保留原来的色彩配置）
QImage loadOriented(const QString &path, int maxSide, bool toSrgb, QString *error = nullptr);
// 按 EXIF 方向（1–8）把图摆正
QImage applyOrientation(const QImage &img, int orientation);

class ThumbCache
{
public:
    static constexpr qint64 kMaxBytes = 1024LL * 1024 * 1024;      // 1 GB
    static constexpr qint64 kTargetBytes = 800LL * 1024 * 1024;    // 删到 800 MB
    static constexpr int kQuality = 82;

    explicit ThumbCache(const QString &root);   // root = <缓存目录>/thumbs
    QString root() const { return m_root; }

    // 请求的宽度 → 档位（≤320 → 320，≤640 → 640，其余 1280）
    static int bucket(int requested);
    // 缓存文件的位置（不管有没有）；照片不存在时返回空
    QString pathFor(const QString &photoAbs, int width) const;
    // 有就直接返回，没有就做一张。失败返回空（error 里写原因）
    QString ensure(const QString &photoAbs, int width, QString *error = nullptr);

    qint64 totalBytes() const;
    int fileCount() const;
    // 超过 maxBytes 时删最旧的，删到 targetBytes。返回删掉的个数
    int prune(qint64 maxBytes = kMaxBytes, qint64 targetBytes = kTargetBytes);
    // 清空：整个文件夹移进回收站（0.12.5，工作规则五）。pathInTrash：回收站里的位置（测试用）
    bool clearToTrash(QString *pathInTrash = nullptr);

private:
    QString m_root;
    QMutex m_mutex;
    QWaitCondition m_done;
    QSet<QString> m_busy;
};

} // namespace tp::data
