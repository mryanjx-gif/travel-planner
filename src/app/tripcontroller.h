// .trip 计划文件导入导出、分享版、批量导出（0.14.1，E-05–E-09）。QML 里是 trips（qml/components/TripFlow.qml）。
// 打包解包和去掉私人内容在 tp_export（src/export/trippack.*），放进现有数据在 tp_data（src/data/planmerge.*）。
// 导入走现有的保存：先把手上的改动存成一步 → 写照片文件 → 改数据 → 存一步（备份、临时文件改名、回读比对），Ctrl+Z 能撤销（照片文件留着）。
#pragma once

#include "jsonvalue.h"
#include "trippack.h"

#include <QHash>
#include <QObject>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace tp::data { class DataStore; }

namespace tp::app {

class AppController;

class TripController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool importOpen READ importOpen NOTIFY importChanged)
public:
    TripController(data::DataStore *store, AppController *app, std::function<QString()> currency, QObject *parent = nullptr);
    ~TripController() override;

    bool importOpen() const { return m_reader != nullptr; }

    // 保存对话框的默认名字（不带 .trip）：一个计划用计划名，多个是"3 个计划"
    Q_INVOKABLE QString defaultName(const QStringList &ids) const;
    Q_INVOKABLE bool isTripFile(const QString &urlOrPath) const;
    // 合并成一个 .trip（一个计划 formatVersion 1，多个 2）。{ ok, error, file, photos, missing（个数）, plans }
    Q_INVOKABLE QVariantMap exportTrip(const QStringList &ids, const QString &path);
    // 每个计划单独一个 .trip，存进 dir（重名加 -2）。{ ok, error, dir, count, photos, missing }
    Q_INVOKABLE QVariantMap exportEach(const QStringList &ids, const QString &dir);
    // 分享版（E-07）。ids 空 = 全部计划；each = 每个计划单独一个文件夹。opts 见 trippack.h ShareOptions（键名相同）。
    // { ok, error, dir（一个时是那个文件夹，多个时是 dest）, count, photos, skipped, missing }
    Q_INVOKABLE QVariantMap exportShare(const QStringList &ids, const QVariantMap &opts, const QString &dest, bool each);

    // 导入第一步：打开、检查、和现有的比。{ ok, error, file, plans: [{ id, name, conflict, local, incoming }] }
    Q_INVOKABLE QVariantMap openImport(const QString &urlOrPath);
    // 第二步：modes = { 计划 id: "replace" / "copy" / "merge" / "skip" }（没有冲突的不用写）。
    // { ok, error, cancelled, message, openId（只导入一个时）, count, photos }
    Q_INVOKABLE QVariantMap commitImport(const QVariantMap &modes);
    Q_INVOKABLE void cancelImport();

signals:
    void importChanged();

private:
    QString localPath(const QString &p) const;
    data::DataStore *m_store;
    AppController *m_app;
    std::function<QString()> m_currency;
    std::unique_ptr<exporter::trip::Reader> m_reader;
    json::Value m_incoming;
    QHash<QString, QString> m_srcOf;
};

} // namespace tp::app
