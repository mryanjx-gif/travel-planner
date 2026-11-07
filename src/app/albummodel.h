// 相册的行（0.11.1，A-01–A-03）：照片模式左边的列表和展开后的整页共用。QML 里是 photos.album。
// 按 1,000–10,000 张设计：行的内容（缩略图地址等）读到时才建（只有屏幕上的行会读），精选这种只改内容的不重置列表。
#pragma once

#include "jsonvalue.h"
#include "photomap.h"

#include <atomic>
#include <functional>
#include <memory>

#include <QAbstractListModel>
#include <QHash>
#include <QSize>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

namespace tp::app {

class AlbumModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY stateChanged)                // trip / year / fav / inbox（记住上次选的）
    Q_PROPERTY(QString plan READ plan WRITE setPlan NOTIFY stateChanged)                // 只看这一趟；"" = 全部
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY stateChanged)          // 窄列表（按旅程时每趟一行）
    Q_PROPERTY(double layoutWidth READ layoutWidth NOTIFY columnsChanged)               // 0.12.9 行是按多宽排的（窗口正在变大小时 QML 按比例缩放）
    Q_PROPERTY(int total READ total NOTIFY infoChanged)
    Q_PROPERTY(int favs READ favs NOTIFY infoChanged)
    Q_PROPERTY(int inbox READ inbox NOTIFY infoChanged)
    Q_PROPERTY(int noPos READ noPos NOTIFY infoChanged)
    Q_PROPERTY(int pinned READ pinned NOTIFY infoChanged)
    Q_PROPERTY(int heic READ heic NOTIFY infoChanged)
    Q_PROPERTY(QVariantList trips READ trips NOTIFY infoChanged)                        // [{ id, name, count }]（整页左边的一列）
    Q_PROPERTY(QStringList inboxIds READ inboxIds NOTIFY infoChanged)
public:
    enum Role { Kind = Qt::UserRole + 1, Title, Sub, PlanId, StopNo, RowCount, Group, Items, Open, Thumb, RowH };

    explicit AlbumModel(QObject *parent = nullptr);
    ~AlbumModel() override;
    // 0.12.9 没记录比例的照片读文件头：dataDir 取数据文件夹，cacheRoot 放结果（不进数据文件），read 读一张的长宽（后台线程调）
    void setRatioSource(std::function<QString()> dataDir, const QString &cacheRoot, std::function<QSize(const QString &dir, const QString &file)> read);
    // get：取现在的数据（COW，便宜）；savedView / saveView：读写设置
    void setDocSource(std::function<const json::Value &()> get) { m_get = std::move(get); }
    void setViewStore(const QString &saved, std::function<void(const QString &)> save);
    void refresh();         // 数据变了

    bool active() const { return m_active; }
    void setActive(bool on);
    QString view() const { return m_view; }
    void setView(const QString &v);
    QString plan() const { return m_plan; }
    void setPlan(const QString &p);
    bool compact() const { return m_compact; }
    void setCompact(bool c);
    double layoutWidth() const { return m_layout.width; }
    Q_INVOKABLE void setLayout(double width, double targetHeight, double gap);
    int total() const { return int(m_lib.recs.size()); }
    int favs() const { return m_lib.favs; }
    int inbox() const { return m_lib.inbox; }
    int noPos() const { return m_lib.noPos; }
    int pinned() const { return m_lib.pinned; }
    int heic() const { return m_lib.heic; }
    QVariantList trips() const;
    QStringList inboxIds() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 看大图：这张照片所在的组 → { list, index }
    Q_INVOKABLE QVariantMap openGroup(int group, const QString &photoId) const;
    // 地图上点的照片（位置来自站点的会标明）
    Q_INVOKABLE QVariantList itemsFor(const QStringList &ids) const;
    Q_INVOKABLE int firstRowOf(const QString &kind) const;   // 自检
    Q_INVOKABLE int tripRow(const QString &planId) const;    // 窄列表里这一趟是第几行；没有给 -1
    // 0.12.9 重排后留在原处：记住顶上那张照片，排好后回到它所在的行
    Q_INVOKABLE QString anchorPhoto(int row) const;
    Q_INVOKABLE int rowOfPhoto(const QString &id) const;

signals:
    void activeChanged();
    void stateChanged();
    void columnsChanged();
    void infoChanged();
    void aboutToRebuild();   // 整份重建前后（列表留在原处）
    void rebuilt();

private:
    QVariantMap lightItem(const tp::map::photomap::Rec &r) const;
    void rebuildRows(bool keep, const tp::map::photomap::Library *old = nullptr);
    void fillRatios();                                  // 从缓存填比例，缺的交给后台读
    void loadRatios(const QString &dir);
    void saveRatios() const;
    void onRatios(const QString &dir, const QHash<QString, double> &got, bool finished);

    bool m_active = false;
    QString m_view = QStringLiteral("trip"), m_plan;
    bool m_compact = true;
    tp::map::photomap::RowLayout m_layout;
    std::function<QString()> m_dataDir;
    std::function<QSize(const QString &, const QString &)> m_readSize;
    QString m_cacheRoot, m_ratioDir;
    QHash<QString, double> m_ratios;                    // 相对路径 → 宽 / 高；读不了的是 0（这次不再读，也不写进文件）
    bool m_scanning = false;
    std::shared_ptr<std::atomic_bool> m_alive = std::make_shared<std::atomic_bool>(true);
    QThreadPool m_pool;
    std::function<const json::Value &()> m_get;
    std::function<void(const QString &)> m_saveView;
    tp::map::photomap::Library m_lib;
    QVector<tp::map::photomap::Row> m_rows;
};

} // namespace tp::app
