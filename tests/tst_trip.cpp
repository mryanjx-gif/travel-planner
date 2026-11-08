// 0.14.1 .trip 计划文件、分享版、导入时放进现有数据（planmerge）、从 Electron 再导入逐个计划选。
// 会写文件的都在临时文件夹里做。Electron 那边的读写：tests/golden/electron-sample.trip 由 tools/claude-kit/trip-golden/gen.js
// 用 Electron 自己的 trip-pack.js 生成；原生版导出的用 check.js 让 Electron 的 unpack 读（设 TP_TRIP_KEEP=文件夹 时这里留一份）。
#include "electronimport.h"
#include "jsonio.h"
#include "nativeformat.h"
#include "planmerge.h"
#include "trippack.h"

#include "miniz.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace tp;
using json::Value;
namespace trip = exporter::trip;
namespace pm = data::planmerge;

namespace {

// 夹具里的 JSON 用单引号写（moc 读不了带 )" 的原始字符串），这里换回双引号
QByteArray Q_(const char *s) { return QByteArray(s).replace('\'', '"'); }
Value J(const QByteArray &s) { return json::parse(QString::fromUtf8(s)); }
QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }

void put(const QString &path, const QByteArray &b)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(b);
}
QByteArray get(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// 原生数据：一个计划带站点（leg 里有个多余的键）、封面、攻略和游记里的照片、站点照片（带拍摄信息）、计划级照片、不认识的字段
Value sampleDoc()
{
    return J(Q_("{'format':'travelplanner','formatVersion':1,'savedBy':'0.14.0','savedAt':'x',"
        "      'plans':[{'id':'p1','name':'北海道','type':'route','status':'planning','currency':'','budget':3000,'cover':'ph_c','mood':'晴',"
        "        'guide':'开头\\n![雪](photo:ph_g)\\n结尾','createdAt':1,'updatedAt':2,"
        "        'stops':[{'id':'s1','name':'札幌','lat':43.06,'lng':141.35,'leg':{'mode':'drive','path':'abc','dist':5,'time':6,'key':'k','extra':1},'schedule':[{'text':'拉面'}],'foo':'bar'},"
        "                 {'id':'s2','name':'小樽','lat':43.19,'lng':141.0}],"
        "        'expenses':[{'id':'e1','title':'拉面','amount':1000,'paidBy':'u1'}],'people':[{'id':'u1','name':'小明'}],"
        "        'bookings':[{'id':'b1','kind':'hotel','title':'酒店','ref':'R123','cost':500,'notes':'私人备注'}],"
        "        'localEmergency':[{'label':'报警','phone':'110'}],'todos':[{'id':'t','text':'办签证'}]},"
        "       {'id':'p2','name':'别的','stops':[]}],"
        "      'photos':[{'id':'ph_s','file':'photos/2026-01/ph_s_a.jpg','plan':'p1','stop':'s1','caption':'街','fav':true,'takenAt':'2026-01-02T10:00','lat':43.0,'lng':141.0,'camera':'X','exposure':{'f':2}},"
        "                {'id':'ph_c','file':'photos/2026-01/ph_c_cover.jpg','plan':'p1','stop':'','caption':'','fav':false,'credit':{'src':'wikimedia','title':'Snow','author':'A','license':'CC BY 4.0','url':'https://w'}},"
        "                {'id':'ph_g','file':'photos/2026-01/ph_g_g.jpg','plan':'p1','stop':'','caption':'','fav':false,'credit':{'src':'google'}},"
        "                {'id':'ph_o','file':'photos/2026-01/ph_o_o.jpg','plan':'p2','stop':'','caption':'','fav':false}],"
        "      'journal':[{'id':'j1','plan':'p1','stop':'s1','title':'第一天','date':'2026-01-02','body':'好冷\\n![](photo:ph_s)'}],"
        "      'documents':[{'id':'d1','kind':'passport','number':'E123'}],'visitedRegions':[{'id':'JP-01'}],"
        "      'emergency':{'insurer':'保险','contacts':[{'name':'妈'}]},'nextTripId':'p2'}"));
}

void writePhotos(const QString &dir, const Value &doc)
{
    for (const Value &p : doc[QStringLiteral("photos")].asArray()) put(QDir(dir).filePath(str(p, "file")), ("IMG:" + str(p, "id")).toUtf8());
}

QString uidSeq(const QString &prefix)
{
    static int n = 0;
    return QStringLiteral("%1_t%2").arg(prefix).arg(++n);
}
pm::Options opts()
{
    pm::Options o;
    o.uid = uidSeq;
    o.copySuffix = QStringLiteral("（导入）");
    o.defaultCurrency = QStringLiteral("AUD");
    return o;
}

const Value *findById(const Value &arr, const QString &id)
{
    for (const Value &v : arr.asArray()) if (str(v, "id") == id) return &v;
    return nullptr;
}

// 读一个 .trip，转原生，放进 existing，照片写进 dataDir
pm::Result importInto(const QString &tripFile, const Value &existing, const QHash<QString, pm::Mode> &modes, const QString &dataDir,
                      pm::WriteResult *wr = nullptr)
{
    trip::Reader r;
    if (!r.open(tripFile)) { qWarning("%s", qUtf8Printable(r.error())); return {}; }
    QHash<QString, QString> srcOf;
    const Value inc = trip::toNative(r.manifest(), r.files(), QDateTime::currentDateTime(), &srcOf);
    pm::Result res = pm::apply(existing, inc, modes, opts());
    const pm::WriteResult w = pm::writeFiles(res.doc, dataDir, res.files, [&](const QString &from, QByteArray *b) { return srcOf.contains(from) && r.read(srcOf.value(from), b); });
    if (wr) *wr = w;
    return res;
}

} // namespace

