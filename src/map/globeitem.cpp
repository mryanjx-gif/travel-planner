#include "globeitem.h"
#include "globetiles.h"
#include "mapcontroller.h"
#include "markerimages.h"

#include <QHash>
#include "sun.h"
#include "tileserver.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QMatrix4x4>
#include <QThreadPool>
#include <QQuickWindow>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <utility>

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

// 底球：经纬网 160 × 80，只要位置
void buildSphere(QVector<float> *v, QVector<quint32> *idx)
{
    const int NX = 160, NY = 80;
    for (int j = 0; j <= NY; ++j) {
        const double lat = -90.0 + 180.0 * j / NY;
        for (int i = 0; i <= NX; ++i) {
            const QVector3D p = toQVec(lat, -180.0 + 360.0 * i / NX);
            *v << p.x() << p.y() << p.z();
        }
    }
    for (int j = 0; j < NY; ++j)
        for (int i = 0; i < NX; ++i) {
            const quint32 a = quint32(j * (NX + 1) + i), b = a + 1, c = a + NX + 1, d = c + 1;
            *idx << a << c << b << b << c << d;
        }
}

// 一块瓦片的网格：33 × 33 个点，(u, v) 在 0..1（v 从北往南）；顶点着色器按实例的墨卡托范围摆到球面上
constexpr int kGrid = 32;
void buildGrid(QVector<float> *v, QVector<quint32> *idx)
{
    for (int j = 0; j <= kGrid; ++j)
        for (int i = 0; i <= kGrid; ++i) *v << float(i) / kGrid << float(j) / kGrid;
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i) {
            const quint32 a = quint32(j * (kGrid + 1) + i), b = a + 1, c = a + kGrid + 1, d = c + 1;
            *idx << a << c << b << b << c << d;
        }
}

struct SyncState {
    double lat = 0, lng = 0, dist = 4;
    QVector3D sun;
    bool lightsOn = true;
    float lightsFade = 1;     // 0.9.5：缩到最远是 1，往里放大 1 级淡成 0（PO 11:05）
    double morph = 0;         // 0.9.7 变形过渡：0 球、1 平面
    // 0.9.5 路线：三维折线（主线程建好、只读共享）、选中 / 碰到的计划、各层透明度（和平面同一套数）、设备像素的视图
    std::shared_ptr<const globe::Routes> routes;
    std::shared_ptr<const globe::Routes> overlay;   // 0.9.7：叠上来的其他旅程（淡灰细线）
    QString selId, hvId;
    float lineOp = 0.92f, edgeOp = 0.85f, hvOp = 0;
    bool linesOn = false;
    globe::View dview;
    double dpr = 1;
    QVector<GlobeTileSet::Instance> instances;
    QVector<GlobeTileSet::Upload> uploads;
    QVector<QImage> lights;   // 0.9.3：新读好的城市灯光（连 mipmap），传上去就清掉
};

// 统一的 uniform：mvp、太阳、相机、参数、海 / 北极 / 南极的颜色、灯光参数、0.9.7 变形（进度 + 切点 + 东 / 北，std140，240 字节）
constexpr quint32 kUboSize = 64 + 16 * 11;

// 0.9.3 城市灯光的亮度（Black Marble 灰度 → 暖黄，夜里按暮光带渐亮；云端截图对照 PO 选的 Q2 A 夜色 62% 调的）
constexpr float kLightsGain = 1.0f;

// ---- 0.9.5 路线：屏幕空间的线（CPU 每帧把三维折线投到屏幕、拉成三角形条；虚线、圆头、抗锯齿在片元着色器里算） ----
struct LineVtx { float x, y, r, g, b, a, along, across, hw, len, on, off; };
static_assert(sizeof(LineVtx) == 48);

QColor colorOf(const QString &css)
{
    QColor c = QColor::fromString(css);
    return c.isValid() ? c : QColor(0xe8, 0x59, 0x0c);
}

// 一段连续的屏幕折线（设备像素）→ 顶点 + 索引。hw = 半个线宽；on / off = 虚线（像素，off = 0 是实线）
void addRun(std::vector<LineVtx> *v, std::vector<quint32> *idx, const globe::ScreenRun &run, float hw, const QColor &c, float alpha,
            float on, float off, double W, double H)
{
    // 抽稀：离上一个留下的点不到 1 像素的去掉（最后一点留着）。0.9.6：缓冲区重复用（每帧几千段，不每段分配）
    thread_local std::vector<globe::Pt2> p;
    thread_local std::vector<double> dx, dy, s;
    p.clear();
    for (size_t i = 0; i < run.size(); ++i) {
        if (!p.empty()) {
            const double ddx = run[i].x - p.back().x, ddy = run[i].y - p.back().y, d2 = ddx * ddx + ddy * ddy;
            if ((i + 1 < run.size() && d2 < 1.0) || d2 < 1e-6) continue;
        }
        p.push_back(run[i]);
    }
    if (p.size() < 2) return;
    const size_t n = p.size();
    dx.assign(n - 1, 0.0);
    dy.assign(n - 1, 0.0);
    s.assign(n, 0.0);
    for (size_t i = 0; i + 1 < n; ++i) {
        const double L = std::hypot(p[i + 1].x - p[i].x, p[i + 1].y - p[i].y);
        dx[i] = (p[i + 1].x - p[i].x) / L;
        dy[i] = (p[i + 1].y - p[i].y) / L;
        s[i + 1] = s[i] + L;
    }
    const double total = s.back(), ext = hw + 1.0;
    const float pa = alpha * float(c.alphaF());
    const float r = float(c.redF()) * pa, g = float(c.greenF()) * pa, b = float(c.blueF()) * pa;
    const quint32 base = quint32(v->size());
    for (size_t i = 0; i < n; ++i) {
        double nx, ny, sc = 1, px = p[i].x, py = p[i].y, along = s[i];
        if (i == 0) { nx = -dy[0]; ny = dx[0]; px -= dx[0] * ext; py -= dy[0] * ext; along = -ext; }
        else if (i + 1 == n) { nx = -dy[n - 2]; ny = dx[n - 2]; px += dx[n - 2] * ext; py += dy[n - 2] * ext; along = total + ext; }
        else {
            const double n1x = -dy[i - 1], n1y = dx[i - 1], n2x = -dy[i], n2y = dx[i];
            double mx = n1x + n2x, my = n1y + n2y;
            const double ml = std::hypot(mx, my);
            if (ml < 1e-6) { mx = n1x; my = n1y; } else { mx /= ml; my /= ml; }
            nx = mx; ny = my;
            sc = std::min(2.0, 1.0 / std::max(0.5, mx * n1x + my * n1y));   // 斜接，最多拉长 2 倍
        }
        for (int side = -1; side <= 1; side += 2) {
            const double qx = px + nx * ext * sc * side, qy = py + ny * ext * sc * side;
            v->push_back({ float(qx / W * 2 - 1), float(1 - qy / H * 2), r, g, b, pa, float(along), float(side), hw, float(total), on, off });
        }
    }
    for (quint32 i = 0; i + 1 < n; ++i) {
        const quint32 a = base + 2 * i;
        idx->insert(idx->end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
    }
}

class GlobeRenderer : public QQuickRhiItemRenderer {
public:
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    void makeTexture(int layers);
    void bindTiles();   // 瓦片的资源：uniform + 纹理数组 + 城市灯光（没有时是 1×1 的黑图）
    void dropLights();
    void buildLines(std::vector<LineVtx> *v, std::vector<quint32> *idx);   // 0.9.5
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_lineV, m_lineI, m_lineU;
    QHash<QString, QColor> m_colors;   // 0.9.6：颜色字符串只解析一次
    std::unique_ptr<QRhiShaderResourceBindings> m_lineSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_linePipe;
    quint32 m_lineVCap = 0, m_lineICap = 0;
    std::unique_ptr<QRhiBuffer> m_vbuf, m_ibuf, m_ubuf, m_gridV, m_gridI, m_inst;
    std::unique_ptr<QRhiTexture> m_tex, m_lightsTex, m_lightsDummy;
    std::unique_ptr<QRhiSampler> m_sampler, m_lightsSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb, m_tileSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipe, m_tilePipe;
    QRhiRenderPassDescriptor *m_rpDesc = nullptr;
    int m_indexCount = 0, m_gridIndexCount = 0;
    int m_texLayers = 0;
    int m_wantLayers = 1;
    int m_instCap = 0;
    bool m_arrays = true;
    bool m_lost = false;   // 渲染那边整个重建过（换了 QRhi）：下次同步时告诉瓦片那边层都没了
    bool m_lightsLost = false;   // 同上，灯光贴图没了：下次同步时告诉 GlobeItem 重新读
    SyncState m_s;
    GlobeItem *m_item = nullptr;
};

void GlobeRenderer::makeTexture(int layers)
{
    m_tex.reset(m_rhi->newTextureArray(QRhiTexture::RGBA8, layers, QSize(256, 256), 1, QRhiTexture::MipMapped));
    m_tex->create();
    m_texLayers = layers;
    bindTiles();
    if (m_item) m_item->setGpuLayers(layers);
}

void GlobeRenderer::bindTiles()
{
    if (!m_tileSrb || !m_tex || !m_lightsDummy) return;
    QRhiTexture *lights = m_lightsTex ? m_lightsTex.get() : m_lightsDummy.get();
    m_tileSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_tex.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, lights, m_lightsSampler.get()) });
    m_tileSrb->create();
}

