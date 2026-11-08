#include "appcontroller.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QMouseEvent>

#include "backup.h"
#include "perf.h"
#include "crashhandler.h"
#include "datastore.h"
#include "electronimport.h"
#include "nativeformat.h"
#include "photolib.h"
#include "fullscreen.h"
#include "paths.h"
#include "settings.h"
#include "translator.h"
#include "tray.h"
#include "version.h"
#include "windowstate.h"

#include "miniz.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <cstring>
#include <stdexcept>
#include <QUrl>

namespace tp::app {

using data::DataStore;

namespace {
QVariantMap result(bool ok, const QString &error = {}, bool conflict = false)
{
    return { { QStringLiteral("ok"), ok }, { QStringLiteral("error"), error }, { QStringLiteral("conflict"), conflict } };
}
}

AppController::AppController(Settings *settings, DataStore *store, QObject *parent)
    : QObject(parent), m_settings(settings), m_store(store)
{
    m_full = new platform::Fullscreen;
    m_electronDir = DataFolder::readElectronDataDir(Paths::electronSettingsFile());
    connect(m_store, &DataStore::contentChanged, this, [this] { ++m_contentSeq; });
    connect(m_store, &DataStore::externallyChanged, this, [this] {
        qInfo("travel.json 被别处改过");
        if (!m_extChanged) { m_extChanged = true; emit extChangedChanged(); }
    });
    connect(m_store, &DataStore::goneChanged, this, &AppController::dataGoneChanged);
    connect(m_store, &DataStore::dirtyChanged, this, &AppController::dirtyChanged);
    connect(m_store, &DataStore::lockChanged, this, [this] {
        if (m_store->lockedByOther()) qInfo("数据正在别处打开：%s（只读）", qUtf8Printable(m_store->lockOwner().describe()));
        else qInfo("数据锁：现在由这个窗口编辑");
        emit lockChanged();
        emit dataChanged();
    });
    connect(&m_saver, &data::SaveScheduler::saveRequested, this, &AppController::autoSave);
}

AppController::~AppController()
{
    shutdownBackup();
    delete m_full;
}

void AppController::setWindow(QQuickWindow *win, WindowKeeper *keeper)
{
    m_win = win;
    m_keeper = keeper;
    if (win) win->installEventFilter(this);   // 0.8.5：窗口里任何地方按下鼠标都告诉 QML（"点别处收回"泡泡），不拦截
}

bool AppController::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_win && ev->type() == QEvent::MouseButtonPress) {
        const QPointF p = static_cast<QMouseEvent *>(ev)->scenePosition();
        emit pointerPressed(p.x(), p.y());
    } else if (obj == m_win && ev->type() == QEvent::MouseButtonRelease) {
        const Qt::MouseButton b = static_cast<QMouseEvent *>(ev)->button();
        if (b == Qt::BackButton) emit navButton(-1);
        else if (b == Qt::ForwardButton) emit navButton(1);
    }
    return QObject::eventFilter(obj, ev);
}

QString AppController::version() const { return QStringLiteral(TP_VERSION_STRING); }

bool AppController::devTools() const
{
#ifdef TP_DEV_TOOLS
    return true;
#else
    return false;
#endif
}

// ---------- 语言 ----------
QString AppController::lang() const { return m_settings->lang(); }
void AppController::setLang(const QString &v)
{
    if (v == lang()) return;
    m_settings->setLang(v);
    m_settings->save();
    applyLanguage();
    emit langChanged();
}

void AppController::applyLanguage()
{
    if (!m_translator) return;
    const bool en = m_settings->lang() == QLatin1String("en");
    if (en && !m_translatorInstalled) m_translatorInstalled = QCoreApplication::installTranslator(m_translator);
    else if (!en && m_translatorInstalled) { QCoreApplication::removeTranslator(m_translator); m_translatorInstalled = false; }
    if (m_engine) m_engine->retranslate();
}

// ---------- 字号 / 缩放（W-09） ----------
QString AppController::fontSize() const { return m_settings->fontSize(); }
void AppController::setFontSize(const QString &v)
{
    if (v == fontSize()) return;
    m_settings->setFontSize(v);
    m_settings->save();
    emit fontSizeChanged();
}
double AppController::fontScale() const { return Settings::fontScaleFor(m_settings->fontSize()); }
void AppController::zoom(int dir)
{
    setFontSize(dir == 0 ? QStringLiteral("m") : Settings::stepFontSize(fontSize(), dir));
}

// ---------- 关闭按钮、窗口标题 ----------
QString AppController::closeAction() const { return m_settings->closeAction(); }
void AppController::setCloseAction(const QString &v)
{
    if (v == closeAction()) return;
    m_settings->setCloseAction(v);
    m_settings->save();
    emit closeActionChanged();
}
QString AppController::windowTitleExtra() const { return m_settings->windowTitle(); }
// ---------- 星空动画（0.9.3）----------
QString AppController::motion() const { return m_settings->motion(); }
void AppController::setMotion(const QString &v) { m_settings->setMotion(v); m_settings->save(); emit appearanceChanged(); }
QString AppController::transparency() const { return m_settings->transparency(); }
void AppController::setTransparency(const QString &v) { m_settings->setTransparency(v); m_settings->save(); emit appearanceChanged(); }
QString AppController::glassMode() const { return m_settings->glassMode(); }
void AppController::setGlassMode(const QString &v) { m_settings->setGlassMode(v); m_settings->save(); emit appearanceChanged(); }
bool AppController::systemMotion() const
{
#ifdef Q_OS_WIN
    static const bool on = [] { BOOL b = TRUE; return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &b, 0) || b; }();
    return on;
