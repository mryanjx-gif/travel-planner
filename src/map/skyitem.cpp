#include "skyitem.h"
#include "globegeom.h"
#include "sun.h"

#include <QFile>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QMutex>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <memory>
#include <rhi/qrhi.h>

using namespace tp::map;

namespace {

QShader loadShader(const QString &name)
{
    QFile f(name);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

QVector3D toQVec(double lat, double lng)
{
    const globe::Vec3 v = globe::toVec(lat, lng);
    return QVector3D(float(v.x), float(v.y), float(v.z));
}

// uniform（std140）：两个 mat4 + 8 个 vec4 = 256 字节，和 sky.frag 的 buf 一致
constexpr quint32 kUbo = 64 * 2 + 16 * 8;

// ---- 0.9.4 星空底图：0.9.3 sky.frag 里的值噪声原样搬过来（结果和那时现算的一样）----
double fract(double x) { return x - std::floor(x); }
double hash31(double x, double y, double z)
{
    x = fract(x * 0.1031); y = fract(y * 0.1031); z = fract(z * 0.1031);
    const double d = x * (z + 31.32) + y * (y + 31.32) + z * (x + 31.32);   // dot(p, p.zyx + 31.32)
    x += d; y += d; z += d;
    return fract((x + y) * z);
}
double vnoise(double x, double y, double z)
{
    const double ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
    double fx = x - ix, fy = y - iy, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
    const auto h = [&](double a, double b, double c) { return hash31(ix + a, iy + b, iz + c); };
    const auto mix = [](double a, double b, double t) { return a + (b - a) * t; };
    return mix(mix(mix(h(0, 0, 0), h(1, 0, 0), fx), mix(h(0, 1, 0), h(1, 1, 0), fx), fy),
               mix(mix(h(0, 0, 1), h(1, 0, 1), fx), mix(h(0, 1, 1), h(1, 1, 1), fx), fy), fz);
}
double fbm(double x, double y, double z)
{
    return vnoise(x, y, z) * 0.55 + vnoise(x * 2.03, y * 2.03, z * 2.03) * 0.3 + vnoise(x * 4.1, y * 4.1, z * 4.1) * 0.15;
}
double smoothStep(double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); }

// 等角立方体：第几个面、面上 -1..1 的坐标 → 方向（和 sky.frag 的 fromCube 一样）
QVector3D fromCube(int face, double ax, double ay)
{
    const double u = std::tan(ax * M_PI / 4), v = std::tan(ay * M_PI / 4);
    QVector3D d;
    switch (face) {
    case 0: d = QVector3D(1, float(u), float(v)); break;
    case 1: d = QVector3D(-1, float(u), float(v)); break;
    case 2: d = QVector3D(float(u), 1, float(v)); break;
    case 3: d = QVector3D(float(u), -1, float(v)); break;
    case 4: d = QVector3D(float(u), float(v), 1); break;
    default: d = QVector3D(float(u), float(v), -1); break;
    }
    return d.normalized();
}

QMutex g_bgMutex;
QVector<QImage> g_bg;      // 算好的
bool g_bgStarted = false;

class SkyRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    QRhi *m_rhi = nullptr;
    QRhiRenderPassDescriptor *m_rpDesc = nullptr;
    void bindBg();
    std::unique_ptr<QRhiBuffer> m_vbuf, m_ubuf;
    std::unique_ptr<QRhiTexture> m_bg, m_bgDummy;
    std::unique_ptr<QRhiSampler> m_sampler;
    QVector<QImage> m_bgPending;   // 拿到了、还没传上去的底图
    bool m_bgLost = false;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipe;
    SkyItem::State m_s;
    double m_dpr = 1;
    QVector3D m_sun, m_band, m_seed;
    SkyItem *m_item = nullptr;
};

