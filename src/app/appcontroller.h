// 界面和程序其余部分之间的桥（QML 里叫 app）。0.4.0：设置、数据文件夹、窗口 / 托盘 / 关闭、语言、字号、
// 冲突和外部修改提示、历史版本。0.5.0：自动保存、撤销 / 重做、备份、开发版"测试改动"。
#pragma once

#include "datafolder.h"
#include "electronimport.h"
#include "jsonvalue.h"
#include "savescheduler.h"
#include "undostack.h"

#include <QObject>
#include <QThread>
#include <atomic>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QQmlEngine;
class QQuickWindow;

namespace tp::data { class DataStore; }
namespace tp::platform { class TrayIcon; class Fullscreen; }

namespace tp::app {

class Settings;
class DictTranslator;
class WindowKeeper;

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool devTools READ devTools CONSTANT)
    Q_PROPERTY(QString lang READ lang WRITE setLang NOTIFY langChanged)
    Q_PROPERTY(QString fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(double fontScale READ fontScale NOTIFY fontSizeChanged)
    Q_PROPERTY(QString closeAction READ closeAction WRITE setCloseAction NOTIFY closeActionChanged)
    Q_PROPERTY(QString windowTitleExtra READ windowTitleExtra WRITE setWindowTitleExtra NOTIFY windowTitleExtraChanged)
    Q_PROPERTY(QString motion READ motion WRITE setMotion NOTIFY appearanceChanged)
    Q_PROPERTY(QString transparency READ transparency WRITE setTransparency NOTIFY appearanceChanged)
    Q_PROPERTY(QString glassMode READ glassMode WRITE setGlassMode NOTIFY appearanceChanged)
    Q_PROPERTY(bool systemMotion READ systemMotion CONSTANT)   // Windows 的"显示动画"，启动时读一次
    Q_PROPERTY(bool spaceAnim READ spaceAnim WRITE setSpaceAnim NOTIFY spaceAnimChanged)   // 0.9.3 星空动画
    // 0.12.3 设置改版。键名和取值同 Electron prefs.js：font、greetName、homeCountry、startBase、startProj
    Q_PROPERTY(QString uiFont READ uiFont WRITE setUiFont NOTIFY uiFontChanged)
    Q_PROPERTY(QString greetName READ greetName WRITE setGreetName NOTIFY prefsChanged)
    Q_PROPERTY(QString homeCountry READ homeCountry WRITE setHomeCountry NOTIFY prefsChanged)
    Q_PROPERTY(QString startBase READ startBase WRITE setStartBase NOTIFY prefsChanged)
    Q_PROPERTY(QString startProj READ startProj WRITE setStartProj NOTIFY prefsChanged)

    Q_PROPERTY(QString dataDir READ dataDir NOTIFY dataChanged)
    Q_PROPERTY(bool dataDirIsDefault READ dataDirIsDefault NOTIFY dataChanged)
    Q_PROPERTY(bool dataDirIsElectron READ dataDirIsElectron NOTIFY dataChanged)
    Q_PROPERTY(bool dataDirTemporary READ dataDirTemporary CONSTANT)   // 命令行 --data 指定的（不记进设置）
    Q_PROPERTY(QString defaultDataDir READ defaultDataDir CONSTANT)
    Q_PROPERTY(QString electronDataDir READ electronDataDir CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY dataChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY dataChanged)
    Q_PROPERTY(bool newerVersion READ newerVersion NOTIFY dataChanged)       // 更新版本的旅行计划写的（只读）
    Q_PROPERTY(double fileVersion READ fileVersion NOTIFY dataChanged)       // 文件里的 formatVersion
    // 别的旅行计划正开着这份数据（0.6.0，.lock）：只读，可以"仍然编辑"
    Q_PROPERTY(bool lockedByOther READ lockedByOther NOTIFY lockChanged)
    Q_PROPERTY(QString lockOwner READ lockOwner NOTIFY lockChanged)
    // 从 Electron 版导入（0.6.0）
    Q_PROPERTY(bool importRunning READ importRunning NOTIFY importProgress)
    Q_PROPERTY(int importDone READ importDone NOTIFY importProgress)
    Q_PROPERTY(int importTotal READ importTotal NOTIFY importProgress)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool extChanged READ extChanged NOTIFY extChangedChanged)
    // 启动时要用户选数据文件夹："" 不用选 / "firstRun" 第一次用（有 Electron 版的数据）/ "missing" 记住的找不到了
    Q_PROPERTY(QString startupChoice READ startupChoice NOTIFY startupChoiceChanged)
    Q_PROPERTY(QString missingDir READ missingDir CONSTANT)
    Q_PROPERTY(QString logDir READ logDir CONSTANT)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    // 备份（0.5.0，D-17 / D-18）
    Q_PROPERTY(QString backupDir READ backupDir NOTIFY backupChanged)
    Q_PROPERTY(bool autoBackup READ autoBackup NOTIFY backupChanged)
    Q_PROPERTY(int backupEveryDays READ backupEveryDays NOTIFY backupChanged)
    Q_PROPERTY(double lastBackup READ lastBackup NOTIFY backupChanged)       // 毫秒时间戳，0 = 没备份过
    Q_PROPERTY(bool backupRunning READ backupRunning NOTIFY backupProgress)
    Q_PROPERTY(int backupDone READ backupDone NOTIFY backupProgress)
    Q_PROPERTY(int backupTotal READ backupTotal NOTIFY backupProgress)
    // 开发版"测试改动"：只在文件夹名里带"测试"或 test 的数据副本上能用（PO 2026-11-03）
    Q_PROPERTY(bool devTestAllowed READ devTestAllowed NOTIFY dataChanged)
    // 上次意外退出（0.5.1）：{ code, dump, report, gl }，没有时为空
    Q_PROPERTY(QVariantMap lastCrash READ lastCrash NOTIFY lastCrashChanged)