#else
    return true;
#endif
}
bool AppController::spaceAnim() const { return m_settings->spaceAnim(); }
void AppController::setSpaceAnim(bool on)
{
    if (on == spaceAnim()) return;
    m_settings->setSpaceAnim(on);
    m_settings->save();
    emit spaceAnimChanged();
}
// ---------- 0.12.3 设置改版 ----------
QString AppController::uiFont() const
{
    const QString v = m_settings->value(QStringLiteral("font")).toString();
    return v == QLatin1String("yahei") || v == QLatin1String("dengxian") || v == QLatin1String("kaiti") ? v : QStringLiteral("serif");
}
void AppController::setUiFont(const QString &v)
{
    if (v == uiFont()) return;
    m_settings->setValue(QStringLiteral("font"), v);
    m_settings->save();
    emit uiFontChanged();
}
void AppController::setPref(const QString &key, const QString &v)
{
    if (v == m_settings->value(key).toString()) return;
    m_settings->setValue(key, v);
    m_settings->save();
    emit prefsChanged();
}
bool AppController::tourDone() const { return m_settings->value(QStringLiteral("tourDone")).toString() == QLatin1String("1"); }
void AppController::setTourDone(bool on) { setPref(QStringLiteral("tourDone"), on ? QStringLiteral("1") : QString()); }
bool AppController::dataGone() const { return m_store->gone(); }
QString AppController::noteVersion()
{
    const QString last = m_settings->value(QStringLiteral("lastVersion")).toString();
    if (last != version()) { m_settings->setValue(QStringLiteral("lastVersion"), version()); m_settings->save(); }
    return last;
}
QString AppController::greetName() const { return m_settings->value(QStringLiteral("greetName")).toString(); }
void AppController::setGreetName(const QString &v) { setPref(QStringLiteral("greetName"), v.trimmed().left(20)); }
QString AppController::homeCountry() const { return m_settings->value(QStringLiteral("homeCountry")).toString(); }
void AppController::setHomeCountry(const QString &v) { setPref(QStringLiteral("homeCountry"), v); }
QString AppController::startBase() const { return m_settings->value(QStringLiteral("startBase")).toString(); }
void AppController::setStartBase(const QString &v) { setPref(QStringLiteral("startBase"), v); }
QString AppController::startProj() const { return m_settings->value(QStringLiteral("startProj")).toString(); }
void AppController::setStartProj(const QString &v) { setPref(QStringLiteral("startProj"), v); }

void AppController::setWindowTitleExtra(const QString &v)
{
    if (v.trimmed().left(60) == windowTitleExtra()) return;
    m_settings->setWindowTitle(v);
    m_settings->save();
    emit windowTitleExtraChanged();
}

// ---------- 数据文件夹 ----------
QString AppController::dataDir() const { return QDir::toNativeSeparators(m_store->dataDir()); }
bool AppController::dataDirIsDefault() const { return DataFolder::samePath(m_store->dataDir(), Paths::defaultDataDir()); }
bool AppController::dataDirIsElectron() const { return DataFolder::samePath(m_store->dataDir(), m_electronDir); }
QString AppController::defaultDataDir() const { return QDir::toNativeSeparators(Paths::defaultDataDir()); }
bool AppController::readOnly() const { return m_store->isReadOnly(); }
QString AppController::loadError() const { return m_store->loadError(); }
bool AppController::newerVersion() const { return m_store->isNewerFormat(); }
double AppController::fileVersion() const { return m_store->fileFormatVersion(); }
bool AppController::lockedByOther() const { return m_store->lockedByOther(); }
QString AppController::lockOwner() const { return m_store->lockedByOther() ? m_store->lockOwner().describe() : QString(); }

void AppController::takeOverLock()
{
    if (!m_store->lockedByOther()) return;
    qWarning("用户选了仍然编辑：接管 %s 的数据锁", qUtf8Printable(m_store->lockOwner().describe()));
    m_store->takeOverLock();
}
bool AppController::dirty() const { return m_store->isDirty(); }
QString AppController::logDir() const { return QDir::toNativeSeparators(Paths::logDir()); }

void AppController::startup(const QString &tempDataDir)
{
    if (!tempDataDir.isEmpty()) {
        m_tempData = true;
        loadFolder(tempDataDir, false);
        return;
    }
    const QString saved = m_settings->dataDir();
    const QString def = Paths::defaultDataDir();
    // 0.5.x 记住的是 Electron 版的数据文件夹：当成导入来源（原生版不再和 Electron 共用）
    if (m_electronDir.isEmpty() && DataFolder::isElectron(saved) && !DataFolder::isValid(saved)) m_electronDir = QDir::toNativeSeparators(saved);
    switch (DataFolder::decide({ saved, def, m_electronDir })) {
    case DataFolder::Startup::UseSaved:
        loadFolder(saved, false);
        break;
    case DataFolder::Startup::UseDefault:
        loadFolder(def, true);
        break;
    case DataFolder::Startup::CreateDefault: {
        // 第一次用（0.12.7，S-16）：先显示欢迎页，点"开始"才写文件；语言按系统猜（可以在欢迎页改）
        if (!m_settings->hasLang()) setLang(QLocale::system().name().startsWith(QLatin1String("zh")) ? QStringLiteral("zh") : QStringLiteral("en"));
        m_startupChoice = QStringLiteral("welcome");
        break;
    }
    case DataFolder::Startup::AskFirstRun:
        m_startupChoice = QStringLiteral("firstRun");
        break;
    case DataFolder::Startup::AskMissing:
        m_missingDir = QDir::toNativeSeparators(saved);
        m_startupChoice = QStringLiteral("missing");
        break;
    }
    if (!m_startupChoice.isEmpty()) {
        qInfo("启动时需要选择数据文件夹（%s）", qUtf8Printable(m_startupChoice));
    }
    // 打开 6 秒后检查自动备份，不抢启动时间（Electron prefs.js）
    QTimer::singleShot(6000, this, &AppController::maybeAutoBackup);
}

