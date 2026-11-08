// 导出（0.14.0，E-01–E-04、E-10）。QML 里是 exporter（qml/components/ExportDialog.qml）。
// 排版和 .ics 在 tp_export（src/export/）；这里负责：取数据、第一页预览图（写到缓存目录）、按选的格式写文件。
// 导出前算路（E-10）在 QML 里接 roads.pendingFor / startFor。
#pragma once

#include <QImage>
#include <QObject>
#include <QVariantMap>
#include <functional>

namespace tp::data { class DataStore; }

namespace tp::app {

class ExportController : public QObject
{
    Q_OBJECT
public:
    struct Env {
        QString font, adminDir, cacheDir;
        std::function<QString()> today, currency;
    };
    ExportController(data::DataStore *store, Env env, QObject *parent = nullptr);

    // { name, stops, hasEmergency（信息卡有东西）, icsCount（.ics 有几项）}；没有这个计划时为空
    Q_INVOKABLE QVariantMap info(const QString &planId) const;
    // kind：itinerary / card；opts：{ refs, money, notes, emergency }。返回预览图的 file:/// 地址（第一页，宽 widthPx）
    Q_INVOKABLE QString preview(const QString &planId, const QString &kind, const QVariantMap &opts, int widthPx);
    // 保存对话框的默认文件名（"计划名 行程单.pdf"），去掉文件名里不能用的字符
    Q_INVOKABLE QString defaultName(const QString &planId, const QString &kind, const QString &format) const;
    Q_INVOKABLE QString defaultFolder() const;   // 文档文件夹（file:/// 地址）
    // 第 page 页的图（自检、预览共用）；pages 给总页数。没有这个计划时是空图
    QImage renderPage(const QString &planId, const QString &kind, const QVariantMap &opts, int page, int widthPx, int *pages = nullptr) const;
    // kind：itinerary / card / ics；format：pdf / png（ics 不看）；path 可以是 file:/// 地址。
    // 返回 { ok, file（本地路径）, error, count（.ics 项数）, pages, truncated（长图放不下）}
    Q_INVOKABLE QVariantMap save(const QString &planId, const QString &kind, const QString &format, const QVariantMap &opts, const QString &path);

private:
    data::DataStore *m_store;
    Env m_env;
    int m_previewNo = 0;
};

} // namespace tp::app
