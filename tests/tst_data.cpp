// 数据层单元测试（阶段 ①，0.3.0）
//   - JSON 读写：和 Node JSON.stringify 的输出逐字节比对（tests/golden/，由 tools/claude-kit/json-bench/gen.js 生成）
//   - data.js 包装格式
//   - 保存流程（在临时文件夹里做：备份、历史、冲突、回读、只读模式、AI 备份、恢复、外部修改检测）；0.6.0 起是原生版的 travel.json
//   - 0.6.0：锁文件、从 Electron 版导入（含真实数据"每个值都找得到"的检查）
//   - 0.5.0：自动保存节拍（SaveScheduler）、撤销 / 重做（UndoStack）、备份（Backup）
//   - 真实数据往返：读进再写回，除保存时间外逐字节一致（默认 testdata\data.js；环境变量 TP_ROUNDTRIP_DIRS 可加文件夹，只读）
#include "backup.h"
#include "datafile.h"
#include "datastore.h"
#include "electronimport.h"
#include "lockfile.h"
#include "nativeformat.h"
#include "jsonio.h"
#include "jsonvalue.h"
#include "savescheduler.h"
#include "undostack.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <functional>
#include <limits>

using namespace tp;
using json::Value;

namespace {

QByteArray readFile(const QString &p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

void writeFileRaw(const QString &p, const QByteArray &b)
{
    QFile f(p);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(p));
    f.write(b);
}

QString goldenPath(const QString &name) { return QStringLiteral(TP_SOURCE_DIR "/tests/golden/") + name; }

// 找出两段文本第一个不同的位置，失败信息里显示前后文
QByteArray firstDiff(const QByteArray &a, const QByteArray &b)
{
    const qsizetype n = qMin(a.size(), b.size());
    qsizetype k = 0;
    while (k < n && a[k] == b[k]) ++k;
    if (k == n && a.size() == b.size()) return "相同";
    const qsizetype from = qMax<qsizetype>(0, k - 60);
    return "第 " + QByteArray::number(k) + " 字节不同\n  实际：" + a.mid(from, 140) + "\n  期望：" + b.mid(from, 140);
}

QDateTime fixedTime(int y, int mo, int d, int h = 10, int mi = 0, int s = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s));
}

QByteArray sampleData()
{
    return QByteArrayLiteral(
        "/*\n * 注释\n */\nwindow.TRAVEL_DATA = {\n"
        "  \"version\": 4,\n"
        "  \"plans\": [\n"
        "    {\n      \"id\": \"p_a\",\n      \"name\": \"甲\",\n      \"futureField\": {\"z\": 1, \"a\": [1.5e-7, 1e21]}\n    },\n"
        "    {\n      \"id\": \"p_b\",\n      \"name\": \"乙\",\n      \"updatedAt\": 1700000000000\n    }\n"
        "  ],\n"
        "  \"unknownTop\": \"保留我\"\n"
        "};\n");
}

// 原生版的 travel.json 样例（带不认识的字段）
QByteArray sampleNative()
{
    return data::nativefmt::serialize(json::parse(uR"({"format":"travelplanner","formatVersion":1,"savedBy":"0.6.0","savedAt":"2026-11-01T10:00:00+11:00",
        "plans":[{"id":"p_a","name":"甲","futureField":{"z":1,"a":[1.5e-7,1e21]}},{"id":"p_b","name":"乙","updatedAt":1700000000000}],
        "photos":[],"journal":[],"unknownTop":"保留我"})"));
}

// 比较两份 travel.json 时去掉 savedBy / savedAt 两行
QByteArray normSavedAt(QByteArray b)
{
    static const QRegularExpression re(QStringLiteral("\n  \"saved(By|At)\": \"[^\"]*\","));
    return QString::fromUtf8(b).replace(re, QString()).toUtf8();
}

} // namespace

class TestData : public QObject
{
    Q_OBJECT
private slots:
    // ---------- JSON ----------
    void golden_data();
    void golden();
    void numberFormat_data();
    void numberFormat();
    void parseErrors_data();
    void parseErrors();
    void objectKeyOrder();
    void copyOnWrite();
    // ---------- data.js 包装 ----------
    void dataFileParse_data();
    void dataFileParse();
    void dataFileHeader();
    void timestamp();
    // ---------- 真实数据往返 ----------
    void realRoundTrip();
    // ---------- 保存流程 ----------
    void storeMissing();
    void storeSaveUnchanged();
    void storeConflict();
    void storeUnreadable();
    void storeHistoryPrune();
    void storeBeforeAi();
    void storeStampUpdated();
    void storeFormatCheck();
    void storeRestoreHistory();
    void storeFlushBeforeExit();
    void storeWatcher();
    // ---------- 0.6.0：锁文件 ----------
    void lockBasic();
    void lockDeadProcess();
    void storeLocked();
    // ---------- 0.6.0：从 Electron 版导入 ----------
    void importSafeName();
    void importConvert();
    void importRun();
    void importCancelAndErrors();
    void importMergeAddNew();
    void importRealData();
    void importRealDataRun();
    // ---------- 0.5.0：自动保存节拍 ----------
    void schedulerDebounce();
    void schedulerNow();
    void schedulerFlushCancel();
    // ---------- 0.5.0：撤销 / 重做 ----------
    void undoBasic();
    void undoMaxSteps();
    void undoNewChangeClearsRedo();
    void undoSuppress();
    void undoGroup();          // 0.8.9 站点编辑器：整次编辑算一步、放弃时拿掉
    void undoSharesUnchanged();
    void undoKeepsUpdatedAt();
    void undoWithStore();
    // ---------- 0.5.0：备份 ----------
    void backupFolderName();
    void backupCopies();
    void backupUniqueName();
    void backupInsideDataDir();
    void backupCancel();
    void backupOnlyDataFile();
};

// ---------- JSON ----------

void TestData::golden_data()
{
    QTest::addColumn<QString>("name");
    for (const char *n : { "edge", "keys", "dupnested", "ws", "numbers", "random", "deep" })
        QTest::newRow(n) << QString::fromLatin1(n);
}

void TestData::golden()
{
    QFETCH(QString, name);
    const QByteArray in = readFile(goldenPath(name + ".in.json"));
    QVERIFY2(!in.isEmpty(), "缺少基准文件（先运行 tools/claude-kit/json-bench/gen.js）");
    json::ParseError err;
    QElapsedTimer t;
    t.start();
    const Value v = json::parse(QString::fromUtf8(in), &err);
    QVERIFY2(err.ok, qPrintable(err.message));
    const QByteArray out2 = json::stringify(v, 2).toUtf8();
    const QByteArray out0 = json::stringify(v, 0).toUtf8();
    const QByteArray exp2 = readFile(goldenPath(name + ".out2.json"));
    const QByteArray exp0 = readFile(goldenPath(name + ".out0.json"));
    QVERIFY2(out2 == exp2, firstDiff(out2, exp2).constData());
    QVERIFY2(out0 == exp0, firstDiff(out0, exp0).constData());
}

void TestData::numberFormat_data()
{
    QTest::addColumn<double>("d");
    QTest::addColumn<QString>("s");
    QTest::newRow("0") << 0.0 << "0";
    QTest::newRow("-0") << -0.0 << "0";
    QTest::newRow("1e21") << 1e21 << "1e+21";
    QTest::newRow("1e20") << 1e20 << "100000000000000000000";
    QTest::newRow("1e-7") << 1e-7 << "1e-7";
    QTest::newRow("1e-6") << 1e-6 << "0.000001";
    QTest::newRow("0.1+0.2") << (0.1 + 0.2) << "0.30000000000000004";
    QTest::newRow("-1.5e-7") << -1.5e-7 << "-1.5e-7";
    QTest::newRow("big") << 123456789012345678901.0 << "123456789012345680000";
    QTest::newRow("min") << 5e-324 << "5e-324";
    QTest::newRow("max") << 1.7976931348623157e308 << "1.7976931348623157e+308";
    QTest::newRow("inf") << std::numeric_limits<double>::infinity() << "null";
    QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN() << "null";
    QTest::newRow("ms") << 1793537178175.0 << "1793537178175";
    QTest::newRow("lng") << 74.5698 << "74.5698";
}

void TestData::numberFormat()
{
    QFETCH(double, d);
    QFETCH(QString, s);
    QCOMPARE(json::numberToString(d), s);
}

void TestData::parseErrors_data()
{
    QTest::addColumn<QString>("text");
    for (const char *t : { "", "[1,]", "{\"a\":1,}", "{'a':1}", "[NaN]", "[Infinity]", "[+1]", "[01]", "[1.]", "[.5]", "[1e]",
                           "\"abc", "\"a\nb\"", "[\"\\x\"]", "[\"\\u12\"]", "{\"a\" 1}", "{a:1}", "[1 2]", "/*c*/[1]", "tru", "[1]]"
                           "", "\"\\u00G0\"" })
        QTest::newRow(t) << QString::fromUtf8(t);
}

void TestData::parseErrors()
{
    QFETCH(QString, text);
    json::ParseError err;
    int consumed = 0;
    json::parse(text, &err, &consumed);
    // "[1]]" 能解析出 [1]，后面多余的由调用方（data.js 包装层）报错
    if (err.ok) QVERIFY2(consumed < text.size(), "应该报错或者没有读完");
    else QVERIFY(!err.message.isEmpty());
}

void TestData::objectKeyOrder()
{
    json::Object o;
    o.set("b", 1);
    o.set("2", 2);
    o.set("a", 3);
    o.set("1", 4);
    o.set("b", 5);   // 已有：原位替换
    o.set("01", 6);  // 不是规范下标：追加
    QCOMPARE(json::stringify(Value(o), 0), QStringLiteral("{\"1\":4,\"2\":2,\"b\":5,\"a\":3,\"01\":6}"));
    QVERIFY(o.remove("a"));
    QCOMPARE(json::stringify(Value(o), 0), QStringLiteral("{\"1\":4,\"2\":2,\"b\":5,\"01\":6}"));
}