class TestTrip : public QObject
{
    Q_OBJECT
private slots:
    void safeRel()
    {
        QVERIFY(trip::safeRel(QStringLiteral("photos/2026-01/a.jpg")));
        for (const char *bad : { "../a.jpg", "photos/../a.jpg", "photos/a/../../b", "photos\\a.jpg", "C:/photos/a.jpg", "/photos/a.jpg",
                                 "photos//a.jpg", "photos/./a.jpg", "plan.json", "photos/", "photos/a:b.jpg" })
            QVERIFY2(!trip::safeRel(QString::fromLatin1(bad)), bad);
    }

    // 往返：导出 → 读回 → 放进空数据，计划、照片记录、游记和原来一样（含不认识的字段、拍摄信息、计划级照片），照片文件内容一样
    void roundTrip()
    {
        QTemporaryDir a, b;
        const Value doc = sampleDoc();
        writePhotos(a.path(), doc);
        const trip::Built built = trip::build(doc, { QStringLiteral("p1") }, QStringLiteral("JPY"), QStringLiteral("0.14.1"), QDateTime::currentDateTime());
        QCOMPARE(built.plans, 1);
        QCOMPARE(built.manifest[QStringLiteral("formatVersion")].toNumber(), 1.0);
        QCOMPARE(built.photos.size(), 3);   // p2 的照片不带
        const QString file = a.filePath(QStringLiteral("out.trip"));
        const trip::WriteResult w = trip::write(file, built.manifest, built.photos, a.path());
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(w.photos, 3);
        if (!qEnvironmentVariableIsEmpty("TP_TRIP_KEEP")) QFile::copy(file, QDir(qEnvironmentVariable("TP_TRIP_KEEP")).filePath(QStringLiteral("native-sample.trip")));

        // Electron 部分：order、leg 拆平、照片对象、coverPhoto、正文里是路径
        const Value &ep = built.manifest[QStringLiteral("plan")];
        QCOMPARE(str(ep, "coverPhoto"), QStringLiteral("photos/2026-01/ph_c_cover.jpg"));
        QVERIFY(!ep.asObject().contains(QStringLiteral("cover")));
        QCOMPARE(str(ep[QStringLiteral("stops")].asArray()[0], "legMode"), QStringLiteral("drive"));
        QCOMPARE(ep[QStringLiteral("stops")].asArray()[1][QStringLiteral("order")].toNumber(), 1.0);
        QCOMPARE(str(ep[QStringLiteral("stops")].asArray()[0][QStringLiteral("photos")].asArray()[0], "path"), QStringLiteral("photos/2026-01/ph_s_a.jpg"));
        QVERIFY(str(ep, "guide").contains(QStringLiteral("](photos/2026-01/ph_g_g.jpg)")));
        QCOMPARE(str(ep, "currency"), QStringLiteral("JPY"));   // 有预算没币种：写上主币种
        QCOMPARE(str(built.manifest[QStringLiteral("journal")].asArray()[0], "planId"), QStringLiteral("p1"));

        QHash<QString, pm::Mode> none;
        pm::WriteResult wr;
        const pm::Result res = importInto(file, data::nativefmt::emptyDoc(QStringLiteral("0.14.1"), QDateTime::currentDateTime()), none, b.path(), &wr);
        QVERIFY(wr.ok);
        QCOMPARE(wr.written, 3);
        QCOMPARE(res.plans, 1);
        Value want = doc[QStringLiteral("plans")].asArray()[0];
        want.mutObject().set(QStringLiteral("currency"), Value(QStringLiteral("JPY")));
        QCOMPARE(json::stringify(res.doc[QStringLiteral("plans")].asArray()[0]), json::stringify(want));
        for (const char *id : { "ph_s", "ph_c", "ph_g" })
            QCOMPARE(json::stringify(*findById(res.doc[QStringLiteral("photos")], QString::fromLatin1(id))),
                     json::stringify(*findById(doc[QStringLiteral("photos")], QString::fromLatin1(id))));
        QCOMPARE(json::stringify(res.doc[QStringLiteral("journal")]), json::stringify(doc[QStringLiteral("journal")]));
        for (const char *id : { "ph_s", "ph_c", "ph_g" }) {
            const QString f = str(*findById(doc[QStringLiteral("photos")], QString::fromLatin1(id)), "file");
            QCOMPARE(get(QDir(b.path()).filePath(f)), get(QDir(a.path()).filePath(f)));
        }
        // 多个计划合并成一个：formatVersion 2
        const trip::Built two = trip::build(doc, { QStringLiteral("p1"), QStringLiteral("p2") }, QString(), QStringLiteral("0.14.1"), QDateTime::currentDateTime());
        QCOMPARE(two.manifest[QStringLiteral("formatVersion")].toNumber(), 2.0);
        QCOMPARE(int(two.manifest[QStringLiteral("plans")].asArray().size()), 2);
    }

