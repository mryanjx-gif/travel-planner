#include "triangleitem.h"
#include <QFile>

static QShader loadShader(const QString &name) {
    QFile f(name);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

static const float kVerts[] = {
     0.0f,  0.6f,   0.85f, 0.35f, 0.30f,
    -0.6f, -0.5f,   0.30f, 0.55f, 0.85f,
     0.6f, -0.5f,   0.40f, 0.80f, 0.45f,
};

void TriangleRenderer::initialize(QRhiCommandBuffer *cb) {
    if (m_rhi != rhi()) { m_pipeline.reset(); m_rhi = rhi(); }
    if (!m_pipeline) {
        m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kVerts)));
        m_vbuf->create();
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
        m_ubuf->create();
        m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({ QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_ubuf.get()) });
        m_srb->create();
        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setShaderStages({
            { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/tri.vert.qsb")) },
            { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/tri.frag.qsb")) } });
        QRhiVertexInputLayout il;
        il.setBindings({ { 5 * sizeof(float) } });
        il.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
                           { 0, 1, QRhiVertexInputAttribute::Float3, 2 * sizeof(float) } });
        m_pipeline->setVertexInputLayout(il);
        m_pipeline->setShaderResourceBindings(m_srb.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->create();
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->uploadStaticBuffer(m_vbuf.get(), kVerts);
        cb->resourceUpdate(u);
    }
    m_proj = m_rhi->clipSpaceCorrMatrix();
}

void TriangleRenderer::synchronize(QQuickRhiItem *) {}

void TriangleRenderer::render(QRhiCommandBuffer *cb) {
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    u->updateDynamicBuffer(m_ubuf.get(), 0, 64, m_proj.constData());
    const QColor clear = QColor::fromRgbF(0.93f, 0.95f, 0.97f, 1.0f);
    cb->beginPass(renderTarget(), clear, { 1.0f, 0 }, u);
    cb->setGraphicsPipeline(m_pipeline.get());
    const QSize sz = renderTarget()->pixelSize();
    cb->setViewport({ 0, 0, float(sz.width()), float(sz.height()) });
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vin(m_vbuf.get(), 0);
    cb->setVertexInput(0, 1, &vin);
    cb->draw(3);
    cb->endPass();
}