void AppController::loadFolder(const QString &dir, bool remember)
{
    m_saver.cancel();
    m_conflictPending = false;
    m_store->stopWatching();
    m_store->load(dir);
    m_undo.reset(m_store->data());   // 换文件夹 / 重新读取后撤销栈清空（对应 Electron 重新加载页面）
    if (m_store->state() == DataStore::State::Unreadable) qWarning("travel.json 读不出来，只读模式：%s", qUtf8Printable(m_store->loadError()));
    else if (m_store->isNewerFormat()) qWarning("travel.json 是更新版本写的（formatVersion %d），只读", m_store->fileFormatVersion());
    else qInfo("数据：%s（%s）", qUtf8Printable(QDir::toNativeSeparators(m_store->dataFile())),
               m_store->state() == DataStore::State::Missing ? "没有 travel.json，按空数据" : "已读取");
    m_store->startWatching();
    if (remember && !m_tempData) { m_settings->setDataDir(dir); m_settings->save(); }
    if (m_extChanged) { m_extChanged = false; emit extChangedChanged(); }
    if (!m_startupChoice.isEmpty()) { m_startupChoice.clear(); emit startupChoiceChanged(); }
    emit dataChanged();
    emit dirtyChanged();
}

QString AppController::localPath(const QVariant &v) const
{
    const QUrl url = v.toUrl();
    const QString s = url.isLocalFile() ? url.toLocalFile() : v.toString();
    return s.isEmpty() ? QString() : QDir::toNativeSeparators(QDir::cleanPath(s));
}

QVariantMap AppController::switchDataFolder(const QString &dirIn, bool create)
{
    const QString dir = localPath(dirIn);
    if (dir.isEmpty()) return result(false, tp::tr("没有选文件夹"));
    if (!DataFolder::isValid(dir)) {
        if (DataFolder::isElectron(dir)) {
            // Electron 版的数据文件夹：原生版不在这里写东西，改为提示导入
            QVariantMap r = result(false);
            r.insert(QStringLiteral("electron"), true);
            r.insert(QStringLiteral("dir"), dir);
            return r;
        }
        if (!create) {
            QVariantMap r = result(false);
            r.insert(QStringLiteral("needCreate"), true);
            r.insert(QStringLiteral("dir"), dir);
            return r;
        }
        QString err;
        if (!DataFolder::createEmpty(dir, version(), &err)) return result(false, err);
    }
    // 先把没保存的改动存好（和 Electron 一样：换文件夹前先保存）
    m_saver.cancel();
    if (m_store->state() != DataStore::State::None && m_store->isDirty()) {
        const auto s = m_store->save(false);
        if (!s.ok) return result(false, s.conflict ? tp::tr("数据文件被别处改过，先处理没保存的改动再换文件夹。") : s.error, s.conflict);
    }
    loadFolder(dir, true);
    emit toast(tp::tr("已切换到新的数据文件夹"), false);
    return result(true);
}

QVariantMap AppController::useDefaultData()
{
    const QString def = Paths::defaultDataDir();
    QString err;
    if (!DataFolder::createEmpty(def, version(), &err)) return result(false, err);
    loadFolder(def, true);
    return result(true);
}

QVariantMap AppController::welcomeStart(const QString &dirIn)
{
    const QString dir = localPath(dirIn);
    if (dir.isEmpty()) return result(false, tp::tr("没有选文件夹"));
    QString err;
    if (!DataFolder::createEmpty(dir, version(), &err, lang())) return result(false, err);
    loadFolder(dir, true);
    return result(true);
}

QString AppController::welcomeFolder(const QVariant &picked) const
{
    // 照 Electron welcome:pick：选的文件夹里已经有数据就用它；否则在里面建一个「旅行计划数据」，不把文件撒在用户选的文件夹里
    const QString dir = localPath(picked);
    if (dir.isEmpty() || DataFolder::isValid(dir) || DataFolder::isElectron(dir)) return dir;
    const QString name = QStringLiteral("旅行计划数据");
    return QFileInfo(dir).fileName() == name ? dir : QDir::toNativeSeparators(QDir(dir).filePath(name));
}

QString AppController::guessCurrency() const
{
    // 照 Electron welcome.html：按系统地区猜主货币，以后在设置里改
    static const QHash<QString, QString> cur = {
        { "CN", "CNY" }, { "AU", "AUD" }, { "US", "USD" }, { "GB", "GBP" }, { "NZ", "NZD" }, { "HK", "HKD" }, { "TW", "TWD" }, { "JP", "JPY" },
        { "KR", "KRW" }, { "SG", "SGD" }, { "CA", "CAD" }, { "DE", "EUR" }, { "FR", "EUR" }, { "IT", "EUR" }, { "ES", "EUR" }, { "NL", "EUR" },
        { "CH", "CHF" }, { "MY", "MYR" }, { "TH", "THB" },
    };
    const QLocale sys = QLocale::system();
    return cur.value(QLocale::territoryToCode(sys.territory()), sys.name().startsWith(QLatin1String("zh")) ? QStringLiteral("CNY") : QStringLiteral("USD"));
}