void GlobeRenderer::dropLights()
{
    if (!m_lightsTex) return;
    m_lightsTex.reset();   // 约 41 MB 显存马上还（QRhi 等用过它的帧做完才真的释放）
    bindTiles();
    if (m_item) QMetaObject::invokeMethod(m_item, "setLightsUploaded", Qt::QueuedConnection, Q_ARG(bool, false));
}

void GlobeRenderer::synchronize(QQuickRhiItem *item)
{
    auto *g = static_cast<GlobeItem *>(item);
    m_item = g;
    GlobeTileSet *tiles = g->tiles();
    // 在地球上：kLayers 层；离开地球（0.8.6 的做法）：换成 1 层的小纹理，显存还回去
    const int want = g->active() ? GlobeTileSet::kLayers : 1;
    if (want != m_wantLayers || m_lost) {
        m_wantLayers = want;
        m_lost = false;
        tiles->gpuLost();      // 层里的东西都作废（新纹理是空的）
        m_s.uploads.clear();
        if (m_rhi && m_tileSrb && m_texLayers != want) makeTexture(want);   // 和 0.8.6 一样在同步时就换（离开地球马上还显存）
    }
    // 城市灯光（0.9.3）：离开地球时显存还回去；渲染那边重建过就让 GlobeItem 重新读；新读好的接过来（render 里传）
    if (!g->active()) { dropLights(); m_s.lights.clear(); }
    if (m_lightsLost) { m_lightsLost = false; g->setLightsUploaded(false); }
    if (g->active()) {
        QVector<QImage> l = g->takeLights();
        if (!l.isEmpty()) m_s.lights = std::move(l);
    }
    if (!g->active() && g->releasePending() && m_texLayers <= 1 && !m_lightsTex)
        QMetaObject::invokeMethod(g, "releaseDone", Qt::QueuedConnection);
    m_s.lat = g->centerLat(); m_s.lng = g->centerLng(); m_s.dist = g->distance();
    const SubSolar sun = subsolarPoint(g->now());
    m_s.sun = toQVec(sun.lat, sun.lng);
    m_s.lightsOn = g->nightLights();
    m_s.lightsFade = float(g->lightsFade());
    m_s.morph = g->morph();
    // 0.9.5 路线
    m_s.linesOn = false;
    if (g->active()) {
        if (auto *mc = qobject_cast<MapController *>(g->mapController())) {
            m_s.routes = mc->globeRoutes();
            m_s.overlay = mc->overlayTrips().plans().isEmpty() ? nullptr : mc->globeOverlayRoutes();
            m_s.selId = mc->markers()->selectedPlan();
            m_s.hvId = mc->markers()->hvPlan();
            m_s.lineOp = float(mc->routeOpacity());
            m_s.edgeOp = float(mc->edgeOpacity());
            m_s.hvOp = float(mc->hvOpacity());
            m_s.linesOn = mc->markers()->shown() && mc->globeOn();
        }
    }
    m_s.dview = g->deviceView();
    m_s.dpr = g->window() ? g->window()->effectiveDevicePixelRatio() : 1.0;
    GlobeTileSet::Frame f = tiles->takeFrame();
    m_s.instances = std::move(f.instances);
    // 上传的累加（上一帧没传完的不会丢：takeFrame 只交出这一帧要传的）
    for (GlobeTileSet::Upload &u : f.uploads) m_s.uploads.push_back(std::move(u));
}

void GlobeRenderer::initialize(QRhiCommandBuffer *cb)
{
    if (m_rhi != rhi() || m_rpDesc != renderTarget()->renderPassDescriptor()) {
        if (m_rhi != rhi() && m_lightsTex) { m_lightsTex.reset(); m_lightsLost = true; }   // 换了 QRhi：旧的贴图不能用了
        m_pipe.reset(); m_tilePipe.reset(); m_linePipe.reset();
        m_rhi = rhi();
        m_rpDesc = renderTarget()->renderPassDescriptor();
    }
    if (m_pipe) return;
    m_arrays = m_rhi->isFeatureSupported(QRhi::TextureArrays) && m_rhi->isFeatureSupported(QRhi::Instancing);
    if (!m_arrays) qWarning("地球：显卡不支持纹理数组或实例化绘制，只画底球（没有瓦片）");
    QVector<float> verts; QVector<quint32> idx;
    buildSphere(&verts, &idx);
    m_indexCount = int(idx.size());
    m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(verts.size() * sizeof(float))));
    m_vbuf->create();
    m_ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(idx.size() * sizeof(quint32))));
    m_ibuf->create();
    QVector<float> gv; QVector<quint32> gi;
    buildGrid(&gv, &gi);
    m_gridIndexCount = int(gi.size());
    m_gridV.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(gv.size() * sizeof(float))));
    m_gridV->create();
    m_gridI.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(gi.size() * sizeof(quint32))));
    m_gridI->create();
    m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUboSize));
    m_ubuf->create();
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    // 城市灯光：经度方向绕一圈（Repeat），纬度方向夹住；还没读好时绑一张 1×1 的黑图
    m_lightsSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                            QRhiSampler::Repeat, QRhiSampler::ClampToEdge));
    m_lightsSampler->create();
    m_lightsDummy.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
    m_lightsDummy->create();
    static const quint8 black[4] = { 0, 0, 0, 0 };

    // 底球
    m_srb.reset(m_rhi->newShaderResourceBindings());
    m_srb->setBindings({ QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()) });
    m_srb->create();
    m_pipe.reset(m_rhi->newGraphicsPipeline());
    m_pipe->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/globe.vert.qsb")) },
                              { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/globe.frag.qsb")) } });
    QRhiVertexInputLayout il;
    il.setBindings({ { 3 * sizeof(float) } });
    il.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
    m_pipe->setVertexInputLayout(il);
    m_pipe->setDepthTest(true);
    m_pipe->setDepthWrite(true);
    m_pipe->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_pipe->setCullMode(QRhiGraphicsPipeline::None);   // 不剔除：Vulkan 的 Y 翻转会让绕序反过来，靠深度测试遮挡背面
    m_pipe->setSampleCount(renderTarget()->sampleCount());
    m_pipe->setShaderResourceBindings(m_srb.get());
    m_pipe->setRenderPassDescriptor(m_rpDesc);
    m_pipe->create();

    // 瓦片
    const bool hadLayers = m_texLayers > 1;
    m_tileSrb.reset(m_rhi->newShaderResourceBindings());
    makeTexture(1);
    if (m_arrays) {
        m_tilePipe.reset(m_rhi->newGraphicsPipeline());
        m_tilePipe->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/globetile.vert.qsb")) },
                                      { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/globetile.frag.qsb")) } });
        QRhiVertexInputLayout tl;
        tl.setBindings({ { 2 * sizeof(float) }, { sizeof(GlobeTileSet::Instance), QRhiVertexInputBinding::PerInstance } });
        tl.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
                           { 1, 1, QRhiVertexInputAttribute::Float4, 0 },
                           { 1, 2, QRhiVertexInputAttribute::Float4, 16 },
                           { 1, 3, QRhiVertexInputAttribute::Float, 32 } });
        m_tilePipe->setVertexInputLayout(tl);
        m_tilePipe->setDepthTest(true);
        m_tilePipe->setDepthWrite(true);
        m_tilePipe->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        m_tilePipe->setCullMode(QRhiGraphicsPipeline::None);
        m_tilePipe->setSampleCount(renderTarget()->sampleCount());
        m_tilePipe->setShaderResourceBindings(m_tileSrb.get());
        m_tilePipe->setRenderPassDescriptor(m_rpDesc);
        m_tilePipe->create();
    }

    // 0.9.5 路线：屏幕空间的三角形，预乘透明度混合，不做深度测试（被球挡住的部分在 CPU 上已经去掉）
    m_lineU.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    m_lineU->create();
    m_lineSrb.reset(m_rhi->newShaderResourceBindings());
    m_lineSrb->setBindings({ QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_lineU.get()) });
    m_lineSrb->create();
    m_linePipe.reset(m_rhi->newGraphicsPipeline());
    m_linePipe->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/globeline.vert.qsb")) },
                                  { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/globeline.frag.qsb")) } });
    {
        QRhiVertexInputLayout ll;
        ll.setBindings({ { sizeof(LineVtx) } });
        ll.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
                           { 0, 1, QRhiVertexInputAttribute::Float4, 8 },
                           { 0, 2, QRhiVertexInputAttribute::Float4, 24 },
                           { 0, 3, QRhiVertexInputAttribute::Float2, 40 } });
        m_linePipe->setVertexInputLayout(ll);
    }
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    m_linePipe->setTargetBlends({ blend });
    m_linePipe->setDepthTest(false);
    m_linePipe->setDepthWrite(false);
    m_linePipe->setCullMode(QRhiGraphicsPipeline::None);
    m_linePipe->setSampleCount(renderTarget()->sampleCount());
    m_linePipe->setShaderResourceBindings(m_lineSrb.get());
    m_linePipe->setRenderPassDescriptor(m_rpDesc);
    m_linePipe->create();

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->uploadStaticBuffer(m_vbuf.get(), verts.constData());
    u->uploadStaticBuffer(m_ibuf.get(), idx.constData());
    u->uploadStaticBuffer(m_gridV.get(), gv.constData());
    u->uploadStaticBuffer(m_gridI.get(), gi.constData());
    {
        QRhiTextureSubresourceUploadDescription sd(black, 1);
        sd.setSourceSize(QSize(1, 1));
        u->uploadTexture(m_lightsDummy.get(), QRhiTextureUploadEntry(0, 0, sd));
    }
    cb->resourceUpdate(u);
    // 纹理是新建的（1 层）：之前排好的层都作废，下次同步时告诉瓦片那边（这里主线程没有被挡住，不能直接动它）
    m_lost = hadLayers;
}