void TestData::copyOnWrite()
{
    Value a = json::parse(u"{\"x\":[1,2,{\"y\":3}],\"z\":{}}");
    Value b = a;
    QVERIFY(a.sharesWith(b));
    b.mutObject().find("x")->mutArray().push_back(Value(4));
    QCOMPARE(json::stringify(a, 0), QStringLiteral("{\"x\":[1,2,{\"y\":3}],\"z\":{}}"));
    QCOMPARE(json::stringify(b, 0), QStringLiteral("{\"x\":[1,2,{\"y\":3},4],\"z\":{}}"));
    // 没改的分支仍然共享
    QVERIFY(a["z"].sharesWith(b["z"]));
}

// ---------- data.js 包装 ----------

void TestData::dataFileParse_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<bool>("ok");
    QTest::newRow("标准") << QByteArray("/*\n x\n */\nwindow.TRAVEL_DATA = {\"plans\":[]};\n") << true;
    QTest::newRow("无注释头") << QByteArray("window.TRAVEL_DATA = {\n  \"plans\": []\n};\n") << true;
    QTest::newRow("BOM") << QByteArray("\xEF\xBB\xBFwindow.TRAVEL_DATA = {};") << true;
    QTest::newRow("CRLF") << QByteArray("/*\r\n */\r\nwindow.TRAVEL_DATA = {\r\n\"a\": 1\r\n};\r\n") << true;
    QTest::newRow("没有分号") << QByteArray("window.TRAVEL_DATA = {}") << true;
    QTest::newRow("结尾注释") << QByteArray("window.TRAVEL_DATA={}; // 完\n/* x */") << true;
    QTest::newRow("null") << QByteArray("window.TRAVEL_DATA = null;") << true;
    QTest::newRow("前缀不对") << QByteArray("var TRAVEL_DATA = {};") << false;
    QTest::newRow("后面还有代码") << QByteArray("window.TRAVEL_DATA = {};\nalert(1);") << false;
    QTest::newRow("JSON 错") << QByteArray("window.TRAVEL_DATA = {\"a\":1,};") << false;
    QTest::newRow("注释没结束") << QByteArray("/* x\nwindow.TRAVEL_DATA = {};") << false;
    QTest::newRow("空文件") << QByteArray("") << false;
    QTest::newRow("截断") << QByteArray("window.TRAVEL_DATA = {\"plans\": [ {\"id\": \"p_") << false;
}

void TestData::dataFileParse()
{
    QFETCH(QByteArray, bytes);
    QFETCH(bool, ok);
    const data::ParsedDataFile p = data::parseDataFile(bytes);
    QVERIFY2(p.ok == ok, qPrintable(p.error));
    if (!ok) QVERIFY(!p.error.isEmpty());
}

void TestData::dataFileHeader()
{
    // 注释头和 Electron map-app.js serializeData 逐字一致
    const QByteArray b = data::serializeDataFile(json::parse(u"{\"version\":4,\"plans\":[]}"), fixedTime(2026, 11, 1, 23, 46, 18));
    const QByteArray expected = QStringLiteral(
        "/*\n"
        " * 旅行计划数据文件 —— 由网页自动生成，一般不需要手动改。\n"
        " * 位置：数据文件夹（默认 D:/旅行计划数据）/data.js\n"
        " * 由「旅行计划」桌面 app 读取和保存。\n"
        " *\n"
        " * 照片路径写成 photos/xxx.jpg（相对于本文件所在的资源文件夹），\n"
        " * 不要写成 旅行计划数据/photos/xxx.jpg。\n"
        " *\n"
        " * 最后保存：2026/11/1 23:46:18\n"
        " */\n"
        "window.TRAVEL_DATA = {\n  \"version\": 4,\n  \"plans\": []\n};\n").toUtf8();
    QVERIFY2(b == expected, firstDiff(b, expected).constData());
    QVERIFY(!b.startsWith("\xEF\xBB\xBF"));
    QVERIFY(!b.contains('\r'));
}

void TestData::timestamp()
{
    QCOMPARE(data::zhCnTimestamp(fixedTime(2026, 1, 2, 8, 5, 9)), QStringLiteral("2026/1/2 08:05:09"));
    QCOMPARE(data::zhCnTimestamp(fixedTime(2026, 11, 1, 0, 0, 0)), QStringLiteral("2026/11/1 00:00:00"));
}

// ---------- 真实数据往返 ----------

void TestData::realRoundTrip()
{
    QStringList files;
    files << QStringLiteral(TP_SOURCE_DIR "/testdata/data.js");
    const QString extra = qEnvironmentVariable("TP_ROUNDTRIP_DIRS");
    for (const QString &dir : extra.split(u';', Qt::SkipEmptyParts)) {
        for (const QString &sub : { QString(), QStringLiteral(".history") }) {
            const QDir d(QDir(dir).filePath(sub));
            for (const QString &n : d.entryList({ QStringLiteral("*.js") }, QDir::Files | QDir::Hidden, QDir::Name))
                files << d.filePath(n);
        }
    }
    int checked = 0;
    QStringList notElectron;
    for (const QString &f : files) {
        const QByteArray orig = readFile(f);
        if (orig.isEmpty()) continue;
        QElapsedTimer t;
        t.start();
        const data::ParsedDataFile p = data::parseDataFile(orig);
        const qint64 parseMs = t.elapsed();
        QVERIFY2(p.ok, qPrintable(f + "：" + p.error));
        t.restart();
        const QByteArray out = data::serializeDataFile(p.data, QDateTime::currentDateTime());
        const qint64 writeMs = t.elapsed();
        const QString name = QFileInfo(f).fileName();
        qInfo("%s：%lld 字节，读 %lld ms，写 %lld ms", qPrintable(name), qlonglong(orig.size()), parseMs, writeMs);
        if (!orig.contains("window.TRAVEL_DATA = ") || !orig.startsWith("/*\n * 旅行计划数据文件")) {
            notElectron << name;   // 不是 app 写的（例如手工 / 别的工具写的），只要求能读
            continue;
        }
        const QByteArray a = data::normalizeSavedAt(out), b = data::normalizeSavedAt(orig);
        if (a != b && name.startsWith(QLatin1String("data.before-claude"))) {
            notElectron << name + "（内容和 JSON.stringify 不一致，不是 app 写的）";
            continue;
        }
        QVERIFY2(a == b, qPrintable(name + "：" + QString::fromUtf8(firstDiff(a, b))));
        ++checked;
    }
    if (!notElectron.isEmpty()) qInfo("只检查了能读、没比对的：%s", qPrintable(notElectron.join(u'、')));
    qInfo("逐字节比对通过：%d 个文件", checked);
    QVERIFY(checked > 0);
}

// ---------- 保存流程（0.6.0 起是原生版的 travel.json） ----------

void TestData::storeMissing()
{
    QTemporaryDir tmp;
    data::DataStore s;
    s.setAppVersion("0.6.0");
    s.setClock([] { return fixedTime(2026, 11, 2); });
    QVERIFY(s.load(tmp.path()));
    QCOMPARE(s.state(), data::DataStore::State::Missing);
    QVERIFY(!s.hasElectronData());
    QCOMPARE(s.data()["format"].toString(), QStringLiteral("travelplanner"));
    QCOMPARE(s.data()["formatVersion"].toNumber(), 1.0);
    QVERIFY(s.data()["plans"].isArray() && s.data()["photos"].isArray() && s.data()["journal"].isArray());
    QVERIFY(s.knownHash().isEmpty());
    const auto r = s.save();
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(QFile::exists(tmp.filePath("travel.json")));
    QVERIFY(!QFile::exists(tmp.filePath("travel.backup.json")));   // 原来没有文件，不备份
    QVERIFY(!QDir(tmp.filePath(".history")).exists());
    QCOMPARE(s.state(), data::DataStore::State::Loaded);
    const QByteArray b = readFile(tmp.filePath("travel.json"));
    QCOMPARE(s.knownHash(), data::DataStore::sha256Hex(b));
    QVERIFY(b.startsWith("{\n  \"format\": \"travelplanner\",\n  \"formatVersion\": 1,\n  \"savedBy\": \"0.6.0\",\n"));
    QVERIFY(b.endsWith("}\n"));
    QVERIFY(!b.contains('\r'));
    QVERIFY(!b.startsWith("\xEF\xBB\xBF"));
    // 文件夹里只有 Electron 的 data.js：照样按空数据，标记"可以导入"
    QTemporaryDir el;
    writeFileRaw(el.filePath("data.js"), sampleData());
    QVERIFY(s.load(el.path()));
    QCOMPARE(s.state(), data::DataStore::State::Missing);
    QVERIFY(s.hasElectronData());
}

void TestData::storeSaveUnchanged()
{
    QTemporaryDir tmp;
    const QByteArray orig = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"));
    QVERIFY2(!orig.isEmpty(), "testdata/travel.json 不存在");
    writeFileRaw(tmp.filePath("travel.json"), orig);
    data::DataStore s;
    s.setClock([] { return fixedTime(2026, 11, 2, 21, 30, 5); });
    QVERIFY(s.load(tmp.path()));
    QCOMPARE(s.state(), data::DataStore::State::Loaded);
    QCOMPARE(s.knownHash(), data::DataStore::sha256Hex(orig));
    const auto r = s.save();
    QVERIFY2(r.ok, qPrintable(r.error));
    const QByteArray now = readFile(tmp.filePath("travel.json"));
    QVERIFY2(normSavedAt(now) == normSavedAt(orig), firstDiff(normSavedAt(now), normSavedAt(orig)).constData());
    QCOMPARE(readFile(tmp.filePath("travel.backup.json")), orig);
    QCOMPARE(readFile(tmp.filePath(".history/travel-2026-11-02.json")), orig);
    QVERIFY(!QFile::exists(tmp.filePath("travel.json.tmp")));
    // 同一天第二次保存：历史不再覆盖（存的是当天第一次保存前的样子）
    QVERIFY(s.save().ok);
    QCOMPARE(readFile(tmp.filePath(".history/travel-2026-11-02.json")), orig);
    QCOMPARE(readFile(tmp.filePath("travel.backup.json")), now);
}