void SkyRenderer::synchronize(QQuickRhiItem *item)
{
    auto *s = static_cast<SkyItem *>(item);
    m_item = s;
    m_s = s->state();
    m_dpr = s->width() > 0 ? double(s->effectiveColorBufferSize().width()) / s->width() : 1.0;
    if (m_dpr <= 0) m_dpr = 1;
    const SubSolar sun = subsolarPoint(QDateTime::currentDateTimeUtc());
    m_sun = toQVec(sun.lat, sun.lng);
    m_band = SkyItem::bandNormal();
    m_seed = SkyItem::nebulaSeed();
    if (m_bgLost) { m_bgLost = false; s->setBackgroundUploaded(false); }
    if (!m_bg && m_bgPending.isEmpty()) m_bgPending = SkyItem::background();   // 后台算好了就接过来（没好是空的，下一帧再问）
}

void SkyRenderer::bindBg()
{
    m_srb->setBindings({ QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()),
                         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                   m_bg ? m_bg.get() : m_bgDummy.get(), m_sampler.get()) });
    m_srb->create();
}

void SkyRenderer::initialize(QRhiCommandBuffer *cb)
{
    if (m_rhi != rhi() || m_rpDesc != renderTarget()->renderPassDescriptor()) {
        if (m_rhi != rhi() && m_bg) { m_bg.reset(); m_bgLost = true; }   // 换了 QRhi：底图要重新传
        m_pipe.reset();
        m_rhi = rhi();
        m_rpDesc = renderTarget()->renderPassDescriptor();
    }
    if (m_pipe) return;
    static const float tri[] = { -1, -1, 3, -1, -1, 3 };   // 盖满屏幕的大三角形（NDC）
    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(tri)));
    m_vbuf->create();
    m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUbo));
    m_ubuf->create();
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    // 底图还没算好时：6 层 1×1、颜色是底色 #070b1e（没有星云、银河小星）
    m_bgDummy.reset(m_rhi->newTextureArray(QRhiTexture::RGBA8, 6, QSize(1, 1)));
    m_bgDummy->create();
    m_srb.reset(m_rhi->newShaderResourceBindings());
    bindBg();
    m_pipe.reset(m_rhi->newGraphicsPipeline());
    m_pipe->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/sky.vert.qsb")) },
                              { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/sky.frag.qsb")) } });
    QRhiVertexInputLayout il;
    il.setBindings({ { 2 * sizeof(float) } });
    il.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 } });
    m_pipe->setVertexInputLayout(il);
    m_pipe->setSampleCount(renderTarget()->sampleCount());
    m_pipe->setShaderResourceBindings(m_srb.get());
    m_pipe->setRenderPassDescriptor(m_rpDesc);
    m_pipe->create();
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->uploadStaticBuffer(m_vbuf.get(), tri);
    static const quint8 base[4] = { 7, 11, 30, 0 };
    for (int layer = 0; layer < 6; ++layer) {
        QRhiTextureSubresourceUploadDescription sd(base, 4);
        sd.setSourceSize(QSize(1, 1));
        u->uploadTexture(m_bgDummy.get(), QRhiTextureUploadEntry(layer, 0, sd));
    }
    cb->resourceUpdate(u);
}