    // Electron 导出的 .trip（用 Electron 自己的 trip-pack.js 打的包）能读：站点按 order 排、legMode 合成 leg、封面和正文换成照片 id、署名、不认识的字段保留
    void electronTrip()
    {
        QTemporaryDir d;
        pm::WriteResult wr;
        const pm::Result res = importInto(QStringLiteral(TP_SOURCE_DIR "/tests/golden/electron-sample.trip"),
                                          data::nativefmt::emptyDoc(QStringLiteral("0.14.1"), QDateTime::currentDateTime()), {}, d.path(), &wr);
        QCOMPARE(res.plans, 1);
        QVERIFY(wr.ok);
        QCOMPARE(wr.written, 3);
        const Value &p = res.doc[QStringLiteral("plans")].asArray()[0];
        QCOMPARE(str(p, "id"), QStringLiteral("p_el1"));
        QCOMPARE(str(p, "mood"), QStringLiteral("开心"));
        QCOMPARE(str(p[QStringLiteral("stops")].asArray()[0], "name"), QStringLiteral("京都站"));
        QCOMPARE(str(p[QStringLiteral("stops")].asArray()[0][QStringLiteral("leg")], "mode"), QStringLiteral("train"));
        const QString cover = str(p, "cover");
        const Value *c = findById(res.doc[QStringLiteral("photos")], cover);
        QVERIFY(c);
        QCOMPARE(get(QDir(d.path()).filePath(str(*c, "file"))).mid(4, 16), QByteArray("test photo cover"));
        QVERIFY(str(p, "guide").contains(QStringLiteral("](photo:")));
        bool credited = false;
        for (const Value &ph : res.doc[QStringLiteral("photos")].asArray()) credited = credited || str(ph[QStringLiteral("credit")], "src") == QLatin1String("wikimedia");
        QVERIFY(credited);
        const Value *st = findById(res.doc[QStringLiteral("photos")], QStringLiteral("ph_a"));
        QVERIFY(st);
        QCOMPARE(str(*st, "stop"), QStringLiteral("s1"));
        QCOMPARE(str(*st, "caption"), QStringLiteral("站台"));
        QCOMPARE(str(res.doc[QStringLiteral("journal")].asArray()[0], "plan"), QStringLiteral("p_el1"));
        QVERIFY(str(res.doc[QStringLiteral("journal")].asArray()[0], "body").contains(QStringLiteral("](photo:ph_a)")));
    }

