#include "thumbprovider.h"

#include "thumbcache.h"

#include <QFileInfo>
#include <QImage>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QUrl>

namespace tp::app {

namespace {

// 后台做缩略图；做完发信号给 Response（Response 被 QML 提前丢掉时信号自动断开，不会访问已删除的对象）
class Job : public QObject, public QRunnable
{
    Q_OBJECT
public:
    Job(data::ThumbCache *cache, const QString &path, int width) : m_cache(cache), m_path(path), m_width(width) {}
    void run() override
    {
        QString err;
        QImage img;
        const QString file = m_path.isEmpty() ? QString() : m_cache->ensure(m_path, m_width, &err);
        if (!file.isEmpty()) img = QImage(file);
        if (img.isNull() && err.isEmpty()) err = QStringLiteral("thumbnail failed");
        emit done(img, err);
    }
signals:
    void done(const QImage &img, const QString &error);
private:
    data::ThumbCache *m_cache;
    QString m_path;
    int m_width;
};

class Response : public QQuickImageResponse
{
public:
    explicit Response(Job *job)
    {
        connect(job, &Job::done, this, [this](const QImage &img, const QString &err) {
            m_image = img;
            m_error = img.isNull() ? err : QString();
            emit finished();
        }, Qt::QueuedConnection);
    }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }

private:
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

QQuickImageResponse *ThumbProvider::requestImageResponse(const QString &id, const QSize &)
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
    auto *job = new Job(m_cache, path, width > 0 ? width : 320);   // QRunnable 默认做完自动删除
    auto *r = new Response(job);
    m_pool.start(job);
    return r;
}

QString ThumbProvider::urlFor(const QString &relOrAbs, int width, int version)
{
    const bool rel = relOrAbs.startsWith(QLatin1String("photos/"));
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(relOrAbs, "/"));
    return QStringLiteral("image://thumb/%1/%2%3?v=%4").arg(data::ThumbCache::bucket(width)).arg(rel ? QString() : QStringLiteral("abs/"), enc).arg(version);
}

} // namespace tp::app

#include "thumbprovider.moc"