void TestData::storeConflict()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    data::DataStore s;
    QVERIFY(s.load(tmp.path()));
    s.modify([](Value &d) { d.mutObject().set("unknownTop", Value("我改的")); });
    const QByteArray external = sampleNative().replace("保留我", "别处改的");
    writeFileRaw(tmp.filePath("travel.json"), external);
    auto r = s.save();
    QVERIFY(!r.ok);
    QVERIFY(r.conflict);
    QCOMPARE(readFile(tmp.filePath("travel.json")), external);   // 没有覆盖
    QVERIFY(s.isDirty());
    r = s.save(true);   // 仍然保存
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(readFile(tmp.filePath("travel.json")).contains("我改的"));
    QCOMPARE(readFile(tmp.filePath("travel.backup.json")), external);   // 被覆盖的那一版在 travel.backup.json
    QVERIFY(!s.isDirty());
}

void TestData::storeUnreadable()
{
    QTemporaryDir tmp;
    const QByteArray broken = "{\"format\":\"travelplanner\",\"plans\": [ {\"id\": \"p_x\", }\n";
    writeFileRaw(tmp.filePath("travel.json"), broken);
    data::DataStore s;
    QVERIFY(!s.load(tmp.path()));
    QCOMPARE(s.state(), data::DataStore::State::Unreadable);
    QVERIFY(s.isReadOnly());
    QVERIFY(s.loadError().contains("第 1 行"));
    s.modify([](Value &d) { d.mutObject().set("x", Value(1)); });
    const auto r = s.save(true);   // 只读时连"仍然保存"也不行
    QVERIFY(!r.ok);
    QCOMPARE(readFile(tmp.filePath("travel.json")), broken);
    QVERIFY(!QFile::exists(tmp.filePath("travel.backup.json")));
    QVERIFY(!s.flushBeforeExit().isEmpty());   // 退出时另存一份，不碰原文件
    QCOMPARE(readFile(tmp.filePath("travel.json")), broken);
    // 内容不是对象 / 不是旅行计划的数据 / JSON 后面还有东西
    for (const QByteArray &bad : { QByteArray("[1,2]"), QByteArray("{\"plans\":[]}"), QByteArray("{\"format\":\"travelplanner\"} x") }) {
        writeFileRaw(tmp.filePath("travel.json"), bad);
        QVERIFY2(!s.load(tmp.path()), bad.constData());
        QVERIFY(s.isReadOnly());
    }
}

void TestData::storeHistoryPrune()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    QDir().mkpath(tmp.filePath(".history"));
    for (int d = 1; d <= 15; ++d)
        writeFileRaw(tmp.filePath(QStringLiteral(".history/travel-2026-10-%1.json").arg(d, 2, 10, QChar(u'0'))), "x");
    writeFileRaw(tmp.filePath(".history/其他文件.txt"), "不动");
    writeFileRaw(tmp.filePath(".history/data-2026-10-01.js"), "Electron 的，不动");
    data::DataStore s;
    s.setClock([] { return fixedTime(2026, 11, 2); });
    QVERIFY(s.load(tmp.path()));
    QVERIFY(s.save().ok);
    QStringList left = QDir(tmp.filePath(".history")).entryList({ "travel-*.json" }, QDir::Files, QDir::Name);
    QCOMPARE(left.size(), 14);
    QCOMPARE(left.last(), QStringLiteral("travel-2026-11-02.json"));
    QCOMPARE(left.first(), QStringLiteral("travel-2026-10-03.json"));   // 最旧的两份被删
    QVERIFY(QFile::exists(tmp.filePath(".history/其他文件.txt")));
    QVERIFY(QFile::exists(tmp.filePath(".history/data-2026-10-01.js")));
    const auto list = s.historyList();
    QCOMPARE(list.size(), 14);
    QCOMPARE(list.first().name, QStringLiteral("travel-2026-11-02.json"));
    QCOMPARE(list.first().date, QStringLiteral("2026-11-02"));
    QCOMPARE(list.first().plans, 2);
    QCOMPARE(list.last().plans, 0);   // 内容是 "x"，读不出来按 0
}

void TestData::storeBeforeAi()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    for (int k = 0; k < 11; ++k)
        writeFileRaw(tmp.filePath(QStringLiteral("travel.before-ai-202610%1-120000.json").arg(10 + k)), "old");
    data::DataStore s;
    s.setClock([] { return fixedTime(2026, 11, 2, 9, 8, 7); });
    QVERIFY(s.load(tmp.path()));
    QString err;
    const QString name = s.snapshotBeforeAi(&err);
    QCOMPARE(name, QStringLiteral("travel.before-ai-20261102-090807.json"));
    QCOMPARE(readFile(tmp.filePath(name)), sampleNative());
    const QStringList left = QDir(tmp.path()).entryList({ "travel.before-ai-*.json" }, QDir::Files, QDir::Name);
    QCOMPARE(left.size(), 10);
    QCOMPARE(left.first(), QStringLiteral("travel.before-ai-20261012-120000.json"));
    QCOMPARE(s.snapshotBeforeImport(&err), QStringLiteral("travel.before-import-20261102-090807.json"));
    QCOMPARE(readFile(tmp.filePath("travel.before-import-20261102-090807.json")), sampleNative());
}

void TestData::storeStampUpdated()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    data::DataStore s;
    s.setAppVersion("9.9.9");
    const QDateTime t = fixedTime(2026, 11, 2, 12, 0, 0);
    s.setClock([t] { return t; });
    QVERIFY(s.load(tmp.path()));
    s.modify([](Value &d) {
        auto &plans = d.mutObject().find("plans")->mutArray();
        plans[0].mutObject().set("name", Value("甲改"));
    });
    QVERIFY(s.save().ok);
    const Value &plans = s.data()["plans"];
    const json::Object &p0 = plans.asArray()[0].asObject();
    QCOMPARE(p0.members().back().first, QStringLiteral("updatedAt"));   // 新字段加在末尾
    QCOMPARE(p0.find("updatedAt")->toNumber(), double(t.toMSecsSinceEpoch()));
    QCOMPARE(plans.asArray()[1]["updatedAt"].toNumber(), 1700000000000.0);   // 没改的不动
    // savedBy / savedAt 原位更新
    QCOMPARE(s.data().asObject().members().at(2).first, QStringLiteral("savedBy"));
    QCOMPARE(s.data()["savedBy"].toString(), QStringLiteral("9.9.9"));
    QCOMPARE(s.data()["savedAt"].toString(), data::nativefmt::isoWithOffset(t));
    // 再保存一次，不应再改 updatedAt
    s.setClock([t] { return t.addSecs(60); });
    QVERIFY(s.save().ok);
    QCOMPARE(s.data()["plans"].asArray()[0]["updatedAt"].toNumber(), double(t.toMSecsSinceEpoch()));
    // 未知字段原样保留、位置不变
    const QByteArray out = readFile(tmp.filePath("travel.json"));
    QVERIFY(out.contains("\"futureField\": {\n        \"z\": 1,\n        \"a\": [\n          1.5e-7,\n          1e+21\n        ]\n      }"));
    QVERIFY(out.contains("\"journal\": [],\n  \"unknownTop\": \"保留我\"\n}\n"));
}

void TestData::storeFormatCheck()
{
    QTemporaryDir tmp;
    data::DataStore s;
    // 缺必有的数组：补在末尾
    writeFileRaw(tmp.filePath("travel.json"), "{\"format\":\"travelplanner\",\"formatVersion\":1,\"a\":1}");
    QVERIFY(s.load(tmp.path()));
    QCOMPARE(json::stringify(s.data(), 0),
             QStringLiteral("{\"format\":\"travelplanner\",\"formatVersion\":1,\"a\":1,\"plans\":[],\"photos\":[],\"journal\":[]}"));
    QVERIFY(!s.isReadOnly());
    // 更新版本写的：只读，照样显示
    const QByteArray newer = "{\"format\":\"travelplanner\",\"formatVersion\":2,\"plans\":[{\"id\":\"p_n\"}],\"photos\":[],\"journal\":[]}";
    writeFileRaw(tmp.filePath("travel.json"), newer);
    QVERIFY(s.load(tmp.path()));
    QCOMPARE(s.state(), data::DataStore::State::Newer);
    QVERIFY(s.isNewerFormat());
    QVERIFY(s.isReadOnly());
    QCOMPARE(s.fileFormatVersion(), 2);
    QCOMPARE(s.data()["plans"].asArray().size(), size_t(1));
    s.modify([](Value &d) { d.mutObject().set("x", Value(1)); });
    QVERIFY(!s.save(true).ok);
    QCOMPARE(readFile(tmp.filePath("travel.json")), newer);
}

void TestData::storeRestoreHistory()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    QDir().mkpath(tmp.filePath(".history"));
    const QByteArray old = "{\"format\":\"travelplanner\",\"formatVersion\":1,\"plans\":[{\"id\":\"p_old\"}]}\n";
    writeFileRaw(tmp.filePath(".history/travel-2026-10-01.json"), old);
    writeFileRaw(tmp.filePath(".history/travel-2026-10-02.json"), "{\"plans\":[]}");   // 不是旅行计划的数据
    data::DataStore s;
    QVERIFY(s.load(tmp.path()));
    QSignalSpy spy(&s, &data::DataStore::reloaded);
    QVERIFY(!s.restoreHistory("../travel.json").ok);
    QVERIFY(!s.restoreHistory("travel-2026-10-02.json").ok);
    const auto r = s.restoreHistory("travel-2026-10-01.json");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(readFile(tmp.filePath("travel.json")), old);
    QCOMPARE(readFile(tmp.filePath("travel.backup.json")), sampleNative());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(s.data()["plans"].asArray()[0]["id"].toString(), QStringLiteral("p_old"));
    QCOMPARE(s.knownHash(), data::DataStore::sha256Hex(old));
}