    // 撞了已有的计划：覆盖 / 另存一份 / 只合并消费和照片 / 跳过；照片同名不同内容改 -2
    void conflicts()
    {
        QTemporaryDir a, b;
        const Value doc = sampleDoc();
        writePhotos(a.path(), doc);
        // 导出的是"对方改过的"：名字不同、多一笔消费、多一个同名的同行人（不同 id）
        Value other = doc;
        {
            Value &p = other.mutObject().find(QStringLiteral("plans"))->mutArray()[0];
            p.mutObject().set(QStringLiteral("name"), Value(QStringLiteral("北海道（对方）")));
            p.mutObject().set(QStringLiteral("expenses"), J(Q_("[{'id':'e1','title':'拉面','amount':1000,'paidBy':'u1'},{'id':'e2','title':'螃蟹','amount':9000,'paidBy':'u9','split':[{'who':'u9'}]}]")));
            p.mutObject().set(QStringLiteral("people"), J(Q_("[{'id':'u9','name':'小明'},{'id':'u8','name':'小红'}]")));
        }
        const trip::Built built = trip::build(other, { QStringLiteral("p1") }, QString(), QStringLiteral("0.14.1"), QDateTime::currentDateTime());
        const QString file = a.filePath(QStringLiteral("other.trip"));
        QVERIFY(trip::write(file, built.manifest, built.photos, a.path()).ok);
        // 本地数据：同样的照片已经在 b 里；其中封面的文件内容不一样（同名不同内容）
        writePhotos(b.path(), doc);
        put(QDir(b.path()).filePath(QStringLiteral("photos/2026-01/ph_c_cover.jpg")), "DIFFERENT");

        // 跳过（不写 = 本地有就跳过）
        QCOMPARE(importInto(file, doc, {}, b.path()).plans, 0);

        // 覆盖
        {
            pm::WriteResult wr;
            const pm::Result r = importInto(file, doc, { { QStringLiteral("p1"), pm::Mode::Replace } }, b.path(), &wr);
            QCOMPARE(r.plans, 1);
            QCOMPARE(str(r.doc[QStringLiteral("plans")].asArray()[0], "name"), QStringLiteral("北海道（对方）"));
            QCOMPARE(int(r.doc[QStringLiteral("plans")].asArray().size()), 2);
            QCOMPARE(wr.same, 2);      // 内容一样的直接用
            QCOMPARE(wr.written, 1);   // 封面同名不同内容：另起名字
            const Value *c = findById(r.doc[QStringLiteral("photos")], QStringLiteral("ph_c"));
            QCOMPARE(str(*c, "file"), QStringLiteral("photos/2026-01/ph_c_cover-2.jpg"));
            QCOMPARE(get(QDir(b.path()).filePath(QStringLiteral("photos/2026-01/ph_c_cover.jpg"))), QByteArray("DIFFERENT"));   // 原来的不动
            int p1photos = 0;
            for (const Value &ph : r.doc[QStringLiteral("photos")].asArray()) p1photos += str(ph, "plan") == QLatin1String("p1");
            QCOMPARE(p1photos, 3);
            QFile::remove(QDir(b.path()).filePath(QStringLiteral("photos/2026-01/ph_c_cover-2.jpg")));
        }
        // 另存一份：新 id、名字加（导入）、照片新 id 新文件、攻略 / 封面 / 游记跟着换
        {
            pm::WriteResult wr;
            const pm::Result r = importInto(file, doc, { { QStringLiteral("p1"), pm::Mode::Copy } }, b.path(), &wr);
            QCOMPARE(r.plans, 1);
            const json::Array &plans = r.doc[QStringLiteral("plans")].asArray();
            QCOMPARE(int(plans.size()), 3);
            const Value &cp = plans[2];
            QVERIFY(str(cp, "id") != QLatin1String("p1"));
            QCOMPARE(str(cp, "name"), QStringLiteral("北海道（对方）（导入）"));
            QCOMPARE(json::stringify(plans[0]), json::stringify(doc[QStringLiteral("plans")].asArray()[0]));   // 原来的不变
            QVERIFY(str(cp, "cover") != QLatin1String("ph_c"));
            const Value *cover = findById(r.doc[QStringLiteral("photos")], str(cp, "cover"));
            QVERIFY(cover);
            QCOMPARE(str(*cover, "plan"), str(cp, "id"));
            QVERIFY(!str(cp, "guide").contains(QStringLiteral("photo:ph_g")));
            QCOMPARE(wr.written, 3);   // 另存一份不和原来的共用文件
            QCOMPARE(wr.same, 0);
            const Value &j = r.doc[QStringLiteral("journal")].asArray()[1];
            QCOMPARE(str(j, "plan"), str(cp, "id"));
            QVERIFY(str(j, "id") != QLatin1String("j1"));
            QVERIFY(!str(j, "body").contains(QStringLiteral("photo:ph_s")));
        }
        // 只合并消费和照片
        {
            const pm::Result r = importInto(file, doc, { { QStringLiteral("p1"), pm::Mode::Merge } }, b.path());
            const Value &p = r.doc[QStringLiteral("plans")].asArray()[0];
            QCOMPARE(str(p, "name"), QStringLiteral("北海道"));   // 名字不动
            QCOMPARE(int(p[QStringLiteral("expenses")].asArray().size()), 2);
            const Value &e2 = p[QStringLiteral("expenses")].asArray()[1];
            QCOMPARE(str(e2, "paidBy"), QStringLiteral("u1"));   // 同名的同行人算一个
            QCOMPARE(str(e2[QStringLiteral("split")].asArray()[0], "who"), QStringLiteral("u1"));
            QCOMPARE(str(e2, "currency"), QStringLiteral("AUD"));
            QCOMPARE(int(p[QStringLiteral("people")].asArray().size()), 2);   // 小红加进来
            QCOMPARE(int(r.doc[QStringLiteral("photos")].asArray().size()), 4);   // 照片都已经有了，不重复
            QCOMPARE(int(r.doc[QStringLiteral("journal")].asArray().size()), 1);
            QCOMPARE(r.messages.value(0), QStringLiteral("已合并：新加 1 笔消费"));
        }
    }

