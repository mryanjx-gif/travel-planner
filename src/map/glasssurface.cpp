#include "glasssurface.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGDynamicTexture>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTextureProvider>
#include <QElapsedTimer>
#include <QSGRendererInterface>
#include <rhi/qrhi.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kMaxNear = 8;     // 着色器里 rects[8]
constexpr qreal kPad = 14;      // 外扩：粘连 8 + 抗锯齿（Electron uPad）
constexpr qreal kReach = 16;    // 两个泡泡隔多远开始算粘连（2k，再远 smooth-min 不起作用）
constexpr float kLod = 2.3f;    // 面上磨砂的 mip 级别（Electron uLod）

QVector<GlassSurface *> &all()
{
    static QVector<GlassSurface *> v;
    return v;
}

// 和 glass.frag 的 std140 布局一致：从第 68 字节开始（前面是 qt_Matrix、qt_Opacity）
struct Uniforms {
    float lod = 0, n = 1, plain = 0;
    float sceneSize[2] = {1, 1};
    float pad1[2] = {0, 0};
    float tint[4] = {1, 1, 1, 1};
    float rects[kMaxNear][4] = {};
    float pars[kMaxNear][4] = {};   // 半径、种类（1 = 泡泡，2 = 大面板）
};
static_assert(sizeof(Uniforms) == 368 - 68);

class GlassMaterial : public QSGMaterial {
public:
    GlassMaterial() { setFlag(Blending | RequiresFullMatrix); }
    QSGMaterialType *type() const override { static QSGMaterialType t; return &t; }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *o) const override { return this < o ? -1 : (this > o ? 1 : 0); }
    Uniforms u;
    QSGTextureProvider *provider = nullptr;
};

class GlassShader : public QSGMaterialShader {
public:
    GlassShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/glass.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/glass.frag.qsb"));
    }
    bool updateUniformData(RenderState &state, QSGMaterial *newMat, QSGMaterial *) override
    {
        char *p = state.uniformData()->data();
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(p, m.constData(), 64);
        }
        if (state.isOpacityDirty()) {
            const float o = state.opacity();
            std::memcpy(p + 64, &o, 4);
        }
        std::memcpy(p + 68, &static_cast<GlassMaterial *>(newMat)->u, sizeof(Uniforms));
        return true;
    }
    void updateSampledImage(RenderState &state, int, QSGTexture **tex, QSGMaterial *newMat, QSGMaterial *) override
    {
        auto *m = static_cast<GlassMaterial *>(newMat);
        QSGTexture *t = m->provider ? m->provider->texture() : nullptr;
        if (!t) return;
        t->setFiltering(QSGTexture::Linear);
        t->setMipmapFiltering(QSGTexture::Linear);
        t->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        t->setVerticalWrapMode(QSGTexture::ClampToEdge);
        t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *tex = t;
    }
};

QSGMaterialShader *GlassMaterial::createShader(QSGRendererInterface::RenderMode) const { return new GlassShader; }

// 背景是 ShaderEffectSource：用它的一方要在画之前让它更新（ShaderEffect 也是这样做的）
class GlassNode : public QSGGeometryNode {
public:
    GlassNode() : geo(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
    {
        geo.setDrawingMode(QSGGeometry::DrawTriangleStrip);
        setGeometry(&geo);
        setMaterial(&mat);
        setFlag(UsePreprocess);
    }
    void preprocess() override
    {
        if (mat.provider)
            if (auto *d = qobject_cast<QSGDynamicTexture *>(mat.provider->texture())) d->updateTexture();
    }
    QSGGeometry geo;
    GlassMaterial mat;
};

qreal effectiveOpacity(const QQuickItem *i)
{
    qreal o = 1;
    for (; i; i = i->parentItem()) o *= i->opacity();
    return o;
}

qreal gap(const QRectF &a, const QRectF &b)
{
    const qreal dx = std::max({a.left() - b.right(), b.left() - a.right(), qreal(0)});
    const qreal dy = std::max({a.top() - b.bottom(), b.top() - a.bottom(), qreal(0)});
    return std::max(dx, dy);
}

} // namespace