QVariantMap AppController::firstRunImport()
{
    if (!DataFolder::isElectron(m_electronDir)) return result(false, tp::tr("找不到 Electron 版的数据文件夹"));
    const QVariantMap r = useDefaultData();
    if (!r.value(QStringLiteral("ok")).toBool()) return r;
    return importElectron(m_electronDir, QStringLiteral("replace"));
}

// ---------- 从 Electron 版导入（0.6.0） ----------
QVariantMap AppController::electronConflicts(const QString &srcIn) const
{
    const QString src = localPath(srcIn);
    QString err;
    const QVariantList list = data::ElectronImport::conflicts(src, m_store->data(), QDateTime::currentDateTime(), &err);
    if (!err.isEmpty()) return result(false, err);
    QVariantMap r = result(true);
    r.insert(QStringLiteral("plans"), list);
    return r;
}

QVariantMap AppController::importElectron(const QString &srcIn, const QString &mode, const QVariantMap &picksIn)
{
    const QString src = localPath(srcIn);
    if (m_importThread) return result(false, tp::tr("正在导入，请稍等"));
    if (m_store->state() == DataStore::State::None || m_store->dataDir().isEmpty()) return result(false, tp::tr("还没有打开数据文件夹"));
    if (m_store->isReadOnly()) return result(false, tp::tr("现在是只读模式，不能导入。"));
    if (!DataFolder::isElectron(src)) return result(false, tp::tr("这个文件夹里没有 Electron 版的 data.js"));
    if (DataFolder::samePath(src, m_store->dataDir())) return result(false, tp::tr("不能导入到同一个文件夹"));
    m_saver.cancel();
    if (m_store->isDirty() && !doSave(false, true)) return result(false, tp::tr("先保存现在的改动，再导入。"));

    const bool replace = mode == QLatin1String("replace");
    const QString dst = m_store->dataDir();
    const json::Value existing = replace ? json::Value() : m_store->data();
    const QDateTime now = QDateTime::currentDateTime();
    const QString ver = version();
    m_importCancel = false;
    m_importDone = m_importTotal = 0;
    QPointer<AppController> self(this);
    qInfo("开始从 Electron 版导入：%s → %s（%s）", qUtf8Printable(QDir::toNativeSeparators(src)), qUtf8Printable(QDir::toNativeSeparators(dst)),
          replace ? "替换全部" : "只加新的");
    QHash<QString, data::planmerge::Mode> picks;
    for (auto it = picksIn.cbegin(); it != picksIn.cend(); ++it) picks.insert(it.key(), data::planmerge::modeFromKey(it.value().toString()));
    m_importThread = QThread::create([self, src, dst, existing, replace, now, ver, picks, cancel = &m_importCancel] {
        json::Value doc;
        const data::ImportReport rep = data::ElectronImport::run(
            src, dst, existing, replace ? data::ElectronImport::Mode::Replace : data::ElectronImport::Mode::AddNew, now, ver, &doc,
            [self, cancel](int done, int total) {
                if (self) QMetaObject::invokeMethod(self.data(), [self, done, total] {
                    if (!self) return;
                    self->m_importDone = done;
                    self->m_importTotal = total;
                    emit self->importProgress();
                }, Qt::QueuedConnection);
                return !cancel->load();
            }, picks);
        if (self) QMetaObject::invokeMethod(self.data(), [self, rep, doc, replace] {
            if (self) self->importDoneOnMain(rep, doc, replace);
        }, Qt::QueuedConnection);
    });
    connect(m_importThread, &QThread::finished, m_importThread, &QObject::deleteLater);
    m_importThread->start(QThread::LowPriority);
    emit importProgress();
    return result(true);
}

void AppController::cancelImport()
{
    if (m_importThread) m_importCancel = true;
}

void AppController::shutdownImport()
{
    if (!m_importThread) return;
    m_importCancel = true;
    m_importThread->wait();
    m_importThread = nullptr;
}