public:
    AppController(Settings *settings, data::DataStore *store, QObject *parent = nullptr);
    ~AppController() override;

    // ---- main.cpp 用 ----
    void setEngine(QQmlEngine *engine) { m_engine = engine; }
    void setTranslator(DictTranslator *t) { m_translator = t; }
    void setWindow(QQuickWindow *win, WindowKeeper *keeper);
    // 启动：决定数据文件夹（tempDataDir 非空 = 命令行指定，直接用、不记进设置）
    void startup(const QString &tempDataDir);
    void applyLanguage();                 // 按设置装上 / 卸下英文词典
    void activateWindow();                // 从托盘 / 最小化恢复并调到前面（单实例、托盘用）

    QString version() const;
    bool devTools() const;
    QString lang() const;
    void setLang(const QString &v);
    QString fontSize() const;
    void setFontSize(const QString &v);
    double fontScale() const;
    QString closeAction() const;
    void setCloseAction(const QString &v);
    QString windowTitleExtra() const;
    QString motion() const;
    void setMotion(const QString &v);
    QString transparency() const;
    void setTransparency(const QString &v);
    QString glassMode() const;
    void setGlassMode(const QString &v);
    bool systemMotion() const;
    bool spaceAnim() const;
    QString uiFont() const;             // serif（默认）/ yahei / dengxian / kaiti
    void setUiFont(const QString &v);
    QString greetName() const;          // 最多 20 字
    void setGreetName(const QString &v);
    QString homeCountry() const;        // 三字母代码；空 = 按 Windows 区域设置猜（Locator）
    void setHomeCountry(const QString &v);
    QString startBase() const;          // '' 上次用的 / 卫星图 / 街道图 / 已解锁，下次打开时生效
    void setStartBase(const QString &v);
    QString startProj() const;          // '' 上次用的 / globe / mercator，下次打开时生效
    void setStartProj(const QString &v);
    void setSpaceAnim(bool on);
    void setWindowTitleExtra(const QString &v);

    QString dataDir() const;
    bool dataDirIsDefault() const;
    bool dataDirIsElectron() const;
    bool dataDirTemporary() const { return m_tempData; }
    QString defaultDataDir() const;
    QString electronDataDir() const { return m_electronDir; }
    bool readOnly() const;
    QString loadError() const;
    bool newerVersion() const;
    double fileVersion() const;
    bool lockedByOther() const;
    QString lockOwner() const;
    bool importRunning() const { return m_importThread != nullptr; }
    int importDone() const { return m_importDone; }
    int importTotal() const { return m_importTotal; }
    bool dirty() const;
    bool extChanged() const { return m_extChanged; }
    QString startupChoice() const { return m_startupChoice; }
    QString missingDir() const { return m_missingDir; }
    QString logDir() const;
    bool fullscreen() const;
    QString backupDir() const;
    bool autoBackup() const;
    int backupEveryDays() const;
    double lastBackup() const;
    bool backupRunning() const { return m_backupThread != nullptr; }
    int backupDone() const { return m_backupDone; }
    int backupTotal() const { return m_backupTotal; }
    bool devTestAllowed() const;
    QVariantMap lastCrash() const { return m_lastCrash; }
    void setLastCrash(const QVariantMap &c) { m_lastCrash = c; emit lastCrashChanged(); }

    // 有改动（以后各页面的编辑都调这个）：now = 立即保存，否则停手 delayMs 后保存（D-07）
    Q_INVOKABLE void noteChanged(bool now = false, int delayMs = data::SaveScheduler::kDefaultDelayMs);
    // 照片（0.7.0）：撤销 / 重做记录里用到的照片文件（清理时不动）
    QSet<QString> undoPhotoFiles() const;
    data::DataStore *store() const { return m_store; }
    // 站点编辑器（0.8.9）：整次编辑在撤销里算一步；编辑器开着时不撤销（Electron："先保存或取消地图编辑器里的改动，再撤销"）
    void beginEditGroup() { m_undo.beginGroup(); m_editing = true; }
    void endEditGroup() { m_undo.endGroup(); m_editing = false; }
    void rollbackEditGroup();
    bool undoGrouped() const { return m_undo.inGroup(); }
    int undoCount() const { return m_undo.undoCount(); }
    bool conflictPending() const { return m_conflictPending; }
    // 截图、内存测试时不做自动备份
    void setAutoBackupAllowed(bool on) { m_autoBackupAllowed = on; }
    // 退出前（main.cpp 在 _Exit 之前）：取消正在做的备份并等它停下（删掉没做完的备份文件夹）
    void shutdownBackup();

    // ---- QML 用 ----
    Q_INVOKABLE void toggleFullscreen();          // F11（W-04）
    Q_INVOKABLE void zoom(int dir);                 // +1 放大一档 / -1 缩小一档 / 0 原始大小
    Q_INVOKABLE void openDataFolder();
    Q_INVOKABLE QString changelogText() const;   // 打包的 CHANGELOG.md，从第一个版本开始（0.12.5）
    Q_INVOKABLE QString licensesDir() const;     // 授权全文所在的文件夹
    Q_INVOKABLE bool openLicensesDir();
    Q_INVOKABLE void openLogFolder();
    Q_INVOKABLE void openUrl(const QString &url);   // 外链用系统浏览器打开（W-10）
    Q_INVOKABLE QString localPath(const QVariant &urlOrPath) const;   // FolderDialog 给的 file:/// → 本地路径
    Q_INVOKABLE bool isDataFolder(const QString &dir) const { return DataFolder::isValid(dir); }
    Q_INVOKABLE bool isElectronFolder(const QString &dir) const { return DataFolder::isElectron(dir); }
    // 换数据文件夹（D-15）。create = 文件夹里没有 travel.json 时在那里新建空数据。
    // 返回 { ok, error, needCreate, electron（选的是 Electron 版的数据文件夹）, conflict, dir }
    Q_INVOKABLE QVariantMap switchDataFolder(const QString &dir, bool create);
    Q_INVOKABLE QVariantMap useDefaultData();       // 软件文件夹里的 data\（没有就新建）
    // 第一次用：在软件文件夹里新建数据，然后从 Electron 版导入
    Q_INVOKABLE QVariantMap firstRunImport();
    // 从 Electron 版导入（0.6.0）：src = Electron 的数据文件夹，mode = "add"（只加新的）/ "replace"（替换全部）。
    // 在后台做，完成时发 importFinished(报告)。返回 { ok, error }（ok = 已经开始）
    Q_INVOKABLE QVariantMap importElectron(const QString &src, const QString &mode);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE void takeOverLock();                // 别的旅行计划开着这份数据时，用户选"仍然编辑"
    Q_INVOKABLE void reloadData();                  // 外部修改提示上的"重新加载"
    Q_INVOKABLE void dismissExtChanged();
    Q_INVOKABLE QVariantList historyList() const;   // [{ name, date, size, plans }]
    Q_INVOKABLE QVariantMap restoreHistory(const QString &name);
    Q_INVOKABLE QVariantMap saveNow(bool force);    // 立即保存（冲突框"仍然保存"、开发版"原样保存一次"）。{ ok, conflict, error }
    // 批量算道路路线（0.8.4）用：quiet = 算完自动保存（成功不提示、冲突照样弹框一次），否则和手动保存一样。返回是否保存成功
    bool saveFor(bool quiet);
    Q_INVOKABLE void conflictKept();                // 冲突框选了"先不保存"：改动留着，下次改动时再问
    // 撤销 / 重做（D-13）：Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z
    Q_INVOKABLE void undo() { undoRedo(-1); }
    Q_INVOKABLE void redo() { undoRedo(1); }
    // 备份（D-17 / D-18）
    Q_INVOKABLE QVariantMap setBackupFolder(const QVariant &urlOrPath);   // { ok, error }
    Q_INVOKABLE void setAutoBackup(bool on);
    Q_INVOKABLE void setBackupEveryDays(int days);
    Q_INVOKABLE void backupNow() { startBackup(false); }
    Q_INVOKABLE void cancelBackup();
    // 开发版"测试改动"：给第一个计划的名字加一个 ★（满三个时去掉），走自动保存。打包版返回错误
    Q_INVOKABLE QVariantMap devTestChange();
    // 开发版：故意制造错误，实测出错提示（0.5.1）。kind = m（C++ 异常）/ crash（崩溃）/ hang（卡住 12 秒）。
    // QML 的 JS 错误在 QML 里直接制造。打包版什么都不做
    Q_INVOKABLE void devTrigger(const QString &kind);
    // 出错小框 / 上次意外退出：复制错误信息
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void clearLastCrash() { setLastCrash({}); }

    // 关闭 / 退出（W-05 / W-07 / D-22）
    Q_INVOKABLE void closeButton();                 // 窗口右上角 ×：按"关闭按钮"设置
    Q_INVOKABLE void requestQuit();                 // Ctrl+Q、托盘"退出"：先保存，失败时发 closeSaveFailed
    Q_INVOKABLE void quitSaveAnyway();              // 关闭时冲突，用户选"仍然保存并退出"
    Q_INVOKABLE void quitWithCopy();                // 用户选"另存一份后退出"