void TestData::storeFlushBeforeExit()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    data::DataStore s;
    s.setClock([] { return fixedTime(2026, 11, 2, 22, 1, 2); });
    QVERIFY(s.load(tmp.path()));
    QVERIFY(s.flushBeforeExit().isEmpty());   // 没改动：什么也不做
    s.modify([](Value &d) { d.mutObject().set("unknownTop", Value("退出前改的")); });
    const QByteArray ext = "{\"format\":\"travelplanner\",\"plans\":[]}";
    writeFileRaw(tmp.filePath("travel.json"), ext);   // 别处改过
    const QString rescue = s.flushBeforeExit();
    QCOMPARE(rescue, QStringLiteral("travel.unsaved-20261102-220102.json"));
    QCOMPARE(readFile(tmp.filePath("travel.json")), ext);
    QVERIFY(readFile(tmp.filePath(rescue)).contains("退出前改的"));
}

void TestData::storeWatcher()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    data::DataStore s;
    QVERIFY(s.load(tmp.path()));
    QSignalSpy spy(&s, &data::DataStore::externallyChanged);
    QVERIFY(!s.checkExternalNow());
    // 自己保存不算外部修改
    QVERIFY(s.save().ok);
    QVERIFY(!s.checkExternalNow());
    s.startWatching(50);
    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);
    writeFileRaw(tmp.filePath("travel.json"), "{\"plans\":[]}");
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);   // 同一个版本只提示一次
    writeFileRaw(tmp.filePath("travel.json"), "{\"plans\":[1]}");
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 3000);
    s.stopWatching();
}

// ---------- 0.6.0：锁文件 ----------

void TestData::lockBasic()
{
    QTemporaryDir tmp;
    qint64 now = 1'000'000;
    data::LockFile a, b;
    for (data::LockFile *l : { &a, &b }) l->setClock([&now] { return now; });
    a.setIdentity("TravelPlannerNative", "0.6.0", "PC-A", 100);
    b.setIdentity("TravelPlannerNative", "0.7.0", "PC-B", 200);
    QCOMPARE(a.acquire(tmp.path(), false), data::LockFile::Result::Acquired);
    QVERIFY(QFile::exists(tmp.filePath(".lock")));
    const data::LockOwner o = data::LockFile::read(tmp.path());
    QVERIFY(o.valid);
    QCOMPARE(o.machine, QStringLiteral("PC-A"));
    QCOMPARE(o.pid, qint64(100));
    QCOMPARE(o.heartbeat, now);
    // 另一台电脑：别人开着 → 只读
    data::LockOwner other;
    QCOMPARE(b.acquire(tmp.path(), false, &other), data::LockFile::Result::HeldByOther);
    QCOMPARE(other.machine, QStringLiteral("PC-A"));
    QVERIFY(other.describe().contains("PC-A"));
    QVERIFY(!b.held());
    // 自己再打开同一个文件夹：还是自己的
    QCOMPARE(a.acquire(tmp.path(), false), data::LockFile::Result::Acquired);
    // 2 分钟没刷新：过时，可以接管
    now += data::LockFile::kStaleMs + 1;
    QCOMPARE(b.acquire(tmp.path(), false), data::LockFile::Result::Acquired);
    QCOMPARE(data::LockFile::read(tmp.path()).machine, QStringLiteral("PC-B"));
    // 原来那份刷新时发现被接管了
    QVERIFY(!a.refresh(&other));
    QCOMPARE(other.machine, QStringLiteral("PC-B"));
    QVERIFY(!a.held());
    a.release();   // 不是自己的锁，不删
    QVERIFY(QFile::exists(tmp.filePath(".lock")));
    // 强制接管（"仍然编辑"）
    QCOMPARE(a.acquire(tmp.path(), true), data::LockFile::Result::Acquired);
    QCOMPARE(data::LockFile::read(tmp.path()).machine, QStringLiteral("PC-A"));
    a.release();
    QVERIFY(!QFile::exists(tmp.filePath(".lock")));
}

void TestData::lockDeadProcess()
{
    QTemporaryDir tmp;
    qint64 now = 5'000'000;
    data::LockFile a, b;
    for (data::LockFile *l : { &a, &b }) l->setClock([&now] { return now; });
    a.setIdentity("TravelPlannerNative", "0.6.0", "PC-A", 100);
    b.setIdentity("TravelPlannerNative", "0.6.0", "pc-a", 101);   // 电脑名不分大小写
    bool alive = true;
    b.setProcessAlive([&alive](qint64) { return alive; });
    QCOMPARE(a.acquire(tmp.path(), false), data::LockFile::Result::Acquired);
    QCOMPARE(b.acquire(tmp.path(), false), data::LockFile::Result::HeldByOther);   // 同一台电脑，那个进程还在
    alive = false;                                                                  // 崩溃了（锁文件还在）
    QCOMPARE(b.acquire(tmp.path(), false), data::LockFile::Result::Acquired);
    QVERIFY(data::LockFile::processAlive(QCoreApplication::applicationPid()));
}

void TestData::storeLocked()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    // 假装别的电脑上的旅行计划刚打开了这份数据
    data::LockFile other;
    other.setIdentity("TravelPlannerNative", "0.6.0", "LAPTOP-OTHER", 4242);
    QCOMPARE(other.acquire(tmp.path(), false), data::LockFile::Result::Acquired);
    data::DataStore s;
    QSignalSpy spy(&s, &data::DataStore::lockChanged);
    QVERIFY(s.load(tmp.path()));
    QVERIFY(s.lockedByOther());
    QVERIFY(s.isReadOnly());
    QCOMPARE(s.lockOwner().machine, QStringLiteral("LAPTOP-OTHER"));
    QCOMPARE(s.data()["plans"].asArray().size(), size_t(2));   // 照样读出来
    s.modify([](Value &d) { d.mutObject().set("unknownTop", Value("改")); });
    auto r = s.save(true);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("LAPTOP-OTHER"));
    QCOMPARE(readFile(tmp.filePath("travel.json")), sampleNative());
    // 对方关掉了：下次检查时自动恢复编辑
    other.release();
    s.refreshLockNow();
    QVERIFY(!s.lockedByOther());
    QVERIFY(!s.isReadOnly());
    QVERIFY(spy.count() >= 2);
    QVERIFY(s.save().ok);
    // 再被别人强制接管：变成只读
    QCOMPARE(other.acquire(tmp.path(), true), data::LockFile::Result::Acquired);
    s.refreshLockNow();
    QVERIFY(s.lockedByOther());
    // 自己"仍然编辑"
    s.takeOverLock();
    QVERIFY(!s.lockedByOther());
    QCOMPARE(data::LockFile::read(tmp.path()).pid, QCoreApplication::applicationPid());
}

// ---------- 0.6.0：从 Electron 版导入 ----------

namespace {
// 覆盖各种情况的 Electron 数据
QByteArray sampleElectron()
{
    return QStringLiteral(R"JS(/*
 * 注释
 */
window.TRAVEL_DATA = {
  "version": 4,
  "plans": [
    {
      "id": "p_a", "name": "甲", "type": "route", "status": "planning",
      "coverPhoto": "photos/p_a/cover.jpg",
      "guide": "看这张：\n![山](photos/p_a/ph_1_山.jpg)\n还有 ![没登记的](photos/p_a/extra.png)\n外链 ![x](https://example.com/a.jpg)",
      "localEmergency": "112（报警）",
      "updatedAt": 1700000000000,
      "mood": "开心",
      "stops": [
        { "id": "s_2", "name": "二", "lat": 2, "lng": 2, "order": 2, "schedule": "旧格式的日程", "photos": [],
          "legMode": "drive", "legPath": "abc", "legKey": "drive:k", "legDist": 1000, "legTime": 60, "weather": "晴" },
        { "id": "s_1", "name": "一", "lat": 1, "lng": 1, "order": 1, "schedule": [{ "date": "2026-01-01", "text": "出发" }],
          "photos": [
            { "id": "ph_1", "path": "photos/p_a/ph_1_山.jpg", "caption": "山", "fav": true, "takenAt": "2024-10-11T16:08", "lat": 46.1, "lng": 7.2, "rating": 5 },
            { "id": "ph_dup", "path": "photos/p_a/ph_1_山.jpg", "caption": "同一个文件" },
            { "id": "ph_missing", "path": "photos/p_a/丢了.jpg", "caption": "" }
          ] },
        { "name": "没有 id", "lat": 3, "lng": 3, "photos": [{ "id": "ph_3", "path": "photos/p_a/3.jpg", "caption": "" }] }
      ]
    },
    { "id": "p_b", "name": "乙", "type": "single", "status": "wish", "coverPhoto": "", "localEmergency": [{ "label": "急救", "phone": "120" }],
      "stops": [] }
  ],
  "journal": [ { "id": "jn_1", "planId": "p_a", "stopId": "s_1", "title": "第一天", "date": "2026-01-01", "body": "![](photos/p_a/ph_1_山.jpg)\n好看", "weather": "雨" } ],
  "inbox": [ { "id": "ph_in", "path": "photos/_inbox/ph_in_未归类.jpg", "caption": "未归类" } ],
  "photoCredits": { "photos/p_a/cover.jpg": { "author": "某人", "license": "CC BY 4.0" }, "photos/没用到.jpg": { "author": "x" } },
  "visitedRegions": [ { "id": "CHN-1152" } ],
  "documents": [ { "id": "doc_1", "kind": "passport", "expires": "2030-01-01" } ],
  "emergency": { "insurer": "保险公司" },
  "seasonGuide": { "rows": [] },
  "packingTemplate": ["护照"],
  "nextTripId": "p_a",
  "futureTop": { "x": 1 }
};
)JS").toUtf8();
}

void makeElectronDir(const QString &dir)
{
    writeFileRaw(dir + "/data.js", sampleElectron());
    for (const char *f : { "photos/p_a/cover.jpg", "photos/p_a/ph_1_山.jpg", "photos/p_a/extra.png", "photos/p_a/3.jpg",
                           "photos/_inbox/ph_in_未归类.jpg", "photos/没用到.jpg", "photos/.originals/p_a/3.jpg",
                           "photos/.thumbs/abc.jpg", "photos/p_a/x.jpg.tmp-123" }) {
        const QString p = dir + "/" + QString::fromUtf8(f);
        QDir().mkpath(QFileInfo(p).absolutePath());
        writeFileRaw(p, QByteArray("内容:") + f);
    }
}

const Value *findById(const Value &arr, const QString &id)
{
    for (const Value &v : arr.asArray()) if (v["id"].toString() == id) return &v;
    return nullptr;
}
}