    // 照片 id 撞了别的计划的照片：换新 id，引用跟着换
    void photoIdCollision()
    {
        const Value inc = J(Q_("{'plans':[{'id':'pn','name':'新','cover':'ph_o','guide':'![](photo:ph_o)'}],"
        "          'photos':[{'id':'ph_o','file':'photos/x/a.jpg','plan':'pn','stop':''}],'journal':[]}"));
        const pm::Result r = pm::apply(sampleDoc(), inc, {}, opts());
        const Value &p = r.doc[QStringLiteral("plans")].asArray()[2];
        QVERIFY(str(p, "cover") != QLatin1String("ph_o"));
        QCOMPARE(str(p, "guide"), QStringLiteral("![](photo:%1)").arg(str(p, "cover")));
        QCOMPARE(str(*findById(r.doc[QStringLiteral("photos")], QStringLiteral("ph_o")), "plan"), QStringLiteral("p2"));   // 原来那张不动
    }

    // zip slip、条目太多、解压后太大、不是计划文件
    void unsafePackages()
    {
        QTemporaryDir d;
        const auto makeZip = [&](const QString &name, const QList<QPair<QByteArray, QByteArray>> &entries) {
            const QString path = d.filePath(name);
            mz_zip_archive z;
            std::memset(&z, 0, sizeof z);
            mz_zip_writer_init_file(&z, path.toUtf8().constData(), 0);
            for (const auto &e : entries) mz_zip_writer_add_mem(&z, e.first.constData(), e.second.constData(), size_t(e.second.size()), 6);
            mz_zip_writer_finalize_archive(&z);
            mz_zip_writer_end(&z);
            return path;
        };
        // native 里把照片路径写成 ../ 想写到数据文件夹外面：包里的这种条目不认，写照片时也跳过
        const QByteArray manifest = Q_("{'format':'travel-planner.trip','formatVersion':1,'plan':{'id':'px','name':'坏'},"
        "            'native':{'plans':[{'id':'px','name':'坏'}],'photos':[{'id':'e1','file':'../evil.jpg','plan':'px'},{'id':'e2','file':'photos/../../evil2.jpg','plan':'px'},{'id':'e3','file':'photos/ok/a.jpg','plan':'px'}],'journal':[]}}");
        const QString slip = makeZip(QStringLiteral("slip.trip"), { { "plan.json", manifest }, { "../evil.jpg", "x" }, { "photos/../../evil2.jpg", "x" },
                                                                    { "photos\\win.jpg", "x" }, { "photos/ok/a.jpg", "fine" } });
        QTemporaryDir data;
        trip::Reader r;
        QVERIFY(r.open(slip));
        QCOMPARE(r.files(), QStringList{ QStringLiteral("photos/ok/a.jpg") });
        pm::WriteResult wr;
        const pm::Result res = importInto(slip, data::nativefmt::emptyDoc(QStringLiteral("0.14.1"), QDateTime::currentDateTime()), {}, data.path(), &wr);
        QCOMPARE(res.plans, 1);
        QCOMPARE(wr.written, 1);
        QVERIFY(!QFile::exists(QDir(data.path()).filePath(QStringLiteral("../evil.jpg"))));
        QVERIFY(!QFile::exists(QDir(data.path()).filePath(QStringLiteral("../../evil2.jpg"))));
        QVERIFY(QFile::exists(QDir(data.path()).filePath(QStringLiteral("photos/ok/a.jpg"))));

        // 条目太多
        QList<QPair<QByteArray, QByteArray>> many{ { "plan.json", manifest } };
        for (int i = 0; i < trip::kMaxEntries; ++i) many.append({ QByteArray("photos/a/") + QByteArray::number(i), "x" });
        QVERIFY(!r.open(makeZip(QStringLiteral("many.trip"), many)));
        QVERIFY2(r.error().contains(QStringLiteral("条目太多")), qPrintable(r.error()));

        // 解压后太大：把中央目录里一条的"解压后大小"改成 3 GB
        const QString big = makeZip(QStringLiteral("big.trip"), { { "plan.json", manifest }, { "photos/a/b.jpg", QByteArray(1000, 'z') } });
        {
            QByteArray z = get(big);
            const int cd = int(z.lastIndexOf(QByteArray("PK\x01\x02", 4)));
            QVERIFY(cd > 0);
            const quint32 huge = 3000000000u;
            std::memcpy(z.data() + cd + 24, &huge, 4);
            put(big, z);
        }
        QVERIFY(!r.open(big));
        QVERIFY2(r.error().contains(QStringLiteral("太大")) || r.error().contains(QStringLiteral("zip")), qPrintable(r.error()));

        // 不是旅行计划的文件、缺 plan.json、更新版本
        QVERIFY(!r.open(makeZip(QStringLiteral("x1.trip"), { { "a.txt", "x" } })));
        QVERIFY(r.error().contains(QStringLiteral("plan.json")));
        QVERIFY(!r.open(makeZip(QStringLiteral("x2.trip"), { { "plan.json", Q_("{'format':'other'}") } })));
        QVERIFY(!r.open(makeZip(QStringLiteral("x3.trip"), { { "plan.json", Q_("{'format':'travel-planner.trip','formatVersion':3,'plan':{'id':'a'}}") } })));
        QVERIFY(r.error().contains(QStringLiteral("更新版本")));
        put(d.filePath(QStringLiteral("junk.trip")), "not a zip");
        QVERIFY(!r.open(d.filePath(QStringLiteral("junk.trip"))));
    }