void GlobeRenderer::render(QRhiCommandBuffer *cb)
{
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    if (m_texLayers != m_wantLayers) makeTexture(m_wantLayers);   // 第一帧：同步时还没有 QRhi（这一帧的上传就是给新纹理的）
    // 城市灯光（0.9.3）：灰度一个通道（R8），mipmap 在后台算好的，这里一次传上去（约 41 MB）
    if (!m_s.lights.isEmpty()) {
        const QSize sz0 = m_s.lights.first().size();
        m_lightsTex.reset(m_rhi->newTexture(QRhiTexture::R8, sz0, 1, QRhiTexture::MipMapped));
        if (m_lightsTex->create()) {
            QVarLengthArray<QRhiTextureUploadEntry, 16> entries;
            for (int level = 0; level < m_s.lights.size(); ++level) {
                const QImage &img = m_s.lights[level];
                QRhiTextureSubresourceUploadDescription sd(img.constBits(), quint32(img.sizeInBytes()));
                sd.setSourceSize(img.size());
                sd.setDataStride(quint32(img.bytesPerLine()));
                entries.append(QRhiTextureUploadEntry(0, level, sd));
            }
            QRhiTextureUploadDescription desc;
            desc.setEntries(entries.cbegin(), entries.cend());
            u->uploadTexture(m_lightsTex.get(), desc);
            bindTiles();
            if (m_item) QMetaObject::invokeMethod(m_item, "setLightsUploaded", Qt::QueuedConnection, Q_ARG(bool, true));
        } else {
            qWarning("地球：城市灯光的纹理建不起来（%dx%d），不画灯光", sz0.width(), sz0.height());
            m_lightsTex.reset();
        }
        m_s.lights.clear();   // 上传要用到的数据 QRhi 已经复制了一份（uploadTexture 时深拷贝）
    }
    if (m_arrays) {
        for (const GlobeTileSet::Upload &up : std::as_const(m_s.uploads)) {
            if (up.layer < 0 || up.layer >= m_texLayers) continue;
            QVarLengthArray<QRhiTextureUploadEntry, 9> entries;
            for (int level = 0; level < up.mips.size(); ++level)
                entries.append(QRhiTextureUploadEntry(up.layer, level, QRhiTextureSubresourceUploadDescription(up.mips[level])));
            QRhiTextureUploadDescription desc;
            desc.setEntries(entries.cbegin(), entries.cend());
            u->uploadTexture(m_tex.get(), desc);
        }
    }
    m_s.uploads.clear();
    // 实例
    const int n = m_arrays && m_texLayers > 1 ? int(m_s.instances.size()) : 0;
    if (n > m_instCap) {
        m_instCap = std::max(256, n * 3 / 2);
        m_inst.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, quint32(m_instCap * sizeof(GlobeTileSet::Instance))));
        m_inst->create();
    }
    if (n > 0) u->updateDynamicBuffer(m_inst.get(), 0, quint32(n * sizeof(GlobeTileSet::Instance)), m_s.instances.constData());

    const QSize sz = renderTarget()->pixelSize();
    QMatrix4x4 proj = m_rhi->clipSpaceCorrMatrix();
    // 近平面跟着相机高度走：拉得很近时深度精度够用
    const float nearP = float(std::clamp((m_s.dist - 1.0) * 0.3, 0.002, 0.05));
    proj.perspective(float(globe::kFovYDeg), float(sz.width()) / qMax(1, sz.height()), nearP, 50.0f);
    const QVector3D eye = toQVec(m_s.lat, m_s.lng) * float(m_s.dist);
    QMatrix4x4 view;
    // 上方向：地轴（z），在两极附近时改用 y 避免退化（和 globegeom.cpp 的 Camera 一样）
    const QVector3D up = std::abs(m_s.lat) > 89.5 ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
    view.lookAt(eye, QVector3D(0, 0, 0), up);
    const QMatrix4x4 vp = proj * view;
    // params：x = 夜晚遮罩不透明度（照 Electron 62%，PO 选 Q2 A），y = 暮光带宽度 12°，z = 底球半径，w = 光边强度
    // 0.9.7：变形时昼夜、光边、城市灯光跟着淡掉（平面上没有）
    const float keep = float(1.0 - std::clamp(m_s.morph, 0.0, 1.0));
    const float params[4] = { 0.62f * keep, 12.0f, 0.9985f, 1.0f * keep };
    // lightParams：x = 画不画城市灯光，y = 亮度
    // 0.9.5：灯光只在缩到最远时显示（往里放大 1 级淡掉，PO 11:05）；淡没了就不取样
    const float lf = m_s.lightsFade * keep;
    const float lightParams[4] = { m_s.lightsOn && m_lightsTex && lf > 0.001f ? 1.0f : 0.0f, kLightsGain * lf, 0, 0 };
    const float sun[4] = { m_s.sun.x(), m_s.sun.y(), m_s.sun.z(), 0 };
    const float cam[4] = { eye.x(), eye.y(), eye.z(), 1 };
    // 瓦片没到的地方 / 两极（墨卡托只到 ±85.05°）的颜色
    const float ocean[4] = { 16 / 255.f, 42 / 255.f, 70 / 255.f, 1 };
    const float north[4] = { 0.80f, 0.85f, 0.90f, 1 };   // 北冰洋：冰
    const float south[4] = { 0.86f, 0.89f, 0.92f, 1 };   // 南极洲：冰
    u->updateDynamicBuffer(m_ubuf.get(), 0, 64, vp.constData());
    u->updateDynamicBuffer(m_ubuf.get(), 64, 16, sun);
    u->updateDynamicBuffer(m_ubuf.get(), 80, 16, cam);
    u->updateDynamicBuffer(m_ubuf.get(), 96, 16, params);
    u->updateDynamicBuffer(m_ubuf.get(), 112, 16, ocean);
    u->updateDynamicBuffer(m_ubuf.get(), 128, 16, north);
    u->updateDynamicBuffer(m_ubuf.get(), 144, 16, south);
    u->updateDynamicBuffer(m_ubuf.get(), 160, 16, lightParams);
    {
        // 0.9.7 变形：切点 C、朝东 E、朝北 N（和 lookAt 的右 / 上一样：右 = 前 × 地轴，上 = 右 × 前）
        const double la = m_s.lat * M_PI / 180, lo = m_s.lng * M_PI / 180;
        const float mo[4] = { float(std::clamp(m_s.morph, 0.0, 1.0)), float(globe::mercX(m_s.lng)), float(globe::mercY(m_s.lat)),
                              float(2 * M_PI * std::max(0.05, std::cos(la))) };
        const float mc[4] = { float(std::cos(la) * std::cos(lo)), float(std::cos(la) * std::sin(lo)), float(std::sin(la)), 0 };
        const float me[4] = { float(-std::sin(lo)), float(std::cos(lo)), 0, 0 };
        const float mn[4] = { float(-std::sin(la) * std::cos(lo)), float(-std::sin(la) * std::sin(lo)), float(std::cos(la)), 0 };
        u->updateDynamicBuffer(m_ubuf.get(), 176, 16, mo);
        u->updateDynamicBuffer(m_ubuf.get(), 192, 16, mc);
        u->updateDynamicBuffer(m_ubuf.get(), 208, 16, me);
        u->updateDynamicBuffer(m_ubuf.get(), 224, 16, mn);
    }

    // 0.9.5 路线
    thread_local std::vector<LineVtx> lv;
    thread_local std::vector<quint32> li;
    lv.clear();
    li.clear();
    if (m_s.linesOn && m_s.routes && m_linePipe) {
        QElapsedTimer lt;
        lt.start();
        buildLines(&lv, &li);
        if (m_item) m_item->setLineStats(int(li.size() / 6), int(lv.size()), lt.nsecsElapsed() / 1e6);   // 也累计平均 / 最慢（0.9.6）
    } else if (m_item) {
        m_item->setLineStats(0, 0, 0);
    }
    if (!lv.empty()) {
        const quint32 vb = quint32(lv.size() * sizeof(LineVtx)), ib = quint32(li.size() * sizeof(quint32));
        if (vb > m_lineVCap) {
            m_lineVCap = std::max<quint32>(64 * 1024, vb * 3 / 2);
            m_lineV.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, m_lineVCap));
            m_lineV->create();
        }
        if (ib > m_lineICap) {
            m_lineICap = std::max<quint32>(32 * 1024, ib * 3 / 2);
            m_lineI.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, m_lineICap));
            m_lineI->create();
        }
        u->updateDynamicBuffer(m_lineV.get(), 0, vb, lv.data());
        u->updateDynamicBuffer(m_lineI.get(), 0, ib, li.data());
        // 顶点里已经是 NDC（y 朝上）；NDC 的 y 朝下的后端（Vulkan）翻一下
        const float lu[4] = { m_rhi->isYUpInNDC() ? 1.0f : -1.0f, 0, 0, 0 };
        u->updateDynamicBuffer(m_lineU.get(), 0, 16, lu);
    }

    // 0.9.3：背景透明（星空在后面的 SkyItem 里）
    cb->beginPass(renderTarget(), QColor::fromRgbF(0, 0, 0, 0), { 1.0f, 0 }, u);
    // 视口要在绑定管线之后设：Vulkan 下 QRhi 在 setViewport 时顺带设置裁剪矩形，没绑管线就不会设（技术选型.md §8）
    const QRhiViewport vp_(0, 0, float(sz.width()), float(sz.height()));
    cb->setGraphicsPipeline(m_pipe.get());
    cb->setViewport(vp_);
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vin(m_vbuf.get(), 0);
    cb->setVertexInput(0, 1, &vin, m_ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
    // 0.9.7：变形时不画底球（瓦片挪到平面上以后，空出来的地方露出下面的平面地图，不露出一个球）
    if (m_s.morph <= 0) cb->drawIndexed(m_indexCount);
    if (n > 0 && m_tilePipe) {
        cb->setGraphicsPipeline(m_tilePipe.get());
        cb->setViewport(vp_);
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput tin[2] = { { m_gridV.get(), 0 }, { m_inst.get(), 0 } };
        cb->setVertexInput(0, 2, tin, m_gridI.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(m_gridIndexCount, n);
    }
    if (!li.empty()) {
        cb->setGraphicsPipeline(m_linePipe.get());
        cb->setViewport(vp_);
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput lin(m_lineV.get(), 0);
        cb->setVertexInput(0, 1, &lin, m_lineI.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(li.size()));
    }
    cb->endPass();
}

void GlobeRenderer::buildLines(std::vector<LineVtx> *v, std::vector<quint32> *idx)
{
    // 和平面同一套（Electron LINE_STYLE / DASH_PX，0.8.4）：平时一层（除了选中的计划：白边 6.5 px、彩线 3.5 px，
    // 其余变淡时 ×0.3）→ 鼠标碰到的高亮层（白边 9、彩线 5，透明度 = 高亮程度）→ 选中的（白边 9、彩线 5，不透明）。
    // 计划中虚线 4/8（选中 4/10）、想去点线 0.5/7、已完成实线。
    // 0.9.6（PO 电脑上 0.9.5 一帧 21 ms）：每段只投影一次（白边和彩线共用；白边改成按段画，站点处两头的圆头叠在一起，
    // 正好被标记盖住）；整段在地平线以外的不投影；颜色只解析一次；缓冲区重复用
    const globe::Camera cam(m_s.dview);
    const double W = m_s.dview.width, H = m_s.dview.height, k = m_s.dpr;
    const QColor white(Qt::white);
    auto dashOf = [](int dash, bool sel, float *on, float *off) {
        if (dash == 2) { *on = 0.5f; *off = 7; }
        else if (dash == 1) { *on = 4; *off = sel ? 10.0f : 8.0f; }
        else { *on = 0; *off = 0; }
    };
    auto colorFor = [this](const QString &css) {
        auto it = m_colors.constFind(css);
        if (it != m_colors.constEnd()) return *it;
        const QColor c = colorOf(css);
        m_colors.insert(css, c);
        return c;
    };
    struct LegRuns { const globe::RouteLeg *leg; std::vector<globe::ScreenRun> runs; };
    struct Layer { const globe::RoutePlan *p; float lw, ew, lop, eop; bool sel; std::vector<LegRuns> legs; };
    auto project = [&](Layer &L) {
        for (const globe::RouteLeg &leg : L.p->legs) {
            if (!globe::legMaybeVisible(m_s.dview, leg)) continue;
            auto runs = globe::screenRuns(cam, leg.pts);
            // 整段在屏幕外的不拉三角形（拉近时大多数线在屏幕外）
            const double m = 12 * k;
            runs.erase(std::remove_if(runs.begin(), runs.end(), [&](const globe::ScreenRun &r) {
                double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
                for (const globe::Pt2 &q : r) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
                return x1 < -m || y1 < -m || x0 > W + m || y0 > H + m;
            }), runs.end());
            if (!runs.empty()) L.legs.push_back({ &leg, std::move(runs) });
        }
    };
    std::vector<Layer> idle, top;
    for (const globe::RoutePlan &p : m_s.routes->plans) {
        if (p.id == m_s.selId) continue;
        idle.push_back({ &p, 3.5f, 6.5f, m_s.lineOp, m_s.edgeOp, false, {} });
    }
    for (const globe::RoutePlan &p : m_s.routes->plans)
        if (!m_s.hvId.isEmpty() && p.id == m_s.hvId && p.id != m_s.selId && m_s.hvOp > 0.001f) top.push_back({ &p, 5, 9, m_s.hvOp, m_s.hvOp, false, {} });
    for (const globe::RoutePlan &p : m_s.routes->plans)
        if (!m_s.selId.isEmpty() && p.id == m_s.selId) top.push_back({ &p, 5, 9, 1, 1, true, {} });
    // 0.9.7：叠上来的其他旅程：淡灰细线（1.6 px、实线、55%），垫在所有路线下面（和平面 rt-overlay 一样）
    if (m_s.overlay) {
        const QColor grey(0x8b, 0x97, 0xa2);
        for (const globe::RoutePlan &p : m_s.overlay->plans) {
            Layer L{ &p, 1.6f, 0, 0.55f, 0, false, {} };
            project(L);
            for (const LegRuns &lr : L.legs)
                for (const globe::ScreenRun &r : lr.runs) addRun(v, idx, r, float(1.6 * k / 2), grey, 0.55f, 0, 0, W, H);
        }
    }
    for (Layer &L : idle) project(L);
    for (Layer &L : top) project(L);
    auto drawEdges = [&](const Layer &L) {
        for (const LegRuns &lr : L.legs)
            for (const globe::ScreenRun &r : lr.runs) addRun(v, idx, r, float(L.ew * k / 2), white, L.eop, 0, 0, W, H);
    };
    auto drawLines = [&](const Layer &L) {
        float on, off;
        dashOf(L.p->dash, L.sel, &on, &off);
        for (const LegRuns &lr : L.legs) {
            const QColor c = colorFor(lr.leg->color);
            for (const globe::ScreenRun &r : lr.runs) addRun(v, idx, r, float(L.lw * k / 2), c, L.lop, float(on * k), float(off * k), W, H);
        }
    };
    for (const Layer &L : idle) drawEdges(L);
    for (const Layer &L : idle) drawLines(L);
    for (const Layer &L : top) { drawEdges(L); drawLines(L); }
}
} // namespace

