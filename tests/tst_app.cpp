// 0.4.0 程序层的单元测试：设置文件、数据文件夹的选择和新建、Electron 设置读取、窗口位置纠正、翻译词典和检查脚本、日志截断。
// 0.5.1：出错报告（错误码和 Electron 一致、忽略规则、去掉路径、同码一次 / 最多 3 个）、上次崩溃的记号、卡住检测计数。
// 都在临时文件夹里做，不碰真实数据。
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "crashcodes.h"
#include "crashinfo.h"
#include "datafile.h"
#include "datafolder.h"
#include "datastore.h"
#include "errorreporter.h"
#include "hangdetector.h"
#include "jsonio.h"
#include "logger.h"
#include "commands.h"
#include "navhistory.h"
#include "settings.h"
#include "translator.h"
#include "windowstate.h"

using namespace tp;
using namespace tp::app;

namespace {
bool writeText(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
// 一个最小的有效数据文件夹
// 原生版的数据文件夹（travel.json）
QString makeDataDir(const QString &dir)
{
    writeText(dir + "/travel.json", "{\n  \"format\": \"travelplanner\",\n  \"formatVersion\": 1,\n  \"plans\": []\n}\n");
    return dir;
}
// Electron 版的数据文件夹（data.js）
QString makeElectronDir(const QString &dir)
{
    writeText(dir + "/data.js", "window.TRAVEL_DATA = {\n  \"version\": 4,\n  \"plans\": []\n};\n");
    return dir;
}
}

class TestApp : public QObject
{
    Q_OBJECT
private slots:
    // ---------- 设置 ----------
    void settingsDefaults()
    {
        QTemporaryDir tmp;
        Settings s;
        QVERIFY(s.load(tmp.filePath("settings.json")));   // 不存在 = 默认
        QCOMPARE(s.closeAction(), QStringLiteral("quit"));
        QCOMPARE(s.lang(), QStringLiteral("zh"));
        QCOMPARE(s.fontSize(), QStringLiteral("m"));
        QCOMPARE(s.dataDir(), QString());
        QVERIFY(!s.hasBounds());
        QVERIFY(!s.maximized());
        QCOMPARE(s.bounds(), QRect(0, 0, 1400, 900));
    }

    void settingsRoundTripKeepsUnknownKeys()
    {
        QTemporaryDir tmp;
        const QString p = tmp.filePath("settings.json");
        // 以后版本的键、顺序都要原样保留
        writeText(p, "{\n  \"future\": {\n    \"a\": [1, 2.5, \"x\"]\n  },\n  \"closeAction\": \"tray\",\n  \"zzz\": true\n}\n");
        Settings s;
        QVERIFY(s.load(p));
        QCOMPARE(s.closeAction(), QStringLiteral("tray"));
        s.setLang("en");
        s.setBounds(QRect(10, 20, 800, 600));
        s.setMaximized(true);
        s.setDataDir("D:/旅行计划数据");
        QVERIFY(s.save());
        const QString text = QString::fromUtf8(readText(p));
        QVERIFY(text.indexOf("\"future\"") < text.indexOf("\"closeAction\""));
        QVERIFY(text.indexOf("\"closeAction\"") < text.indexOf("\"zzz\""));
        QVERIFY(text.indexOf("\"zzz\"") < text.indexOf("\"lang\""));
        QVERIFY(text.contains("\"a\": [\n      1,\n      2.5,\n      \"x\"\n    ]"));
        QVERIFY(text.contains(QStringLiteral("旅行计划数据")));   // 中文不转义
        QVERIFY(!QFile::exists(p + ".tmp"));
        Settings t;
        QVERIFY(t.load(p));
        QCOMPARE(t.lang(), QStringLiteral("en"));
        QCOMPARE(t.bounds(), QRect(10, 20, 800, 600));
        QVERIFY(t.maximized());
        QCOMPARE(QDir::fromNativeSeparators(t.dataDir()), QStringLiteral("D:/旅行计划数据"));
        // 坐标的形状和 Electron 一样：{ x, y, width, height }
        QVERIFY(text.contains("\"bounds\": {\n    \"x\": 10,\n    \"y\": 20,\n    \"width\": 800,\n    \"height\": 600\n  }"));
    }

    void settingsBrokenFileIsKept()
    {
        QTemporaryDir tmp;
        const QString p = tmp.filePath("settings.json");
        writeText(p, "{ \"closeAction\": \"tray\", ");   // 写到一半
        Settings s;
        QVERIFY(!s.load(p));
        QCOMPARE(s.closeAction(), QStringLiteral("quit"));
        QVERIFY(!s.brokenBackup().isEmpty());
        QVERIFY(QFile::exists(tmp.filePath(s.brokenBackup())));
        QVERIFY(!QFile::exists(p));
        QVERIFY(s.save());   // 之后正常写新文件
        QVERIFY(QFile::exists(p));
        // 不是对象也算坏
        writeText(p, "[1,2]");
        Settings t;
        QVERIFY(!t.load(p));
    }

    void settingsInvalidValuesFallBack()
    {
        QTemporaryDir tmp;
        const QString p = tmp.filePath("settings.json");
        writeText(p, "{\"closeAction\":\"explode\",\"lang\":\"fr\",\"fontSize\":\"huge\",\"bounds\":{\"x\":\"a\",\"width\":500}}");
        Settings s;
        QVERIFY(s.load(p));
        QCOMPARE(s.closeAction(), QStringLiteral("quit"));
        QCOMPARE(s.lang(), QStringLiteral("zh"));
        QCOMPARE(s.fontSize(), QStringLiteral("m"));
        QVERIFY(!s.hasBounds());   // 没有 height
        s.setCloseAction("nonsense");
        QCOMPARE(s.closeAction(), QStringLiteral("quit"));
        s.setWindowTitle(QString(80, QChar('a')));
        QCOMPARE(s.windowTitle().size(), 60);
    }

