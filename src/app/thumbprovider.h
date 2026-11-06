// QML 用的缩略图（0.7.0，A-06）：image://thumb/<宽度>/<路径>?v=<版本号>
//   <路径> = 照片库里的相对路径（photos/…，相对当前数据文件夹），或 abs/<百分号编码的绝对路径>（导入确认表里还没复制进来的文件）
//   ?v= 只用来让 QML 重新请求（旋转以后版本号 +1），不参与缓存键（缓存键看文件大小和修改时间）
// 后台最多 2 张同时做（自己的线程池，不占 QML 的全局线程池）。读不了时返回错误，QML 的 Image.status 变成 Error（显示缺图）。
#pragma once

#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QString>
#include <QThreadPool>

namespace tp::data { class ThumbCache; }

namespace tp::app {

class ThumbProvider : public QQuickAsyncImageProvider
{
public:
    explicit ThumbProvider(data::ThumbCache *cache);
    ~ThumbProvider() override;

    void setDataDir(const QString &dir);
    QString dataDir() const;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // QML 里用的地址
    static QString urlFor(const QString &relOrAbs, int width, int version);

private:
    data::ThumbCache *m_cache;
    QThreadPool m_pool;
    mutable QMutex m_mutex;
    QString m_dataDir;
};

} // namespace tp::app