// ================= GlobeItem =================

GlobeItem::GlobeItem(QQuickItem *parent) : QQuickRhiItem(parent)
{
    setAlphaBlending(true);   // 0.9.3：背景透明，后面是 SkyItem
    m_tiles = new GlobeTileSet(this);
    m_tiles->setAdminFile(QCoreApplication::applicationDirPath() + QStringLiteral("/assets/admin/adm1.geojson"));
    connect(m_tiles, &GlobeTileSet::changed, this, [this] { scheduleTiles(); emit tilesChanged(); });
    m_tileTimer.setSingleShot(true);
    m_tileTimer.setInterval(0);
    connect(&m_tileTimer, &QTimer::timeout, this, &GlobeItem::doTiles);
    m_animTimer.setInterval(16);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, &GlobeItem::tick);
    // 昼夜阴影随时间移动：每分钟重画一次
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this] { if (m_active) update(); });
    t->start(60000);
    // 空闲自转（0.9.3）
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (!m_spinAllowed || !m_active || !isVisible() || animating() || morphing() || m_morph > 0) { updateSpinTimer(); return; }
        m_spinning = true;
        m_spinClock.start();
        m_spinLast = 0;
        m_spinTimer.start();
        qInfo("地球：%.1f 秒没有操作，开始自转", m_idleDelay / 1000.0);
        emit spinChanged();
    });
    m_spinTimer.setInterval(33);   // 约 30 帧：一秒转 1°，赤道上每帧不到半个像素，够顺；比 60 帧省一半显卡
    m_spinTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_spinTimer, &QTimer::timeout, this, &GlobeItem::spinTick);
    // 0.9.5：镜头变了告诉 MapController（标记跟着摆、比例尺）；在动 / 停了也告诉（提示框、合并）
    connect(this, &GlobeItem::viewChanged, this, &GlobeItem::pushView);
    connect(this, &GlobeItem::spinChanged, this, &GlobeItem::updateMoving);
    // 交通小图标：约 30 帧（QML 覆盖层，不重画地球）
    m_vehTimer.setInterval(33);
    m_vehTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_vehTimer, &QTimer::timeout, this, &GlobeItem::vehicleTick);
    // 0.9.7 变形过渡
    m_morphTimer.setInterval(16);
    m_morphTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_morphTimer, &QTimer::timeout, this, &GlobeItem::morphTick);
}