    void fontSteps()
    {
        QCOMPARE(Settings::fontScaleFor("s"), 0.9);
        QCOMPARE(Settings::fontScaleFor("m"), 1.0);
        QCOMPARE(Settings::fontScaleFor("l"), 1.12);
        QCOMPARE(Settings::fontScaleFor("xl"), 1.25);
        QCOMPARE(Settings::stepFontSize("m", 1), QStringLiteral("l"));
        QCOMPARE(Settings::stepFontSize("xl", 1), QStringLiteral("xl"));
        QCOMPARE(Settings::stepFontSize("m", -1), QStringLiteral("s"));
        QCOMPARE(Settings::stepFontSize("s", -1), QStringLiteral("s"));
    }

    // ---------- 数据文件夹 ----------
    void startupDecision()
    {
        QTemporaryDir tmp;
        const QString saved = makeDataDir(tmp.filePath("saved"));
        const QString def = tmp.filePath("app/data");
        const QString electron = makeElectronDir(tmp.filePath("electron"));
        const QString gone = tmp.filePath("gone");
        using S = DataFolder::Startup;
        QCOMPARE(DataFolder::decide({ saved, def, electron }), S::UseSaved);
        QCOMPARE(DataFolder::decide({ gone, def, electron }), S::AskMissing);
        QCOMPARE(DataFolder::decide({ {}, def, electron }), S::AskFirstRun);
        QCOMPARE(DataFolder::decide({ {}, def, {} }), S::CreateDefault);
        // 0.5.x 记住的是 Electron 版的数据文件夹（没有 travel.json）：问要不要导入
        QCOMPARE(DataFolder::decide({ electron, def, {} }), S::AskFirstRun);
        makeDataDir(def);
        QCOMPARE(DataFolder::decide({ {}, def, electron }), S::UseDefault);
        // 同一个文件夹里两种数据都有：原生版的优先
        makeDataDir(electron);
        QCOMPARE(DataFolder::decide({ electron, def, {} }), S::UseSaved);
        QVERIFY(DataFolder::isElectron(electron));
        QVERIFY(!DataFolder::isElectron(saved));
    }

    void readElectronSettings()
    {
        QTemporaryDir tmp;
        const QString data = makeElectronDir(tmp.filePath("旅行计划数据"));
        const QString file = tmp.filePath("TravelPlanner/settings.json");
        QCOMPARE(DataFolder::readElectronDataDir(file), QString());   // 没有文件
        json::Object o;
        o.set("dataDir", json::Value(QDir::toNativeSeparators(data)));
        o.set("closeAction", json::Value("tray"));
        writeText(file, json::stringify(json::Value(std::move(o))).toUtf8());
        QVERIFY(DataFolder::samePath(DataFolder::readElectronDataDir(file), data));
        writeText(file, "{\"dataDir\": \"" + tmp.filePath("nowhere").toUtf8() + "\"}");
        QCOMPARE(DataFolder::readElectronDataDir(file), QString());   // 那个文件夹里没有 data.js
        writeText(file, "not json");
        QCOMPARE(DataFolder::readElectronDataDir(file), QString());
    }

    // 欢迎页新建的数据（0.12.7，D-19）：季节表 + 一个示例计划；往返不变；删掉示例计划和普通计划一样
    void createSampleData_data()
    {
        QTest::addColumn<QString>("lang");
        QTest::addColumn<QString>("nameHas");
        QTest::newRow("zh") << "zh" << QString::fromUtf8("示例");
        QTest::newRow("en") << "en" << "Sample";
    }
    void createSampleData()
    {
        QFETCH(QString, lang);
        QFETCH(QString, nameHas);
        QTemporaryDir tmp;
        const QString dir = tmp.filePath("d");
        QString err;
        QVERIFY2(DataFolder::createEmpty(dir, "0.12.7", &err, lang), qPrintable(err));
        QVERIFY(QFileInfo(dir + "/photos").isDir());
        data::DataStore store;
        QVERIFY(store.load(dir));
        QVERIFY(!store.isReadOnly());
        const json::Value root = store.data();
        const auto &plans = root["plans"].asArray();
        QCOMPARE(int(plans.size()), 1);
        const json::Value plan = plans[0];
        QVERIFY2(plan["name"].toString().contains(nameHas), qPrintable(plan["name"].toString()));
        QCOMPARE(plan["status"].toString(), QString("planning"));
        const auto &stops = plan["stops"].asArray();
        QVERIFY(stops.size() >= 4);
        for (const json::Value &s : stops) QVERIFY(!s.asObject().contains("order"));   // 原生格式：数组顺序就是站点顺序
        QVERIFY(stops[0]["leg"].isObject());   // legMode 合成 leg
        QVERIFY(root["seasonGuide"]["rows"].asArray().size() > 10);
        QVERIFY(root["photos"].asArray().empty());
        // 往返不变：读进来、原样保存，内容一样
        QVERIFY(store.save().ok);
        data::DataStore again;
        QVERIFY(again.load(dir));
        QCOMPARE(json::stringify(again.data()["plans"], 2), json::stringify(root["plans"], 2));
        QCOMPARE(json::stringify(again.data()["seasonGuide"], 2), json::stringify(root["seasonGuide"], 2));
        // 已经有数据的文件夹：不写示例
        const QByteArray before = readText(dir + "/travel.json");
        QVERIFY(DataFolder::createEmpty(dir, "0.12.7", &err, lang));
        QCOMPARE(readText(dir + "/travel.json"), before);
    }

