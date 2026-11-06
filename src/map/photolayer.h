// 地图上的照片标记（0.11.1，A-04；PO 第 5 题 A）。QML 里是 mapc.photoLayer，画在 qml/components/PhotoMarkers.qml。
// 按 1,000–10,000 张设计：
// - 合并按整数缩放级分层，每层第一次用到时算一次（photomap::cluster）；
// - 模型的行是"槽"：只有看得见的组占槽，移出视野的槽留给新进来的组，不增删 QML 元素，也不让别的行换图；
// - 只在照片模式（active）工作，别的模式不读照片库、不占元素。
#pragma once

#include "geo.h"
#include "globegeom.h"
#include "jsonvalue.h"
#include "photomap.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <functional>

namespace tp::map {

class PhotoLayer : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int pinned READ pinned NOTIFY infoChanged)        // 地图上的张数
    Q_PROPERTY(int noPos READ noPos NOTIFY infoChanged)          // 没有位置的张数
    Q_PROPERTY(int shownCount READ shownCount NOTIFY infoChanged)   // 现在画着的标记个数（只算看得见的）
    Q_PROPERTY(int slotCount READ slotCount NOTIFY infoChanged)  // QML 元素个数（只增不减）
public:
    enum Role { X = Qt::UserRole + 1, Y, File, Count, Fav, Shown, Sc, Op, Hit, Same };

    explicit PhotoLayer(QObject *parent = nullptr);
    void setCamera(const geo::Camera *cam) { m_cam = cam; }
    void setGlobeView(bool on, const globe::View &v, double zoomEq);
    void setFitBounds(std::function<void(double, double, double, double, double, int)> fit) { m_fit = std::move(fit); }
    void rebuild(const json::Value &root);
    void cameraChanged();

    bool active() const { return m_active; }
    void setActive(bool on);
    int pinned() const { return m_lib.pinned; }
    int noPos() const { return m_lib.noPos; }
    int shownCount() const { return m_shown; }
    int slotCount() const { return int(m_slots.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 点标记：一张、同一点、或已经放到最大 → 返回 { ids: [...] }（看大图）；否则放大（返回空）
    Q_INVOKABLE QVariantMap click(int row, bool motion);
    Q_INVOKABLE QString tipOf(int row) const;
    Q_INVOKABLE QVariantList pointsOf(const QString &planId) const;   // 这一趟（"" = 全部）有位置的照片：[[lat, lng], …]，给 fitPoints
    Q_INVOKABLE int rowOfPlan(const QString &planId) const;            // 自检：哪个标记含这一趟的照片；没有给 -1
    Q_INVOKABLE QVariantMap rowInfo(int row) const;                    // 自检：{ x, y, count, fav, same, file, shown }

signals:
    void activeChanged();
    void infoChanged();
    void rebuildDataWanted();   // 打开时要一份最新的数据

private:
    struct Slot { int group = -1; double x = -10000, y = -10000, limb = 1, sc = 1; };
    const QVector<photomap::Group> &groups();
    void layout(bool tierChanged);
    int tier() const;

    bool m_active = false;
    const geo::Camera *m_cam = nullptr;
    bool m_globe = false;
    globe::View m_gv;
    double m_gzoom = 0;
    std::function<void(double, double, double, double, double, int)> m_fit;
    photomap::Library m_lib;
    QHash<int, QVector<photomap::Group>> m_tiers;
    int m_tier = -1;
    QVector<int> m_groupSlot;   // 当前层：组 → 槽（-1 = 不在视野里）
    QVector<Slot> m_slots;
    QVector<int> m_free;
    int m_shown = 0;
};

} // namespace tp::map