GlobeItem::~GlobeItem()
{
    if (m_mapc) m_mapc->setGlobeOps(nullptr);
}

QQuickRhiItemRenderer *GlobeItem::createRenderer() { return new GlobeRenderer; }

QObject *GlobeItem::mapController() const { return m_mapc.data(); }

void GlobeItem::setMapController(QObject *o)
{
    auto *m = qobject_cast<MapController *>(o);
    if (m == m_mapc) return;
    if (m_mapc) { m_mapc->setGlobeOps(nullptr); disconnect(m_mapc, nullptr, this, nullptr); disconnect(m_mapc->markers(), nullptr, this, nullptr); }
    m_mapc = m;
    m_tiles->setTileServer(m ? m->tileServer() : nullptr);
    if (m) {
        m->setGlobeOps(this);
        // 0.9.5：路线、高亮 / 变淡、选中变了要重画；选中变了小图标重新排
        auto redraw = [this] { if (m_active) update(); };
        connect(m, &MapController::routesChanged, this, redraw);
        connect(m, &MapController::routesHvChanged, this, redraw);
        connect(m, &MapController::lineStyleChanged, this, redraw);
        connect(m, &MapController::dataChanged, this, [this] { m_vehPlanId.clear(); if (m_active) { replanVehicle(); update(); } });
        connect(m, &MapController::globeOnChanged, this, [this] { updateVehicleTimer(); if (m_active) update(); });
        connect(m->markers(), &MarkerLayer::highlightChanged, this, redraw);
        connect(m->markers(), &MarkerLayer::shownChanged, this, redraw);
        // 0.9.6：只在地球上时才排小图标（0.9.5 在平面上选中计划也排，第一次要建地球上的线，主线程卡一下，PO 电脑上自检"点线选中"那项变淡慢了）
        connect(m->markers(), &MarkerLayer::selectionChanged, this, [this] { if (m_active && m_mapc && m_mapc->globeOn()) replanVehicle(); updateVehicleTimer(); if (m_active) update(); });
    }
    emit mapControllerChanged();
}

// 镜头被别处移动（定位、自检、拖动惯性…）也算操作；自转自己转的不算
void GlobeItem::setCenterLat(double v) { v = std::clamp(v, -85.0, 85.0); if (v == m_lat) return; m_lat = v; noteActivity(); scheduleTiles(); update(); emit viewChanged(); }
void GlobeItem::setCenterLng(double v)
{
    v = std::fmod(std::fmod(v + 540.0, 360.0) + 360.0, 360.0) - 180.0;
    if (v == m_lng) return;
    m_lng = v; noteActivity(); scheduleTiles(); update(); emit viewChanged();
}
// 0.9.3：参数从 d 改名（MSVC C4458：和 QQuickItem 的成员 d 同名）
double GlobeItem::clampDist(double dist) const
{
    // 还不知道自己多大（QML 刚建好、高度是 0）时不按窗口限最远，免得一开始就被夹到很近
    return std::clamp(dist, globe::kMinDist, height() > 1 ? maxDistance() : globe::kMaxDistCap);
}
void GlobeItem::setDistance(double v) { v = clampDist(v); if (v == m_dist) return; m_dist = v; noteActivity(); scheduleTiles(); update(); emit viewChanged(); }

void GlobeItem::setActive(bool v)
{
    if (v == m_active) return;
    m_active = v;
    if (!m_active) {
        // 0.9.7：离开地球时变形回到球（下次进来从球开始；卷回地球时 QML 会先 setMorph(1)）
        m_morphTimer.stop();
        m_morphWaiting = false;
        if (m_morph != 0) { m_morph = 0; emit morphChanged(); }
        // 离开地球（0.8.6 起）：瓦片、省界、城市灯光全丢掉，再同步一帧让渲染那边换掉大纹理
        stop();
        m_tileTimer.stop();
        m_tiles->release();
        ++m_lightsGen;
        m_lights.clear();
        m_lightsLoading = false;
        if (!m_releasePending) { m_releasePending = true; emit releasePendingChanged(); }
    } else {
        setDistance(m_dist);   // 窗口大小可能变了，最远的限制跟着变
        scheduleTiles();
        if (m_nightLights && !m_lightsReady) loadLights();
    }
    noteActivity();
    updateSpinTimer();
    updateVehicleTimer();
    update();
    emit activeChanged();
}

// ---- 城市灯光（0.9.3）----

QString GlobeItem::lightsPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/assets/nightlights/blackmarble-2016.jpg");
}

void GlobeItem::setNightLights(bool on)
{
    if (on == m_nightLights) return;
    m_nightLights = on;
    if (on && m_active && !m_lightsReady) loadLights();
    update();
    emit nightLightsChanged();
}

void GlobeItem::loadLights()
{
    if (m_lightsLoading) return;
    m_lightsLoading = true;
    const int gen = m_lightsGen;
    QPointer<GlobeItem> self(this);
    // 后台：解码 8000×4000 的 JPEG（约 0.2–0.4 秒），转成一个通道，算好 mipmap（每级缩一半，和 QRhi 的级数一致）
    QThreadPool::globalInstance()->start([self, gen, path = lightsPath()] {
        QElapsedTimer clock;
        clock.start();
        QImage img(path);
        QVector<QImage> mips;
        if (!img.isNull()) {
            img = img.convertToFormat(QImage::Format_Grayscale8);
            mips.push_back(img);
            QSize sz = img.size();
            while (sz.width() > 1 || sz.height() > 1) {
                sz = QSize(std::max(1, sz.width() / 2), std::max(1, sz.height() / 2));
                mips.push_back(mips.last().scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }
        const qint64 ms = clock.elapsed();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, mips, ms, path] {
            if (!self || gen != self->m_lightsGen) return;
            self->m_lightsLoading = false;
            if (mips.isEmpty()) { qWarning("地球：城市灯光没读到（%s），不画灯光", qUtf8Printable(path)); return; }
            self->m_lightsSize = mips.first().size();
            self->m_lights = mips;
            qInfo("[计时] 地球城市灯光：读图 + mipmap %lld ms（%dx%d，%lld 级）", ms, self->m_lightsSize.width(), self->m_lightsSize.height(), qint64(mips.size()));
            self->update();
        }, Qt::QueuedConnection);
    });
}

QVector<QImage> GlobeItem::takeLights() { return std::exchange(m_lights, {}); }

void GlobeItem::setLightsUploaded(bool on)
{
    if (on == m_lightsReady) return;
    m_lightsReady = on;
    // 显卡上的没了但还在地球上（渲染那边重建过）：重新读
    if (!on && m_active && m_nightLights) loadLights();
    emit lightsReadyChanged();
}

