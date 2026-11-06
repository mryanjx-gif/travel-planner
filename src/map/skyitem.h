// 地球后面的天空（0.9.3，阶段 ③ 天空和光照）：星空 + 大气光晕，照 Electron map-app.js 的"宇宙背景"。
// 单独一个 QQuickRhiItem 放在 GlobeItem 后面：星星闪烁、流星每秒 24 帧只重画这一层，地球那一层不跟着重画（省显卡）。
// 镜头（centerLat / centerLng / distance）由 QML 绑到 GlobeItem 上，两边用同一套投影（globegeom.h 的视角）。
// "动不动"（animate）：QML 给（动效开着、星空动画开着、窗口没最小化、在地球上）；不动时星星不闪、没有流星（Electron noAnim）。
// 0.9.4：底色 + 星云 + 银河光雾在后台算好一次（bakeBackground，整个程序共用一份），着色器只取样（UHD 620 上拖地球太慢）。
#pragma once
#include <QElapsedTimer>
#include <QQuickRhiItem>
#include <QTimer>
#include <QImage>
#include <QVector>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

class SkyItem : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double centerLat READ centerLat WRITE setCenterLat NOTIFY viewChanged)
    Q_PROPERTY(double centerLng READ centerLng WRITE setCenterLng NOTIFY viewChanged)
    Q_PROPERTY(double distance READ distance WRITE setDistance NOTIFY viewChanged)
    Q_PROPERTY(bool animate READ animate WRITE setAnimate NOTIFY animateChanged)
    Q_PROPERTY(int frames READ frames NOTIFY framesChanged)           // 画了几帧（自检）
    Q_PROPERTY(bool meteorActive READ meteorActive NOTIFY framesChanged)
    Q_PROPERTY(double starGain READ starGain WRITE setStarGain NOTIFY viewChanged)   // 星星亮度（自检对比用，平时 1）
    Q_PROPERTY(bool backgroundReady READ backgroundReady NOTIFY framesChanged)       // 0.9.4：算好的星云底图传上显卡了
public:
    explicit SkyItem(QQuickItem *parent = nullptr);
    QQuickRhiItemRenderer *createRenderer() override;

    double centerLat() const { return m_lat; }
    double centerLng() const { return m_lng; }
    double distance() const { return m_dist; }
    bool animate() const { return m_animate; }
    int frames() const { return m_frames; }
    bool meteorActive() const { return m_meteor.active; }
    double starGain() const { return m_starGain; }
    bool backgroundReady() const { return m_bgReady; }
    Q_INVOKABLE void setBackgroundUploaded(bool on);   // 渲染那边（排队调用）
    void setCenterLat(double v);
    void setCenterLng(double v);
    void setDistance(double v);
    void setAnimate(bool on);
    void setStarGain(double g);

    Q_INVOKABLE void fireMeteor();   // 自检：马上来一颗流星
    Q_INVOKABLE void frameDone();    // 渲染那边画完一帧（内部用，排队调用）

    // 交给渲染那边
    struct Meteor { bool active = false; double x = 0, y = 0, dx = 0, dy = 0, len = 0, dist = 0; qint64 start = 0; };
    struct State {
        double lat = 0, lng = 0, dist = 4, time = 0, starGain = 1;
        bool animate = false;
        double mx = 0, my = 0, mdx = 0, mdy = 0, mlen = 0, malpha = 0;   // 这一帧的流星（逻辑像素）
    };
    State state() const;

    // 银河大圆的法线、星云噪声的偏移：由固定种子 20261016 用 mulberry32 算（和 Electron 的随机数一样，tst_map 对照）
    static QVector3D bandNormal();
    static QVector3D nebulaSeed();
    // 0.9.4：算星空底图（6 个面 × size²，RGBA8；rgb = 底色 + 星云 + 银河光雾，a = 银河小星的疏密 ÷ 1.2）。
    // 第一次调用时在后台线程算（约 0.2–0.4 秒），算好之前返回空
    static constexpr int kBgSize = 256;
    static QVector<QImage> bakeBackground(int size);
    static QVector<QImage> background();

signals:
    void viewChanged();
    void animateChanged();
    void framesChanged();

private:
    void tick();
    void syncTimer();

    double m_lat = 30, m_lng = 105, m_dist = 4.2;
    bool m_animate = false;
    double m_starGain = 1.0;
    bool m_bgReady = false;
    int m_frames = 0;
    QTimer m_timer;          // 24 帧（Electron SPACE.fps）
    QTimer m_minute;         // 不动时每分钟重画一次（光晕跟着太阳）
    QElapsedTimer m_clock;
    Meteor m_meteor;
    qint64 m_nextMeteorAt = 0;
};