signals:
    void langChanged();
    void fontSizeChanged();
    void closeActionChanged();
    void windowTitleExtraChanged();
    void appearanceChanged();
    void spaceAnimChanged();
    void uiFontChanged();
    void prefsChanged();
    void dataChanged();
    void docReplaced();       // 撤销 / 重做换了一版数据（0.7.0，照片页跟着刷新）
    void dirtyChanged();
    void extChangedChanged();
    void startupChoiceChanged();
    void fullscreenChanged();
    void backupChanged();
    void backupProgress();
    void lastCrashChanged();
    void lockChanged();
    void importProgress();
    void importFinished(const QVariantMap &report);
    void toast(const QString &text, bool error);
    void saveConflict();                                 // 保存时发现 data.js 被别处改过（不是关闭时）
    void closeSaveFailed(bool conflict, const QString &error);
    // 窗口里按下了鼠标（0.8.5，scene 坐标；只是通知，不拦截）：图层泡泡"点别处收回"用（Electron document click）
    void pointerPressed(double x, double y);
    // 鼠标侧键（0.8.8，S-13）：-1 = 后退键，1 = 前进键（Electron 在 mouseup 时后退 / 前进）
    void navButton(int dir);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void setPref(const QString &key, const QString &v);
    void loadFolder(const QString &dir, bool remember);
    bool doSave(bool force, bool quiet);   // 保存并处理结果（冲突 → 弹框一次；失败 → 提示）。返回 true = 保存成功
    void autoSave();                       // SaveScheduler 到点
    void afterSaved();
    void undoRedo(int dir);
    void startBackup(bool automatic);
    void maybeAutoBackup();
    void backupFinished(bool ok, bool cancelled, const QString &dir, int files, const QString &error, bool automatic);
    void finishQuit();
    void hideToTray();
    QString iconPath() const;
    void importDoneOnMain(const data::ImportReport &rep, const json::Value &doc, bool replace);
    void shutdownImport();

    Settings *m_settings;
    data::DataStore *m_store;
    QQmlEngine *m_engine = nullptr;
    DictTranslator *m_translator = nullptr;
    bool m_translatorInstalled = false;
    QPointer<QQuickWindow> m_win;
    WindowKeeper *m_keeper = nullptr;
    platform::TrayIcon *m_tray = nullptr;
    platform::Fullscreen *m_full = nullptr;
    QString m_electronDir;
    QString m_startupChoice;
    QString m_missingDir;
    bool m_tempData = false;
    bool m_extChanged = false;
    bool m_quitting = false;

    data::SaveScheduler m_saver;
    data::UndoStack m_undo;
    bool m_editing = false;           // 站点编辑器开着（0.8.9）
    bool m_conflictPending = false;   // 冲突框开着 / 还没处理：先不自动保存

    QThread *m_backupThread = nullptr;
    std::atomic<bool> m_backupCancel { false };
    int m_backupDone = 0;
    int m_backupTotal = 0;
    bool m_autoBackupAllowed = true;
    QVariantMap m_lastCrash;

    QThread *m_importThread = nullptr;
    std::atomic<bool> m_importCancel { false };
    int m_importDone = 0;
    int m_importTotal = 0;
};

} // namespace tp::app