void TestData::importSafeName()
{
    using data::ElectronImport;
    QCOMPARE(ElectronImport::safeName("我的 照片#1.jpg"), QStringLiteral("我的_照片_1.jpg"));
    QCOMPARE(ElectronImport::safeName("a  \t b"), QStringLiteral("a_b"));
    QCOMPARE(ElectronImport::safeName(QStringLiteral("a　b﻿c")), QStringLiteral("a_b_c"));   // JS \s 含全角空格、BOM
    QCOMPARE(ElectronImport::safeName(""), QStringLiteral("photo"));
    QCOMPARE(ElectronImport::safeName(QString(70, u'x') + ".jpg").size(), 60);   // 只留最后 60 个字符
    QVERIFY(ElectronImport::safeName(QString(70, u'x') + ".jpg").endsWith(".jpg"));
}

void TestData::importConvert()
{
    QTemporaryDir tmp;
    makeElectronDir(tmp.path());
    const auto parsed = data::parseDataFile(sampleElectron());
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    const QDateTime now = fixedTime(2026, 11, 3, 4, 30);
    const double nowMs = double(now.toMSecsSinceEpoch());
    auto conv = data::ElectronImport::convert(parsed.data, now, "0.6.0", [&tmp](const QString &rel) { return QFileInfo(tmp.filePath(rel)).isFile(); });
    const Value &d = conv.doc;
    const auto &rep = conv.report;
    // 顶层顺序
    QStringList keys;
    for (const auto &m : d.asObject().members()) keys << m.first;
    QCOMPARE(keys, QStringList({ "format", "formatVersion", "savedBy", "savedAt", "plans", "photos", "journal", "documents", "visitedRegions",
                                 "emergency", "seasonGuide", "packingTemplate", "nextTripId", "futureTop" }));
    QCOMPARE(data::nativefmt::check(d), data::nativefmt::Check::Ok);

    // 计划
    const Value &pa = d["plans"].asArray()[0];
    keys.clear();
    for (const auto &m : pa.asObject().members()) keys << m.first;
    QCOMPARE(keys, QStringList({ "id", "name", "type", "status", "cover", "guide", "localEmergency", "createdAt", "updatedAt", "mood", "stops" }));
    QCOMPARE(pa["createdAt"].toNumber(), nowMs);
    QCOMPARE(pa["mood"].toString(), QStringLiteral("开心"));   // 不认识的字段原样保留
    QCOMPARE(json::stringify(pa["localEmergency"], 0), QStringLiteral("[{\"label\":\"112（报警）\",\"phone\":\"\"}]"));
    const QString coverId = pa["cover"].toString();
    QVERIFY(coverId.startsWith("ph_"));
    QString extraId;
    for (const Value &p : d["photos"].asArray()) if (p["file"].toString().endsWith("extra.png")) extraId = p["id"].toString();
    QVERIFY(!extraId.isEmpty());
    QCOMPARE(pa["guide"].toString(), QStringLiteral("看这张：\n![山](photo:ph_1)\n还有 ![没登记的](photo:%1)\n外链 ![x](https://example.com/a.jpg)").arg(extraId));
    // 站点：按 order 排序、去掉 order、leg 合并、旧日程转数组、没 id 的补上
    const auto &stops = pa["stops"].asArray();
    QCOMPARE(stops.size(), size_t(3));
    QVERIFY(stops[0]["id"].toString().startsWith("s_"));   // 没有 order：JS (undefined ?? 0) = 0，排最前；没有 id：补一个
    QCOMPARE(stops[0]["name"].toString(), QStringLiteral("没有 id"));
    QCOMPARE(stops[0].asObject().members().front().first, QStringLiteral("id"));
    QCOMPARE(stops[1]["id"].toString(), QStringLiteral("s_1"));
    QCOMPARE(stops[2]["id"].toString(), QStringLiteral("s_2"));
    QVERIFY(!stops[1].asObject().contains("order"));
    QVERIFY(!stops[1].asObject().contains("photos"));
    QCOMPARE(json::stringify(stops[2]["leg"], 0), QStringLiteral("{\"mode\":\"drive\",\"path\":\"abc\",\"dist\":1000,\"time\":60,\"key\":\"drive:k\"}"));
    keys.clear();
    for (const auto &m : stops[2].asObject().members()) keys << m.first;
    QCOMPARE(keys, QStringList({ "id", "name", "lat", "lng", "schedule", "leg", "weather" }));
    QCOMPARE(json::stringify(stops[2]["schedule"], 0), QStringLiteral("[{\"text\":\"旧格式的日程\"}]"));
    QCOMPARE((*findById(d["photos"], "ph_3"))["stop"].toString(), stops[0]["id"].toString());   // 照片归属用补上的 id
    QCOMPARE(json::stringify(d["plans"].asArray()[1]["localEmergency"], 0), QStringLiteral("[{\"label\":\"急救\",\"phone\":\"120\"}]"));
    QCOMPARE(d["plans"].asArray()[1]["cover"].toString(), QString());

    // 照片
    const Value *p1 = findById(d["photos"], "ph_1");
    QVERIFY(p1);
    QCOMPARE((*p1)["file"].toString(), QStringLiteral("photos/2024-10/ph_1_山.jpg"));   // 按拍摄月份；原名已带 id 不重复加
    QCOMPARE((*p1)["plan"].toString(), QStringLiteral("p_a"));
    QCOMPARE((*p1)["stop"].toString(), QStringLiteral("s_1"));
    QCOMPARE((*p1)["fav"].toBool(), true);
    QCOMPARE((*p1)["rating"].toNumber(), 5.0);
    QCOMPARE((*p1)["addedAt"].toNumber(), nowMs);
    QVERIFY(!findById(d["photos"], "ph_dup"));                  // 同一个文件只留一条
    QCOMPARE(rep.duplicatePhotos, QStringList({ "photos/p_a/ph_1_山.jpg" }));
    const Value *miss = findById(d["photos"], "ph_missing");
    QVERIFY(miss);
    QCOMPARE(rep.missingFiles, QStringList({ "photos/p_a/丢了.jpg" }));
    const Value *cover = findById(d["photos"], coverId);
    QCOMPARE((*cover)["file"].toString(), QStringLiteral("photos/2026-11/%1_cover.jpg").arg(coverId));   // 没有拍摄时间：导入月份
    QCOMPARE((*cover)["stop"].toString(), QString());
    QCOMPARE(json::stringify((*cover)["credit"], 0), QStringLiteral("{\"author\":\"某人\",\"license\":\"CC BY 4.0\"}"));
    const Value *inbox = findById(d["photos"], "ph_in");
    QCOMPARE((*inbox)["plan"].toString(), QString());
    QCOMPARE(rep.externalPhotos, QStringList());   // 正文里的外链不是照片引用（不是 photos/ 开头）
    // 要复制的：存在的本地照片 + .originals 里有的原图
    QStringList srcs;
    for (const auto &c : conv.copies) srcs << c.src;
    srcs.sort();
    QCOMPARE(srcs, QStringList({ "photos/_inbox/ph_in_未归类.jpg", "photos/p_a/3.jpg", "photos/p_a/cover.jpg", "photos/p_a/extra.png", "photos/p_a/ph_1_山.jpg" }));
    QCOMPARE(conv.originals.size(), qsizetype(1));
    QCOMPARE(conv.originals[0].src, QStringLiteral("photos/.originals/p_a/3.jpg"));
    QVERIFY(conv.originals[0].dst.startsWith("photos/.originals/2026-11/ph_3_3.jpg"));

    // 游记
    const Value &j = d["journal"].asArray()[0];
    keys.clear();
    for (const auto &m : j.asObject().members()) keys << m.first;
    QCOMPARE(keys, QStringList({ "id", "plan", "stop", "title", "date", "body", "weather" }));
    QCOMPARE(j["body"].toString(), QStringLiteral("![](photo:ph_1)\n好看"));

    // 报告
    QCOMPARE(rep.plans, 2);
    QCOMPARE(rep.stops, 3);
    QCOMPARE(rep.journal, 1);
    QVERIFY(rep.unknownFields.contains(QStringLiteral("计划.mood")));
    QVERIFY(rep.unknownFields.contains(QStringLiteral("站点.weather")));
    QVERIFY(rep.unknownFields.contains(QStringLiteral("照片.rating")));
    QVERIFY(rep.unknownFields.contains(QStringLiteral("游记.weather")));
    QVERIFY(rep.unknownFields.contains(QStringLiteral("顶层.futureTop")));
}