    void createEmptyData()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath("新数据/data");
        QString err;
        QVERIFY2(DataFolder::createEmpty(dir, "0.6.0", &err), qPrintable(err));
        QVERIFY(DataFolder::isValid(dir));
        QVERIFY(QFileInfo(dir + "/photos").isDir());
        const QByteArray bytes = readText(dir + "/travel.json");
        QVERIFY(bytes.startsWith("{\n  \"format\": \"travelplanner\",\n  \"formatVersion\": 1,\n  \"savedBy\": \"0.6.0\",\n"));
        QVERIFY(bytes.endsWith("}\n"));
        QVERIFY(!bytes.contains('\r'));
        QVERIFY(!bytes.startsWith("\xEF\xBB\xBF"));
        QVERIFY(bytes.contains("\"plans\": [],\n  \"photos\": [],\n  \"journal\": [],\n  \"documents\": [],\n  \"visitedRegions\": [],\n  \"seasonGuide\": {"));
        // 数据仓库能正常打开、能保存
        data::DataStore store;
        QVERIFY(store.load(dir));
        QCOMPARE(store.state(), data::DataStore::State::Loaded);
        QVERIFY(!store.isReadOnly());
        QVERIFY(store.save().ok);
        // 已经有 travel.json 时不动它
        writeText(dir + "/travel.json", "{\"format\":\"travelplanner\",\"plans\":[{\"id\":\"p_1\"}]}\n");
        QVERIFY(DataFolder::createEmpty(dir, "0.6.0", &err));
        QVERIFY(readText(dir + "/travel.json").contains("p_1"));
        QVERIFY(!QFile::exists(dir + "/travel.json.tmp"));
        // Electron 版的数据文件夹：不在那里写
        const QString el = makeElectronDir(tmp.filePath("electron"));
        QVERIFY(!DataFolder::createEmpty(el, "0.6.0", &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!QFile::exists(el + "/travel.json"));
        QVERIFY(!QFileInfo(el + "/photos").exists());
    }

    void samePathRules()
    {
        QVERIFY(DataFolder::samePath("/a/b/../c", "/a/c"));
        QVERIFY(DataFolder::samePath("/a/c/", "/a/c"));
        QVERIFY(!DataFolder::samePath("", "/a"));
        QVERIFY(!DataFolder::samePath("/a/c", "/a/d"));
    }

    void rescueCopyAndDiscard()
    {
        QTemporaryDir tmp;
        const QString dir = makeDataDir(tmp.filePath("d"));
        data::DataStore store;
        QVERIFY(store.load(dir));
        store.modify([](json::Value &v) { v.mutObject().set("x", json::Value(1)); });
        QVERIFY(store.isDirty());
        const QString name = store.saveRescueCopy();
        QVERIFY(name.startsWith("travel.unsaved-"));
        QVERIFY(readText(dir + "/" + name).contains("\"x\": 1"));
        QVERIFY(!readText(dir + "/travel.json").contains("\"x\""));   // travel.json 不动
        store.discardUnsaved();
        QVERIFY(!store.isDirty());
        QCOMPARE(store.flushBeforeExit(), QString());   // 退出时不再写
        QVERIFY(!readText(dir + "/travel.json").contains("\"x\""));
    }

    // ---------- 窗口位置 ----------
    void fitWindow()
    {
        const QSize minSize(360, 500);
        const QRect primary(0, 0, 1920, 1040);
        const QRect second(1920, 0, 2560, 1400);
        // 在屏幕里：不动
        QCOMPARE(fitToScreens(QRect(100, 100, 1400, 900), { primary }, primary, minSize), QRect(100, 100, 1400, 900));
        // 在第二个屏幕里，第二个屏幕还在：不动
        QCOMPARE(fitToScreens(QRect(2200, 100, 1400, 900), { primary, second }, primary, minSize), QRect(2200, 100, 1400, 900));
        // 第二个屏幕拔了：回到主屏幕居中
        const QRect r = fitToScreens(QRect(2200, 100, 1400, 900), { primary }, primary, minSize);
        QCOMPARE(r.size(), QSize(1400, 900));
        QCOMPARE(r.center(), primary.center());
        // 比屏幕大：缩小
        const QRect big = fitToScreens(QRect(0, 0, 3000, 2000), { primary }, primary, minSize);
        QVERIFY(primary.contains(big));
        // 比最小尺寸小：放大
        QCOMPARE(fitToScreens(QRect(10, 10, 100, 100), { primary }, primary, minSize).size(), minSize);
        // 标题栏伸到屏幕上面去了：拉回来
        const QRect up = fitToScreens(QRect(100, -20, 800, 600), { primary }, primary, minSize);
        QVERIFY(up.top() >= 0);
        // 只露出一小角（抓不住）：也算不在屏幕上
        QCOMPARE(fitToScreens(QRect(1880, 1020, 800, 600), { primary }, primary, minSize).center(), primary.center());
    }

    // ---------- 翻译 ----------
    void dictionaryParsing()
    {
        const auto m = DictTranslator::parse("\xEF\xBB\xBF" "甲\tA\r\n乙\t\n丙\tC\\nD\n# 注释\tX\n没有tab\n", true);
        QCOMPARE(m.value("甲"), QStringLiteral("A"));
        QVERIFY(!m.contains("乙"));                      // 英文为空：忽略
        QCOMPARE(m.value("丙"), QStringLiteral("C\nD"));  // native.txt 的 \n
        QVERIFY(!m.contains("# 注释"));
        const auto e = DictTranslator::parse("丙\tC\\nD\n", false);
        QCOMPARE(e.value("丙"), QStringLiteral("C\\nD"));  // Electron 词典原样
    }

