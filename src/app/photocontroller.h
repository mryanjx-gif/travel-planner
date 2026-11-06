// 照片（0.7.0，阶段 ① 第 5 部分）：界面和照片库之间的桥（QML 里叫 photos）。
//   - 导入：选文件 → 后台读 EXIF、猜计划和站点、查重复 → 确认表（PhotoImportReview.qml，独立组件，④⑤ 直接用）→ 复制、加记录、立即保存
//   - 旋转（只许 jpg / jpeg / png / webp）
//   - 清理没用的照片（三道保护，见 photolib.h）→ 回收站
//   - 缩略图地址、缓存大小、清空缓存
// 0.11.1：相册的行（album）、看大图的旋转（后台）、HEIC 能不能解（Windows 自带的解码，没装扩展时提示去哪装）。
#pragma once

#include "albummodel.h"
#include "photolib.h"
#include "photorotate.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <atomic>
#include <memory>

namespace tp::data { class ThumbCache; }

namespace tp::app {

class AppController;
class Settings;
class ThumbProvider;

class PhotoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY libraryChanged)
    Q_PROPERTY(int revision READ revision NOTIFY libraryChanged)        // 照片库变了就 +1（QML 重新取列表）
    // 导入："" 没在导入 / "reading" 读拍摄信息 / "review" 等用户确认 / "copying" 复制中
    Q_PROPERTY(QString importStage READ importStage NOTIFY importChanged)
    Q_PROPERTY(int importDone READ importDone NOTIFY importProgress)
    Q_PROPERTY(int importTotal READ importTotal NOTIFY importProgress)
    Q_PROPERTY(QVariantList importItems READ importItems NOTIFY importItemsChanged)
    // 清理："" / "trashing"
    Q_PROPERTY(bool trashRunning READ trashRunning NOTIFY trashChanged)
    Q_PROPERTY(int trashDone READ trashDone NOTIFY trashChanged)
    Q_PROPERTY(int trashTotal READ trashTotal NOTIFY trashChanged)
    // 缩略图缓存
    Q_PROPERTY(double cacheBytes READ cacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(QString cacheDir READ cacheDir CONSTANT)
    Q_PROPERTY(tp::app::AlbumModel *album READ album CONSTANT)          // 0.11.1 相册
    Q_PROPERTY(bool rotating READ rotating NOTIFY rotatingChanged)      // 0.11.1 看大图的旋转在后台做

public:
    PhotoController(AppController *app, data::ThumbCache *cache, ThumbProvider *provider, QObject *parent = nullptr);
    ~PhotoController() override;

    int count() const;
    int revision() const { return m_revision; }
    QString importStage() const { return m_stage; }
    int importDone() const { return m_done; }
    int importTotal() const { return m_total; }
    QVariantList importItems() const;
    bool trashRunning() const { return m_trashThread != nullptr; }
    int trashDone() const { return m_trashDone; }
    int trashTotal() const { return m_trashTotal; }
    double cacheBytes() const { return double(m_cacheBytes); }
    QString cacheDir() const;
    AlbumModel *album() { return &m_album; }
    bool rotating() const { return m_rotateThread != nullptr; }
    void setSettings(Settings *settings);

    // ---- 照片库 ----
    // 照片列表（自检用）：[{ id, file, caption, plan, planName, stop, stopName, takenAt, tz, lat, lng, alt, dir, width, height,
    //   camera, lens, exposure, missing, heic, canRotate, thumb(320 的地址) }]
    Q_INVOKABLE QVariantList library() const;
    Q_INVOKABLE QString thumbUrl(const QString &file, int width) const;
    Q_INVOKABLE QVariantList planPhotos(const QString &planId) const;    // plans::planPhotos + thumb（320）
    Q_INVOKABLE QString fileOf(const QString &photoId) const;            // 封面（照片 id）→ 文件；找不到给空
    // 原图的地址（看大图）；不在 photos/ 下的给空。HEIC 是 image://thumb/0/…（Qt 读不了，走 WIC）；转过的带 ?r=，换地址才重新读
    Q_INVOKABLE QString fileUrl(const QString &file) const;
    Q_INVOKABLE bool heicReady() const;                                  // 这台电脑能解 HEIC（HEIF + HEVC 扩展都在）
    Q_INVOKABLE bool isHeic(const QString &file) const;
    Q_INVOKABLE void openStore(const QString &what);                     // 打开 Microsoft Store 的扩展页：heif / hevc
    Q_INVOKABLE void setHeicTestOff(bool off);                           // 自检：假装没装扩展
    Q_INVOKABLE bool assign(const QStringList &ids, const QString &planId, const QString &stopId);   // 0.11.1 归到计划（A-03）
    Q_INVOKABLE QSize imageSize(const QString &file) const;              // 原图的长宽，按 EXIF 方向摆正（只读文件头）；读不出来给空
    // 计划里的照片（0.10.5，P-14；plans::setPhotoFav 等）。都走撤销和自动保存；只读时返回 false
    Q_INVOKABLE bool setFav(const QString &photoId, bool on);
    Q_INVOKABLE bool setCaption(const QString &photoId, const QString &caption);
    Q_INVOKABLE bool removeFromPlan(const QString &planId, const QString &photoId);
    Q_INVOKABLE bool setCover(const QString &planId, const QString &photoId);
    // 照片说明文字："2024-10-03 14:25 (UTC+09:00) · Apple iPhone 15 Pro · f/1.8 1/250s ISO 80 24mm"
    Q_INVOKABLE QString exposureText(const QVariantMap &photo) const;

    // ---- 导入 ----
    // urls：FileDialog 给的 file:/// 地址或本地路径；planId / stopId：从计划页进来时默认放进的计划（站点），可以空
    Q_INVOKABLE QVariantMap startImport(const QVariantList &urls, const QString &planId = {}, const QString &stopId = {});
    Q_INVOKABLE void setItemPlan(int index, const QString &planId);
    Q_INVOKABLE void setItemStop(int index, const QString &stopId);
    Q_INVOKABLE void setAllPlan(const QString &planId);   // "全部改成…"
    Q_INVOKABLE void setItemSkip(int index, bool skip);
    Q_INVOKABLE void confirmImport();
    Q_INVOKABLE void cancelImport();
    // 确认表里的计划（有能用的站点的，按出发日期新的在前）：[{ id, name, date }]
    Q_INVOKABLE QVariantList planOptions() const;
    Q_INVOKABLE QVariantList stopOptions(const QString &planId) const;   // [{ id, name }]

    // ---- 旋转 ----
    // 返回 { ok, error }
    Q_INVOKABLE QVariantMap rotate(const QString &photoId, int turnsCw = 1);
    Q_INVOKABLE bool canRotate(const QString &file) const;
    // 0.11.1：后台转（大 PNG 约 1 秒，不卡界面）；做完发 rotateFinished
    Q_INVOKABLE void rotateLater(const QString &photoId, int turnsCw = 1);

    // ---- 清理 ----
    // { ok, error, unused:[{ rel, size }], unusedBytes, orphans:[{ rel, size }], orphanBytes, keptRecent, keptUndo }
    Q_INVOKABLE QVariantMap scanCleanup();
    // 把选中的文件移到回收站（后台；完成时发 trashFinished）。返回 { ok, error }（ok = 已经开始）
    Q_INVOKABLE QVariantMap trashFiles(const QStringList &rels);

    // ---- 缩略图缓存 ----
    Q_INVOKABLE void refreshCacheSize();
    Q_INVOKABLE void clearThumbCache();

    // 程序启动后：缓存超过上限就删旧的（后台）
    void pruneCacheLater();
    // 退出前：等后台线程停下
    void shutdown();

signals:
    void libraryChanged();
    void importChanged();
    void importProgress();
    void importItemsChanged();
    void importFinished(const QVariantMap &result);   // { ok, cancelled, error, count, toPlan, inbox, skipped }
    void trashChanged();
    void trashFinished(const QVariantMap &result);    // { moved, failed:[…] }
    void cacheChanged();
    void rotatingChanged();
    void rotateFinished(const QString &photoId, bool ok, const QString &error);

private:
    void afterRotate(const QString &photoId, const QString &file, const data::RotateResult &r);
    bool applyEdit(const std::function<bool(json::Value &)> &fn);
    struct Item
    {
        data::photolib::ImportItem in;
        QString takenAt, tz, why, dupOf;
        bool hasGps = false;
        QString container;
    };
    void setStage(const QString &s);
    void onDataChanged();
    void readDone(const QList<Item> &items);
    void copyDone(const data::photolib::ImportResult &r);
    QString planName(const QString &id) const;
    QString stopName(const QString &planId, const QString &stopId) const;
    QString dataDir() const;
    QVariantMap photoMap(const json::Value &ph) const;

    AppController *m_app;
    data::ThumbCache *m_cache;
    ThumbProvider *m_provider;
    int m_revision = 0;
    AlbumModel m_album;
    QThread *m_rotateThread = nullptr;
    QHash<QString, int> m_turned;         // 文件 → 旋转次数（原图地址的 ?r=）
    int m_epoch = 0;                      // 清空缓存时 +1000

    QString m_stage;
    QList<Item> m_items;
    int m_done = 0, m_total = 0;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel { false };
    QString m_forcePlan, m_forceStop;

    QThread *m_trashThread = nullptr;
    int m_trashDone = 0, m_trashTotal = 0;

    qint64 m_cacheBytes = -1;
    QThread *m_cacheThread = nullptr;
};

} // namespace tp::app