void AppController::importDoneOnMain(const data::ImportReport &repIn, const json::Value &doc, bool replace)
{
    if (m_importThread) { m_importThread->wait(); m_importThread = nullptr; }
    data::ImportReport rep = repIn;
    if (rep.ok) {
        if (replace) {
            QString err;
            rep.backupName = m_store->snapshotBeforeImport(&err);
            if (rep.backupName.isEmpty() && !err.isEmpty()) {
                rep.ok = false;
                rep.error = tp::tr("备份现在的数据失败：") + err;
            }
        }
        if (rep.ok) {
            m_store->replaceData(doc);
            const auto r = m_store->save(false);
            if (!r.ok) {
                rep.ok = false;
                rep.error = r.conflict ? tp::tr("导入期间数据文件被别处改过，导入没有保存。") : r.error;
            }
        }
        if (!rep.ok) {
            data::ElectronImport::rollback(rep.copiedFiles);
            m_store->discardUnsaved();
            loadFolder(m_store->dataDir(), false);   // 回到磁盘上的那一份
        } else {
            m_undo.reset(m_store->data());
            emit dataChanged();
        }
    }
    if (rep.ok)
        qInfo("导入完成：%d 个计划（跳过已有的 %d 个）、%d 个站点、%d 张照片（复制 %d 张，原图 %d 张）、%d 篇游记；缺文件 %lld 张，没用到的文件 %lld 个，不认识的字段 %lld 个%s",
              rep.plans, rep.plansSkipped, rep.stops, rep.photos, rep.photosCopied, rep.originalsCopied, rep.journal,
              qlonglong(rep.missingFiles.size()), qlonglong(rep.unusedFiles.size()), qlonglong(rep.unknownFields.size()),
              rep.backupName.isEmpty() ? "" : qUtf8Printable(QStringLiteral("；原来的数据备份为 ") + rep.backupName));
    else if (rep.cancelled) qInfo("导入已取消");
    else qWarning("导入失败：%s", qUtf8Printable(rep.error));
    for (const QString &m : rep.missingFiles) qInfo("  找不到照片文件：%s", qUtf8Printable(m));
    for (const QString &u : rep.unknownFields) qInfo("  不认识的字段（原样保留）：%s", qUtf8Printable(u));
    for (const QString &d : rep.duplicatePhotos) qInfo("  同一个文件被多处当成照片：%s", qUtf8Printable(d));

    const auto first = [](const QStringList &l, int n) { return QVariant(l.mid(0, n)); };
    emit importProgress();
    emit importFinished({
        { QStringLiteral("ok"), rep.ok }, { QStringLiteral("cancelled"), rep.cancelled }, { QStringLiteral("error"), rep.error },
        { QStringLiteral("replace"), replace },
        { QStringLiteral("plans"), rep.plans }, { QStringLiteral("plansSkipped"), rep.plansSkipped }, { QStringLiteral("stops"), rep.stops },
        { QStringLiteral("photos"), rep.photos }, { QStringLiteral("journal"), rep.journal },
        { QStringLiteral("photosCopied"), rep.photosCopied }, { QStringLiteral("originalsCopied"), rep.originalsCopied },
        { QStringLiteral("missing"), first(rep.missingFiles, 8) }, { QStringLiteral("missingCount"), int(rep.missingFiles.size()) },
        { QStringLiteral("unusedCount"), int(rep.unusedFiles.size()) },
        { QStringLiteral("unknown"), first(rep.unknownFields, 8) }, { QStringLiteral("unknownCount"), int(rep.unknownFields.size()) },
        { QStringLiteral("duplicateCount"), int(rep.duplicatePhotos.size()) }, { QStringLiteral("externalCount"), int(rep.externalPhotos.size()) },
        { QStringLiteral("backupName"), rep.backupName },
    });
}

void AppController::reloadData()
{
    if (m_store->dataDir().isEmpty()) return;
    loadFolder(m_store->dataDir(), false);
    emit toast(tp::tr("已重新加载"), false);
}

void AppController::dismissExtChanged()
{
    if (m_extChanged) { m_extChanged = false; emit extChangedChanged(); }
}

QVariantList AppController::historyList() const
{
    QVariantList out;
    for (const auto &h : m_store->historyList())
        out << QVariantMap{ { QStringLiteral("name"), h.name }, { QStringLiteral("date"), h.date },
                            { QStringLiteral("size"), h.size }, { QStringLiteral("plans"), h.plans } };
    return out;
}

QVariantMap AppController::restoreHistory(const QString &name)
{
    const auto r = m_store->restoreHistory(name);
    if (!r.ok) return result(false, r.error);
    qInfo("从历史版本恢复：%s", qUtf8Printable(name));
    loadFolder(m_store->dataDir(), false);
    emit toast(tp::tr("已恢复（原来的数据已存成 travel.backup.json）"), false);
    return result(true);
}

// ---------- 保存（D-05 / D-06 / D-07） ----------
bool AppController::doSave(bool force, bool quiet)
{
    perf::Scope timing("保存数据", 0);   // 0.9.1：每次保存都记（序列化 + 备份 + 写临时文件改名 + 回读比对）
    const auto r = m_store->save(force);
    if (r.ok) {
        m_conflictPending = false;
        if (m_extChanged) { m_extChanged = false; emit extChangedChanged(); }
        afterSaved();
        if (!quiet) emit toast(tp::tr("已保存"), false);
        return true;
    }
    if (r.conflict) {
        if (!m_conflictPending) { m_conflictPending = true; emit saveConflict(); }
        return false;
    }
    qWarning("保存失败：%s", qUtf8Printable(r.error));
    // Electron：'保存失败：' + 原因 + '\n改动还在窗口里没丢，可以再试一次。'（自动保存失败也提示）
    emit saveFailed(tp::tr("保存失败：") + r.error + QLatin1Char('\n') + tp::tr("改动还在窗口里没丢，可以再试一次。"));
    return false;
}

void AppController::rollbackEditGroup()
{
    m_undo.rollbackGroup(m_store->data());
    m_editing = false;
}

void AppController::afterSaved()
{
    m_undo.noteSaved(m_store->data());
}

QSet<QString> AppController::undoPhotoFiles() const
{
    QSet<QString> s;
    m_undo.forEachSnapshot([&s](const json::Value &v) { s.unite(data::photolib::referencedFiles(v)); });
    return s;
}

void AppController::noteChanged(bool now, int delayMs)
{
    m_saver.changed(now ? data::SaveScheduler::Mode::Now : data::SaveScheduler::Mode::Debounced, delayMs);
}

void AppController::commitStep()
{
    if (m_editing || m_conflictPending || m_store->state() == DataStore::State::None || m_store->isReadOnly() || !m_store->isDirty()) return;
    m_saver.cancel();
    doSave(false, true);
}

QString AppController::listPref(const QString &key) const { return m_settings->value(QStringLiteral("list.") + key).toString(); }

void AppController::autoSave()
{
    if (m_quitting || m_conflictPending) return;   // 冲突还没处理：改动留在窗口里，等用户选
    if (m_store->state() == DataStore::State::None || m_store->isReadOnly() || !m_store->isDirty()) return;
    doSave(false, true);   // 自动保存成功不提示（Electron quiet），失败照样提示
}

QVariantMap AppController::saveNow(bool force)
{
    m_saver.cancel();
    m_conflictPending = false;   // 用户刚在冲突框里做了选择（或手动保存）：这次的结果重新算
    const bool ok = doSave(force, false);
    return result(ok, QString(), !ok && m_conflictPending);
}