void TestData::importRun()
{
    QTemporaryDir el, nat;
    makeElectronDir(el.path());
    const QDateTime now = fixedTime(2026, 11, 3, 4, 30);
    Value doc;
    QList<QPair<int, int>> steps;
    const auto rep = data::ElectronImport::run(el.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, "0.6.0", &doc,
                                               [&steps](int a, int b) { steps << qMakePair(a, b); return true; });
    QVERIFY2(rep.ok, qPrintable(rep.error));
    QCOMPARE(rep.photosCopied, 5);
    QCOMPARE(rep.originalsCopied, 1);
    QCOMPARE(steps.last(), qMakePair(6, 6));
    QCOMPARE(rep.unusedFiles, QStringList({ "photos/没用到.jpg" }));   // .thumbs、.originals、.tmp 不算
    // 文件内容原样复制到新位置
    QCOMPARE(readFile(nat.filePath("photos/2024-10/ph_1_山.jpg")), QByteArray("内容:photos/p_a/ph_1_山.jpg"));
    for (const Value &p : doc["photos"].asArray()) {
        const QString f = p["file"].toString();
        if (p["id"].toString() == "ph_missing") { QVERIFY(!QFileInfo::exists(nat.filePath(f))); continue; }
        QVERIFY2(QFileInfo::exists(nat.filePath(f)), qPrintable(f));
    }
    // Electron 的文件夹没被改动
    QCOMPARE(readFile(el.filePath("data.js")), sampleElectron());
    QVERIFY(QFileInfo::exists(el.filePath("photos/p_a/ph_1_山.jpg")));

    // 存进 DataStore，读回来一样
    data::DataStore s;
    QVERIFY(s.load(nat.path()));
    s.replaceData(doc);
    QVERIFY(s.save().ok);
    data::DataStore s2;
    QVERIFY(s2.load(nat.path()));
    QCOMPARE(s2.state(), data::DataStore::State::Loaded);
    QCOMPARE(json::stringify(s2.data(), 0), json::stringify(s.data(), 0));

    // 再导入一次（替换全部）：同一张照片内容一样，不再复制、不改名
    Value doc2;
    const auto rep2 = data::ElectronImport::run(el.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, "0.6.0", &doc2);
    QVERIFY(rep2.ok);
    QVERIFY(rep2.copiedFiles.isEmpty());
    QCOMPARE(json::stringify(doc2["photos"], 0), json::stringify(doc["photos"], 0));
    // 同名但内容不同：另起名字，照片记录跟着改
    writeFileRaw(nat.filePath("photos/2024-10/ph_1_山.jpg"), "别的内容");
    Value doc3;
    const auto rep3 = data::ElectronImport::run(el.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, "0.6.0", &doc3);
    QVERIFY(rep3.ok);
    QCOMPARE((*findById(doc3["photos"], "ph_1"))["file"].toString(), QStringLiteral("photos/2024-10/ph_1_山-2.jpg"));
    QCOMPARE(readFile(nat.filePath("photos/2024-10/ph_1_山-2.jpg")), QByteArray("内容:photos/p_a/ph_1_山.jpg"));
    QCOMPARE(readFile(nat.filePath("photos/2024-10/ph_1_山.jpg")), QByteArray("别的内容"));
}

void TestData::importCancelAndErrors()
{
    QTemporaryDir el, nat;
    makeElectronDir(el.path());
    const QDateTime now = fixedTime(2026, 11, 3, 4, 30);
    Value doc;
    // 取消：已经复制的删掉
    auto rep = data::ElectronImport::run(el.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, "0.6.0", &doc,
                                         [](int done, int) { return done < 3; });
    QVERIFY(!rep.ok);
    QVERIFY(rep.cancelled);
    QDirIterator it(nat.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    QVERIFY2(!it.hasNext(), "取消后不应留下文件");
    QVERIFY(doc.isNull());
    // 没有 data.js / 读不出来
    QTemporaryDir empty;
    rep = data::ElectronImport::run(empty.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, "0.6.0", &doc);
    QVERIFY(!rep.ok);
    QVERIFY(!rep.error.isEmpty());
    writeFileRaw(empty.filePath("data.js"), "window.TRAVEL_DATA = {\"plans\": [}");
    rep = data::ElectronImport::run(empty.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, "0.6.0", &doc);
    QVERIFY(!rep.ok);
    QVERIFY(rep.error.contains("data.js"));
    QVERIFY(data::ElectronImport::isElectronFolder(el.path()));
    QVERIFY(!data::ElectronImport::isElectronFolder(nat.path()));
}

void TestData::importMergeAddNew()
{
    QTemporaryDir el, nat;
    makeElectronDir(el.path());
    const QDateTime now = fixedTime(2026, 11, 3, 4, 30);
    // 原生版里已经有 p_a（改过名）和自己的计划 p_n
    Value existing = json::parse(uR"({"format":"travelplanner","formatVersion":1,"plans":[{"id":"p_a","name":"甲（原生里改过）"},{"id":"p_n","name":"原生"}],
        "photos":[{"id":"ph_own","file":"photos/2026-11/ph_own.jpg","plan":"p_n","stop":""}],"journal":[],"documents":[],"visitedRegions":[{"id":"CHN-1152"}],
        "nextTripId":"p_n"})");
    Value doc;
    const auto rep = data::ElectronImport::run(el.path(), nat.path(), existing, data::ElectronImport::Mode::AddNew, now, "0.6.0", &doc);
    QVERIFY2(rep.ok, qPrintable(rep.error));
    QCOMPARE(rep.plans, 1);           // 只加了 p_b
    QCOMPARE(rep.plansSkipped, 1);    // p_a 已经有了
    const auto &plans = doc["plans"].asArray();
    QCOMPARE(plans.size(), size_t(3));
    QCOMPARE(plans[0]["name"].toString(), QStringLiteral("甲（原生里改过）"));   // 已有的不动
    QCOMPARE(plans[2]["id"].toString(), QStringLiteral("p_b"));
    // 照片：p_a 的跳过；未归类的加进来；自己的保留
    QVERIFY(findById(doc["photos"], "ph_own"));
    QVERIFY(findById(doc["photos"], "ph_in"));
    QVERIFY(!findById(doc["photos"], "ph_1"));
    QCOMPARE(rep.photosCopied, 1);    // 只复制了未归类那张
    QVERIFY(!QFileInfo::exists(nat.filePath("photos/2024-10/ph_1_山.jpg")));
    QCOMPARE(doc["journal"].asArray().size(), size_t(0));   // 游记属于跳过的 p_a
    QCOMPARE(doc["visitedRegions"].asArray().size(), size_t(1));   // 按 id 去重
    QCOMPARE(doc["documents"].asArray().size(), size_t(1));
    QCOMPARE(doc["nextTripId"].toString(), QStringLiteral("p_n"));   // 已有的不换
    QCOMPARE(doc["emergency"]["insurer"].toString(), QStringLiteral("保险公司"));   // 原来没有的补上
    QCOMPARE(doc["futureTop"]["x"].toNumber(), 1.0);
}