// ---- 空闲自转（0.9.3，C7）----

void GlobeItem::setSpinAllowed(bool on)
{
    if (on == m_spinAllowed) return;
    m_spinAllowed = on;
    if (!on && m_spinning) { m_spinning = false; m_spinTimer.stop(); }
    updateSpinTimer();
    emit spinChanged();
}

void GlobeItem::setIdleDelay(int ms)
{
    ms = std::max(100, ms);
    if (ms == m_idleDelay) return;
    m_idleDelay = ms;
    updateSpinTimer();
    emit spinChanged();
}

void GlobeItem::updateSpinTimer()
{
    if (m_spinAllowed && m_active && !m_spinning) m_idleTimer.start(m_idleDelay);
    else m_idleTimer.stop();
}

void GlobeItem::noteActivity()
{
    if (m_spinStep) return;
    if (m_spinning) {
        m_spinning = false;
        m_spinTimer.stop();
        emit spinChanged();
    }
    if (m_spinAllowed) updateSpinTimer();
}

void GlobeItem::spinTick()
{
    if (!m_spinning || !m_spinAllowed || !m_active) { m_spinTimer.stop(); return; }
    const qint64 t = m_spinClock.elapsed();
    const double dt = (t - m_spinLast) / 1000.0;
    m_spinLast = t;
    // 约 6 分钟一圈（1°/秒），头 2 秒慢慢加速；向东转（和地球自转同向：地面往右走，中心经度变小）
    const double ramp = std::clamp(t / 2000.0, 0.0, 1.0);
    const double speed = 1.0 * ramp * ramp * (3 - 2 * ramp);
    m_spinStep = true;
    setCenterLng(m_lng - speed * std::min(dt, 0.2));
    m_spinStep = false;
}

void GlobeItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickRhiItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        if (m_filterWin) m_filterWin->removeEventFilter(this);
        m_filterWin = value.window;
        if (m_filterWin) m_filterWin->installEventFilter(this);
    }
}

bool GlobeItem::eventFilter(QObject *watched, QEvent *event)
{
    // 窗口里任何鼠标、键盘、触摸操作都算"有操作"（看列表、打字时地球也不转）
    if (m_spinAllowed || m_spinning) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: case QEvent::MouseMove: case QEvent::Wheel: case QEvent::KeyPress:
        case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TabletPress: case QEvent::HoverMove:
            noteActivity();
            break;
        default:
            break;
        }
    }
    return QQuickRhiItem::eventFilter(watched, event);
}

void GlobeItem::releaseDone()
{
    if (!m_releasePending || m_active) return;
    m_releasePending = false;
    emit releasePendingChanged();
}

void GlobeItem::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickRhiItem::geometryChange(n, o);
    if (n.size() != o.size()) {
        if (n.height() > 1 && !qIsNaN(m_pendingZoom)) setFromZoomLevel(m_pendingZoom);
        if (m_dist > maxDistance()) setDistance(maxDistance());
        scheduleTiles();
        pushView();
    }
}

// ---- 0.9.5 ----

void GlobeItem::pushView()
{
    if (m_mapc && m_active) m_mapc->globeViewChanged();
}

void GlobeItem::updateMoving()
{
    const bool mv = moving();
    if (mv == m_lastMoving) return;
    m_lastMoving = mv;
    pushView();
    if (!mv) replanVehicle();   // 停下来按新的屏幕长度重新排小图标的时长
    emit movingChanged();
}

double GlobeItem::lightsFade() const
{
    // 离最远还有几级（相机高度每减一半 = 一级）；0 级完全显示，1 级完全没有，中间平滑过渡
    const double lv = std::log2(std::max(1e-6, maxDistance() - 1.0) / std::max(1e-6, m_dist - 1.0));
    const double t = std::clamp(lv, 0.0, 1.0);
    return 1.0 - t * t * (3 - 2 * t);
}

QVariantMap GlobeItem::lineStats() const
{
    const int n = m_lineFrames.loadAcquire();
    return { { "runs", m_lineRuns.loadAcquire() }, { "verts", m_lineVerts.loadAcquire() }, { "ms", m_lineMs.loadAcquire() / 1000.0 },
             { "frames", n }, { "avgMs", n > 0 ? m_lineSumUs.loadAcquire() / 1000.0 / n : 0.0 }, { "maxMs", m_lineMaxUs.loadAcquire() / 1000.0 } };
}

void GlobeItem::globeEaseTo(double lat, double lng, double zoom, int ms, double toDist)
{
    lat = std::clamp(lat, -85.0, 85.0);
    lng = std::fmod(std::fmod(lng + 540.0, 360.0) + 360.0, 360.0) - 180.0;
    double dist = toDist > 0 ? toDist : zoom < 0 ? m_dist : globe::distForZoom(zoom, logicalView().focal(), lat);
    dist = clampDist(dist);
    noteActivity();
    if (ms <= 0 || height() <= 1) {
        stop();
        setCenterLat(lat);
        setCenterLng(lng);
        setDistance(dist);
        updateMoving();
        QTimer::singleShot(0, this, &GlobeItem::flyFinished);   // 0.9.7：QML 等"飞到了"接着做下一步
        return;
    }
    m_flyLat0 = m_lat; m_flyLng0 = m_lng; m_flyDist0 = m_dist;
    m_flyLat1 = lat; m_flyLng1 = lng; m_flyDist1 = dist;
    // 转得远时中途拉远一点（能看见两头）：途中最高到相机高度 ≈ 转过的角度 × 0.9（弧度），不比两头低
    const double ang = globe::angleBetween(m_flyLat0, m_flyLng0, lat, lng);
    m_flyBump = std::max(0.0, std::min(maxDistance() - 1.0, ang * 0.9) - std::max(m_flyDist0, m_flyDist1) + 1.0);
    startAnim(Anim::Fly, ms);
}

QVariantList GlobeItem::zoomSteps(double x, double y, int steps, double notch)
{
    // 自检 / 单元测试：照 wheel → tick 的算法一帧帧走完（不等计时器），返回每一帧的相机距离和中心
    QVariantList out;
    for (int i = 0; i < steps; ++i) {
        wheel(notch, x, y);
        for (int f = 1; f <= 12; ++f) {
            applyZoomDist(globe::lerpDist(m_distFrom, m_distTo, tp::map::geo::easeDefault(f / 12.0)), m_anchorX, m_anchorY);
            out.push_back(QVariantList{ m_dist, m_lat, m_lng });
        }
        stop();
    }
    updateMoving();
    return out;
}

// ---- 交通小图标（G1，PO 选；按交通方式，Claude 定） ----

void GlobeItem::setVehicleAllowed(bool on)
{
    if (on == m_vehAllowed) return;
    m_vehAllowed = on;
    updateVehicleTimer();
    emit vehicleChanged();
}

void GlobeItem::updateVehicleTimer()
{
    const bool want = m_vehAllowed && m_active && m_mapc && m_mapc->globeOn() && !m_mapc->markers()->selectedPlan().isEmpty();
    if (want && !m_vehTimer.isActive()) { replanVehicle(); m_vehClock.start(); m_vehTimer.start(); vehicleTick(); }
    else if (!want) {
        m_vehTimer.stop();
        if (m_vehShown) { m_vehShown = false; emit vehicleChanged(); }
    }
}

void GlobeItem::replanVehicle()
{
    if (!m_mapc || !m_active || !m_mapc->globeOn()) return;
    const QString sel = m_mapc->markers()->selectedPlan();
    const bool newPlan = sel != m_vehPlanId;
    m_vehPlanId = sel;
    m_vehPlan = {};
    if (sel.isEmpty()) return;
    const globe::Camera cam(logicalView());
    for (const globe::RoutePlan &p : m_mapc->globeRoutes()->plans) {
        if (p.id != sel) continue;
        std::vector<double> px;
        for (const globe::RouteLeg &leg : p.legs) px.push_back(globe::runsLength(globe::screenRuns(cam, leg.pts)));
        m_vehPlan = globe::planVehicle(px);
        break;
    }
    if (newPlan) m_vehClock.restart();   // 换了计划从第一段开始
}