    // 分享版：私人内容去掉、只带可分发来源的照片、署名文件 BOM + CRLF、不能导出到数据文件夹里
    void shareFolder()
    {
        QTemporaryDir data, dest;
        const Value doc = sampleDoc();
        writePhotos(data.path(), doc);
        trip::ShareOptions o;   // 默认：去掉消费和订单号，只带封面
        const trip::Shared s = trip::share(doc, {}, o, QStringLiteral("AUD"), QDateTime::currentDateTime());
        const QString all = json::stringify(s.doc);
        for (const char *gone : { "\"emergency\"", "\"documents\"", "localEmergency", "\"expenses\"", "\"budget\"", "R123", "私人备注", "E123" })
            QVERIFY2(!all.contains(QString::fromUtf8(gone)), gone);
        QVERIFY(all.contains(QStringLiteral("办签证")));   // 待办没勾就留着
        QCOMPARE(s.photos, QStringList{ QStringLiteral("photos/2026-01/ph_c_cover.jpg") });   // 只有封面（wikimedia）
        QCOMPARE(s.skipped, 3);
        QVERIFY(!str(s.doc[QStringLiteral("plans")].asArray()[0], "guide").contains(QStringLiteral("ph_g")));   // google 的图那一行去掉
        QVERIFY(!str(s.doc[QStringLiteral("journal")].asArray()[0], "body").contains(QStringLiteral("ph_s")));
        QVERIFY(s.credits.contains(QStringLiteral("CC BY 4.0")));

        // 全部可分发的 + 没有来源的站点照片也带：站点照片带上，google 的仍不带
        o.allPhotos = o.unknownStops = true;
        o.noTodo = o.noJournal = o.noVisited = true;
        const trip::Shared s2 = trip::share(doc, { QStringLiteral("p1") }, o, QString(), QDateTime::currentDateTime());
        QCOMPARE(s2.photos.size(), 2);
        const QString all2 = json::stringify(s2.doc);
        for (const char *gone : { "办签证", "visitedRegions", "nextTripId", "\"p2\"" }) QVERIFY2(!all2.contains(QString::fromUtf8(gone)), gone);
        QVERIFY(s2.doc[QStringLiteral("journal")].asArray().empty());

        QVERIFY(!trip::writeShare(QDir(data.path()).filePath(QStringLiteral("sub")), QString(), s, data.path(), QStringLiteral("0.14.1"), QDateTime::currentDateTime()).ok);
        const trip::ShareWrite w = trip::writeShare(dest.path(), QString(), s, data.path(), QStringLiteral("0.14.1"), QDateTime::currentDateTime());
        QVERIFY2(w.ok, qPrintable(w.error));
        QCOMPARE(w.photos, 1);
        const QByteArray credits = get(QDir(w.dir).filePath(QStringLiteral("照片署名.txt")));
        QVERIFY(credits.startsWith("\xEF\xBB\xBF"));
        QVERIFY(credits.contains("\r\n"));
        QCOMPARE(credits.count('\n'), credits.count("\r\n"));
        json::ParseError e;
        const Value back = json::parse(QString::fromUtf8(get(QDir(w.dir).filePath(QStringLiteral("travel.json")))), &e);
        QVERIFY(e.ok);
        QCOMPARE(data::nativefmt::check(back), data::nativefmt::Check::Ok);
        QVERIFY(QFile::exists(QDir(w.dir).filePath(QStringLiteral("photos/2026-01/ph_c_cover.jpg"))));
        // 再导一次：文件夹重名加 -2
        const trip::ShareWrite w2 = trip::writeShare(dest.path(), QString(), s, data.path(), QStringLiteral("0.14.1"), QDateTime::currentDateTime());
        QVERIFY(w2.ok && w2.dir != w.dir);
    }

