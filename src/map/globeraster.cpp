#include "globeraster.h"
#include "globegeom.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QPainter>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <cmath>

namespace tp::map {

namespace {

// 抽稀：和上一个留下的点距离小于 eps（归一化墨卡托）就跳过；首尾一定留。简图只画 z0–z1（一个像素 ≥ 0.002），
// 0.9.0 给 z ≤ 4 用的那一套就够了（细的那一套是已解锁拉近时用的，0.9.3 删掉）
constexpr double kCoarseEps = 0.00035;

void addRing(const QJsonArray &ring, QPainterPath *path)
{
    if (ring.size() < 3) return;
    QPolygonF c;
    QPointF last;
    for (int i = 0; i < ring.size(); ++i) {
        const QJsonArray p = ring[i].toArray();
        const QPointF pt(globe::mercX(p[0].toDouble()), globe::mercY(p[1].toDouble()));
        if (i == 0 || i + 1 == ring.size()) { c << pt; last = pt; continue; }
        const QPointF d = pt - last;
        if (std::abs(d.x()) + std::abs(d.y()) >= kCoarseEps) { c << pt; last = pt; }
    }
    if (c.size() >= 4) { path->addPolygon(c); path->closeSubpath(); }
}

void addGeometry(const QJsonObject &g, QPainterPath *path)
{
    QJsonArray polys = g.value(QLatin1String("coordinates")).toArray();
    if (g.value(QLatin1String("type")).toString() == QLatin1String("Polygon")) {
        QJsonArray one;
        one.append(polys);   // 不能写 QJsonArray{ polys }：MSVC 当成复制（Windows差异清单 #15）
        polys = one;
    }
    for (const QJsonValue &pv : polys)
        for (const QJsonValue &rv : pv.toArray()) addRing(rv.toArray(), path);
}

QJsonArray readFeatures(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("读不了 %1").arg(path);
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("features")).toArray();
}

} // namespace

GlobeRaster::GlobeRaster(QObject *parent) : QObject(parent), m_alive(std::make_shared<std::atomic_bool>(true)) {}

GlobeRaster::~GlobeRaster()
{
    m_alive->store(false);
    // 后台还在画的等它画完（最多约 2 秒；画一块一般几十毫秒）
    for (int i = 0; i < 200 && m_busy.load() > 0; ++i) QThread::msleep(10);
}

std::shared_ptr<const GlobeRaster::Geometry> GlobeRaster::load(const QString &adm1, QString *error)
{
    auto g = std::make_shared<Geometry>();
    for (const QJsonValue &fv : readFeatures(adm1, error)) {
        Geometry::Unit u;
        u.path.setFillRule(Qt::OddEvenFill);
        addGeometry(fv.toObject().value(QLatin1String("geometry")).toObject(), &u.path);
        if (u.path.isEmpty()) continue;
        u.box = u.path.boundingRect();
        g->bytes += qint64(u.path.elementCount()) * 24;
        g->units.push_back(std::move(u));
    }
    if (g->units.isEmpty()) return nullptr;
    return g;
}

QImage GlobeRaster::paint(const Geometry &g, int z, int x, int y)
{
    QImage img(256, 256, QImage::Format_ARGB32_Premultiplied);
    const double n = std::exp2(z), s = 256.0 * n;
    const double m = 3.0 / s;   // 边上多留 3 像素：抗锯齿不在接缝处断开
    const QRectF tile(x / n - m, y / n - m, 1 / n + 2 * m, 1 / n + 2 * m);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setTransform(QTransform(s, 0, 0, s, -x * 256.0, -y * 256.0));
    p.fillRect(QRectF(x / n, y / n, 1 / n, 1 / n), QColor(16, 42, 70));   // 海：深蓝
    for (const Geometry::Unit &u : g.units)   // 陆地：所有省合起来，暗橄榄绿（原型 drawFallback 也是这样画的）
        if (u.box.intersects(tile)) p.fillPath(u.path, QColor(74, 92, 64));
    p.end();
    return img.convertToFormat(QImage::Format_RGBA8888);
}

std::shared_ptr<const GlobeRaster::Geometry> GlobeRaster::geometry()
{
    QMutexLocker lock(&m_mutex);
    if (!m_geom) {
        QString err;
        m_geom = load(m_adm1, &err);
        if (m_geom) qInfo("地球：简图的陆地读好了（%d 个省，约 %lld KB）", int(m_geom->units.size()), m_geom->bytes / 1024);
        else qWarning("地球：省界没读到（%s），简图画不出来", qUtf8Printable(err));
    }
    return m_geom;
}

bool GlobeRaster::geometryLoaded() const
{
    QMutexLocker lock(&m_mutex);
    return m_geom != nullptr;
}

void GlobeRaster::release()
{
    QMutexLocker lock(&m_mutex);
    m_geom.reset();   // 正在画的那几块手里还有一份，画完自己释放
}

void GlobeRaster::render(int z, int x, int y, int gen)
{
    ++m_busy;
    auto alive = m_alive;
    QThreadPool::globalInstance()->start([this, alive, z, x, y, gen] {
        QImage img;
        if (alive->load()) {
            const auto g = geometry();
            if (g) img = paint(*g, z, x, y);
        }
        if (alive->load())
            QMetaObject::invokeMethod(this, [this, z, x, y, gen, img] { emit rendered(z, x, y, gen, img); }, Qt::QueuedConnection);
        --m_busy;
    });
}

} // namespace tp::map
