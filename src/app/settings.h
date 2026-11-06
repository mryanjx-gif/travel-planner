// 原生版自己的设置：settings.json（位置见 src/platform/paths.h）。0.4.0。
// 用数据层的 JSON 读写器（保持键顺序、原样保留不认识的键）；写入是原子的（先写临时文件再改名）。
// 文件坏了：退回默认设置，把坏文件改名成 settings.broken-YYYYMMDD-HHMMSS.json 留着，不覆盖它。
// 键名尽量和 Electron 的 settings.json 一样：dataDir、bounds、maximized、closeAction、lang；
// 原生版另有 fontSize（Electron 存在页面的 localStorage 里）、windowTitle（Electron 是页面标题）、
// backupDir / autoBackup / backupEveryDays / lastBackup（0.5.0，Electron 也在 localStorage 里）。
#pragma once

#include "jsonvalue.h"

#include <QObject>
#include <QRect>
#include <QString>

namespace tp::app {

class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr) : QObject(parent) {}

    // 读；文件不存在 = 默认设置。返回 false = 文件坏了（已改名留存，用默认设置）
    bool load(const QString &path);
    // 写（原子）；失败返回 false，原因在 lastError()
    bool save();
    QString path() const { return m_path; }
    QString lastError() const { return m_error; }
    QString brokenBackup() const { return m_broken; }   // 坏文件留存的文件名（没有就是空）

    // ---- 各项 ----
    QString dataDir() const;
    void setDataDir(const QString &dir);

    // 'quit' 直接退出（默认）/ 'tray' 收到托盘 / 'taskbar' 最小化到任务栏（和 Electron 一样）
    QString closeAction() const;
    void setCloseAction(const QString &v);

    QString lang() const;               // 'zh'（默认）/ 'en'
    void setLang(const QString &v);

    // 星空动画（0.9.3，功能清单 G-03 / T-04）：地球后面的星星闪烁、流星。键名和 Electron prefs.js 一样，默认开
    bool spaceAnim() const;
    void setSpaceAnim(bool on);
    // 命令行 --lang：只管这次运行，不写进 settings.json（0.5.0 修：以前写设置时会被一起存进去）
    void setLangOverride(const QString &v) { m_langOverride = v == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("zh"); }

    // 字号：'s' 0.9 / 'm' 1（默认）/ 'l' 1.12 / 'xl' 1.25（Electron prefs.js FONT_SIZES）
    QString fontSize() const;
    void setFontSize(const QString &v);
    static double fontScaleFor(const QString &size);
    static QString stepFontSize(const QString &size, int dir);   // Ctrl+ / Ctrl−：往大 / 往小一档

    QString windowTitle() const;        // 用户加在标题后面的文字（默认空）
    void setWindowTitle(const QString &v);

    // 备份（0.5.0，功能清单 D-17 / D-18）。键名和 Electron prefs.js 一样（Electron 存在页面的 localStorage 里，两边不共享）
    QString backupDir() const;
    void setBackupDir(const QString &dir);
    bool autoBackup() const;            // 默认关
    void setAutoBackup(bool on);
    int backupEveryDays() const;        // 1–90，默认 7
    void setBackupEveryDays(int days);
    static int clampBackupDays(double days);
    double lastBackup() const;          // 上次备份成功的时间（毫秒时间戳，0 = 没备份过）
    void setLastBackup(double ms);

    bool hasBounds() const;
    QRect bounds() const;
    void setBounds(const QRect &r);
    bool maximized() const;
    void setMaximized(bool m);

    // 通用（测试、以后的项）
    json::Value value(const QString &key) const;
    void setValue(const QString &key, json::Value v);

    static constexpr int kDefaultWidth = 1400;
    static constexpr int kDefaultHeight = 900;
    static constexpr int kMinWidth = 360;
    static constexpr int kMinHeight = 500;

signals:
    void changed(const QString &key);

private:
    QString str(const QString &key) const;
    json::Value m_root = json::Value::object();
    QString m_path, m_error, m_broken;
    QString m_langOverride;
};

} // namespace tp::app