    // 真实数据（只读）：TP_TRIP_REAL = 放着 gen-real.js 打好的 Electron .trip 的文件夹；没设就跳过
    void realElectron()
    {
        const QString dir = qEnvironmentVariable("TP_TRIP_REAL");
        if (dir.isEmpty()) QSKIP("没设 TP_TRIP_REAL");
        const QStringList files = QDir(dir).entryList({ QStringLiteral("*.trip") }, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString &f : files) {
            QTemporaryDir d;
            trip::Reader r;
            QVERIFY2(r.open(QDir(dir).filePath(f)), qPrintable(r.error()));
            const int stops = int(r.manifest()[QStringLiteral("plan")][QStringLiteral("stops")].asArray().size());
            const int pkg = int(r.files().size());
            pm::WriteResult wr;
            const pm::Result res = importInto(QDir(dir).filePath(f), data::nativefmt::emptyDoc(QStringLiteral("t"), QDateTime::currentDateTime()), {}, d.path(), &wr);
            QCOMPARE(res.plans, 1);
            QVERIFY(wr.ok);
            QCOMPARE(wr.written, pkg);
            QCOMPARE(int(res.doc[QStringLiteral("plans")].asArray()[0][QStringLiteral("stops")].asArray().size()), stops);
            for (const Value &ph : res.doc[QStringLiteral("photos")].asArray())
                QVERIFY2(QFile::exists(QDir(d.path()).filePath(str(ph, "file"))), qPrintable(str(ph, "file")));
            qInfo("%s：%d 站、%d 张照片", qUtf8Printable(f), stops, pkg);
        }
    }
    // 真实原生数据（只读）：TP_TRIP_REAL_NATIVE = 数据文件夹；每个计划导出再导入进空数据，计划、照片记录、游记原样
    void realNativeRoundTrip()
    {
        const QString dir = qEnvironmentVariable("TP_TRIP_REAL_NATIVE");
        if (dir.isEmpty()) QSKIP("没设 TP_TRIP_REAL_NATIVE");
        json::ParseError e;
        const Value doc = json::parse(QString::fromUtf8(get(QDir(dir).filePath(QStringLiteral("travel.json")))), &e);
        QVERIFY(e.ok);
        int n = 0, photos = 0;
        for (const Value &p : doc[QStringLiteral("plans")].asArray()) {
            QTemporaryDir t, d;
            const QString id = str(p, "id");
            const trip::Built b = trip::build(doc, { id }, QString(), QStringLiteral("t"), QDateTime::currentDateTime());
            const QString file = t.filePath(QStringLiteral("x.trip"));
            const trip::WriteResult w = trip::write(file, b.manifest, b.photos, dir);
            QVERIFY2(w.ok, qPrintable(w.error));
            if (w.photos > photos && !qEnvironmentVariableIsEmpty("TP_TRIP_KEEP")) {
                photos = w.photos;
                const QString keep = QDir(qEnvironmentVariable("TP_TRIP_KEEP")).filePath(QStringLiteral("native-real.trip"));
                QFile::remove(keep);
                QFile::copy(file, keep);
            }
            const pm::Result res = importInto(file, data::nativefmt::emptyDoc(QStringLiteral("t"), QDateTime::currentDateTime()), {}, d.path());
            QCOMPARE(res.plans, 1);
            QCOMPARE(json::stringify(res.doc[QStringLiteral("plans")].asArray()[0]), json::stringify(p));
            for (const Value &ph : res.doc[QStringLiteral("photos")].asArray()) {
                const Value *orig = findById(doc[QStringLiteral("photos")], str(ph, "id"));
                QVERIFY(orig);
                Value o = *orig;
                o.mutObject().set(QStringLiteral("plan"), Value(id));   // 封面借用的别的照片导出时算这个计划的
                if (str(*orig, "plan") != id) o.mutObject().set(QStringLiteral("stop"), Value(QString()));
                QCOMPARE(json::stringify(ph), json::stringify(o));
            }
            int journal = 0;
            for (const Value &j : doc[QStringLiteral("journal")].asArray()) journal += str(j, "plan") == id;
            QCOMPARE(int(res.doc[QStringLiteral("journal")].asArray().size()), journal);
            ++n;
        }
        qInfo("真实原生数据：%d 个计划往返不变", n);
    }

