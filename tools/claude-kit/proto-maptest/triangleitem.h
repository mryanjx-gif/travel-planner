#pragma once
#include <QQuickRhiItem>
#include <rhi/qrhi.h>
#include <memory>

class TriangleRenderer : public QQuickRhiItemRenderer {
protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;
private:
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_vbuf, m_ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QMatrix4x4 m_proj;
};

class TriangleItem : public QQuickRhiItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(TriangleItem)
public:
    QQuickRhiItemRenderer *createRenderer() override { return new TriangleRenderer; }
};
