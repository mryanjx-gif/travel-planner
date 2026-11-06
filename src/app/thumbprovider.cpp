#include "thumbprovider.h"

#include "thumbcache.h"

#include <QFileInfo>
#include <QImage>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <memory>

namespace tp::app {

namespace {

// 后台做缩略图；做完发信号给 Response（Response 被 QML 提前丢掉时信号自动断开，不会访问已删除的对象）
// 0.11.1：width = 0 不进缓存、按请求的大小解原图（HEIC 看大图）；滚走了的（cancel）排到时直接跳过，快速滚过几千张时不堵后面的
class Job : public QObject, public QRunnable
{
    Q_OBJECT
public:
    Job(data::ThumbCache *cache, const QString &path, int width, const QSize &requested, std::shared_ptr<std::atomic_bool> cancelled)
        : m_cache(cache), m_path(path), m_width(width), m_requested(requested), m_cancelled(std::move(cancelled)) {}
    void run() override
    {
        QString err;
        QImage img;
        if (m_cancelled->load()) err = QStringLiteral("cancelled");
        else if (m_width == 0) {
            if (!m_path.isEmpty()) img = data::loadOriented(m_path, m_requested.isValid() ? std::max(m_requested.width(), m_requested.height()) : 0, true, &err);
        } else {
            const QString file = m_path.isEmpty() ? QString() : m_cache->ensure(m_path, m_width, &err);
            if (!file.isEmpty()) img = QImage(file);
        }
        if (img.isNull() && err.isEmpty()) err = QStringLiteral("thumbnail failed");
        if (img.isNull() && !m_cancelled->load()) qInfo("缩略图读不出：%s（%s）", qUtf8Printable(m_path), qUtf8Printable(err));
        emit done(img, err);
    }
signals:
    void done(const QImage &img, const QString &error);
private:
    data::ThumbCache *m_cache;
    QString m_path;
    int m_width;
    QSize m_requested;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

class Response : public QQuickImageResponse
{
public:
    Response(Job *job, std::shared_ptr<std::atomic_bool> cancelled) : m_cancelled(std::move(cancelled))
    {
        connect(job, &Job::done, this, [this](const QImage &img, const QString &err) {
            m_image = img;
            m_error = img.isNull() ? err : QString();
            emit finished();
        }, Qt::QueuedConnection);
    }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
    void cancel() override { m_cancelled->store(true); }   // 还是要等 Job 发 done：engine 收到 finished 才清理

private:
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QImage m_image;
    QString m_error;
};

} // namespace

ThumbProvider::ThumbProvider(data::ThumbCache *cache) : m_cache(cache)
{
    m_pool.setMaxThreadCount(2);
}

ThumbProvider::~ThumbProvider()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void ThumbProvider::setDataDir(const QString &dir)
{
    QMutexLocker lk(&m_mutex);
    m_dataDir = dir;
}

QString ThumbProvider::dataDir() const
{
    QMutexLocker lk(&m_mutex);
    return m_dataDir;
}

QQuickImageResponse *ThumbProvider::requestImageResponse(const QString &id, const QSize &requested)
{
    // id = "<宽度>/<路径>?v=…"
    QString rest = id.section(u'?', 0, 0);
    const int width = rest.section(u'/', 0, 0).toInt();
    rest = rest.section(u'/', 1);
    QString path;
    if (rest.startsWith(QLatin1String("abs/"))) path = QUrl::fromPercentEncoding(rest.mid(4).toUtf8());
    else {
        const QString rel = QUrl::fromPercentEncoding(rest.toUtf8());
        const QString dir = dataDir();
        if (!dir.isEmpty() && rel.startsWith(QLatin1String("photos/")) && !rel.contains(QLatin1String(".."))) path = dir + u'/' + rel;
    }
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto *job = new Job(m_cache, path, width, requested, cancelled);   // QRunnable 默认做完自动删除
    auto *r = new Response(job, cancelled);
    m_pool.start(job);
    return r;
}

QHash<QString, int> ThumbProvider::s_turned;

void ThumbProvider::noteRotated(const QString &file) { ++s_turned[file]; }

QString ThumbProvider::urlFor(const QString &relOrAbs, int width, int version)
{
    const bool rel = relOrAbs.startsWith(QLatin1String("photos/"));
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(relOrAbs, "/"));
    // 0.11.1：不能用 .arg 拼：enc 里的 %3A 会被后面的 .arg 当成占位符（C:/… 变成 C0A/…，确认表的缩略图一直出不来）
    return QStringLiteral("image://thumb/") + QString::number(width > 0 ? data::ThumbCache::bucket(width) : 0) + u'/' + (rel ? QString() : QStringLiteral("abs/")) + enc
         + QStringLiteral("?v=") + QString::number(version + s_turned.value(relOrAbs));
}

} // namespace tp::app

#include "thumbprovider.moc"