GlassSurface::GlassSurface(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    all().append(this);
}

GlassSurface::~GlassSurface() { all().removeOne(this); }

void GlassSurface::setSource(QQuickItem *s)
{
    if (m_source == s) return;
    m_source = s;
    update();
    emit sourceChanged();
}

void GlassSurface::setRadius(qreal r)
{
    if (qFuzzyCompare(m_radius, r)) return;
    m_radius = r;
    update();
    emit radiusChanged();
}

void GlassSurface::setTint(const QColor &c)
{
    if (m_tint == c) return;
    m_tint = c;
    update();
    emit tintChanged();
}

void GlassSurface::setPlain(int v)
{
    if (m_plain == v) return;
    m_plain = v;
    update();
    emit plainChanged();
}

bool GlassSurface::pill() const { return m_radius >= std::min(width(), height()) / 2 - 1; }

qreal GlassSurface::sceneRadius() const { return width() > 0 ? m_radius * m_rect.width() / width() : m_radius; }

void GlassSurface::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange && value.window) {
        // 面板可能跟着祖先一起动，自己收不到通知：每帧画之前（GUI 线程）量一遍。整个程序只连一次。
        static QPointer<QQuickWindow> watched;
        if (watched != value.window) {
            watched = value.window;
            QObject::connect(value.window, &QQuickWindow::afterAnimating, value.window, &GlassSurface::refreshAll);
        }
    }
    QQuickItem::itemChange(change, value);
}

void GlassSurface::refreshAll()
{
    for (GlassSurface *s : all()) {
        const bool on = s->isVisible() && s->width() > 0 && s->height() > 0;
        const QRectF r = on ? s->mapRectToScene(s->boundingRect()) : QRectF();
        if (r != s->m_rect) {
            s->m_rect = r;
            s->update();
        }
    }
    for (GlassSurface *s : all()) {
        if (s->m_rect.isEmpty()) continue;
        QVector<Near> near;
        if (s->pill()) {
            for (GlassSurface *o : all()) {
                if (o == s || o->m_rect.isEmpty() || !o->pill() || near.size() >= kMaxNear - 1) continue;
                if (gap(s->m_rect, o->m_rect) >= kReach || effectiveOpacity(o) < 0.01) continue;
                near.append({o->m_rect, o->sceneRadius()});
            }
        }
        if (near != s->m_near) {
            s->m_near = near;
            s->update();
        }
    }
}