    void dictionaryOrder()
    {
        QTemporaryDir tmp;
        // 文件名字典序：b10 在 b2 前面，所以 b2 覆盖 b10；native.txt 最后
        writeText(tmp.filePath("electron/b2.txt"), "键\t来自b2\n只有b2\tB2\n");
        writeText(tmp.filePath("electron/b10.txt"), "键\t来自b10\n");
        writeText(tmp.filePath("native.txt"), "只有b2\tNative\n");
        DictTranslator t;
        t.loadDir(tmp.path());
        QCOMPARE(t.lookup("键"), QStringLiteral("来自b2"));
        QCOMPARE(t.lookup("只有b2"), QStringLiteral("Native"));
        QCOMPARE(t.translate("ctx", "键"), QStringLiteral("来自b2"));
        QVERIFY(t.translate("ctx", "没有的").isNull());   // 没有 = 回退到中文原文
    }

    void dictionaryInstalled()
    {
        DictTranslator t;
        QVERIFY(t.loadDir(QStringLiteral(TP_SOURCE_DIR "/i18n/tr")) > 1500);
        QCoreApplication::installTranslator(&t);
        QCOMPARE(tp::tr("取消"), QStringLiteral("Cancel"));
        QCOMPARE(tp::tr("打开"), QStringLiteral("Open"));   // native.txt 覆盖了 Electron 的 "open"
        QCOMPARE(tp::tr("这句没有翻译"), QStringLiteral("这句没有翻译"));
        QCoreApplication::removeTranslator(&t);
        QCOMPARE(tp::tr("取消"), QStringLiteral("取消"));
    }

    // 检查脚本：qml/ 和 src/ 里所有 qsTr("…") / tp::tr("…") 都要有英文（PO 项目说明第八条）
    void allStringsTranslated()
    {
        DictTranslator t;
        t.loadDir(QStringLiteral(TP_SOURCE_DIR "/i18n/tr"));
        static const QRegularExpression re(QStringLiteral("(?:qsTr|tp::tr|trd)\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
        QStringList missing;
        int total = 0;
        for (const QString &sub : { QStringLiteral("qml"), QStringLiteral("src") }) {
            QDirIterator it(QStringLiteral(TP_SOURCE_DIR "/") + sub, { "*.qml", "*.cpp" }, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                const QString text = QString::fromUtf8(readText(file));
                for (auto m = re.globalMatch(text); m.hasNext();) {
                    QString s = m.next().captured(1);
                    s.replace("\\\"", "\"").replace("\\n", "\n").replace("\\\\", "\\");
                    ++total;
                    if (!t.contains(s)) missing << QDir(QStringLiteral(TP_SOURCE_DIR)).relativeFilePath(file) + "：" + s;
                }
            }
        }
        qInfo("界面文字 %d 处", total);
        if (!missing.isEmpty()) qWarning("缺英文：\n%s", qPrintable(missing.join('\n')));
        QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("有 %1 处界面文字缺英文（见上面的 QWARN）").arg(missing.size())));
        QVERIFY(total > 50);
    }

    // ---------- 备份设置（0.5.0） ----------
    void settingsBackup()
    {
        QTemporaryDir tmp;
        Settings s;
        QVERIFY(s.load(tmp.filePath("settings.json")));
        QCOMPARE(s.backupDir(), QString());
        QCOMPARE(s.autoBackup(), false);
        QCOMPARE(s.backupEveryDays(), 7);       // 默认 7 天
        QCOMPARE(s.lastBackup(), 0.0);
        // Electron：Math.min(90, Math.max(1, Math.round(+v || 7)))
        QCOMPARE(Settings::clampBackupDays(0), 7);
        QCOMPARE(Settings::clampBackupDays(-3), 1);
        QCOMPARE(Settings::clampBackupDays(100), 90);
        QCOMPARE(Settings::clampBackupDays(2.5), 3);
        QCOMPARE(Settings::clampBackupDays(std::nan("")), 7);
        s.setValue("backupEveryDays", tp::json::Value("5"));   // 字符串也按数字读（+v）
        QCOMPARE(s.backupEveryDays(), 5);
        s.setBackupDir("E:/备份");
        s.setAutoBackup(true);
        s.setBackupEveryDays(120);
        s.setLastBackup(1793600000000.0);
        QVERIFY(s.save());
        Settings t;
        QVERIFY(t.load(tmp.filePath("settings.json")));
        QCOMPARE(t.backupDir(), QDir::toNativeSeparators("E:/备份"));
        QVERIFY(t.autoBackup());
        QCOMPARE(t.backupEveryDays(), 90);
        QCOMPARE(t.lastBackup(), 1793600000000.0);
        // 写出来的数字是整数形式（和 Electron 的 Date.now() 一样）
        QVERIFY(readText(tmp.filePath("settings.json")).contains("\"lastBackup\": 1793600000000"));
        // 命令行 --lang 只管这次运行，不写进文件
        t.setLangOverride("en");
        QCOMPARE(t.lang(), QStringLiteral("en"));
        t.setLastBackup(1.0);
        QVERIFY(t.save());
        QVERIFY(!readText(tmp.filePath("settings.json")).contains("\"lang\""));
        t.setLang("zh");   // 在设置里改了语言：以设置为准
        QCOMPARE(t.lang(), QStringLiteral("zh"));
    }

