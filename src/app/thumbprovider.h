// QML 用的缩略图（0.7.0，A-06）：image://thumb/<宽度>/<路径>?v=<版本号>
//   <路径> = 照片库里的相对路径（photos/…，相对当前数据文件夹），或 abs/<百分号编码的绝对路径>（导入确认表里还没复制进来的文件）
//   ?v= 只用来让 QML 重新请求（旋转以后版本号 +1），不参与缓存键（缓存键看文件大小和修改时间）
//   0.11.1：<宽度> = 0 不进缓存，按 Image.sourceSize 解原图（HEIC 看大图用，Qt 自己读不了）；旋转次数记在这里，
//   所有地方（列表、简易面板、计划页）拿到的地址都带上，转完重新取地址就换新图
// 后台最多 2 张同时做（自己的线程池，不占 QML 的全局线程池）。读不了时返回错误，QML 的 Image.status 变成 Error（显示缺图）。
#pragma once

#include <QHash>
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
    static void noteRotated(const QString &file);   // 主线程
    static int pending();                           // 0.12.8 排着队和正在做的（相册显示"正在生成缩略图"）

private:
    data::ThumbCache *m_cache;
    QThreadPool m_pool;
    mutable QMutex m_mutex;
    QString m_dataDir;
    static QHash<QString, int> s_turned;
};

} // namespace tp::app
