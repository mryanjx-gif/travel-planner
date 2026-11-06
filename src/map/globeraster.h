// 地球上本机画的瓦片：卫星图还没到（或离线）时垫底的简图——深蓝海 + 暗橄榄绿陆地（原型的 drawFallback，只画 z0–z1）。
// 0.9.3：地球上不再有"已解锁"（0.9.2 起地球只有卫星图），0.9.0 的已解锁画法、颜色、国界线、圆点都删掉了，
// 只留简图要的陆地轮廓（assets/admin/adm1.geojson，抽稀过的一套）。
// 省界数据在后台线程读一次，转成墨卡托坐标的路径；离开地球时丢掉。画瓦片也在后台线程（QThreadPool），不卡界面。
#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>

namespace tp::map {

class GlobeRaster : public QObject {
    Q_OBJECT
public:
    explicit GlobeRaster(QObject *parent = nullptr);
    ~GlobeRaster() override;

    void setFile(const QString &adm1) { m_adm1 = adm1; }
    // 画一块（异步）：画好了发 rendered。gen 是调用方的代数（离开地球后再回来的结果不要）
    void render(int z, int x, int y, int gen);
    // 丢掉省界路径（离开地球）
    void release();
    bool geometryLoaded() const;
    int busy() const { return m_busy.load(); }

    // 同步画一块（单元测试 / 后台线程里用）
    struct Geometry;
    static QImage paint(const Geometry &g, int z, int x, int y);
    static std::shared_ptr<const Geometry> load(const QString &adm1, QString *error = nullptr);

signals:
    void rendered(int z, int x, int y, int gen, const QImage &img);

private:
    std::shared_ptr<const Geometry> geometry();   // 没读过就读（在后台线程里调）

    QString m_adm1;
    mutable QMutex m_mutex;
    std::shared_ptr<const Geometry> m_geom;
    std::atomic_int m_busy{ 0 };
    std::shared_ptr<std::atomic_bool> m_alive;
};

struct GlobeRaster::Geometry {
    struct Unit { QRectF box; QPainterPath path; };   // box / 路径都是归一化墨卡托（0..1，y 向南），抽稀过
    QVector<Unit> units;   // adm1
    qint64 bytes = 0;      // 估计的内存（自检参考）
};

} // namespace tp::map