void SkyRenderer::render(QRhiCommandBuffer *cb)
{
    const QSize sz = renderTarget()->pixelSize();
    // 和 GlobeRenderer 同一套投影、视角（globegeom.h 的 kFovYDeg；上方向是地轴，两极附近改用 y）
    QMatrix4x4 proj = m_rhi->clipSpaceCorrMatrix();
    const float nearP = float(std::clamp((m_s.dist - 1.0) * 0.3, 0.002, 0.05));
    proj.perspective(float(globe::kFovYDeg), float(sz.width()) / qMax(1, sz.height()), nearP, 50.0f);
    const QVector3D eye = toQVec(m_s.lat, m_s.lng) * float(m_s.dist);
    QMatrix4x4 view;
    view.lookAt(eye, QVector3D(0, 0, 0), std::abs(m_s.lat) > 89.5 ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1));
    const QMatrix4x4 vp = proj * view;
    const QMatrix4x4 inv = vp.inverted();
    const float px = float(m_dpr);   // 设备像素比（不叫 d：MSVC C4458，Windows差异清单 #30）
    const float cam[4] = { eye.x(), eye.y(), eye.z(), 1 };
    const float sun[4] = { m_sun.x(), m_sun.y(), m_sun.z(), 0 };
    const float params[4] = { float(std::fmod(m_s.time, 3600.0)), m_s.animate ? 1.0f : 0.0f, px, float(m_s.starGain) };
    const float viewport[4] = { float(sz.width()), float(sz.height()), m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0 };
    const float meteorA[4] = { float(m_s.mx) * px, float(m_s.my) * px, float(m_s.mdx), float(m_s.mdy) };
    const float meteorB[4] = { float(m_s.mlen) * px, float(m_s.malpha), 1.4f * px, 0 };
    const float band[4] = { m_band.x(), m_band.y(), m_band.z(), 0 };
    const float seed[4] = { m_seed.x(), m_seed.y(), m_seed.z(), 0 };
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    if (!m_bgPending.isEmpty()) {   // 0.9.4：算好的星空底图传上去（6 × 256² RGBA8，1.5 MB）
        const int n = m_bgPending.first().width();
        m_bg.reset(m_rhi->newTextureArray(QRhiTexture::RGBA8, 6, QSize(n, n)));
        if (m_bg->create()) {
            QVarLengthArray<QRhiTextureUploadEntry, 6> entries;
            for (int layer = 0; layer < 6 && layer < m_bgPending.size(); ++layer)
                entries.append(QRhiTextureUploadEntry(layer, 0, QRhiTextureSubresourceUploadDescription(m_bgPending[layer])));
            QRhiTextureUploadDescription desc;
            desc.setEntries(entries.cbegin(), entries.cend());
            u->uploadTexture(m_bg.get(), desc);
            bindBg();
            if (m_item) QMetaObject::invokeMethod(m_item, "setBackgroundUploaded", Qt::QueuedConnection, Q_ARG(bool, true));
        } else {
            m_bg.reset();
        }
        m_bgPending.clear();
    }
    u->updateDynamicBuffer(m_ubuf.get(), 0, 64, vp.constData());
    u->updateDynamicBuffer(m_ubuf.get(), 64, 64, inv.constData());
    u->updateDynamicBuffer(m_ubuf.get(), 128, 16, cam);
    u->updateDynamicBuffer(m_ubuf.get(), 144, 16, sun);
    u->updateDynamicBuffer(m_ubuf.get(), 160, 16, params);
    u->updateDynamicBuffer(m_ubuf.get(), 176, 16, viewport);
    u->updateDynamicBuffer(m_ubuf.get(), 192, 16, meteorA);
    u->updateDynamicBuffer(m_ubuf.get(), 208, 16, meteorB);
    u->updateDynamicBuffer(m_ubuf.get(), 224, 16, band);
    u->updateDynamicBuffer(m_ubuf.get(), 240, 16, seed);
    cb->beginPass(renderTarget(), QColor::fromRgbF(0.027f, 0.043f, 0.118f, 1.0f), { 1.0f, 0 }, u);
    cb->setGraphicsPipeline(m_pipe.get());
    // 视口要在绑定管线之后设（Vulkan 下 QRhi 顺带设置裁剪矩形，技术选型.md §8）
    cb->setViewport(QRhiViewport(0, 0, float(sz.width()), float(sz.height())));
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vin(m_vbuf.get(), 0);
    cb->setVertexInput(0, 1, &vin);
    cb->draw(3);
    cb->endPass();
    if (m_item) QMetaObject::invokeMethod(m_item, "frameDone", Qt::QueuedConnection);
}

} // namespace

// ================= SkyItem =================

SkyItem::SkyItem(QQuickItem *parent) : QQuickRhiItem(parent)
{
    m_clock.start();
    m_timer.setInterval(1000 / 24);   // Electron SPACE.fps = 24
    connect(&m_timer, &QTimer::timeout, this, &SkyItem::tick);
    m_minute.setInterval(60000);
    connect(&m_minute, &QTimer::timeout, this, [this] { if (isVisible()) update(); });
    m_minute.start();
    connect(this, &QQuickItem::visibleChanged, this, &SkyItem::syncTimer);
    background();   // 0.9.4：程序一开就在后台算星空底图（第一次进地球时一般已经好了）
}