bool AppController::saveFor(bool quiet)
{
    if (!quiet) return saveNow(false).value(QStringLiteral("ok")).toBool();
    if (m_quitting || m_conflictPending) return false;
    if (m_store->state() == DataStore::State::None || m_store->isReadOnly() || !m_store->isDirty()) return !m_store->isDirty();
    m_saver.cancel();
    return doSave(false, true);
}

void AppController::conflictKept()
{
    m_conflictPending = false;   // 下次改动时再问（和 Electron 一样）
    emit toast(tp::tr("已取消保存，改动还在窗口里没丢。"), false);
}

// ---------- 撤销 / 重做（D-13） ----------
quint64 AppController::undoStateId() const { return m_store->isDirty() ? 0 : m_undo.stateId(); }

void AppController::undoRedo(int dir)
{
    if (m_store->state() == DataStore::State::None || m_store->isReadOnly()) return;
    if (m_editing) {
        emit toast(tp::tr("编辑器开着时不能撤销：先点「完成」，或者点「放弃这次的修改」"), false);
        return;
    }
    if (m_conflictPending) {
        emit toast(tp::tr("先处理“数据文件被别处改过”，再撤销或重做。"), false);
        return;
    }
    // 还没保存的改动先存成一步（Electron 一样）；存不了就不撤销
    if (m_store->isDirty()) {
        m_saver.cancel();
        if (!doSave(false, true)) return;
    }
    const auto snap = dir < 0 ? m_undo.undo(m_store->data()) : m_undo.redo(m_store->data());
    if (!snap) {
        emit toast(dir < 0 ? tp::tr("没有可以撤销的了") : tp::tr("没有可以重做的了"), false);
        return;
    }
    m_store->restoreData(*snap);
    emit docReplaced();
    bool ok;
    {
        data::UndoStack::Suppress s(m_undo);   // 撤销后的这次保存不再记一步
        ok = doSave(false, true);
    }
    emit undoApplied(dir);
    qInfo("%s（%s）", dir < 0 ? "撤销" : "重做", ok ? "已保存" : "没保存");
    if (ok) emit toast(dir < 0 ? tp::tr("已撤销上一步（已保存）") : tp::tr("已重做（已保存）"), false);
}

// ---------- 开发版：测试改动 ----------
bool AppController::devTestAllowed() const
{
    if (!devTools() || m_store->dataDir().isEmpty()) return false;
    const QString name = QFileInfo(m_store->dataDir()).fileName();
    return name.contains(QStringLiteral("测试")) || name.contains(QLatin1String("test"), Qt::CaseInsensitive);
}

QVariantMap AppController::devTestChange()
{
    if (!devTools()) return result(false, QStringLiteral("only in dev builds"));
    if (!devTestAllowed()) return result(false, tp::tr("只在文件夹名里带“测试”或 test 的数据副本上能用。"));
    if (m_store->isReadOnly()) return result(false, tp::tr("现在是只读模式，不能改。"));
    if (m_store->data()[QStringLiteral("plans")].asArray().empty()) return result(false, tp::tr("这份数据里没有计划。"));
    QString newName;
    m_store->modify([&newName](json::Value &d) {
        json::Value &plan = d.mutObject().find(QStringLiteral("plans"))->mutArray().front();
        json::Object &o = plan.mutObject();
        const json::Value *n = o.find(QStringLiteral("name"));
        QString name = n ? n->toString() : QString();
        if (name.endsWith(QStringLiteral("★★★"))) { while (name.endsWith(QChar(0x2605))) name.chop(1); }
        else name += QChar(0x2605);
        newName = name;
        o.set(QStringLiteral("name"), json::Value(name));
    });
    noteChanged(false);
    qInfo("测试改动：第一个计划的名字改成 %s", qUtf8Printable(newName));
    QVariantMap r = result(true);
    r.insert(QStringLiteral("name"), newName);
    return r;
}

// ---------- 备份（D-17 / D-18） ----------
QString AppController::backupDir() const { return m_settings->backupDir(); }
bool AppController::autoBackup() const { return m_settings->autoBackup(); }
int AppController::backupEveryDays() const { return m_settings->backupEveryDays(); }
double AppController::lastBackup() const { return m_settings->lastBackup(); }

QVariantMap AppController::setBackupFolder(const QVariant &urlOrPath)
{
    const QString dir = localPath(urlOrPath);
    if (dir.isEmpty()) return result(false, tp::tr("没有选文件夹"));
    if (!m_store->dataDir().isEmpty() && data::Backup::isInside(dir, m_store->dataDir()))
        return result(false, tp::tr("备份文件夹不能放在数据文件夹里面"));
    m_settings->setBackupDir(dir);
    m_settings->save();
    emit backupChanged();
    return result(true);
}

void AppController::setAutoBackup(bool on)
{
    if (on == autoBackup()) return;
    m_settings->setAutoBackup(on);
    m_settings->save();
    emit backupChanged();
}

void AppController::setBackupEveryDays(int days)
{
    m_settings->setBackupEveryDays(days);
    m_settings->save();
    emit backupChanged();
}

void AppController::maybeAutoBackup()
{
    if (!m_autoBackupAllowed || !autoBackup() || backupDir().isEmpty()) return;
    if (m_store->state() == DataStore::State::None) return;
    const double now = double(QDateTime::currentMSecsSinceEpoch());
    if (now - lastBackup() > backupEveryDays() * 86400e3) {
        qInfo("自动备份：距上次已超过 %d 天", backupEveryDays());
        startBackup(true);
    }
}