    // ---------- 日志 ----------
    void logTrim()
    {
        QTemporaryDir tmp;
        const QString p = tmp.filePath("app.log");
        QByteArray big;
        for (int i = 0; i < 10000; ++i) big += "第 " + QByteArray::number(i) + " 行 line\n";
        writeText(p, big);
        QVERIFY(!Logger::trimFile(p, 10 * 1024 * 1024));   // 没超：不动
        QVERIFY(Logger::trimFile(p, 1024));
        const QByteArray after = readText(p);
        QVERIFY(after.size() < big.size() / 2 + 64);
        QVERIFY(after.startsWith("第 "));                    // 从一行的开头开始
        QVERIFY(after.endsWith("第 9999 行 line\n"));       // 留的是后一半
    }

    // ---------- 出错报告（0.5.1） ----------
    void errorHashMatchesElectron()
    {
        // 基准：Node 跑 Electron errors.js 的 hash()（2026-11-03）
        QCOMPARE(ErrorReporter::hash(""), QStringLiteral("NTFP"));
        QCOMPARE(ErrorReporter::hash("a"), QStringLiteral("WI7G"));
        QCOMPARE(ErrorReporter::hash("TypeError: x is undefined|map-app.js:120"), QStringLiteral("0ZBR"));
        QCOMPARE(ErrorReporter::hash(QStringLiteral("旅行计划")), QStringLiteral("3DMF"));
        QCOMPARE(ErrorReporter::hash(QStringLiteral("😀emoji")), QStringLiteral("CEFA"));   // 代理对按两个码元算，和 charCodeAt 一样
        QCOMPARE(ErrorReporter::hash("hello world|"), QStringLiteral("15FL"));
        // codeOf：同 errors.js（第一行 + 第一个带 .js:行 的位置，去掉列号）
        const QString stack = QStringLiteral("TypeError: x\n    at boom (file:///D:/旅行计划App/app/js/map-app.js:120:7)\n"
                                             "    at HTMLButtonElement.<anonymous> (D:\\a\\b\\pages-plan.js:9:1)");
        QCOMPARE(ErrorReporter::codeOf("R", "TypeError: Cannot read properties of null (reading 'boom')\nmore", stack), QStringLiteral("TP-R-5RFC"));
    }

    void errorCleanStack()
    {
        QCOMPARE(ErrorReporter::cleanStack(QStringLiteral("at boom (file:///D:/旅行计划App/app/js/map-app.js:120:7)\n    at x (D:\\Users\\Jason\\app\\pages-plan.js:9:1)")),
                 QStringLiteral("at boom (map-app.js:120:7)\n    at x (pages-plan.js:9:1)"));
        QCOMPARE(ErrorReporter::cleanStack("at f (qrc:/qt/qml/TPApp/qml/components/ErrorCard.qml:12:3)"), QStringLiteral("at f (ErrorCard.qml:12:3)"));
        QCOMPARE(ErrorReporter::cleanStack("at g (/home/jason/src/app/main.cpp:5)"), QStringLiteral("at g (main.cpp:5)"));
        QString many;
        for (int i = 0; i < 15; ++i) many += QStringLiteral("at f%1 (a.js:1:1)\n").arg(i);
        QCOMPARE(ErrorReporter::cleanStack(many).split('\n').size(), 10);   // 最多 10 行
    }

    void errorIgnoreAndJsException()
    {
        QVERIFY(ErrorReporter::ignored("Failed to fetch"));
        QVERIFY(ErrorReporter::ignored("net::ERR_INTERNET_DISCONNECTED"));
        QVERIFY(ErrorReporter::ignored("connect ECONNREFUSED 127.0.0.1"));
        QVERIFY(ErrorReporter::ignored("Script error."));
        QVERIFY(ErrorReporter::ignored("   "));
        QVERIFY(!ErrorReporter::ignored("TypeError: Cannot read property 'x' of null"));
        QVERIFY(ErrorReporter::isJsException("TypeError: Cannot read property 'boom' of null"));
        QVERIFY(ErrorReporter::isJsException("ReferenceError: foo is not defined"));
        QVERIFY(ErrorReporter::isJsException("Error: 故意的"));
        QVERIFY(ErrorReporter::isJsException("Uncaught RangeError: Invalid array length"));
        QVERIFY(!ErrorReporter::isJsException("Unable to assign [undefined] to QString"));
        QVERIFY(!ErrorReporter::isJsException("QML Text: Binding loop detected for property \"width\""));
        QVERIFY(!ErrorReporter::isJsException("Cannot anchor to an item that isn't a parent or sibling."));
    }

