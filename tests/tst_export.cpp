// 导出（0.14.0）：.ics 和 Electron 原样代码逐字节对照（golden/ics.json，tools/claude-kit/ics-golden/gen.js 生成）、转义、折行、CRLF、UID；
// 行程单分页"每站不跨页"、长图高度上限、PDF 能写出来。界面语言是中文（没装词典，tp::tr 原样返回）。
#include "ics.h"
#include "itinerary.h"
#include "jsonio.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTest>

using namespace tp;

namespace {
json::Value parseJson(const QByteArray &b) { return json::parse(QString::fromUtf8(b)); }
json::Value planWithStops(int n, int schedPerStop)
{
    QString s = QStringLiteral("{\"id\":\"p\",\"name\":\"测试\",\"type\":\"route\",\"startDate\":\"2026-01-01\",\"endDate\":\"2026-03-01\",\"stops\":[");
    for (int i = 0; i < n; ++i) {
        if (i) s += u',';
        s += QStringLiteral("{\"id\":\"s%1\",\"name\":\"站点 %1\",\"lat\":%2,\"lng\":%3,\"order\":%1,\"arriveDate\":\"2026-01-02\",\"schedule\":[").arg(i).arg(30 + i * 0.1).arg(120 + i * 0.1);
        for (int j = 0; j < schedPerStop; ++j) s += (j ? QStringLiteral(",") : QString()) + QStringLiteral("{\"date\":\"2026-01-02\",\"time\":\"09:00\",\"text\":\"第 %1 条日程，写得长一点看看会不会换行，再长一点再长一点再长一点再长一点再长一点再长一点再长一点\"}").arg(j);
        s += QStringLiteral("]}");
    }
    s += QStringLiteral("]}");
    return json::parse(s);
}
}