// 真实数据：Electron 里的每一个值都要在原生数据里找到（不悄悄丢字段）
void TestData::importRealData()
{
    const QByteArray bytes = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/data.js"));
    QVERIFY(!bytes.isEmpty());
    const auto parsed = data::parseDataFile(bytes);
    QVERIFY(parsed.ok);
    const QDateTime now = fixedTime(2026, 11, 3, 4, 30);
    auto conv = data::ElectronImport::convert(parsed.data, now, "0.6.0", [](const QString &) { return true; });

    // 原生数据里所有叶子值（字符串 / 数字 / 布尔）计数
    QHash<QString, int> have;
    std::function<void(const Value &)> collect = [&](const Value &v) {
        switch (v.type()) {
        case Value::Type::Array: for (const Value &x : v.asArray()) collect(x); break;
        case Value::Type::Object: for (const auto &m : v.asObject().members()) collect(m.second); break;
        case Value::Type::String: have[u's' + v.toString()]++; break;
        case Value::Type::Number: have[u'n' + json::numberToString(v.toNumber())]++; break;
        case Value::Type::Bool: have[v.toBool() ? "bt" : "bf"]++; break;
        default: break;
        }
    };
    collect(conv.doc);
    // Electron 的每个叶子都要有（这几种是按设计换掉的：版本号、站点 order、照片路径 / 封面路径、正文里的照片链接）
    int checked = 0, skipped = 0;
    QStringList lost;
    std::function<void(const Value &, const QString &)> check = [&](const Value &v, const QString &key) {
        switch (v.type()) {
        case Value::Type::Array: for (const Value &x : v.asArray()) check(x, key); return;
        case Value::Type::Object: for (const auto &m : v.asObject().members()) check(m.second, m.first); return;
        default: break;
        }
        if (key == "version" || key == "order" || key == "path" || key == "coverPhoto"
            || (v.isString() && v.toString().contains("](photos/"))) { ++skipped; return; }
        QString k;
        if (v.isString()) k = u's' + v.toString();
        else if (v.isNumber()) k = u'n' + json::numberToString(v.toNumber());
        else if (v.isBool()) k = v.toBool() ? "bt" : "bf";
        else return;
        if (have.value(k) > 0) { have[k]--; ++checked; }
        else lost << key + "=" + k.mid(1).left(40);
    };
    check(parsed.data, QString());
    QVERIFY2(lost.isEmpty(), qPrintable("丢了：" + lost.mid(0, 10).join(" | ")));
    qInfo("真实数据：%d 个值都找到了（%d 个按设计换掉）；%d 个计划、%d 个站点、%d 张照片；不认识的字段 %lld 个",
          checked, skipped, conv.report.plans, conv.report.stops, conv.report.photos, qlonglong(conv.report.unknownFields.size()));
    QVERIFY(conv.report.unknownFields.isEmpty());   // 真实数据里的字段都认识
    QVERIFY(conv.report.duplicatePhotos.isEmpty());

    // 计划、站点数量不变；站点顺序和 Electron 按 order 排的一样
    const auto &ep = parsed.data["plans"].asArray(), &np = conv.doc["plans"].asArray();
    QCOMPARE(np.size(), ep.size());
    for (size_t k = 0; k < ep.size(); ++k) {
        std::vector<Value> st(ep[k]["stops"].asArray().begin(), ep[k]["stops"].asArray().end());
        std::stable_sort(st.begin(), st.end(), [](const Value &a, const Value &b) {
            return (a["order"].isNull() ? 0 : a["order"].jsToNumber()) < (b["order"].isNull() ? 0 : b["order"].jsToNumber()); });
        QCOMPARE(np[k]["stops"].asArray().size(), st.size());
        for (size_t i = 0; i < st.size(); ++i) QCOMPARE(np[k]["stops"].asArray()[i]["id"].toString(), st[i]["id"].toString());
    }
    // 每个计划的封面和站点照片都能在照片库里按 id 找到，文件路径唯一
    QSet<QString> ids, files;
    for (const Value &p : conv.doc["photos"].asArray()) { ids.insert(p["id"].toString()); QVERIFY(!files.contains(p["file"].toString())); files.insert(p["file"].toString()); }
    for (const Value &p : np) if (!p["cover"].toString().isEmpty()) QVERIFY(ids.contains(p["cover"].toString()));
    // 原生格式读进再写回逐字节一致
    const QByteArray out = data::nativefmt::serialize(conv.doc);
    QCOMPARE(data::nativefmt::serialize(json::parse(QString::fromUtf8(out))), out);

    // 生成 testdata/travel.json（截图、tst_data 用）：TP_WRITE_TESTDATA=1 时写
    if (qEnvironmentVariableIsSet("TP_WRITE_TESTDATA")) {
        QFile f(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(out);
        qInfo("已写 testdata/travel.json");
    }
}

// 用真实数据引用的路径造一个假的 Electron 文件夹，跑完整导入（不需要真照片）
void TestData::importRealDataRun()
{
    const QByteArray bytes = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/data.js"));
    const auto parsed = data::parseDataFile(bytes);
    QVERIFY(parsed.ok);
    QTemporaryDir el, nat;
    writeFileRaw(el.filePath("data.js"), bytes);
    auto conv = data::ElectronImport::convert(parsed.data, QDateTime::currentDateTime(), "0.6.0", [](const QString &) { return true; });
    int made = 0;
    for (const QString &rel : conv.referenced) {
        if (made % 10 == 9) { ++made; continue; }   // 每 10 张故意少一张，看缺失报告
        QDir().mkpath(QFileInfo(el.filePath(rel)).absolutePath());
        writeFileRaw(el.filePath(rel), rel.toUtf8());
        ++made;
    }
    Value doc;
    QElapsedTimer t;
    t.start();
    const auto rep = data::ElectronImport::run(el.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, QDateTime::currentDateTime(), "0.6.0", &doc);
    QVERIFY2(rep.ok, qPrintable(rep.error));
    const int expectedMissing = int(conv.referenced.size()) / 10;
    QCOMPARE(rep.missingFiles.size(), qsizetype(expectedMissing));
    QCOMPARE(rep.photosCopied, int(conv.referenced.size()) - expectedMissing);
    qInfo("真实数据完整导入：%d 张照片（复制 %d、缺 %lld），%lld ms", rep.photos, rep.photosCopied, qlonglong(rep.missingFiles.size()), t.elapsed());
    data::DataStore s;
    QVERIFY(s.load(nat.path()));
    s.replaceData(doc);
    QVERIFY(s.save().ok);
    QVERIFY(s.load(nat.path()));
    QCOMPARE(s.state(), data::DataStore::State::Loaded);
    QCOMPARE(s.data()["plans"].asArray().size(), parsed.data["plans"].asArray().size());
}

// ---------- 0.5.0：自动保存节拍（D-07） ----------

void TestData::schedulerDebounce()
{
    data::SaveScheduler s;
    QSignalSpy spy(&s, &data::SaveScheduler::saveRequested);
    // 连续改三下（间隔比停手时间短）：只保存一次，且在最后一下之后才保存
    s.changed(data::SaveScheduler::Mode::Debounced, 150);
    QTest::qWait(60);
    s.changed(data::SaveScheduler::Mode::Debounced, 150);
    QTest::qWait(60);
    s.changed(data::SaveScheduler::Mode::Debounced, 150);
    QVERIFY(s.pending());
    QTest::qWait(60);
    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QVERIFY(!s.pending());
    QTest::qWait(250);
    QCOMPARE(spy.count(), 1);
    // 默认停手时间是 0.8 秒（Electron store.js）
    QCOMPARE(data::SaveScheduler::kDefaultDelayMs, 800);
    s.changed();
    QVERIFY(s.remainingMs() > 700 && s.remainingMs() <= 800);
    s.cancel();
}

void TestData::schedulerNow()
{
    data::SaveScheduler s;
    QSignalSpy spy(&s, &data::SaveScheduler::saveRequested);
    s.changed(data::SaveScheduler::Mode::Debounced, 100);
    s.changed(data::SaveScheduler::Mode::Now);   // 立即保存（带照片 / now），之前在等的那次一起算
    QCOMPARE(spy.count(), 1);
    QVERIFY(!s.pending());
    QTest::qWait(200);
    QCOMPARE(spy.count(), 1);
}

void TestData::schedulerFlushCancel()
{
    data::SaveScheduler s;
    QSignalSpy spy(&s, &data::SaveScheduler::saveRequested);
    QVERIFY(!s.flush());              // 没在等：什么都不做
    QCOMPARE(spy.count(), 0);
    s.changed(data::SaveScheduler::Mode::Debounced, 5000);
    QVERIFY(s.flush());               // 退出 / 备份前：立即保存
    QCOMPARE(spy.count(), 1);
    s.changed(data::SaveScheduler::Mode::Debounced, 100);
    s.cancel();                       // 重新读取后：不保存了
    QTest::qWait(250);
    QCOMPARE(spy.count(), 1);
}

// ---------- 0.5.0：撤销 / 重做（D-13） ----------

namespace {
Value docWithName(const QString &name)
{
    json::Object plan;
    plan.set("id", Value("p_a"));
    plan.set("name", Value(name));
    json::Array plans;
    plans.push_back(Value(std::move(plan)));
    json::Object o;
    o.set("version", Value(4));
    o.set("plans", Value(std::move(plans)));
    return Value(std::move(o));
}
QString nameOf(const Value &d) { return d["plans"].asArray().at(0)["name"].toString(); }
}

void TestData::undoGroup()
{
    data::UndoStack u;
    const Value v0 = docWithName("零"), v1 = docWithName("一"), v2 = docWithName("二"), v3 = docWithName("三");
    u.reset(v0);
    u.beginGroup();
    QVERIFY(u.noteSaved(v1));         // 编辑期间第一次保存：照常压一步
    QVERIFY(!u.noteSaved(v2));        // 以后的保存不再压栈
    QVERIFY(!u.noteSaved(v3));
    u.endGroup();
    QCOMPARE(u.undoCount(), 1);
    auto s = u.undo(v3);
    QCOMPARE(nameOf(*s), QStringLiteral("零"));   // 一步回到打开编辑器之前
    // 放弃：编辑期间压进去的那一步拿掉
    u.reset(v0);
    QVERIFY(u.noteSaved(v1));         // 编辑前的一步
    u.beginGroup();
    QVERIFY(u.noteSaved(v2));
    u.rollbackGroup(v1);              // 恢复成 v1 并保存之后
    QCOMPARE(u.undoCount(), 1);
    s = u.undo(v1);
    QCOMPARE(nameOf(*s), QStringLiteral("零"));
    // 编辑期间什么都没存：放弃不动撤销栈
    u.reset(v0);
    u.beginGroup();
    u.rollbackGroup(v0);
    QVERIFY(!u.canUndo());
}

void TestData::undoBasic()
{
    data::UndoStack u;
    const Value v0 = docWithName("零"), v1 = docWithName("一"), v2 = docWithName("二");
    u.reset(v0);
    QVERIFY(!u.canUndo());
    QVERIFY(!u.noteSaved(v0));        // 内容没变的保存不算一步
    QVERIFY(u.noteSaved(v1));
    QVERIFY(u.noteSaved(v2));
    QCOMPARE(u.undoCount(), 2);
    auto s = u.undo(v2);
    QVERIFY(s);
    QCOMPARE(nameOf(*s), QStringLiteral("一"));
    QVERIFY(u.canRedo());
    s = u.undo(*s);
    QCOMPARE(nameOf(*s), QStringLiteral("零"));
    QVERIFY(!u.undo(*s));             // 没有可以撤销的了
    s = u.redo(v0);
    QCOMPARE(nameOf(*s), QStringLiteral("一"));
    s = u.redo(*s);
    QCOMPARE(nameOf(*s), QStringLiteral("二"));
    QVERIFY(!u.redo(*s));
    QCOMPARE(u.undoCount(), 2);
    // reset 清空两个栈
    u.reset(v2);
    QVERIFY(!u.canUndo() && !u.canRedo());
}

void TestData::undoMaxSteps()
{
    data::UndoStack u;
    u.reset(docWithName("0"));
    for (int k = 1; k <= 35; ++k) u.noteSaved(docWithName(QString::number(k)));
    QCOMPARE(u.undoCount(), data::UndoStack::kMaxSteps);   // 最多 30 步
    Value cur = docWithName("35");
    for (int k = 0; k < 30; ++k) cur = *u.undo(cur);
    QCOMPARE(nameOf(cur), QStringLiteral("5"));          // 最早的 5 步丢掉了
    QVERIFY(!u.canUndo());
}

void TestData::undoNewChangeClearsRedo()
{
    data::UndoStack u;
    u.reset(docWithName("零"));
    u.noteSaved(docWithName("一"));
    const auto s = u.undo(docWithName("一"));
    QVERIFY(u.canRedo());
    QVERIFY(u.noteSaved(docWithName("新")));   // 撤销后又改了：重做栈清空
    QVERIFY(!u.canRedo());
    QCOMPARE(nameOf(*u.undo(docWithName("新"))), QStringLiteral("零"));
}

void TestData::undoSuppress()
{
    data::UndoStack u;
    u.reset(docWithName("零"));
    u.noteSaved(docWithName("一"));
    const auto s = u.undo(docWithName("一"));
    {
        data::UndoStack::Suppress guard(u);
        QVERIFY(!u.noteSaved(docWithName("一·撤销后保存时写了 updatedAt")));   // 撤销后的那次保存不记一步
    }
    QCOMPARE(u.undoCount(), 0);
    QCOMPARE(u.redoCount(), 1);
}

void TestData::undoSharesUnchanged()
{
    // 写时复制：快照和当前数据共享没改过的部分
    const QByteArray bytes = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/data.js"));
    QVERIFY(!bytes.isEmpty());
    auto parsed = data::parseDataFile(bytes);
    QVERIFY(parsed.ok);
    Value cur = parsed.data;
    data::UndoStack u;
    u.reset(cur);
    cur.mutObject().find("plans")->mutArray().front().mutObject().set("name", Value("改了第一个"));
    QVERIFY(u.noteSaved(cur));
    const Value snap = *u.undo(cur);
    const auto &a = snap["plans"].asArray(), &b = cur["plans"].asArray();
    QVERIFY(a.size() > 2);
    QVERIFY(!a.front().sharesWith(b.front()));   // 改过的计划各有一份
    QVERIFY(a.at(1).sharesWith(b.at(1)));        // 没改的计划共享
    QVERIFY(snap["plans"] != cur["plans"]);
}

void TestData::undoWithStore()
{
    // 和 AppController 一样的用法：保存成功后 noteSaved；撤销 = replaceData + 保存
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    data::DataStore s;
    QVERIFY(s.load(tmp.path()));
    data::UndoStack u;
    u.reset(s.data());
    const auto setName = [&s](const QString &n) {
        s.modify([&n](Value &d) { d.mutObject().find("plans")->mutArray().front().mutObject().set("name", Value(n)); });
    };
    setName("甲1");
    QVERIFY(s.save().ok);
    QVERIFY(u.noteSaved(s.data()));
    setName("甲2");
    QVERIFY(s.save().ok);
    QVERIFY(u.noteSaved(s.data()));
    QVERIFY(readFile(tmp.filePath("travel.json")).contains("\"name\": \"甲2\""));
    // 撤销
    s.replaceData(*u.undo(s.data()));
    QVERIFY(s.isDirty());
    {
        data::UndoStack::Suppress g(u);
        QVERIFY(s.save().ok);
        u.noteSaved(s.data());
    }
    QVERIFY(readFile(tmp.filePath("travel.json")).contains("\"name\": \"甲1\""));
    QCOMPARE(u.undoCount(), 1);
    // 再撤销回到最初
    s.replaceData(*u.undo(s.data()));
    QVERIFY(s.save().ok);
    const QByteArray back = readFile(tmp.filePath("travel.json"));
    QVERIFY(back.contains("\"name\": \"甲\""));
    QVERIFY(back.contains("\"unknownTop\": \"保留我\""));   // 不认识的字段还在
    QVERIFY(back.contains("\"futureField\""));
    // 重做两次回到 甲2
    s.replaceData(*u.redo(s.data()));
    s.replaceData(*u.redo(s.data()));
    QVERIFY(s.save().ok);
    QVERIFY(readFile(tmp.filePath("travel.json")).contains("\"name\": \"甲2\""));
}

// 0.11.2：撤销后数据和原来完全一样，连 updatedAt 也不重新写（日历拖动的一步撤销）
void TestData::undoKeepsUpdatedAt()
{
    QTemporaryDir tmp;
    writeFileRaw(tmp.filePath("travel.json"), sampleNative());
    data::DataStore s;
    QVERIFY(s.load(tmp.path()));
    data::UndoStack u;
    s.modify([](Value &d) { d.mutObject().find("plans")->mutArray().front().mutObject().set("name", Value("基准")); });
    QVERIFY(s.save().ok);   // 现在有 updatedAt 了
    u.reset(s.data());
    const Value before = s.data();
    s.modify([](Value &d) { d.mutObject().find("plans")->mutArray().front().mutObject().set("startDate", Value("2030-01-01")); });
    QVERIFY(s.save().ok);
    QVERIFY(u.noteSaved(s.data()));
    QVERIFY(s.data() != before);
    s.restoreData(*u.undo(s.data()));
    QVERIFY(s.isDirty());
    {
        data::UndoStack::Suppress g(u);
        QVERIFY(s.save().ok);
        u.noteSaved(s.data());
    }
    const auto withoutTime = [](Value v) { v.mutObject().remove("savedAt"); return v; };
    QVERIFY(withoutTime(s.data()) == withoutTime(before));
    QCOMPARE(s.data()["plans"].asArray()[0]["updatedAt"].toNumber(), before["plans"].asArray()[0]["updatedAt"].toNumber());
    // 磁盘上也是
    data::DataStore again;
    QVERIFY(again.load(tmp.path()));
    QVERIFY(withoutTime(again.data()) == withoutTime(before));
    again.lock().release();
}

// ---------- 0.5.0：备份（D-17 / D-18） ----------

namespace {
void makeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    writeFileRaw(path, bytes);
}
void makeDataDir(const QString &d)
{
    makeFile(d + "/travel.json", sampleNative());
    makeFile(d + "/travel.backup.json", "backup");
    makeFile(d + "/.lock", "lock");                             // 锁文件不备份
    makeFile(d + "/photos/p_a/1.jpg", "jpg1");
    makeFile(d + "/photos/p_a/子文件夹/2.jpg", "jpg2");
    makeFile(d + "/photos/.originals/p_a/1.jpg", "orig");
    makeFile(d + "/photos/.thumbs/p_a/1.jpg", "thumb");
    makeFile(d + "/photos/p_a/.thumbs/x.jpg", "thumb2");
    makeFile(d + "/.history/travel-2026-11-01.json", "history");   // 不在备份范围（Electron 也不备份）
}
}