    void errorReporterDedupAndLimit()
    {
        QTemporaryDir tmp;
        ErrorReporter r;
        r.setVersion("0.5.1");
        r.setSystem("测试系统");
        r.setLogFile(tmp.filePath("errors.log"));
        QSignalSpy spy(&r, &ErrorReporter::popup);
        const QString c1 = r.reportQml("TypeError: Cannot read property 'boom' of null", "qrc:/qt/qml/TPApp/qml/Main.qml", 30, 9);
        QVERIFY(c1.startsWith("TP-Q-"));
        QCOMPARE(c1, ErrorReporter::codeOf("Q", "TypeError: Cannot read property 'boom' of null", "at Main.qml:30:9"));
        QCOMPARE(r.reportQml("TypeError: Cannot read property 'boom' of null", "qrc:/qt/qml/TPApp/qml/Main.qml", 30, 9), c1);   // 同码只弹一次
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(r.reportQml("Unable to assign [undefined] to QString", "qrc:/x/Main.qml", 3, 1).isEmpty());   // 绑定警告不弹
        QVERIFY(r.report("M", "Failed to fetch").isEmpty());                                                 // 断网不算
        r.report("M", "错误二", QString(), "std::runtime_error in QObject::Timer");
        r.report("M", "错误三", QString(), "x");
        r.report("M", "错误四", QString(), "y");   // 第 4 个：只写日志，不弹
        QTRY_COMPARE(spy.count(), ErrorReporter::kMaxPopups);   // 小框排队发出
        // 报告内容：版本、系统、页面、错误、去掉路径的位置；errors.log 里每个错误一段（含没弹的第 4 个）
        const QString report = spy.at(0).at(2).toString();
        QVERIFY(report.contains("错误码 Code: " + c1));
        QVERIFY(report.contains("版本 Version: v0.5.1"));
        QVERIFY(report.contains("系统 System: 测试系统"));
        QVERIFY(report.contains("页面 Page: map"));
        QVERIFY(report.contains("at Main.qml:30:9"));
        const QString log = QString::fromUtf8(readText(tmp.filePath("errors.log")));
        QCOMPARE(log.count("===== "), 5);
        QVERIFY(log.contains("错误四"));
    }

    // ---------- 上次崩溃的记号、转储只留 3 个（0.5.1） ----------
    void crashMarker()
    {
        QTemporaryDir tmp;
        const QByteArray marker = "code: TP-CRASH-K2W7\r\ndump: crash-20261103-021530.dmp\r\n\r\n旅行计划 错误报告 / Travel Planner error report\r\n错误码 Code: TP-CRASH-K2W7\r\n";
        writeText(tmp.filePath(CrashInfo::kMarker), marker);
        writeText(tmp.filePath("crash-20261103-021530.dmp"), "MDMP");
        CrashInfo::Last l = CrashInfo::takeLast(tmp.path());
        QVERIFY(l.found);
        QCOMPARE(l.code, QStringLiteral("TP-CRASH-K2W7"));
        QCOMPARE(l.dump, QStringLiteral("crash-20261103-021530.dmp"));
        QVERIFY(l.report.startsWith(QStringLiteral("旅行计划 错误报告")));
        QVERIFY(!QFile::exists(tmp.filePath(CrashInfo::kMarker)));   // 读完就删，只提示一次
        QVERIFY(!CrashInfo::takeLast(tmp.path()).found);
        // 转储文件不在了：dump 为空
        writeText(tmp.filePath(CrashInfo::kMarker), "code: TP-CRASH-AAAA\ndump: crash-gone.dmp\n\nr");
        QVERIFY(CrashInfo::takeLast(tmp.path()).dump.isEmpty());
        // 坏的记号不算
        QVERIFY(!CrashInfo::parse("乱码").found);
        // 只留最近 3 个转储
        for (const char *n : { "crash-20261101-010000.dmp", "crash-20261102-010000.dmp", "crash-20261103-010000.dmp", "crash-20261103-020000.dmp" })
            writeText(tmp.filePath(n), "MDMP");
        QCOMPARE(CrashInfo::pruneDumps(tmp.path()), 2);   // 5 个留 3 个
        const QStringList left = QDir(tmp.path()).entryList({ "crash-*.dmp" }, QDir::Files, QDir::Name);
        QCOMPARE(left, QStringList({ "crash-20261103-010000.dmp", "crash-20261103-020000.dmp", "crash-20261103-021530.dmp" }));
    }

    // ---------- 卡住检测的计数（0.5.1） ----------
    void hangDetector()
    {
        HangDetector d(10, 60);
        std::uint64_t beat = 0;
        QCOMPARE(d.step(beat), HangDetector::Ok);
        for (int i = 0; i < 9; ++i) QCOMPARE(d.step(beat), HangDetector::Ok);   // 9 秒没心跳：还不算
        QCOMPARE(d.step(beat), HangDetector::Hung);                              // 第 10 秒：卡住
        QCOMPARE(d.step(beat), HangDetector::Ok);                                // 只报一次
        QVERIFY(d.hung());
        QCOMPARE(d.step(++beat), HangDetector::Recovered);
        // 恢复后 60 秒内再卡住：只记日志
        for (int i = 0; i < 9; ++i) d.step(beat);
        QCOMPARE(d.step(beat), HangDetector::HungQuiet);
        QCOMPARE(d.step(++beat), HangDetector::Recovered);
        for (int i = 0; i < 70; ++i) d.step(++beat);                            // 冷却过去
        for (int i = 0; i < 9; ++i) d.step(beat);
        QCOMPARE(d.step(beat), HangDetector::Hung);
        // 调试器挂着 / 窗口还没显示：不算
        HangDetector e(3, 0);
        for (int i = 0; i < 10; ++i) QCOMPARE(e.step(7, true), HangDetector::Ok);
        QCOMPARE(e.step(7), HangDetector::Ok);
        QCOMPARE(e.step(7), HangDetector::Ok);
        QCOMPARE(e.step(7), HangDetector::Hung);
    }