class TestExport : public QObject
{
    Q_OBJECT
private slots:
    void icsGolden()
    {
        QFile f(QStringLiteral(TP_SOURCE_DIR "/tests/golden/ics.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject g = QJsonDocument::fromJson(f.readAll()).object();
        const json::Value plan = parseJson(QJsonDocument(g.value(QStringLiteral("plan")).toObject()).toJson(QJsonDocument::Compact));
        const auto r = exporter::ics(plan, QDateTime::fromString(g.value(QStringLiteral("now")).toString(), Qt::ISODate));
        QCOMPARE(r.count, g.value(QStringLiteral("count")).toInt());
        const QString want = g.value(QStringLiteral("text")).toString();
        const QString got = QString::fromUtf8(r.text);
        if (got != want) {   // 指出第一处不同
            int i = 0;
            while (i < got.size() && i < want.size() && got[i] == want[i]) ++i;
            QFAIL(qPrintable(QStringLiteral("第 %1 个字不同：得到「%2」，应为「%3」").arg(i).arg(got.mid(std::max(0, i - 20), 60), want.mid(std::max(0, i - 20), 60))));
        }
    }
    void icsRules()
    {
        QCOMPARE(exporter::icsEscape(QStringLiteral("a\\b;c,d\ne")), QStringLiteral("a\\\\b\\;c\\,d\\ne"));
        // 75 字节折行：每行（含续行开头的空格）不超过 75 个字节，拼回去和原文一样，不切开一个字（含四字节的表情）
        QString line = QStringLiteral("DESCRIPTION:");
        for (int i = 0; i < 40; ++i) line += (i % 3 == 0) ? QStringLiteral("😀") : (i % 3 == 1 ? QStringLiteral("中") : QStringLiteral("a"));
        const QString folded = exporter::icsFold(line);
        const QStringList parts = folded.split(QStringLiteral("\r\n"));
        QVERIFY(parts.size() > 1);
        QString joined;
        for (int i = 0; i < parts.size(); ++i) {
            QVERIFY2(parts[i].toUtf8().size() <= 75, qPrintable(parts[i]));
            if (i) QVERIFY(parts[i].startsWith(u' '));
            joined += i ? parts[i].mid(1) : parts[i];
        }
        QCOMPARE(joined, line);
        QCOMPARE(exporter::icsFold(QStringLiteral("SHORT")), QStringLiteral("SHORT"));
        // CRLF、UID、跨过半夜的日程（Electron 会写成结束早于开始，这里日期跟着变）
        const json::Value p = json::parse(QStringLiteral("{\"id\":\"x\",\"name\":\"n\",\"stops\":[{\"id\":\"s\",\"name\":\"A\",\"lat\":1,\"lng\":2,\"schedule\":[{\"date\":\"2026-01-01\",\"time\":\"23:30\",\"text\":\"夜车\"}]}]}"));
        const auto r = exporter::ics(p, QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC));
        QCOMPARE(r.count, 1);
        const QString t = QString::fromUtf8(r.text);
        QVERIFY(t.endsWith(QStringLiteral("END:VCALENDAR\r\n")));
        QVERIFY(!t.contains(QRegularExpression(QStringLiteral("[^\\r]\\n"))));
        QVERIFY(t.contains(QStringLiteral("UID:x-s-0@travelplanner\r\n")));
        QVERIFY(t.contains(QStringLiteral("DTSTART:20260101T233000\r\nDTEND:20260102T003000\r\n")));
        QCOMPARE(exporter::ics(json::parse(QStringLiteral("{\"id\":\"e\"}")), QDateTime::currentDateTimeUtc()).count, 0);
    }
    void paginateKeepsStops()
    {
        // 纯分页：一组不超过一页时不拆开；小标题跟着下一组；比一页还高的组从新的一页开始
        QVector<exporter::BlockSize> b{ { 100, -1, false }, { 30, -1, true } };
        for (int g = 0; g < 30; ++g) for (int k = 0; k < 1 + g % 5; ++k) b.push_back({ 40.0 + (g * 7 % 30), g, false });
        b.push_back({ 30, -1, true });
        for (int k = 0; k < 40; ++k) b.push_back({ 50, 99, false });   // 2000 高的一组 > 一页
        const double H = 1000;
        const auto pages = exporter::paginate(b, H);
        QHash<int, int> pageOfGroup;
        int count = 0;
        for (int pi = 0; pi < pages.size(); ++pi) {
            double h = 0;
            for (int k : pages[pi]) {
                h += b[k].h;
                ++count;
                if (b[k].group >= 0 && b[k].group != 99) {
                    if (pageOfGroup.contains(b[k].group)) QVERIFY2(pageOfGroup[b[k].group] == pi, qPrintable(QStringLiteral("第 %1 组跨页了").arg(b[k].group)));
                    pageOfGroup[b[k].group] = pi;
                }
                if (b[k].keepNext) QVERIFY2(k != pages[pi].last(), "小标题落在页尾");
            }
            QVERIFY(h <= H + 0.01);
        }
        QCOMPARE(count, int(b.size()));
    }
    void itineraryPages()
    {
        // 真排版：40 站、每站 6 条日程 → 多页；每站的块都在同一页
        const json::Value root = json::parse(QStringLiteral("{\"plans\":[]}"));
        const json::Value plan = planWithStops(40, 6);
        exporter::Options o;
        o.today = QStringLiteral("2026-11-08");
        const exporter::Doc d = exporter::Doc::itinerary(root, plan, o);
        QVERIFY(d.pageCount() > 2);
        const auto sizes = d.sizes();
        QHash<int, int> pageOf;
        for (int pi = 0; pi < d.pageCount(); ++pi)
            for (int k : d.pages()[pi])
                if (sizes[k].group >= 0) {
                    if (pageOf.contains(sizes[k].group)) QCOMPARE(pageOf[sizes[k].group], pi);
                    pageOf[sizes[k].group] = pi;
                }
        QCOMPARE(pageOf.size(), 40);
        const QImage first = d.renderPage(0, 420);
        QCOMPARE(first.width(), 420);
        QVERIFY(first.height() > 580);
        QTemporaryDir tmp;
        QString err;
        QVERIFY2(d.writePdf(tmp.filePath(QStringLiteral("a.pdf")), &err), qPrintable(err));
        QFile pdf(tmp.filePath(QStringLiteral("a.pdf")));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const QByteArray bytes = pdf.readAll();
        QVERIFY(bytes.startsWith("%PDF"));
        QCOMPARE(int(bytes.count("/Type /Page\n") + bytes.count("/Type /Page\r") + bytes.count("/Type /Page ") + bytes.count("/Type/Page")) >= d.pageCount(), true);
    }
    void longImageCap()
    {
        const json::Value root = json::parse(QStringLiteral("{\"plans\":[]}"));
        exporter::Options o;
        bool cut = true;
        const QImage small = exporter::Doc::itinerary(root, planWithStops(3, 2), o).longImage(&cut);
        QCOMPARE(small.width(), int(exporter::kPngW));
        QVERIFY(!cut);
        QVERIFY(small.height() < 2000);
        const QImage big = exporter::Doc::itinerary(root, planWithStops(200, 8), o).longImage(&cut);
        QVERIFY(cut);
        QVERIFY(big.height() <= exporter::kPngMaxH);
        QVERIFY(big.height() > exporter::kPngMaxH - 400);
    }
    void optionsChangeOutput()
    {
        // 勾选订单号 / 消费 / 紧急信息会多出内容（块变多、变高）
        const json::Value root = json::parse(QStringLiteral("{\"plans\":[],\"emergency\":{\"insurer\":\"某保险\",\"policyNo\":\"P-1\",\"contacts\":[{\"name\":\"妈妈\",\"phone\":\"+86 1\"}]}}"));
        const json::Value plan = json::parse(QStringLiteral(
            "{\"id\":\"p\",\"name\":\"n\",\"stops\":[{\"name\":\"A\",\"lat\":1,\"lng\":2}],\"bookings\":[{\"id\":\"b\",\"kind\":\"flight\",\"title\":\"CA1\",\"start\":\"2026-01-01T09:00\",\"ref\":\"XYZ\",\"cost\":100}],"
            "\"expenses\":[{\"id\":\"e\",\"date\":\"2026-01-01\",\"cat\":\"food\",\"title\":\"拉面\",\"amount\":12.5,\"currency\":\"JPY\"}],\"localEmergency\":[{\"label\":\"报警\",\"phone\":\"110\"}]}"));
        exporter::Options a, b;
        b.refs = b.money = b.emergency = true;
        const auto total = [](const exporter::Doc &d) { double h = 0; for (const auto &s : d.sizes()) h += s.h; return h; };
        const exporter::Doc da = exporter::Doc::itinerary(root, plan, a), db = exporter::Doc::itinerary(root, plan, b);
        QVERIFY(db.sizes().size() > da.sizes().size() + 5);
        QVERIFY(total(db) > total(da) + 100);
        QVERIFY(exporter::Doc::card(root, plan, a).sizes().size() >= 7);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestExport t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_export.moc"