    // 从 Electron 再导入，逐个计划选：另存一份 / 跳过 / 覆盖
    void electronPicks()
    {
        QTemporaryDir el, nat;
        put(QDir(el.path()).filePath(QStringLiteral("data.js")),
            "window.TRAVEL_DATA = {\"version\":4,\"plans\":[{\"id\":\"pa\",\"name\":\"甲\",\"coverPhoto\":\"photos/pa/c.jpg\",\"stops\":[{\"id\":\"s\",\"name\":\"站\",\"lat\":1,\"lng\":2,\"photos\":[{\"path\":\"photos/pa/s.jpg\"}]}]},"
            "{\"id\":\"pb\",\"name\":\"乙\",\"stops\":[]}],\"journal\":[{\"id\":\"j\",\"planId\":\"pa\",\"body\":\"x\"}]};\n");
        put(QDir(el.path()).filePath(QStringLiteral("photos/pa/c.jpg")), "COVER");
        put(QDir(el.path()).filePath(QStringLiteral("photos/pa/s.jpg")), "STOP");
        const QDateTime now = QDateTime::currentDateTime();
        Value first;
        QVERIFY(data::ElectronImport::run(el.path(), nat.path(), Value(), data::ElectronImport::Mode::Replace, now, QStringLiteral("t"), &first).ok);
        // 这边改过名字
        first.mutObject().find(QStringLiteral("plans"))->mutArray()[0].mutObject().set(QStringLiteral("name"), Value(QStringLiteral("甲（这边改的）")));
        QString err;
        const QVariantList cs = data::ElectronImport::conflicts(el.path(), first, now, &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(cs.size(), 2);

        Value doc;
        const auto rep = data::ElectronImport::run(el.path(), nat.path(), first, data::ElectronImport::Mode::AddNew, now, QStringLiteral("t"), &doc, {},
                                                   { { QStringLiteral("pa"), pm::Mode::Copy }, { QStringLiteral("pb"), pm::Mode::Skip } });
        QVERIFY2(rep.ok, qPrintable(rep.error));
        const json::Array &plans = doc[QStringLiteral("plans")].asArray();
        QCOMPARE(int(plans.size()), 3);
        QCOMPARE(str(plans[0], "name"), QStringLiteral("甲（这边改的）"));
        QCOMPARE(str(plans[2], "name"), QStringLiteral("甲（导入）"));
        const Value *cover = findById(doc[QStringLiteral("photos")], str(plans[2], "cover"));
        QVERIFY(cover);
        QCOMPARE(get(QDir(nat.path()).filePath(str(*cover, "file"))), QByteArray("COVER"));
        QVERIFY(str(*cover, "file") != str(*findById(doc[QStringLiteral("photos")], str(plans[0], "cover")), "file"));   // 另存的不共用文件
        QCOMPARE(int(doc[QStringLiteral("journal")].asArray().size()), 2);

        Value doc2;
        QVERIFY(data::ElectronImport::run(el.path(), nat.path(), first, data::ElectronImport::Mode::AddNew, now, QStringLiteral("t"), &doc2, {},
                                          { { QStringLiteral("pa"), pm::Mode::Replace } }).ok);
        QCOMPARE(int(doc2[QStringLiteral("plans")].asArray().size()), 2);
        QCOMPARE(str(doc2[QStringLiteral("plans")].asArray()[0], "name"), QStringLiteral("甲"));
        QCOMPARE(int(doc2[QStringLiteral("photos")].asArray().size()), 2);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestTrip t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_trip.moc"
