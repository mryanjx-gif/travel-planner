// 液态玻璃的一块面（0.12.0）。着色器一对一移植 Electron liquidglass.js（shaders/glass.vert、glass.frag）。
// 全窗口共用一份背景（Main.qml 的 glassSource，带 mipmap）和一张玻璃位置表；
// 每块面画在自己面板的层次上，所以上面的面板能正常盖住下面面板的字（Electron 一层画完，只好不让玻璃互相盖）。
// 只画外扩 14 像素的范围。小泡泡（胶囊 / 圆形）彼此靠近时用 smooth-min 连在一起，大面板不参与。
#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QVector>
#include <atomic>
#include <QtQml/qqmlregistration.h>

class GlassSurface : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(QColor tint READ tint WRITE setTint NOTIFY tintChanged)
    Q_PROPERTY(int plain READ plain WRITE setPlain NOTIFY plainChanged)   // 0 = 液态，1 = 标准（只磨砂），2 = 实色
public:
    explicit GlassSurface(QQuickItem *parent = nullptr);
    ~GlassSurface() override;
    QQuickItem *source() const { return m_source; }
    void setSource(QQuickItem *s);
    qreal radius() const { return m_radius; }
    void setRadius(qreal r);
    QColor tint() const { return m_tint; }
    void setTint(const QColor &c);
    int plain() const { return m_plain; }
    void setPlain(int v);

signals:
    void sourceChanged();
    void radiusChanged();
    void tintChanged();
    void plainChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    struct Near {
        QRectF rect;
        qreal radius = 0;
        bool operator==(const Near &o) const { return rect == o.rect && radius == o.radius; }
    };
    bool pill() const;
    qreal sceneRadius() const;
    static void refreshAll();

    QPointer<QQuickItem> m_source;
    qreal m_radius = 0;
    QColor m_tint = Qt::white;
    QRectF m_rect;          // 场景坐标
    QVector<Near> m_near;   // 够得着、要连起来的别的泡泡
    bool m_watching = false;
    int m_plain = 0;
};

// 真玻璃带不动时报告（0.12.1）：显卡耗时连续 2 秒都超过 20 ms（至少 20 帧）。
// 看显卡耗时而不是两帧间隔：UHD 620 上不开玻璃，拖地图时两帧间隔也约 27 ms（v0.12.0记录.md）。
class GlassWatch : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow)
    Q_PROPERTY(bool active READ active WRITE setActive)
public:
    using QObject::QObject;
    QQuickWindow *window() const { return m_win; }
    void setWindow(QQuickWindow *w);
    bool active() const { return m_active; }
    void setActive(bool on) { m_active = on; }
signals:
    void slow();
private:
    QPointer<QQuickWindow> m_win;
    std::atomic_bool m_active{false};
    double m_since = -1;   // 这两个只在渲染线程用
    int m_count = 0;
};

// 内容卡片的形状贴图（qml/components/TpCard.qml）：连续轻圆角。id = 半径/倍数/颜色（不带 #）
class CardImages : public QQuickImageProvider {
public:
    CardImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override;
};