QQuickRhiItemRenderer *SkyItem::createRenderer() { return new SkyRenderer; }

void SkyItem::setCenterLat(double v) { if (v == m_lat) return; m_lat = v; update(); emit viewChanged(); }
void SkyItem::setCenterLng(double v) { if (v == m_lng) return; m_lng = v; update(); emit viewChanged(); }
void SkyItem::setDistance(double v) { if (v == m_dist) return; m_dist = v; update(); emit viewChanged(); }
void SkyItem::setStarGain(double g) { if (g == m_starGain) return; m_starGain = g; update(); emit viewChanged(); }

void SkyItem::setAnimate(bool on)
{
    if (on == m_animate) return;
    m_animate = on;
    m_meteor.active = false;   // Electron：改了星空动画设置时 meteor = null
    syncTimer();
    update();
    emit animateChanged();
}

void SkyItem::syncTimer()
{
    if (m_animate && isVisible()) { if (!m_timer.isActive()) m_timer.start(); }
    else m_timer.stop();
}

void SkyItem::tick()
{
    // 流星（Electron drawSpace）：第一次只定下一次的时间；之后每 15–40 秒一颗，0.9 秒划过，大致从右上往左下
    const qint64 now = m_clock.elapsed();
    if (!m_meteor.active && now > m_nextMeteorAt) {
        if (m_nextMeteorAt) fireMeteor();
        m_nextMeteorAt = now + 15000 + qint64(QRandomGenerator::global()->bounded(25000));
    }
    if (m_meteor.active && now - m_meteor.start >= 900) m_meteor.active = false;
    update();
}

void SkyItem::fireMeteor()
{
    QRandomGenerator *r = QRandomGenerator::global();
    const double W = std::max(1.0, width()), H = std::max(1.0, height());
    const double ang = (200 + r->generateDouble() * 40) * M_PI / 180;
    m_meteor.active = true;
    m_meteor.x = W * (0.3 + r->generateDouble() * 0.7);
    m_meteor.y = H * r->generateDouble() * 0.5;
    m_meteor.dx = std::cos(ang);
    m_meteor.dy = -std::sin(ang);   // 屏幕 y 向下：往下走
    m_meteor.start = m_clock.elapsed();
    m_meteor.len = 120 + r->generateDouble() * 80;
    m_meteor.dist = 260 + r->generateDouble() * 200;
    update();
}

void SkyItem::setBackgroundUploaded(bool on)
{
    if (on == m_bgReady) return;
    m_bgReady = on;
    emit framesChanged();
}

QVector<QImage> SkyItem::background()
{
    QMutexLocker lock(&g_bgMutex);
    if (!g_bg.isEmpty()) return g_bg;
    if (!g_bgStarted) {
        g_bgStarted = true;
        QThreadPool::globalInstance()->start([] {
            QElapsedTimer t;
            t.start();
            QVector<QImage> faces = bakeBackground(kBgSize);
            qInfo("[计时] 星空底图：%lld ms（6 × %d²）", t.elapsed(), kBgSize);
            QMutexLocker l(&g_bgMutex);
            g_bg = std::move(faces);
        });
    }
    return {};
}