void TestData::backupFolderName()
{
    QCOMPARE(data::Backup::folderName(fixedTime(2026, 11, 3, 1, 2, 59)), QStringLiteral("旅行计划备份-20261103-0102"));
    QCOMPARE(data::Backup::folderName(fixedTime(2026, 1, 9, 23, 45)), QStringLiteral("旅行计划备份-20260109-2345"));
}

void TestData::backupCopies()
{
    QTemporaryDir tmp;
    const QString dataDir = tmp.filePath("数据"), dest = tmp.filePath("备份盘");
    makeDataDir(dataDir);
    QList<QPair<int, int>> steps;
    const auto r = data::Backup::run(dataDir, dest, fixedTime(2026, 11, 3, 1, 2), [&steps](int d, int t) { steps << qMakePair(d, t); return true; });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(QDir::fromNativeSeparators(r.dir), dest + "/旅行计划备份-20261103-0102");
    QCOMPARE(r.files, 5);   // travel.json、travel.backup.json、两张照片、.originals 里的原图
    const QString b = dest + "/旅行计划备份-20261103-0102";
    QCOMPARE(readFile(b + "/travel.json"), sampleNative());
    QCOMPARE(readFile(b + "/travel.backup.json"), QByteArray("backup"));
    QVERIFY(!QFileInfo::exists(b + "/.lock"));
    QCOMPARE(readFile(b + "/photos/p_a/1.jpg"), QByteArray("jpg1"));
    QCOMPARE(readFile(b + "/photos/p_a/子文件夹/2.jpg"), QByteArray("jpg2"));
    QCOMPARE(readFile(b + "/photos/.originals/p_a/1.jpg"), QByteArray("orig"));
    QVERIFY(!QFileInfo::exists(b + "/photos/.thumbs"));          // 缩略图缓存不备份（任何一层）
    QVERIFY(!QFileInfo::exists(b + "/photos/p_a/.thumbs"));
    QVERIFY(!QFileInfo::exists(b + "/.history"));
    // 进度：从 0/5 到 5/5
    QCOMPARE(steps.first(), qMakePair(0, 5));
    QCOMPARE(steps.last(), qMakePair(5, 5));
    QCOMPARE(steps.size(), 6);
    // 原数据没动
    QCOMPARE(readFile(dataDir + "/photos/.thumbs/p_a/1.jpg"), QByteArray("thumb"));
}

void TestData::backupUniqueName()
{
    QTemporaryDir tmp;
    const QString dataDir = tmp.filePath("data"), dest = tmp.filePath("dest");
    makeDataDir(dataDir);
    const QDateTime t = fixedTime(2026, 11, 3, 1, 2);
    QVERIFY(data::Backup::run(dataDir, dest, t).ok);
    const auto r2 = data::Backup::run(dataDir, dest, t);
    const auto r3 = data::Backup::run(dataDir, dest, t);
    QVERIFY(r2.ok && r3.ok);
    QCOMPARE(QFileInfo(r2.dir).fileName(), QStringLiteral("旅行计划备份-20261103-0102-2"));
    QCOMPARE(QFileInfo(r3.dir).fileName(), QStringLiteral("旅行计划备份-20261103-0102-3"));
}

void TestData::backupInsideDataDir()
{
    QTemporaryDir tmp;
    const QString dataDir = tmp.filePath("data");
    makeDataDir(dataDir);
    QVERIFY(data::Backup::isInside(dataDir, dataDir));
    QVERIFY(data::Backup::isInside(dataDir + "/photos/备份", dataDir));
    QVERIFY(!data::Backup::isInside(tmp.filePath("data2"), dataDir));   // 名字前缀相同但不是里面
    QVERIFY(!data::Backup::isInside(tmp.path(), dataDir));
    const auto r = data::Backup::run(dataDir, dataDir + "/备份", fixedTime(2026, 11, 3));
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QStringLiteral("备份文件夹不能放在数据文件夹里面"));
    QVERIFY(!QFileInfo::exists(dataDir + "/备份"));
    QVERIFY(!data::Backup::run(QString(), tmp.filePath("x"), fixedTime(2026, 11, 3)).ok);
    QVERIFY(!data::Backup::run(dataDir, QString(), fixedTime(2026, 11, 3)).ok);
}

void TestData::backupCancel()
{
    QTemporaryDir tmp;
    const QString dataDir = tmp.filePath("data"), dest = tmp.filePath("dest");
    makeDataDir(dataDir);
    makeFile(dest + "/别的东西.txt", "keep");
    const auto r = data::Backup::run(dataDir, dest, fixedTime(2026, 11, 3, 1, 2), [](int done, int) { return done < 2; });
    QVERIFY(!r.ok);
    QVERIFY(r.cancelled);
    QVERIFY(!QFileInfo::exists(dest + "/旅行计划备份-20261103-0102"));   // 没做完的备份文件夹删掉
    QCOMPARE(readFile(dest + "/别的东西.txt"), QByteArray("keep"));      // 别的东西不碰
    QCOMPARE(readFile(dataDir + "/travel.json"), sampleNative());
}

void TestData::backupOnlyDataFile()
{
    QTemporaryDir tmp;
    const QString dataDir = tmp.filePath("data"), dest = tmp.filePath("dest");
    makeFile(dataDir + "/travel.json", sampleNative());   // 没有 travel.backup.json、没有 photos
    makeFile(dataDir + "/data.js", sampleData());         // Electron 的文件不在备份范围
    const auto r = data::Backup::run(dataDir, dest, fixedTime(2026, 11, 3));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.files, 1);
    QCOMPARE(data::Backup::collect(dataDir), QStringList { "travel.json" });
}

QTEST_GUILESS_MAIN(TestData)
#include "tst_data.moc"