    // ---------- notify 外层 SEH 接哪些异常码（0.5.2） ----------
    void crashCodes()
    {
        using tp::crashcodes::isFatal;
        // 会让程序停下的：访问冲突、栈溢出、除以零、非法指令、读文件页失败、回调里没处理的异常、快速结束
        for (std::uint32_t c : { 0xC0000005u, 0xC00000FDu, 0xC0000094u, 0xC000001Du, 0xC0000006u, 0xC000041Du, 0xC0000409u })
            QVERIFY2(isFatal(c), qPrintable(QString::number(c, 16)));
        // 我们自己抛的（terminate、纯虚函数、CRT 参数、abort、qFatal）
        for (std::uint32_t c = tp::crashcodes::kSoftBase + 1; c <= tp::crashcodes::kSoftBase + 5; ++c) QVERIFY(isFatal(c));
        // 警告类里只有对齐、断点、单步
        QVERIFY(isFatal(0x80000002u));
        QVERIFY(isFatal(0x80000003u));
        QVERIFY(isFatal(0x80000004u));
        QVERIFY(!isFatal(0x80000005u));   // 保护页（栈自动增长用，系统自己处理）
        QVERIFY(!isFatal(0x80000001u));
        // 不接：C++ 异常（留给里层 catch）、线程命名、OutputDebugString、成功码
        QVERIFY(!isFatal(tp::crashcodes::kCppException));
        QVERIFY(!isFatal(0x406D1388u));
        QVERIFY(!isFatal(0x40010006u));
        QVERIFY(!isFatal(0x4001000Au));
        QVERIFY(!isFatal(0x00000000u));
        QVERIFY(!isFatal(0x000006BAu));   // RPC 错误码（严重级别 0）
    }

    // ---- 0.8.8：前进 / 后退（Electron shell.js go / back / fwd） ----
    void navHistory()
    {
        tp::app::NavHistory n;
        QSignalSpy spy(&n, &tp::app::NavHistory::changed);
        QCOMPARE(n.page(), QStringLiteral("map"));
        QVERIFY(!n.canBack());
        n.go(QStringLiteral("map"));                       // 和当前一样：不加、不发信号
        QCOMPARE(n.size(), 1);
        QCOMPARE(spy.count(), 0);
        n.go(QStringLiteral("settings"));
        n.go(QStringLiteral("map"));
        QCOMPARE(n.size(), 3);
        QVERIFY(n.back());
        QCOMPARE(n.page(), QStringLiteral("settings"));
        QVERIFY(n.canForward());
        n.go(QStringLiteral("ledger"));                 // 后退以后再走：丢掉后面的记录
        QCOMPARE(n.size(), 3);
        QVERIFY(!n.canForward());
        QVERIFY(!n.forward());
        n.go(QStringLiteral("map"), {}, true);             // replace：换掉当前这一步
        QCOMPARE(n.size(), 3);
        QCOMPARE(n.page(), QStringLiteral("map"));
        n.go(QStringLiteral("plan"), { { QStringLiteral("id"), QStringLiteral("a") } });
        n.go(QStringLiteral("plan"), { { QStringLiteral("id"), QStringLiteral("b") } });   // 参数不同算不同的一步
        QCOMPARE(n.size(), 5);
        QCOMPARE(n.params().value(QStringLiteral("id")).toString(), QStringLiteral("b"));
        // 最多 100 步，超过丢掉最早的
        for (int i = 0; i < 150; ++i) n.go(QStringLiteral("p%1").arg(i));
        QCOMPARE(n.size(), tp::app::NavHistory::kMax);
        QCOMPARE(n.index(), tp::app::NavHistory::kMax - 1);
        QCOMPARE(n.page(), QStringLiteral("p149"));
        int backs = 0;
        while (n.back()) ++backs;
        QCOMPARE(backs, tp::app::NavHistory::kMax - 1);
        QCOMPARE(n.page(), QStringLiteral("p50"));
    }

    // ---- 0.8.8：Ctrl K 的排序（Electron palette.js filter：开头 0、包含 1、副标题 2；同分 计划 → 站点 → 页面 → 操作；最多 40 条） ----
    void commandScoring()
    {
        using tp::app::commands::Item;
        const QVector<Item> items = {
            { QStringLiteral("page"), QStringLiteral("设置"), QStringLiteral("页面"), {} },          // 0
            { QStringLiteral("act"), QStringLiteral("底图：卫星图"), QStringLiteral("底图"), {} },    // 1
            { QStringLiteral("plan"), QStringLiteral("东京之旅"), QStringLiteral("计划中"), QStringLiteral("p1") },   // 2
            { QStringLiteral("stop"), QStringLiteral("东京"), QStringLiteral("第 1 站 · 关东"), QStringLiteral("p2") }, // 3
            { QStringLiteral("stop"), QStringLiteral("京都"), QStringLiteral("第 2 站 · 关西"), QStringLiteral("p3") }, // 4
            { QStringLiteral("plan"), QStringLiteral("关西"), QStringLiteral("计划中 · 11.20 — 11.27"), QStringLiteral("p3") }, // 5
            { QStringLiteral("plan"), QStringLiteral("Tokyo Trip"), QStringLiteral("Planning"), QStringLiteral("p4") }, // 6
        };
        const QString q = QStringLiteral("东京");
        QCOMPARE(tp::app::commands::score(items[2], q), 0);
        QCOMPARE(tp::app::commands::score(items[3], q), 0);
        QCOMPARE(tp::app::commands::score(items[4], q), 9);
        // 同为开头匹配：计划在站点前面
        QCOMPARE(tp::app::commands::electronOrder(items, QStringLiteral(" 东京 ")), (QVector<int>{ 2, 3 }));
        // "京"：东京之旅(1 计划) 东京(1 站点) 京都(0 站点) → 京都 先（分数低），再 东京之旅、东京
        QCOMPARE(tp::app::commands::electronOrder(items, QStringLiteral("京")), (QVector<int>{ 4, 2, 3 }));
        // 副标题匹配排在后面："关西" → 关西(0 计划) 京都(2 站点，副标题"关西")
        QCOMPARE(tp::app::commands::electronOrder(items, QStringLiteral("关西")), (QVector<int>{ 5, 4 }));
        // 不分大小写
        QCOMPARE(tp::app::commands::electronOrder(items, QStringLiteral("TOKYO")), (QVector<int>{ 6 }));
        QCOMPARE(tp::app::commands::electronOrder(items, QStringLiteral("没有")), QVector<int>{});
        // 最多 40 条
        QVector<Item> many;
        for (int i = 0; i < 60; ++i) many.push_back({ QStringLiteral("stop"), QStringLiteral("站%1").arg(i), {}, {} });
        QCOMPARE(tp::app::commands::electronOrder(many, QStringLiteral("站")).size(), 40);
        QCOMPARE(tp::app::commands::electronOrder(many, QStringLiteral("站")).first(), 0);   // 同分保持原顺序
        // 0.8.10 别名：搜旧页面名找到对应的模式（算 1 分，和名字包含一样）
        const QVector<Item> modes = {
            { QStringLiteral("page"), QStringLiteral("计划"), QStringLiteral("地图模式"), {}, QStringLiteral("计划库 Library") },   // 0
            { QStringLiteral("page"), QStringLiteral("想去"), QStringLiteral("地图模式"), {}, QStringLiteral("愿望清单") },          // 1
            { QStringLiteral("plan"), QStringLiteral("计划库之旅"), QStringLiteral("计划中"), QStringLiteral("p9") },               // 2
        };
        QCOMPARE(tp::app::commands::score(modes[0], QStringLiteral("计划库")), 1);
        QCOMPARE(tp::app::commands::score(modes[1], QStringLiteral("愿望")), 1);
        QCOMPARE(tp::app::commands::score(modes[0], QStringLiteral("library")), 1);
        QCOMPARE(tp::app::commands::score(modes[1], QStringLiteral("计划库")), 9);
        // 名字开头匹配（计划库之旅，0 分）排在别名匹配（1 分）前面
        QCOMPARE(tp::app::commands::electronOrder(modes, QStringLiteral("计划库")), (QVector<int>{ 2, 0 }));
    }