void GlobeItem::vehicleTick()
{
    // 0.9.7：变形时藏起来（小图标只按球面摆）
    if (m_morph > 0) { if (m_vehShown) { m_vehShown = false; emit vehicleChanged(); } return; }
    bool shown = false;
    double vx = 0, vy = 0, angle = 0;
    bool flip = false;
    QString img;
    double vsz = MarkerImages::kVehicleSize;
    if (m_mapc && !m_vehPlan.legs.empty()) {
        const double t = m_vehMsOverride >= 0 ? m_vehMsOverride : std::fmod(double(m_vehClock.elapsed()), m_vehPlan.totalMs);
        int legIdx = 0;
        double frac = 0;
        globe::vehicleAt(m_vehPlan, t, &legIdx, &frac);
        for (const globe::RoutePlan &p : m_mapc->globeRoutes()->plans) {
            if (p.id != m_vehPlanId) continue;
            if (legIdx < 0 || legIdx >= int(p.legs.size())) break;
            const globe::RouteLeg &leg = p.legs[legIdx];
            globe::Vec3 ahead;
            const globe::Vec3 pos = globe::pointAlong(leg, frac, &ahead);
            const globe::Camera cam(logicalView());
            double ax = 0, ay = 0;
            if (!cam.occluded(pos) && cam.project(pos, &vx, &vy) && cam.project(ahead, &ax, &ay)) {
                shown = true;
                angle = std::atan2(ay - vy, ax - vx) * 180.0 / M_PI;
                flip = ax < vx;
                const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
                img = MarkerImages::vehicleId(leg.mode, leg.color, dpr);
                vsz = leg.mode == QLatin1String("flight") ? MarkerImages::kVehicleSize : MarkerImages::kBadgeSize;
            }
            break;
        }
    }
    if (shown == m_vehShown && (!shown || (vx == m_vehX && vy == m_vehY && img == m_vehImg && angle == m_vehAngle))) return;
    m_vehShown = shown; m_vehX = vx; m_vehY = vy; m_vehAngle = angle; m_vehFlip = flip; m_vehSize = vsz;
    if (!img.isEmpty()) m_vehImg = img;
    emit vehicleChanged();
}

QVariantMap GlobeItem::vehicleAtMs(double ms)
{
    replanVehicle();
    m_vehMsOverride = ms;
    vehicleTick();
    m_vehMsOverride = -1;
    int leg = -1;
    double frac = 0;
    const bool inLeg = globe::vehicleAt(m_vehPlan, ms, &leg, &frac);
    return { { "shown", m_vehShown }, { "x", m_vehX }, { "y", m_vehY }, { "img", m_vehImg }, { "leg", leg }, { "frac", frac },
             { "moving", inLeg }, { "legs", int(m_vehPlan.legs.size()) }, { "totalMs", m_vehPlan.totalMs } };
}

globe::View GlobeItem::logicalView() const
{
    globe::View v;
    v.lat = m_lat; v.lng = m_lng; v.dist = m_dist;
    v.width = std::max(1.0, width()); v.height = std::max(1.0, height());
    v.morph = m_morph;
    return v;
}

globe::View GlobeItem::deviceView() const
{
    globe::View v = logicalView();
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    v.width *= dpr; v.height *= dpr;
    return v;
}

void GlobeItem::scheduleTiles()
{
    if (m_active && !m_tileTimer.isActive()) m_tileTimer.start();
}

void GlobeItem::doTiles()
{
    if (!m_active || width() <= 0 || height() <= 0) return;
    m_tiles->update(deviceView());
    update();
}

QString GlobeItem::tileStatus() const
{
    const GlobeTileSet::Stats s = m_tiles->stats();
    return QStringLiteral("%1 z≤%2：%3 块，显卡 %4/%5 层%6")
        .arg(QStringLiteral("卫星"))
        .arg(s.maxZ).arg(s.selected).arg(s.resident).arg(GlobeTileSet::kLayers)
        .arg(s.netInFlight + s.rasterInFlight > 0 ? QStringLiteral("，加载中 %1").arg(s.netInFlight + s.rasterInFlight) : QString());
}

QVariantMap GlobeItem::tileStats() const
{
    const GlobeTileSet::Stats s = m_tiles->stats();
    return { { "selected", s.selected }, { "maxZ", s.maxZ }, { "drawn", s.drawn }, { "resident", s.resident },
             { "pending", s.pending }, { "net", s.netInFlight }, { "raster", s.rasterInFlight }, { "fetched", s.fetched },
             { "failed", s.failed }, { "rendered", s.rendered }, { "evicted", s.evicted }, { "uploaded", s.uploaded },
             { "fallback", s.fallbackDrawn }, { "geometry", s.geometry }, { "gpuLayers", gpuLayers() },
             { "zoom", zoomLevelEquivalent() }, { "dist", m_dist }, { "maxDist", maxDistance() },
             { "lights", m_lightsReady }, { "spinning", m_spinning } };
}

QVariantList GlobeItem::pick(double x, double y) const
{
    globe::Vec3 p;
    if (!globe::Camera(logicalView()).pick(x, y, &p)) return {};
    double lat, lng;
    globe::toLatLng(p, &lat, &lng);
    return { lat, lng };
}

// ---- 相机 ----

double GlobeItem::maxDistance() const { return globe::maxDistFor(logicalView().focal()); }
double GlobeItem::zoomLevelEquivalent() const { return globe::zoomForDist(m_dist, logicalView().focal(), m_lat); }
void GlobeItem::setFromZoomLevel(double z)
{
    // 还不知道自己多大（QML 刚建好）：记下来，有了大小再换算（换算要用窗口高度）
    if (height() <= 1) { m_pendingZoom = z; return; }
    m_pendingZoom = qQNaN();
    setDistance(globe::distForZoom(z, logicalView().focal(), m_lat));
}
double GlobeItem::sunLng() const { return subsolarPoint(now()).lng; }
double GlobeItem::sunLat() const { return subsolarPoint(now()).lat; }

QVariantList GlobeItem::nightCity() const
{
    struct City { const char *name; double lat, lng; };
    static const City cities[] = {
        { "London", 51.5, -0.1 }, { "Paris", 48.9, 2.35 }, { "Moscow", 55.75, 37.6 }, { "Cairo", 30.0, 31.2 },
        { "Delhi", 28.6, 77.2 }, { "Shanghai", 31.2, 121.5 }, { "Tokyo", 35.7, 139.7 }, { "New York", 40.7, -74.0 },
        { "Chicago", 41.9, -87.6 }, { "Los Angeles", 34.05, -118.25 }, { "Mexico City", 19.4, -99.1 },
        { "Sao Paulo", -23.55, -46.6 }, { "Johannesburg", -26.2, 28.0 }, { "Sydney", -33.9, 151.2 }, { "Jakarta", -6.2, 106.8 } };
    const SubSolar s = subsolarPoint(now());
    const globe::Vec3 sv = globe::toVec(s.lat, s.lng);
    const City *best = nullptr;
    double bestAlt = 90;
    for (const City &c : cities) {
        const globe::Vec3 v = globe::toVec(c.lat, c.lng);
        const double alt = std::asin(std::clamp(v.x * sv.x + v.y * sv.y + v.z * sv.z, -1.0, 1.0)) * 180.0 / M_PI;
        if (alt < bestAlt) { bestAlt = alt; best = &c; }
    }
    return { best->lat, best->lng, QString::fromLatin1(best->name), bestAlt };
}

void GlobeItem::rotateBy(double dx, double dy)
{
    // 屏幕中心每像素对应的地面角度 = (dist - 1) / f；经度再除以 cos(纬度)（高纬度时放慢，免得转得飞快）
    const double k = (m_dist - 1.0) / logicalView().focal() * 180.0 / M_PI;
    const double c = std::max(0.2, std::cos(m_lat * M_PI / 180.0));
    setCenterLng(m_lng - dx * k / c);
    setCenterLat(m_lat + dy * k);
}

void GlobeItem::dragStart()
{
    noteActivity();
    stop();
    m_samples.clear();
    m_dragClock.start();
    m_dragging = true;
    updateMoving();
}

void GlobeItem::dragBy(double dx, double dy)
{
    if (!m_dragClock.isValid()) m_dragClock.start();
    rotateBy(dx, dy);
    const qint64 now = m_dragClock.elapsed();
    m_samples.push_back({ now, dx, dy });
    while (!m_samples.isEmpty() && now - m_samples.first().t > 160) m_samples.removeFirst();
}

void GlobeItem::dragEnd()
{
    // 和平面一样（maplibre-gl handler_inertia.js）：最近 160 毫秒的位移 → 速度 × .3，最快 1400 px/s，减速度 2500 px/s²
    m_dragging = false;
    const qint64 now = m_dragClock.isValid() ? m_dragClock.elapsed() : 0;
    while (!m_samples.isEmpty() && now - m_samples.first().t > 160) m_samples.removeFirst();
    if (m_samples.size() < 2) { m_samples.clear(); updateMoving(); return; }
    double sx = 0, sy = 0;
    for (const Sample &s : std::as_const(m_samples)) { sx += s.dx; sy += s.dy; }
    const double dt = (m_samples.last().t - m_samples.first().t) / 1000.0;
    m_samples.clear();
    if (dt <= 0) { updateMoving(); return; }
    double vx = sx * 0.3 / dt, vy = sy * 0.3 / dt;
    double speed = std::hypot(vx, vy);
    if (speed > 1400) { vx *= 1400 / speed; vy *= 1400 / speed; speed = 1400; }
    const double duration = speed / (2500 * 0.3);
    if (duration < 0.05) { updateMoving(); return; }
    m_inertiaDx = vx * duration / 2;
    m_inertiaDy = vy * duration / 2;
    m_inertiaDone = 0;
    startAnim(Anim::Inertia, int(duration * 1000));
}