void AppController::startBackup(bool automatic)
{
    if (m_backupThread) return;
    const QString dest = backupDir();
    const QString dataDir = m_store->dataDir();
    if (dest.isEmpty()) { emit toast(tp::tr("备份失败：") + tp::tr("还没选备份文件夹"), true); return; }
    if (dataDir.isEmpty()) { emit toast(tp::tr("备份失败：") + tp::tr("数据文件夹未设置"), true); return; }
    // 还在等自动保存的改动先存掉，备份里是最新的（存不了就备份磁盘上现有的那份）
    if (m_saver.pending()) m_saver.flush();

    m_backupCancel = false;
    m_backupDone = 0;
    m_backupTotal = 0;
    const QDateTime now = QDateTime::currentDateTime();
    QPointer<AppController> self(this);
    m_backupThread = QThread::create([self, dataDir, dest, now, automatic, cancel = &m_backupCancel] {
        const auto r = data::Backup::run(dataDir, dest, now, [self, cancel](int done, int total) {
            if (self) QMetaObject::invokeMethod(self.data(), [self, done, total] {
                if (!self) return;
                self->m_backupDone = done;
                self->m_backupTotal = total;
                emit self->backupProgress();
            }, Qt::QueuedConnection);
            return !cancel->load();
        });
        if (self) QMetaObject::invokeMethod(self.data(), [self, r, automatic] {
            if (self) self->backupFinished(r.ok, r.cancelled, r.dir, r.files, r.error, automatic);
        }, Qt::QueuedConnection);
    });
    connect(m_backupThread, &QThread::finished, m_backupThread, &QObject::deleteLater);
    qInfo("开始%s备份到 %s", automatic ? "自动" : "", qUtf8Printable(QDir::toNativeSeparators(dest)));
    m_backupThread->start(QThread::LowPriority);
    emit backupProgress();
}

void AppController::backupFinished(bool ok, bool cancelled, const QString &dir, int files, const QString &error, bool automatic)
{
    if (m_backupThread) { m_backupThread->wait(); m_backupThread = nullptr; }
    if (ok) {
        m_settings->setLastBackup(double(QDateTime::currentMSecsSinceEpoch()));
        m_settings->save();
        emit backupChanged();
        qInfo("备份完成：%s（%d 个文件）", qUtf8Printable(dir), files);
        emit toast(automatic ? tp::tr("自动备份：已备份到 %1").arg(dir) : tp::tr("已备份到 %1（%2 个文件）").arg(dir).arg(files), false);
    } else if (cancelled) {
        qInfo("备份已取消");
        emit toast(tp::tr("已取消备份"), false);
    } else {
        qWarning("备份失败：%s", qUtf8Printable(error));
        emit toast(tp::tr("备份失败：") + error, true);
    }
    emit backupProgress();
}

void AppController::cancelBackup()
{
    if (m_backupThread) m_backupCancel = true;
}

void AppController::shutdownBackup()
{
    shutdownImport();
    if (!m_backupThread) return;
    m_backupCancel = true;
    m_backupThread->wait();
    m_backupThread = nullptr;
}

// ---------- 窗口、托盘、关闭 ----------
bool AppController::fullscreen() const { return m_full->isActive(); }

void AppController::toggleFullscreen()
{
    if (!m_win) return;
    if (m_keeper) {
        if (!m_full->isActive()) m_keeper->rememberNow();   // 进入前先记下普通状态
        m_keeper->setSuspended(true);                       // 全屏期间不记位置
    }
    m_full->toggle(m_win);
    if (m_keeper && !m_full->isActive()) QTimer::singleShot(600, m_keeper, [k = m_keeper] { k->setSuspended(false); });
    emit fullscreenChanged();
}

QString AppController::iconPath() const { return Paths::appDir() + QStringLiteral("/assets/app/icon.ico"); }

void AppController::activateWindow()
{
    if (!m_win) return;
    if (m_tray) m_tray->hide();
    const QWindow::Visibility v = m_win->visibility();
    if (v == QWindow::Hidden || v == QWindow::Minimized) {
        if (m_settings->maximized()) m_win->showMaximized(); else m_win->showNormal();
    }
    m_win->raise();
    m_win->requestActivate();
}

void AppController::hideToTray()
{
    if (!m_win) return;
    if (m_full->isActive()) toggleFullscreen();
    if (!m_tray) {
        m_tray = new platform::TrayIcon(this);
        connect(m_tray, &platform::TrayIcon::openRequested, this, &AppController::activateWindow);
        connect(m_tray, &platform::TrayIcon::quitRequested, this, [this] {
            activateWindow();   // 先显示窗口：保存失败时弹框要有地方显示（和 Electron 一样）
            requestQuit();
        });
    }
    const QString title = tp::tr("旅行计划") + QStringLiteral(" v") + version();
    if (m_keeper) m_keeper->rememberNow();
    if (m_tray->show(iconPath(), title, tp::tr("打开"), tp::tr("退出"))) {
        m_win->hide();
    } else {
        qWarning("托盘图标创建失败，改为最小化");
        m_win->showMinimized();
    }
}

void AppController::closeButton()
{
    const QString act = closeAction();
    if (act == QLatin1String("tray")) hideToTray();
    else if (act == QLatin1String("taskbar") && m_win) { if (m_full->isActive()) toggleFullscreen(); m_win->showMinimized(); }
    else requestQuit();
}