QVector<QImage> SkyItem::bakeBackground(int size)
{
    // 照 0.9.3 sky.frag：底色 #070b1e → 四种颜色的星云（Electron 四团 radialGradient，用低频噪声铺满天球）→ 银河光雾；
    // a = 银河小星的疏密（离大圆的角度高斯 × 团块噪声）÷ 1.2
    const QVector3D band = bandNormal(), seed = nebulaSeed();
    const double sx = seed.x(), sy = seed.y(), sz = seed.z();
    QVector<QImage> faces;
    for (int face = 0; face < 6; ++face) {
        QImage img(size, size, QImage::Format_RGBA8888);
        for (int j = 0; j < size; ++j) {
            uchar *line = img.scanLine(j);
            for (int i = 0; i < size; ++i) {
                const QVector3D d = fromCube(face, (i + 0.5) / size * 2 - 1, (j + 0.5) / size * 2 - 1);
                const double x = d.x(), y = d.y(), z = d.z();
                const double qx = x * 2.2, qy = y * 2.2, qz = z * 2.2;
                const double n1 = fbm(qx + sx, qy + sy, qz + sz), n2 = fbm(qx - sz, qy - sx, qz - sy);
                const double n3 = fbm(qx * 1.4 + sy, qy * 1.4 + sz, qz * 1.4 + sx), n4 = fbm(qx * 1.2 - sx, qy * 1.2 - sy, qz * 1.2 - sz);
                double r = 7 / 255.0, g = 11 / 255.0, b = 30 / 255.0;
                const auto mixc = [&](double cr, double cg, double cb, double a) { r += (cr / 255 - r) * a; g += (cg / 255 - g) * a; b += (cb / 255 - b) * a; };
                mixc(94, 72, 170, 0.16 * std::pow(smoothStep(0.42, 0.78, n1), 1.5));
                mixc(40, 110, 170, 0.14 * std::pow(smoothStep(0.42, 0.78, n2), 1.5));
                mixc(170, 70, 120, 0.08 * std::pow(smoothStep(0.45, 0.80, n3), 1.5));
                mixc(60, 140, 150, 0.07 * std::pow(smoothStep(0.45, 0.80, n4), 1.5));
                const double th = std::asin(std::clamp(double(QVector3D::dotProduct(d, band)), -1.0, 1.0));
                const double clump = 0.55 + 0.9 * fbm(x * 6 + sy, y * 6 + sx, z * 6 + sz);
                const double hazeW = std::exp(-0.5 * (th / 0.064) * (th / 0.064)) * clump;
                const double starW = std::exp(-0.5 * (th / 0.032) * (th / 0.032)) * std::clamp(clump, 0.0, 1.2);
                mixc(180, 190, 230, std::clamp(0.06 * hazeW, 0.0, 0.12));
                uchar *p = line + i * 4;
                p[0] = uchar(std::lround(std::clamp(r, 0.0, 1.0) * 255));
                p[1] = uchar(std::lround(std::clamp(g, 0.0, 1.0) * 255));
                p[2] = uchar(std::lround(std::clamp(b, 0.0, 1.0) * 255));
                p[3] = uchar(std::lround(std::clamp(starW / 1.2, 0.0, 1.0) * 255));
            }
        }
        faces.push_back(img);
    }
    return faces;
}

void SkyItem::frameDone()
{
    ++m_frames;
    emit framesChanged();
}

SkyItem::State SkyItem::state() const
{
    State s;
    s.lat = m_lat; s.lng = m_lng; s.dist = m_dist;
    s.time = m_clock.elapsed() / 1000.0;
    s.animate = m_animate;
    s.starGain = m_starGain;
    if (m_animate && m_meteor.active) {
        const double p = (m_clock.elapsed() - m_meteor.start) / 900.0;
        if (p >= 0 && p < 1) {
            s.mx = m_meteor.x + m_meteor.dx * m_meteor.dist * p;
            s.my = m_meteor.y + m_meteor.dy * m_meteor.dist * p;
            s.mdx = m_meteor.dx; s.mdy = m_meteor.dy;
            s.mlen = m_meteor.len;
            const double fade = p < 0.2 ? p / 0.2 : 1 - (p - 0.2) / 0.8;
            s.malpha = 0.85 * fade;
        }
    }
    return s;
}

QVector3D SkyItem::bandNormal()
{
    // 银河：一圈和"赤道"夹 55°–70° 的大圆（Electron 是斜着穿过整张图的一条带）
    static const QVector3D n = [] {
        const std::vector<double> r = globe::mulberry32(20261016u, 2);
        const double incl = (55 + r[0] * 15) * M_PI / 180, node = r[1] * 2 * M_PI;
        return QVector3D(float(std::sin(incl) * std::cos(node)), float(std::sin(incl) * std::sin(node)), float(std::cos(incl)));
    }();
    return n;
}

QVector3D SkyItem::nebulaSeed()
{
    static const QVector3D v = [] {
        const std::vector<double> r = globe::mulberry32(20261016u, 5);
        return QVector3D(float(r[2] * 100), float(r[3] * 100), float(r[4] * 100));
    }();
    return v;
}