void GlobeItem::wheel(double angleDeltaY, double x, double y)
{
    // 一格（120）半级，和平面一样（相机高度 × 2^-0.5）；连着滚时从上一次的目标接着加。
    // 0.9.5：按相机距离算，不再按"换算成平面的缩放级别"（含 1/cos 纬度，极点附近中心一动距离就跳，PO 11:02 报的抽搐）
    noteActivity();
    // 0.9.7：变形期间滚的轮攒起来，平面接手后再交给平面
    if (morphing() || m_morph > 0) { m_pendWheel += angleDeltaY; m_pendX = x; m_pendY = y; return; }
    const double base = m_anim == Anim::Zoom ? m_distTo : m_dist;
    const double to = std::clamp(1.0 + (base - 1.0) * std::exp2(-angleDeltaY / 240.0), globe::kMinDist, maxDistance());
    // 0.9.7 自动摊平（B3，PO 选 B）：目标越过 5.5 级就不在地球上缩放，交给 QML 播摊平动画（终点 = 缩放后鼠标下的点不动）
    if (m_autoFlatten && angleDeltaY > 0 && height() > 1) {
        const globe::View v = logicalView();
        double lat = m_lat, lng = m_lng;
        globe::zoomAnchorCenter(v, to, x, y, 85.0, &lat, &lng);
        const double z = globe::zoomForDist(to, v.focal(), lat);
        if (z >= kFlattenZoom - 1e-6) {
            stop();
            qInfo("地球：放大到 %.2f 级（≥ %.1f），摊平成平面", z, kFlattenZoom);
            emit flattenRequested(lat, lng, std::max(kFlattenZoom, z));
            return;
        }
    }
    m_distTo = to;
    m_distFrom = m_dist;
    m_anchorX = x; m_anchorY = y;
    startAnim(Anim::Zoom, 200);
}

void GlobeItem::zoomBy(double factor)
{
    if (!(factor > 0)) return;
    setDistance(1.0 + (m_dist - 1.0) / factor);
}

void GlobeItem::applyZoomDist(double dist, double ax, double ay)
{
    // 以鼠标处为中心（0.9.5）：把"换距离后鼠标下的点"转回"换距离前的点"（最小旋转），中心转到 ±85° 就停。
    // 0.9.4 以前按经纬度差补，中心卡在 85° 时纬度补不上、只补经度，极点附近经度差非常敏感 → 抽搐
    const globe::View before = logicalView();
    const double nd = clampDist(dist);
    double lat = m_lat, lng = m_lng;
    const bool ok = globe::zoomAnchorCenter(before, nd, ax, ay, 85.0, &lat, &lng);
    setDistance(nd);
    if (!ok) return;
    setCenterLat(lat);
    setCenterLng(lng);
}

void GlobeItem::startAnim(Anim a, int ms)
{
    m_anim = a;
    m_animMs = std::max(1, ms);
    m_animClock.start();
    if (!m_animTimer.isActive()) m_animTimer.start();
    updateMoving();
}

void GlobeItem::stop()
{
    m_anim = Anim::None;
    m_animTimer.stop();
    updateMoving();
}

void GlobeItem::tick()
{
    const double t = std::clamp(m_animClock.elapsed() / double(m_animMs), 0.0, 1.0);
    switch (m_anim) {
    case Anim::None: m_animTimer.stop(); return;
    case Anim::Inertia: {
        const double k = 1 - std::pow(1 - t, 3);   // bezier(0, 0, .3, 1) 近似为 ease-out cubic（和平面一样）
        const double step = k - m_inertiaDone;
        m_inertiaDone = k;
        rotateBy(m_inertiaDx * step, m_inertiaDy * step);
        break;
    }
    case Anim::Zoom:
        applyZoomDist(globe::lerpDist(m_distFrom, m_distTo, tp::map::geo::easeDefault(t)), m_anchorX, m_anchorY);
        break;
    case Anim::Fly: {
        // 0.9.5：沿大圆转过去，相机高度按对数插值；转得远时中途拉远一点
        const double k = tp::map::geo::easeDefault(t);
        double lat, lng;
        globe::slerpCenter(m_flyLat0, m_flyLng0, m_flyLat1, m_flyLng1, k, &lat, &lng);
        setCenterLat(lat);
        setCenterLng(lng);
        setDistance(globe::lerpDist(m_flyDist0, m_flyDist1, k) + m_flyBump * std::sin(M_PI * k));
        break;
    }
    }
    if (t >= 1) {
        const bool fly = m_anim == Anim::Fly;
        stop();
        if (fly) emit flyFinished();
    }
}

// ---- 0.9.7 变形过渡（C1）----

void GlobeItem::setAutoFlatten(bool on)
{
    if (on == m_autoFlatten) return;
    m_autoFlatten = on;
    emit autoFlattenChanged();
}

void GlobeItem::setMorph(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const bool was = morphing();
    m_morphTimer.stop();
    m_morphWaiting = false;
    if (t == m_morph && !was) return;
    m_morph = t;
    update();
    pushView();
    updateMoving();
    emit morphChanged();
}

void GlobeItem::morphTo(double target, int ms, double lat, double lng, double zoom, bool waitTiles)
{
    stop();
    noteActivity();
    target = std::clamp(target, 0.0, 1.0);
    lat = std::clamp(lat, -85.0, 85.0);
    lng = std::fmod(std::fmod(lng + 540.0, 360.0) + 360.0, 360.0) - 180.0;
    m_flyLat0 = m_lat; m_flyLng0 = m_lng; m_flyDist0 = m_dist;
    m_flyLat1 = lat; m_flyLng1 = lng;
    m_flyDist1 = clampDist(zoom < 0 ? m_dist : globe::distForZoom(zoom, logicalView().focal(), lat));
    m_morphFrom = m_morph;
    m_morphTarget = target;
    m_morphMs = std::max(1, ms);
    m_morphWaiting = waitTiles;
    m_pendWheel = 0;
    qInfo("地球：变形 %.2f → %.0f（%d 毫秒%s），镜头 → %.4f, %.4f, %.2f 级", m_morph, target, ms, waitTiles ? "，先等瓦片" : "", lat, lng,
          globe::zoomForDist(m_flyDist1, logicalView().focal(), lat));
    m_morphClock.start();
    m_morphTimer.start();
    updateMoving();
    emit morphChanged();
    morphTick();
}

void GlobeItem::morphTick()
{
    if (m_morphWaiting) {
        doTiles();
        if (!tilesSettled() && m_morphClock.elapsed() < 250) return;
        qInfo("地球：卷回前等瓦片 %lld 毫秒（%s）", m_morphClock.elapsed(), tilesSettled() ? "到齐了" : "没等齐，先开始");
        m_morphWaiting = false;
        m_morphClock.restart();
    }
    const double t = std::clamp(m_morphClock.elapsed() / double(m_morphMs), 0.0, 1.0);
    // 变形本身两头慢中间快（三次）；镜头和平时飞过去一样（bezier(.25, .1, .25, 1)）
    const double km = t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
    const double kc = tp::map::geo::easeDefault(t);
    double lat, lng;
    globe::slerpCenter(m_flyLat0, m_flyLng0, m_flyLat1, m_flyLng1, kc, &lat, &lng);
    m_morph = m_morphFrom + (m_morphTarget - m_morphFrom) * km;
    setCenterLat(lat);
    setCenterLng(lng);
    setDistance(globe::lerpDist(m_flyDist0, m_flyDist1, kc));
    update();
    pushView();
    emit morphChanged();
    if (t >= 1) {
        m_morph = m_morphTarget;
        m_morphTimer.stop();
        updateMoving();
        emit morphChanged();
        emit morphFinished(m_morph);
    }
}

QVariantList GlobeItem::takeWheel()
{
    const QVariantList r{ m_pendWheel, m_pendX, m_pendY };
    m_pendWheel = 0;
    return r;
}

bool GlobeItem::tilesSettled() const
{
    const GlobeTileSet::Stats s = m_tiles->stats();
    return s.selected > 0 && s.netInFlight + s.rasterInFlight == 0 && s.fallbackDrawn == 0;
}