    // 原生版的分组（PO 0.8.8 方案 B）
    void commandGrouping()
    {
        using tp::app::commands::Item;
        QVector<Item> items = {
            { QStringLiteral("page"), QStringLiteral("地图"), QStringLiteral("页面"), {} },
            { QStringLiteral("page"), QStringLiteral("设置"), QStringLiteral("页面"), {} },
            { QStringLiteral("act"), QStringLiteral("撤销"), QStringLiteral("操作"), {} },
        };
        for (int i = 0; i < 9; ++i) items.push_back({ QStringLiteral("plan"), QStringLiteral("计划%1").arg(i), QStringLiteral("计划中"), QStringLiteral("p%1").arg(i) });
        items.push_back({ QStringLiteral("stop"), QStringLiteral("设置点"), QStringLiteral("第 1 站 · 计划0"), QStringLiteral("p0") });
        // 没输入、没有最近：前 6 个计划（Electron），然后页面、操作
        auto g = tp::app::commands::grouped(items, {}, {});
        QCOMPARE(g.size(), 6 + 2 + 1);
        QCOMPARE(g[0].group, QStringLiteral("plan"));
        QCOMPARE(items[g[0].index].label, QStringLiteral("计划0"));
        QCOMPARE(g[6].group, QStringLiteral("page"));
        QCOMPARE(g[8].group, QStringLiteral("act"));
        // 有最近：按最近的先后，认不出来的、重复的跳过，最多 8 个；不再列"前 6 个计划"
        QStringList recent = { QStringLiteral("p5"), QStringLiteral("gone"), QStringLiteral("p2"), QStringLiteral("p5") };
        for (int i = 0; i < 9; ++i) recent << QStringLiteral("p%1").arg(i);
        g = tp::app::commands::grouped(items, QStringLiteral("  "), recent);
        QCOMPARE(g[0].group, QStringLiteral("recent"));
        QCOMPARE(items[g[0].index].id, QStringLiteral("p5"));
        QCOMPARE(items[g[1].index].id, QStringLiteral("p2"));
        int nRecent = 0, nPlan = 0;
        for (const auto &h : g) { nRecent += h.group == QLatin1String("recent"); nPlan += h.group == QLatin1String("plan"); }
        QCOMPARE(nRecent, 8);
        QCOMPARE(nPlan, 0);
        // 有输入："设置" → 页面"设置"(0) 和 站点"设置点"(0)：同分时 站点组 在 页面组 前面
        g = tp::app::commands::grouped(items, QStringLiteral("设置"), recent);
        QCOMPARE(g.size(), 2);
        QCOMPARE(g[0].group, QStringLiteral("stop"));
        QCOMPARE(g[1].group, QStringLiteral("page"));
        // "计划"：计划组 (0) 在 站点组（副标题包含，2）前面；组内保持 Electron 的先后
        g = tp::app::commands::grouped(items, QStringLiteral("计划"), {});
        QCOMPARE(g.first().group, QStringLiteral("plan"));
        QCOMPARE(g.last().group, QStringLiteral("stop"));
        QCOMPARE(g.last().score, 2);
        // QML 用的接口：原样带回条目，多 group / score / index
        tp::app::CommandRanker rk;
        const QVariantList out = rk.rank({ QVariantMap{ { "kind", "page" }, { "label", "地图" }, { "sub", "页面" }, { "key", "map" } } }, QStringLiteral("地"), {});
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].toMap().value("key").toString(), QStringLiteral("map"));
        QCOMPARE(out[0].toMap().value("group").toString(), QStringLiteral("page"));
        QCOMPARE(out[0].toMap().value("score").toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(TestApp)
#include "tst_app.moc"