QSGNode *GlassSurface::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<GlassNode *>(old);
    QSGTextureProvider *tp = m_source && m_source->isTextureProvider() ? m_source->textureProvider() : nullptr;
    if (!tp || !tp->texture() || m_rect.isEmpty() || !window()) {
        delete node;
        if (tp && !tp->texture()) update();   // 背景还没准备好，下一帧再来
        return nullptr;
    }
    if (!node) node = new GlassNode;
    if (!m_watching) {
        m_watching = true;
        connect(tp, &QSGTextureProvider::textureChanged, this, &QQuickItem::update, Qt::QueuedConnection);
    }
    GlassMaterial &m = node->mat;
    m.provider = tp;
    Uniforms &u = m.u;
    const QSizeF scene = window()->contentItem()->size();
    const QSize tex = tp->texture()->textureSize();
    u.lod = kLod * float(scene.width() > 0 ? tex.width() / scene.width() : 1);
    u.sceneSize[0] = float(scene.width());
    u.sceneSize[1] = float(scene.height());
    u.tint[0] = float(m_tint.redF());
    u.tint[1] = float(m_tint.greenF());
    u.tint[2] = float(m_tint.blueF());
    u.tint[3] = float(m_tint.alphaF());
    const auto put = [&u](int i, const QRectF &r, qreal rad, float kind) {
        u.rects[i][0] = float(r.x());
        u.rects[i][1] = float(r.y());
        u.rects[i][2] = float(r.width());
        u.rects[i][3] = float(r.height());
        u.pars[i][0] = float(rad);
        u.pars[i][1] = kind;
    };
    put(0, m_rect, sceneRadius(), pill() ? 1.f : 2.f);   // 第 0 个总是自己
    for (int i = 0; i < m_near.size(); ++i) put(i + 1, m_near[i].rect, m_near[i].radius, 1.f);
    u.n = float(1 + m_near.size());
    u.plain = float(m_plain);

    // 顶点：本地坐标（跟着缩放走）；纹理坐标放场景坐标，着色器按它算
    const qreal k = width() > 0 ? m_rect.width() / width() : 1;
    const qreal lp = kPad / std::max(k, 0.01);
    auto *v = node->geo.vertexDataAsTexturedPoint2D();
    const QRectF sr = m_rect.adjusted(-kPad, -kPad, kPad, kPad);
    v[0].set(float(-lp), float(-lp), float(sr.left()), float(sr.top()));
    v[1].set(float(width() + lp), float(-lp), float(sr.right()), float(sr.top()));
    v[2].set(float(-lp), float(height() + lp), float(sr.left()), float(sr.bottom()));
    v[3].set(float(width() + lp), float(height() + lp), float(sr.right()), float(sr.bottom()));
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}

void GlassWatch::setWindow(QQuickWindow *w)
{
    if (m_win == w || !w) return;
    m_win = w;
    connect(w, &QQuickWindow::afterRendering, this, [this, w] {
        static QElapsedTimer clock;
        if (!clock.isValid()) clock.start();
        QSGRendererInterface *ri = w->rendererInterface();
        auto *sc = static_cast<QRhiSwapChain *>(ri->getResource(w, QSGRendererInterface::RhiSwapchainResource));
        QRhiCommandBuffer *cb = sc ? sc->currentFrameCommandBuffer() : nullptr;
        const double ms = cb ? cb->lastCompletedGpuTime() * 1000 : 0;
        if (!m_active || ms <= 20) {
            if (ms > 0 || !m_active) { m_since = -1; m_count = 0; }   // 0 = 这帧没拿到结果，不算
            return;
        }
        const double now = clock.elapsed();
        if (m_since < 0) m_since = now;
        if (++m_count >= 20 && now - m_since >= 2000) {
            m_active = false;
            QMetaObject::invokeMethod(this, &GlassWatch::slow, Qt::QueuedConnection);
        }
    }, Qt::DirectConnection);
}

QImage CardImages::requestImage(const QString &id, QSize *size, const QSize &)
{
    const QStringList a = id.split(u'/');
    const int r = a.value(0).toInt(), k = std::max(1, a.value(1).toInt());
    const QColor color(QLatin1Char('#') + a.value(2));
    const int edge = int(std::ceil(r * 1.25)) * k + 1;   // 和 TpCard.qml 的 edge 一样
    const int side = edge * 2 + 3;
    const double R = r * 1.25 * k;   // n = 2.6 时切掉的量和半径 r 的圆弧相同
    QPainterPath path;
    const auto corner = [&](double cx, double cy, double sx, double sy, bool first, bool back) {
        for (int i = 0; i <= 24; ++i) {
            const double t = (back ? 24 - i : i) / 24.0 * M_PI / 2;
            const QPointF pt(cx + sx * R * std::pow(std::cos(t), 2 / 2.6), cy + sy * R * std::pow(std::sin(t), 2 / 2.6));
            if (first && i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
    };
    // 顺时针：上 → 右 → 下 → 左
    corner(side - R, R, 1, -1, true, true);
    corner(side - R, side - R, 1, 1, false, false);
    corner(R, side - R, -1, 1, false, true);
    corner(R, R, -1, -1, false, false);
    path.closeSubpath();
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillPath(path, color);
    p.end();
    if (size) *size = img.size();
    return img;
}