void AppController::requestQuit()
{
    if (m_quitting) return;
    m_saver.cancel();   // 下面直接保存（还在等自动保存的改动也在里面）
    if (m_store->state() != DataStore::State::None && m_store->isDirty()) {
        const auto r = m_store->save(false);
        if (!r.ok) {
            qWarning("关闭前保存失败：%s", r.conflict ? "travel.json 被别处改过" : qUtf8Printable(r.error));
            emit closeSaveFailed(r.conflict, r.conflict ? QString() : r.error);
            return;
        }
    }
    finishQuit();
}

void AppController::quitSaveAnyway()
{
    const auto r = m_store->save(true);
    if (!r.ok) {
        // 还是不行（磁盘满、文件被占用…）：不覆盖，另存一份
        emit closeSaveFailed(false, r.error);
        return;
    }
    finishQuit();
}

void AppController::quitWithCopy()
{
    const QString name = m_store->saveRescueCopy();
    if (name.isEmpty()) {
        emit toast(tp::tr("另存失败，改动还在窗口里。"), true);
        return;
    }
    qInfo("关闭前另存为 %s", qUtf8Printable(name));
    m_store->discardUnsaved();   // 已经另存了，退出时不再写 travel.json
    finishQuit();
}

void AppController::finishQuit()
{
    m_quitting = true;
    if (m_full->isActive()) toggleFullscreen();   // 退出前回到原来的状态，记下的位置才对
    if (m_keeper) m_keeper->rememberNow();
    if (m_tray) m_tray->hide();
    QCoreApplication::exit(0);
}

QVariantMap AppController::exportReport(const QString &info)
{
    const QString name = QStringLiteral("TravelPlanner-report-%1.zip").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).filePath(name);
    const auto fail = [](const QString &why) { qWarning("导出问题报告失败：%s", qUtf8Printable(why)); return QVariantMap{ { QStringLiteral("ok"), false }, { QStringLiteral("error"), why } }; };
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20)) return fail(QStringLiteral("zip"));
    const QByteArray about = (QStringLiteral("TravelPlanner %1\nQt %2\n%3\n").arg(QStringLiteral(TP_VERSION_STRING), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName()) + info + u'\n').toUtf8();
    bool ok = mz_zip_writer_add_mem(&zip, "info.txt", about.constData(), size_t(about.size()), 6);
    // 只拿日志文件夹第一层的文件（app.log、errors.log 和滚动的旧日志）
    const QFileInfoList logs = QDir(Paths::logDir()).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : logs) {
        if (!ok) break;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray b = f.read(32 << 20);
        ok = mz_zip_writer_add_mem(&zip, ("logs/" + fi.fileName()).toUtf8().constData(), b.constData(), size_t(b.size()), 6);
    }
    void *buf = nullptr;
    size_t len = 0;
    if (ok) ok = mz_zip_writer_finalize_heap_archive(&zip, &buf, &len);
    QByteArray bytes = ok ? QByteArray(static_cast<const char *>(buf), qsizetype(len)) : QByteArray();
    mz_zip_writer_end(&zip);
    if (!ok) return fail(QStringLiteral("zip"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) return fail(QDir::toNativeSeparators(path));
    out.close();
    qInfo("导出问题报告：%s（%d 个日志文件）", qUtf8Printable(path), int(logs.size()));
    return { { QStringLiteral("ok"), true }, { QStringLiteral("name"), name }, { QStringLiteral("path"), path } };
}

void AppController::openDataFolder()
{
    if (!m_store->dataDir().isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->dataDir()));
}

QString AppController::changelogText() const
{
    QFile f(QStringLiteral(":/CHANGELOG.md"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QString text = QString::fromUtf8(f.readAll());
    const qsizetype first = text.indexOf(QLatin1String("\n## "));   // 开头的版本号规则是写给开发的
    return first < 0 ? text : text.mid(first + 1);
}

QString AppController::licensesDir() const { return QDir::toNativeSeparators(Paths::appDir() + QStringLiteral("/LICENSES")); }

bool AppController::openLicensesDir()
{
    return QFileInfo(licensesDir()).isDir() && QDesktopServices::openUrl(QUrl::fromLocalFile(licensesDir()));
}

void AppController::copyText(const QString &text)
{
    if (QClipboard *c = QGuiApplication::clipboard()) c->setText(text);
}

void AppController::devTrigger(const QString &kind)
{
    if (!devTools()) return;
    qWarning("开发版测试：故意制造 %s", qUtf8Printable(kind));
    if (kind == QLatin1String("m")) {
        // 在事件处理里抛出、没人接 → TpApplication::notify 接住 → 出错小框 TP-M-xxxx
        QTimer::singleShot(0, this, [] { throw std::runtime_error("开发版测试：故意抛出的 C++ 异常"); });
    } else if (kind == QLatin1String("crash") || kind == QLatin1String("crash-thread")) {
        // 0.5.2：崩溃前记一行崩溃处理的状态（过滤器是不是我们的），没接住时能分清原因
        qWarning("开发版测试：崩溃前 %s", qUtf8Printable(platform::CrashHandler::status()));
        if (kind == QLatin1String("crash"))   // 主线程：定时器 → notify（外层 SEH 接）
            QTimer::singleShot(300, this, [] { platform::CrashHandler::crashForTest(); });
        else                                   // 后台线程：不经过 notify（只靠过滤器）
            QTimer::singleShot(300, this, [] { platform::CrashHandler::crashOnBackgroundThreadForTest(); });
    } else if (kind == QLatin1String("hang")) {
        QTimer::singleShot(300, this, [] { QThread::sleep(12); });
    }
}

void AppController::openLogFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logDir())); }

void AppController::openUrl(const QString &url)
{
    const QUrl u(url);
    if (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")) QDesktopServices::openUrl(u);
}

} // namespace tp::app
