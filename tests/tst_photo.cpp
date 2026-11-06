// 照片（0.7.0）：EXIF、缩略图、旋转、导入、清理。
// 测试照片打包在 tests/photos.txt（tools/claude-kit/gen_photo_fixtures.py 生成；期望值和 exiftool 12.76 读出来的一致）。
// 真实照片对照（云端）：TP_EXIF_REAL=<exiftool -j -n 的输出> 时逐张比对拍摄时间、位置、方向、相机、尺寸。
//   exiftool -j -n -File:ImageWidth -File:ImageHeight -Rotation -EXIF:Orientation -EXIF:Make -EXIF:Model -EXIF:DateTimeOriginal \
//            -EXIF:GPSLatitude -EXIF:GPSLongitude …（只比 EXIF 里的；厂商私有块、XMP 里的我们不读）
#include "exif.h"
#include "jsonio.h"
#include "photolib.h"
#include "photorotate.h"
#include "thumbcache.h"
#include "wicimage.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace tp::data;
namespace ex = tp::data::exif;
using tp::json::Value;

namespace {

QByteArray readAll(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
// 测试照片打包在 tests/photos.txt（每行"文件名<Tab>base64"），第一次用时解到临时文件夹
QString fixtureDir()
{
    static QTemporaryDir dir;
    static bool done = false;
    if (!done) {
        done = true;
        const QList<QByteArray> lines = readAll(QStringLiteral(TP_SOURCE_DIR "/tests/photos.txt")).split('\n');
        for (const QByteArray &l : lines) {
            if (l.isEmpty() || l.startsWith('#')) continue;
            const int tab = l.indexOf('\t');
            if (tab <= 0) continue;
            QFile f(dir.filePath(QString::fromUtf8(l.left(tab))));
            if (f.open(QIODevice::WriteOnly)) f.write(QByteArray::fromBase64(l.mid(tab + 1).trimmed()));
        }
    }
    return dir.path() + u'/';
}
QString fx(const QString &name) { return fixtureDir() + name; }
bool writeAll(const QString &p, const QByteArray &b) { QDir().mkpath(QFileInfo(p).absolutePath()); QFile f(p); return f.open(QIODevice::WriteOnly) && f.write(b) == b.size(); }

// 四个角的颜色：R / G / B / W（左上、右上、左下、右下）
QString corners(const QImage &img)
{
    auto c = [&img](int x, int y) {
        const QColor k = img.pixelColor(x, y);
        if (k.red() > 200 && k.green() > 200 && k.blue() > 200) return u'W';
        if (k.red() > 150 && k.green() < 100 && k.blue() < 100) return u'R';
        if (k.green() > 150 && k.red() < 100 && k.blue() < 100) return u'G';
        if (k.blue() > 150 && k.red() < 100 && k.green() < 100) return u'B';
        return u'?';
    };
    const int w = img.width(), h = img.height();
    return QString() + c(2, 2) + c(w - 3, 2) + c(2, h - 3) + c(w - 3, h - 3);
}

QString ser(const Value &v) { return tp::json::stringify(v, 0); }

// 小数据：两个计划，几个站点
Value sampleDoc()
{
    const QByteArray j = R"({"format":"travelplanner","formatVersion":1,"plans":[
      {"id":"p_jp","name":"日本","startDate":"2024-10-01","endDate":"2024-10-06","stops":[
        {"id":"s_osaka","name":"大阪","lat":34.69,"lng":135.50,"arriveDate":"2024-10-01","departDate":"2024-10-02"},
        {"id":"s_tokyo","name":"东京","lat":35.6812,"lng":139.7671,"arriveDate":"2024-10-03","departDate":"2024-10-06"}]},
      {"id":"p_cl","name":"智利","startDate":"2025-01-10","endDate":"2025-01-20","stops":[
        {"id":"s_scl","name":"圣地亚哥","lat":-33.45,"lng":-70.66},
        {"id":"s_bad","name":"没坐标"}]},
      {"id":"p_empty","name":"空计划","stops":[]}],
     "photos":[]})";
    return tp::json::parse(QString::fromUtf8(j));
}

} // namespace

class TestPhoto : public QObject
{
    Q_OBJECT
private slots:
    // ---------- EXIF ----------
    void exifFull_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<bool>("le");
        QTest::newRow("jpeg-le") << "full_le.jpg" << true;
        QTest::newRow("jpeg-be") << "full_be.jpg" << false;
        QTest::newRow("png") << "exif.png" << false;
        QTest::newRow("webp") << "lossy.webp" << true;
    }
    void exifFull()
    {
        QFETCH(QString, file);
        QFETCH(bool, le);
        const ex::Info i = ex::readFile(fx(file));
        QVERIFY(i.hasExif);
        if (file.endsWith(QLatin1String(".jpg"))) QCOMPARE(i.littleEndian, le);
        QCOMPARE(i.orientation, file == QLatin1String("exif.png") ? 8 : 6);
        QCOMPARE(i.width, 64);
        QCOMPARE(i.height, 48);
        QCOMPARE(i.displayWidth(), 48);
        QCOMPARE(ex::takenAt(i), QStringLiteral("2024-10-03T14:25"));
        QCOMPARE(ex::timeZone(i), QStringLiteral("+09:00"));
        QCOMPARE(i.lat, 35.658581);
        QCOMPARE(i.lng, 139.745433);
        QCOMPARE(i.alt, 40.5);
        QCOMPARE(i.dir, 214.5);
        QCOMPARE(ex::cameraName(i.make, i.model), QStringLiteral("Apple iPhone 15 Pro"));
        QCOMPARE(i.lens, QStringLiteral("iPhone 15 Pro back camera 6.86mm f/1.78"));
        QCOMPARE(i.fnumber, 1.78);
        QCOMPARE(i.exposure, 0.004);
        QCOMPARE(i.iso, 80);
        QCOMPARE(i.focal, 6.86);
        QCOMPARE(i.focal35, 24);
        QVERIFY(std::abs(i.ev + 0.7) < 0.01);
        QVERIFY(i.orientationPos > 0);
    }
    void exifPhotoFields()
    {
        const tp::json::Object o = ex::photoFields(ex::readFile(fx("full_le.jpg")));
        QCOMPARE(ser(Value(o)), QStringLiteral(R"({"takenAt":"2024-10-03T14:25","tz":"+09:00","lat":35.658581,"lng":139.745433,"alt":40.5,"dir":214.5,"width":48,"height":64,"camera":"Apple iPhone 15 Pro","lens":"iPhone 15 Pro back camera 6.86mm f/1.78","exposure":{"f":1.78,"t":0.004,"iso":80,"fl":6.86,"fl35":24,"ev":-0.7}})"));
    }
    void exifGpsTz()
    {
        const ex::Info i = ex::readFile(fx("gps_tz.jpg"));
        QCOMPARE(i.offsetTime, QString());
        QCOMPARE(ex::timeZone(i), QStringLiteral("+09:00"));   // 14:25:36 当地 − 05:25:30 UTC
        QCOMPARE(i.lat, -33.8688);
        QCOMPARE(i.lng, -70.5);
        QCOMPARE(i.alt, -12.0);
        QCOMPARE(i.orientation, 1);
    }
    void exifNone()
    {
        ex::Info i = ex::readFile(fx("noexif.jpg"));
        QCOMPARE(i.container, ex::Container::Jpeg);
        QVERIFY(!i.hasExif);
        QCOMPARE(i.width, 64);
        QCOMPARE(i.height, 48);
        QCOMPARE(ser(Value(ex::photoFields(i))), QStringLiteral(R"({"width":64,"height":48})"));
        i = ex::readFile(fx("alpha.png"));
        QCOMPARE(i.container, ex::Container::Png);
        QVERIFY(!i.hasExif);
        i = ex::readFile(fx("plain.gif"));
        QCOMPARE(i.container, ex::Container::Gif);
        QCOMPARE(i.width, 64);
        i = ex::readFile(fx("nothing-here.jpg"));
        QCOMPARE(i.container, ex::Container::Unknown);
    }
    void exifGpsInvalid()
    {
        ex::Info i = ex::readFile(fx("gps_void.jpg"));
        QVERIFY(i.hasExif);
        QVERIFY(!i.hasGps());     // GPSStatus = V
        i = ex::readFile(fx("gps_zero.jpg"));
        QVERIFY(!i.hasGps());     // 0,0
        QCOMPARE(ex::takenAt(i), QString());   // 0000:00:00
    }
    void exifNoOrientation()
    {
        const ex::Info i = ex::readFile(fx("noorient.jpg"));
        QVERIFY(i.hasExif);
        QCOMPARE(i.orientation, 0);
        QCOMPARE(i.orientationPos, qint64(-1));
        QVERIFY(i.nextIfdPos > 0);   // 有嵌入小图
        QCOMPARE(ex::cameraName(i.make, i.model), QStringLiteral("Canon EOS R5"));
        QCOMPARE(ex::takenAt(i), QStringLiteral("2023-05-01T08:00"));
    }
    void exifWebpLossless()
    {
        const ex::Info i = ex::readFile(fx("lossless.webp"));
        QCOMPARE(i.container, ex::Container::Webp);
        QCOMPARE(i.width, 64);
        QCOMPARE(i.height, 48);
        QCOMPARE(ex::takenAt(i), QStringLiteral("2022-01-02T03:04"));
    }
    void exifHeif()
    {
        const ex::Info i = ex::readFile(fx("photo.heic"));
        QCOMPARE(i.container, ex::Container::Heif);
        QVERIFY(i.hasExif);
        QCOMPARE(i.width, 64);    // clap 裁到 64×48（编码尺寸 64×64）
        QCOMPARE(i.height, 48);
        QCOMPARE(ex::cameraName(i.make, i.model), QStringLiteral("Apple iPhone 15 Pro"));
        QCOMPARE(ex::takenAt(i), QStringLiteral("2024-10-03T14:25"));
        QCOMPARE(ex::timeZone(i), QStringLiteral("-07:00"));
    }
    void tzFromGps()
    {
        QCOMPARE(ex::tzFromGps("2024:10:03 14:25:36", "2024:10:03", 5, 25, 30), QStringLiteral("+09:00"));
        QCOMPARE(ex::tzFromGps("2024:10:03 01:10:00", "2024:10:02", 15, 40, 0), QStringLiteral("+09:30"));   // 跨日
        QCOMPARE(ex::tzFromGps("2024:10:02 20:30:00", "2024:10:03", 0, 0, 5), QStringLiteral("-03:30"));
        QCOMPARE(ex::tzFromGps("2024:10:03 12:00:00", "2024:10:03", 12, 7, 0), QStringLiteral("+00:00"));    // 取最近的 15 分钟
        QCOMPARE(ex::tzFromGps("2024:10:05 12:00:00", "2024:10:03", 12, 0, 0), QString());                   // 差两天：不算
        QCOMPARE(ex::tzFromGps("2024:10:03 12:00:00", "", 12, 0, 0), QString());
        QCOMPARE(ex::tzFromGps("2024:10:03 12:00:00", "2024:10:03", 25, 0, 0), QString());
    }
    void cameraNames()
    {
        QCOMPARE(ex::cameraName("Apple", "iPhone 15 Pro"), QStringLiteral("Apple iPhone 15 Pro"));
        QCOMPARE(ex::cameraName("Canon", "Canon EOS R5"), QStringLiteral("Canon EOS R5"));
        QCOMPARE(ex::cameraName("NIKON CORPORATION", "NIKON Z 6"), QStringLiteral("NIKON Z 6"));
        QCOMPARE(ex::cameraName("SONY", "ILCE-7M4"), QStringLiteral("SONY ILCE-7M4"));
        QCOMPARE(ex::cameraName("", "X100V"), QStringLiteral("X100V"));
        QCOMPARE(ex::cameraName("  OLYMPUS  ", ""), QStringLiteral("OLYMPUS"));
    }
    // 坏文件：随机改字节、截断，不能崩溃、不能越界（云端另用 AddressSanitizer 跑）
    void exifFuzz()
    {
        QRandomGenerator rng(20261103);
        int n = 0;
        QStringList files;
        for (const QString &name : QDir(fx(QString())).entryList(QDir::Files)) files << fx(name);
        // 云端：TP_EXIF_REAL 里的真实照片也拿来改（只取 EXIF 所在的前 64 KB，加快速度）
        if (const QByteArray env = qgetenv("TP_EXIF_REAL"); !env.isEmpty())
            for (const QJsonValue &jv : QJsonDocument::fromJson(readAll(QString::fromLocal8Bit(env))).array()) files << jv.toObject().value("SourceFile").toString();
        for (const QString &path : files) {
            const QByteArray orig = readAll(path).left(65536);
            for (int cut = 0; cut < orig.size(); cut += std::max<int>(1, int(orig.size()) / 97)) { ex::readBytes(orig.left(cut)); ++n; }
            for (int k = 0; k < 600; ++k) {
                QByteArray b = orig;
                const int flips = 1 + int(rng.bounded(8));
                for (int f = 0; f < flips; ++f) b[int(rng.bounded(quint32(b.size())))] = char(rng.bounded(256));
                const ex::Info i = ex::readBytes(b);
                QVERIFY(i.width >= 0 && i.height >= 0);
                ++n;
            }
        }
        QVERIFY(n > 5000);
    }
    // 云端：真实照片和 exiftool 对照
    void exifReal()
    {
        const QByteArray env = qgetenv("TP_EXIF_REAL");
        if (env.isEmpty()) QSKIP("没设 TP_EXIF_REAL");
        const QJsonArray arr = QJsonDocument::fromJson(readAll(QString::fromLocal8Bit(env))).array();
        QVERIFY(!arr.isEmpty());
        int compared = 0;
        QStringList bad;
        auto check = [&bad](bool ok, const QString &tag, const QString &what, const QString &got, const QString &want) {
            if (!ok) bad << QStringLiteral("%1 %2：我们 %3 / exiftool %4").arg(tag, what, got, want);
        };
        for (const QJsonValue &jv : arr) {
            const QJsonObject e = jv.toObject();
            const QString path = e.value("SourceFile").toString();
            const ex::Info i = ex::readFile(path);
            const QString tag = QFileInfo(path).fileName();
            check(i.container != ex::Container::Unknown, tag, "格式", "?", "");
            int ew = e.value("ImageWidth").toInt(), eh = e.value("ImageHeight").toInt();
            if (e.value("Rotation").toInt() % 180 == 90) std::swap(ew, eh);   // HEIF 的 irot：我们给摆正后的尺寸
            check(i.width == ew && i.height == eh, tag, "尺寸", QStringLiteral("%1x%2").arg(i.width).arg(i.height), QStringLiteral("%1x%2").arg(ew).arg(eh));
            const int eo = e.value("Orientation").toInt();
            if (i.container != ex::Container::Heif) check(i.orientation == eo, tag, "方向", QString::number(i.orientation), QString::number(eo));
            check(i.make == e.value("Make").toVariant().toString().trimmed(), tag, "Make", i.make, e.value("Make").toVariant().toString());
            check(i.model == e.value("Model").toVariant().toString().trimmed(), tag, "Model", i.model, e.value("Model").toVariant().toString());
            const QString dto = e.value("DateTimeOriginal").toVariant().toString();
            if (dto.size() >= 16 && !dto.startsWith(QLatin1String("0000")) && !dto.startsWith(u' '))
                check(i.dateTime.left(16) == dto.left(16), tag, "拍摄时间", i.dateTime, dto);
            double elat = e.value("GPSLatitude").toDouble(), elng = e.value("GPSLongitude").toDouble();
            if (e.value("GPSLatitudeRef").toString() == QLatin1String("S")) elat = -std::abs(elat);
            if (e.value("GPSLongitudeRef").toString() == QLatin1String("W")) elng = -std::abs(elng);
            if (e.contains("GPSLatitude") && e.contains("GPSLongitude") && (elat != 0 || elng != 0))
                check(std::abs(i.lat - elat) < 1e-5 && std::abs(i.lng - elng) < 1e-5, tag, "位置",
                      QStringLiteral("%1,%2").arg(i.lat).arg(i.lng), QStringLiteral("%1,%2").arg(elat).arg(elng));
            ++compared;
        }
        for (const QString &b : bad) qWarning("%s", qUtf8Printable(b));
        QVERIFY2(bad.isEmpty(), qPrintable(QStringLiteral("%1 处不一致").arg(bad.size())));
        qInfo("和 exiftool 对照了 %d 张", compared);
    }

    // ---------- 方向、缩略图 ----------
    void orientation_data()
    {
        QTest::addColumn<int>("o");
        QTest::addColumn<QString>("expect");   // PIL ImageOps.exif_transpose 的结果
        QTest::newRow("1") << 1 << "RGBW";
        QTest::newRow("2") << 2 << "GRWB";
        QTest::newRow("3") << 3 << "WBGR";
        QTest::newRow("4") << 4 << "BWRG";
        QTest::newRow("5") << 5 << "RBGW";
        QTest::newRow("6") << 6 << "BRWG";
        QTest::newRow("7") << 7 << "WGBR";
        QTest::newRow("8") << 8 << "GWRB";
    }
    void orientation()
    {
        QFETCH(int, o);
        QFETCH(QString, expect);
        QTemporaryDir tmp;
        const QString p = tmp.filePath("o.jpg");
        QVERIFY(writeAll(p, jpegWithApp1(readAll(fx("noexif.jpg")), buildMinimalExifApp1(o))));
        QCOMPARE(ex::readFile(p).orientation, o);
        const QImage img = loadOriented(p, 0, true);
        QCOMPARE(corners(img), expect);
        QCOMPARE(img.width(), o >= 5 ? 48 : 64);
    }
    // 方向值再转 90° 的表：和"摆正后再转 90°"的像素结果一致
    void rotateOrientationTable()
    {
        QImage s(4, 3, QImage::Format_RGB32);
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 4; ++x) s.setPixel(x, y, qRgb(x * 60, y * 80, 7));
        for (int o = 1; o <= 8; ++o)
            for (int t = 1; t <= 3; ++t) {
                const QImage want = applyOrientation(s, o).transformed(QTransform().rotate(90.0 * t));
                QCOMPARE(applyOrientation(s, rotateOrientation(o, t)), want);
            }
        QCOMPARE(rotateOrientation(1, 1), 6);
        QCOMPARE(rotateOrientation(6, 1), 3);
        QCOMPARE(rotateOrientation(0, 1), 6);
    }
    void thumbCache()
    {
        QTemporaryDir tmp;
        const QString photo = tmp.filePath("data/photos/2024-10/a.jpg");
        QVERIFY(writeAll(photo, readAll(fx("full_le.jpg"))));
        ThumbCache c(tmp.filePath("cache/thumbs"));
        QCOMPARE(ThumbCache::bucket(100), 160);
        QCOMPARE(ThumbCache::bucket(200), 320);
        QCOMPARE(ThumbCache::bucket(321), 640);
        QCOMPARE(ThumbCache::bucket(4000), 1280);
        QString err;
        const QString t1 = c.ensure(photo, 320, &err);
        QVERIFY2(!t1.isEmpty(), qPrintable(err));
        QVERIFY(t1.startsWith(tmp.filePath("cache/thumbs/")));
        const QImage img(t1);
        QCOMPARE(img.size(), QSize(48, 64));     // 方向 6 摆正，不放大
        QCOMPARE(corners(img), QStringLiteral("BRWG"));
        QCOMPARE(c.ensure(photo, 300), t1);       // 同一档直接用
        QVERIFY(c.ensure(photo, 1000) != t1);
        QCOMPARE(c.fileCount(), 2);
        // 照片换了：文件名跟着变
        QFile f(photo);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        f.close();
        QVERIFY(c.pathFor(photo, 320) != t1);
        // 上限
        QVERIFY(c.totalBytes() > 0);
        QCOMPARE(c.prune(1, 0), 2);
        QCOMPARE(c.fileCount(), 0);
        c.ensure(photo, 320);
        QCOMPARE(c.clear(), 1);
        // 读不了
        QVERIFY(c.ensure(tmp.filePath("nope.jpg"), 320, &err).isEmpty());
        QVERIFY(!err.isEmpty());
    }
    // 0.11.1 HEIC 用 Windows 自带的解码：装了扩展就真解（这台开发机装了），没装走"解不了"
    void heicViaWic()
    {
        namespace wic = tp::platform::wic;
        QTemporaryDir tmp;
        ThumbCache c(tmp.filePath("thumbs"));
        QString err;
        wic::setTestOff(true);
        QVERIFY(!wic::heifReady());
        QVERIFY(c.ensure(fx("photo.heic"), 320, &err).isEmpty());
        QVERIFY2(err.contains(QStringLiteral("HEIF")), qUtf8Printable(err));
        wic::setTestOff(false);
        if (!wic::heifReady()) QSKIP("这台电脑没有 HEIF / HEVC 扩展");
        const auto closeTo = [](QRgb a, int r, int g, int b) { return std::abs(qRed(a) - r) < 45 && std::abs(qGreen(a) - g) < 45 && std::abs(qBlue(a) - b) < 45; };
        // photo.heic：64×48，左上红、右上绿、左下蓝
        err.clear();
        QImage i = loadOriented(fx("photo.heic"), 0, true, &err);
        QVERIFY2(!i.isNull(), qUtf8Printable(err));
        QCOMPARE(i.size(), QSize(64, 48));
        QVERIFY(closeTo(i.pixel(3, 3), 255, 0, 0));
        QVERIFY(closeTo(i.pixel(60, 3), 0, 255, 0));
        QVERIFY(closeTo(i.pixel(3, 44), 0, 0, 255));
        // photo_irot.heic：同一张加了 irot（顺时针 90°），WIC 自己摆正：48×64，左上蓝、右上红
        i = loadOriented(fx("photo_irot.heic"), 0, true, &err);
        QVERIFY2(!i.isNull(), qUtf8Printable(err));
        QCOMPARE(i.size(), QSize(48, 64));
        QVERIFY(closeTo(i.pixel(3, 3), 0, 0, 255));
        QVERIFY(closeTo(i.pixel(44, 3), 255, 0, 0));
        const ex::Info info = ex::readFile(fx("photo_irot.heic"));
        QCOMPARE(info.displayWidth(), 48);
        QCOMPARE(info.displayHeight(), 64);
        // 缩小解码、缩略图缓存
        i = loadOriented(fx("photo.heic"), 32, true, &err);
        QCOMPARE(i.size(), QSize(32, 24));
        const QString out = c.ensure(fx("photo.heic"), 160, &err);
        QVERIFY2(!out.isEmpty(), qUtf8Printable(err));
        QCOMPARE(QImage(out).size(), QSize(64, 48));
        // 坏文件：说读不出来，不是说没装扩展
        QVERIFY(writeAll(tmp.filePath("bad.heic"), readAll(fx("photo.heic")).left(400)));
        err.clear();
        QVERIFY(c.ensure(tmp.filePath("bad.heic"), 320, &err).isEmpty());
        QVERIFY2(!err.contains(QStringLiteral("HEIF")), qUtf8Printable(err));
    }
    void thumbScaleAndAlpha()
    {
        QTemporaryDir tmp;
        QImage big(2000, 1000, QImage::Format_ARGB32);
        big.fill(Qt::transparent);
        QVERIFY(big.save(tmp.filePath("big.png")));
        ThumbCache c(tmp.filePath("thumbs"));
        const QImage t(c.ensure(tmp.filePath("big.png"), 640));
        QCOMPARE(t.size(), QSize(640, 320));
        QCOMPARE(t.pixelColor(10, 10), QColor(Qt::white));   // 透明铺白底
    }
    void thumbToSrgb()
    {
        QTemporaryDir tmp;
        QImage p3(32, 32, QImage::Format_RGB32);
        p3.fill(qRgb(0, 200, 0));
        p3.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        QVERIFY(p3.save(tmp.filePath("p3.png")));
        const QImage keep = loadOriented(tmp.filePath("p3.png"), 0, false);
        QCOMPARE(keep.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
        const QImage s = loadOriented(tmp.filePath("p3.png"), 0, true);
        QCOMPARE(s.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QVERIFY(s.pixelColor(5, 5).green() > 200);   // P3 的绿比 sRGB 更饱和：换算后 G 变大
    }

    // ---------- 旋转 ----------
    void rotateJpegTagOnly()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path();
        const QString rel = QStringLiteral("photos/2024-10/a.jpg");
        const QByteArray orig = readAll(fx("full_le.jpg"));
        QVERIFY(writeAll(dir + u'/' + rel, orig));
        const QString before = corners(loadOriented(dir + u'/' + rel, 0, true));
        RotateResult r = rotatePhoto(dir, rel, 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.lossless);
        QVERIFY(r.backedUp);
        QCOMPARE(QSize(r.width, r.height), QSize(64, 48));
        const QByteArray after = readAll(dir + u'/' + rel);
        QCOMPARE(after.size(), orig.size());
        int diff = 0;
        for (int i = 0; i < orig.size(); ++i) diff += orig[i] != after[i];
        QCOMPARE(diff, 1);   // 只改了方向值（6 → 3，小端只差 1 个字节）
        QCOMPARE(ex::readFile(dir + u'/' + rel).orientation, 3);
        QCOMPARE(readAll(dir + "/photos/.originals/2024-10/a.jpg"), orig);
        // 摆正后的画面 = 原来摆正后的画面顺时针转 90°
        QCOMPARE(before, QStringLiteral("BRWG"));
        QCOMPARE(corners(loadOriented(dir + u'/' + rel, 0, true)), QStringLiteral("WBGR"));
        // 再转一次：原图备份不变
        r = rotatePhoto(dir, rel, 1);
        QVERIFY(r.ok);
        QVERIFY(!r.backedUp);
        QCOMPARE(ex::readFile(dir + u'/' + rel).orientation, 8);
        QCOMPARE(readAll(dir + "/photos/.originals/2024-10/a.jpg"), orig);
        QVERIFY(QDir(dir + "/photos/2024-10").entryList({ "*.tmp*" }, QDir::Files).isEmpty());
    }
    void rotateJpegBigEndianAndXmp()
    {
        QTemporaryDir tmp;
        QVERIFY(writeAll(tmp.filePath("photos/b.jpg"), readAll(fx("full_be.jpg"))));
        QVERIFY(rotatePhoto(tmp.path(), "photos/b.jpg", 3).ok);
        QCOMPARE(ex::readFile(tmp.filePath("photos/b.jpg")).orientation, 1);   // 6 + 270° = 1
        QVERIFY(writeAll(tmp.filePath("photos/x.jpg"), readAll(fx("xmp_orient.jpg"))));
        QVERIFY(rotatePhoto(tmp.path(), "photos/x.jpg", 1).ok);
        const QByteArray x = readAll(tmp.filePath("photos/x.jpg"));
        QCOMPARE(ex::readBytes(x).orientation, 8);
        QVERIFY(x.contains("tiff:Orientation>8<") || x.contains("tiff:Orientation=\"8\""));
        QVERIFY(!x.contains("tiff:Orientation>3<") && !x.contains("tiff:Orientation=\"3\""));
    }
    void rotateJpegNoExif()
    {
        QTemporaryDir tmp;
        const QByteArray orig = readAll(fx("noexif.jpg"));
        QVERIFY(writeAll(tmp.filePath("photos/n.jpg"), orig));
        const RotateResult r = rotatePhoto(tmp.path(), "photos/n.jpg", 1);
        QVERIFY(r.ok);
        QVERIFY(r.lossless);
        const QByteArray after = readAll(tmp.filePath("photos/n.jpg"));
        QCOMPARE(ex::readBytes(after).orientation, 6);
        // 图像数据（SOS 以后）一个字节都没变
        QCOMPARE(after.mid(after.indexOf("\xFF\xDA")), orig.mid(orig.indexOf("\xFF\xDA")));
        QCOMPARE(corners(loadOriented(tmp.filePath("photos/n.jpg"), 0, true)), QStringLiteral("BRWG"));
    }
    void rotateJpegNoOrientationTag()
    {
        QTemporaryDir tmp;
        QVERIFY(writeAll(tmp.filePath("photos/c.jpg"), readAll(fx("noorient.jpg"))));
        const RotateResult r = rotatePhoto(tmp.path(), "photos/c.jpg", 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.lossless);
        const ex::Info i = ex::readFile(tmp.filePath("photos/c.jpg"));
        QCOMPARE(i.make, QStringLiteral("Canon"));          // 原 EXIF 写回了
        QCOMPARE(ex::takenAt(i), QStringLiteral("2023-05-01T08:00"));
        QCOMPARE(QSize(i.width, i.height), QSize(48, 64));  // 像素真的转了
        QCOMPARE(corners(loadOriented(tmp.filePath("photos/c.jpg"), 0, true)), QStringLiteral("BRWG"));
        // 嵌入小图去掉了（下一个 IFD 指针 = 0）
        const QByteArray b = readAll(tmp.filePath("photos/c.jpg"));
        const qint64 p = i.nextIfdPos;
        QVERIFY(p > 0);
        QCOMPARE(b.mid(int(p), 4), QByteArray(4, '\0'));
    }
    void rotatePng()
    {
        QTemporaryDir tmp;
        QVERIFY(writeAll(tmp.filePath("photos/p.png"), readAll(fx("exif.png"))));
        QCOMPARE(corners(loadOriented(tmp.filePath("photos/p.png"), 0, true)), QStringLiteral("GWRB"));   // 方向 8
        const RotateResult r = rotatePhoto(tmp.path(), "photos/p.png", 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.lossless);
        const ex::Info i = ex::readFile(tmp.filePath("photos/p.png"));
        QCOMPARE(i.orientation, 1);
        QCOMPARE(i.make, QStringLiteral("Apple"));
        QCOMPARE(ex::timeZone(i), QStringLiteral("+09:00"));
        QCOMPARE(QSize(i.width, i.height), QSize(64, 48));
        QCOMPARE(corners(loadOriented(tmp.filePath("photos/p.png"), 0, true)), QStringLiteral("RGBW"));   // GWRB 顺时针 90° = RGBW
        QVERIFY(!QImage(tmp.filePath("photos/p.png")).isNull());   // Qt 照样读得出来（eXIf 块的 CRC 对）
    }
    void rotateWebp()
    {
        QTemporaryDir tmp;
        QVERIFY(writeAll(tmp.filePath("photos/w.webp"), readAll(fx("lossy.webp"))));
        RotateResult r = rotatePhoto(tmp.path(), "photos/w.webp", 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.lossless);
        ex::Info i = ex::readFile(tmp.filePath("photos/w.webp"));
        QCOMPARE(i.orientation, 1);
        QCOMPARE(i.make, QStringLiteral("Apple"));
        QCOMPARE(QSize(i.width, i.height), QSize(64, 48));
        QCOMPARE(corners(loadOriented(tmp.filePath("photos/w.webp"), 0, true)), QStringLiteral("WBGR"));   // BRWG 顺时针 90°
        QVERIFY(!QImage(tmp.filePath("photos/w.webp")).isNull());

        QVERIFY(writeAll(tmp.filePath("photos/l.webp"), readAll(fx("lossless.webp"))));
        r = rotatePhoto(tmp.path(), "photos/l.webp", 2);
        QVERIFY(r.ok);
        QVERIFY(r.lossless);
        const QByteArray b = readAll(tmp.filePath("photos/l.webp"));
        QVERIFY(b.contains("VP8L"));
        QVERIFY(b.contains("VP8X"));
        QCOMPARE(ex::takenAt(ex::readBytes(b)), QStringLiteral("2022-01-02T03:04"));
        QCOMPARE(corners(QImage(tmp.filePath("photos/l.webp"))), QStringLiteral("WBGR"));
    }
    void rotateRefused()
    {
        QTemporaryDir tmp;
        QVERIFY(writeAll(tmp.filePath("photos/g.gif"), readAll(fx("plain.gif"))));
        RotateResult r = rotatePhoto(tmp.path(), "photos/g.gif", 1);
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("这种格式不能旋转"));
        QVERIFY(writeAll(tmp.filePath("photos/fake.jpg"), readAll(fx("exif.png"))));   // 扩展名和内容对不上
        QVERIFY(!rotatePhoto(tmp.path(), "photos/fake.jpg", 1).ok);
        QVERIFY(writeAll(tmp.filePath("photos/h.heic"), readAll(fx("photo.heic"))));
        QVERIFY(!rotatePhoto(tmp.path(), "photos/h.heic", 1).ok);
        QVERIFY(!rotatePhoto(tmp.path(), "photos/none.jpg", 1).ok);
        QVERIFY(!QFileInfo::exists(tmp.filePath("photos/.originals")));
    }

    // ---------- 导入 ----------
    void guessPlan()
    {
        const Value doc = sampleDoc();
        const double nan = std::nan("");
        photolib::Guess g = photolib::guess(doc, "2024-10-04T10:00", nan, nan);
        QCOMPARE(g.plan, QStringLiteral("p_jp"));
        QCOMPARE(g.stop, QStringLiteral("s_tokyo"));   // 日期在东京那站
        QCOMPARE(g.why, QStringLiteral("date"));
        g = photolib::guess(doc, "2024-10-05T10:00", 34.70, 135.49);
        QCOMPARE(g.stop, QStringLiteral("s_tokyo"));   // 日期优先于 GPS
        g = photolib::guess(doc, "2025-01-12T10:00", 35.0, 139.0);
        QCOMPARE(g.plan, QStringLiteral("p_cl"));
        QCOMPARE(g.stop, QStringLiteral("s_scl"));     // 计划里只有一站有坐标
        g = photolib::guess(doc, "", 34.695, 135.505);
        QCOMPARE(g.plan, QStringLiteral("p_jp"));
        QCOMPARE(g.stop, QStringLiteral("s_osaka"));
        QCOMPARE(g.why, QStringLiteral("gps"));
        g = photolib::guess(doc, "", 34.80, 135.50);   // 12 km 外：认不出来
        QVERIFY(g.plan.isEmpty());
        g = photolib::guess(doc, "2019-01-01T00:00", nan, nan);
        QVERIFY(g.plan.isEmpty());
        g = photolib::guess(doc, "", nan, nan, "p_jp", "s_tokyo");   // 从计划页进来
        QCOMPARE(g.stop, QStringLiteral("s_tokyo"));
        g = photolib::guess(doc, "2024-10-01T09:00", nan, nan, "p_jp", "s_tokyo");
        QCOMPARE(g.stop, QStringLiteral("s_osaka"));
        QVERIFY(std::abs(photolib::distKm(34.69, 135.50, 35.6812, 139.7671) - 403) < 5);
    }
    void targetFiles()
    {
        QTemporaryDir tmp;
        const QDateTime now(QDate(2026, 11, 3), QTime(5, 0));
        QSet<QString> taken;
        QCOMPARE(photolib::targetFile(tmp.path(), "ph_1", "IMG 0001.JPG", "2024-10-03T14:25", now, &taken), QStringLiteral("photos/2024-10/ph_1_IMG_0001.JPG"));
        QCOMPARE(photolib::targetFile(tmp.path(), "ph_1", "img 0001.jpg", "2024-10-03T14:25", now, &taken), QStringLiteral("photos/2024-10/ph_1_img_0001-2.jpg"));
        QCOMPARE(photolib::targetFile(tmp.path(), "ph_2", "a#b%c.png", "", now, &taken), QStringLiteral("photos/2026-11/ph_2_a_b_c.png"));
        QVERIFY(writeAll(tmp.filePath("photos/2026-11/ph_3_x.jpg"), "x"));
        QCOMPARE(photolib::targetFile(tmp.path(), "ph_3", "x.jpg", "", now), QStringLiteral("photos/2026-11/ph_3_x-2.jpg"));
        const QString id = photolib::newPhotoId(now.toMSecsSinceEpoch());
        QVERIFY(id.startsWith(QLatin1String("ph_")));
        QCOMPARE(id.size(), 3 + QString::number(now.toMSecsSinceEpoch(), 36).size() + 4);
    }
    void importCopy()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath("data");
        QDir().mkpath(dir);
        const QDateTime now(QDate(2026, 11, 3), QTime(5, 0));
        QList<photolib::ImportItem> items;
        for (const char *n : { "full_le.jpg", "noexif.jpg", "lossy.webp" }) {
            photolib::ImportItem it;
            it.src = fx(n);
            it.name = n;
            it.exif = ex::photoFields(ex::readFile(it.src));
            items << it;
        }
        items[0].plan = "p_jp"; items[0].stop = "s_tokyo";
        items[2].skip = true;
        int calls = 0;
        const photolib::ImportResult r = photolib::copyIn(dir, items, now, [&calls](int, int total) { ++calls; return total == 2; });
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.skipped, 1);
        QCOMPARE(calls, 3);
        QCOMPARE(int(r.records.size()), 2);
        const Value &a = r.records[0];
        QVERIFY(a["file"].toString().startsWith(QLatin1String("photos/2024-10/ph_")));
        QVERIFY(a["file"].toString().endsWith(QLatin1String("_full_le.jpg")));
        QCOMPARE(a["plan"].toString(), QStringLiteral("p_jp"));
        QCOMPARE(a["tz"].toString(), QStringLiteral("+09:00"));
        QCOMPARE(a["addedAt"].toNumber(), double(now.toMSecsSinceEpoch()));
        const auto &keys = a.asObject().members();
        QCOMPARE(keys.front().first, QStringLiteral("id"));
        QCOMPARE(keys[1].first, QStringLiteral("file"));
        QCOMPARE(keys.back().first, QStringLiteral("addedAt"));
        QVERIFY(r.records[1]["file"].toString().startsWith(QLatin1String("photos/2026-11/")));
        QCOMPARE(r.records[1]["plan"].toString(), QString());
        QCOMPARE(readAll(dir + u'/' + a["file"].toString()), readAll(fx("full_le.jpg")));
        Value doc = sampleDoc();
        photolib::addRecords(doc, r.records);
        QCOMPARE(int(doc["photos"].asArray().size()), 2);
        // 查重复
        QHash<QString, QByteArray> cache;
        QCOMPARE(photolib::findDuplicate(doc, dir, fx("full_le.jpg"), &cache), a["id"].toString());
        QCOMPARE(photolib::findDuplicate(doc, dir, fx("full_be.jpg"), &cache), QString());   // 大小一样、内容不同
        QCOMPARE(photolib::findDuplicate(doc, dir, fx("exif.png"), &cache), QString());
    }
    void importRollback()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.filePath("data");
        const QDateTime now(QDate(2026, 11, 3), QTime(5, 0));
        QList<photolib::ImportItem> items;
        photolib::ImportItem ok; ok.src = fx("noexif.jpg"); ok.name = "a.jpg"; items << ok;
        photolib::ImportItem bad; bad.src = fx("does-not-exist.jpg"); bad.name = "b.jpg"; items << bad;
        photolib::ImportResult r = photolib::copyIn(dir, items, now);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.records.empty());
        QCOMPARE(QDir(dir + "/photos/2026-11").entryList(QDir::Files).size(), 0);   // 第一张也删掉了
        // 取消
        items.removeLast();
        items << ok;
        r = photolib::copyIn(dir, items, now, [](int done, int) { return done < 1; });
        QVERIFY(r.cancelled);
        QCOMPARE(QDir(dir + "/photos/2026-11").entryList(QDir::Files).size(), 0);
    }

    // ---------- 清理 ----------
    void cleanScan()
    {
        QTemporaryDir tmp;
        const QString dir = tmp.path();
        const QDateTime now = QDateTime::currentDateTime();
        auto make = [&dir, &now](const QString &rel, int minutesAgo) {
            QVERIFY(writeAll(dir + u'/' + rel, rel.toUtf8()));
            QFile f(dir + u'/' + rel);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(now.addSecs(-60 * minutesAgo), QFileDevice::FileModificationTime));
            // 创建时间也要设（Windows 上有，"刚放进来"的保护会看它；Linux 上设不了，返回 false 不管）
            f.setFileTime(now.addSecs(-60 * minutesAgo), QFileDevice::FileBirthTime);
        };
        make("photos/2024-10/used.jpg", 100);
        make("photos/2024-10/Used2.JPG", 100);
        make("photos/2024-10/unused.jpg", 100);
        make("photos/2024-10/fresh.jpg", 3);          // 刚放进来的文件
        make("photos/2024-10/justadded.jpg", 100);    // 记录里 addedAt 是刚才
        make("photos/2024-10/inundo.jpg", 100);
        make("photos/2024-10/x.jpg.tmp-123", 100);
        make("photos/.thumbs/abc.jpg", 100);
        make("photos/.originals/2024-10/used.jpg", 100);
        make("photos/.originals/2024-10/gone.jpg", 100);
        make("photos/Thumbs.db", 100);
        Value doc = sampleDoc();
        photolib::addRecords(doc, tp::json::parse(QStringLiteral(R"([
            {"id":"a","file":"photos/2024-10/used.jpg"},
            {"id":"b","file":"photos/2024-10/used2.jpg"},
            {"id":"c","file":"photos/2024-10/missing.jpg"}])")).asArray());
        Value undoDoc = doc;
        photolib::addRecords(undoDoc, tp::json::parse(QStringLiteral(R"([{"id":"d","file":"photos/2024-10/inundo.jpg"}])")).asArray());
        Value recentDoc = doc;
        photolib::addRecords(recentDoc, tp::json::parse(QStringLiteral(R"([{"id":"e","file":"photos/2024-10/justadded.jpg","addedAt":%1}])").arg(now.toMSecsSinceEpoch() - 60000)).asArray());

        const QSet<QString> ref = photolib::referencedFiles(doc);
        const QSet<QString> undoRef = photolib::referencedFiles(undoDoc);
        const QSet<QString> recent = photolib::recentlyAddedFiles(recentDoc, now.toMSecsSinceEpoch());
        QCOMPARE(recent, QSet<QString>({ "photos/2024-10/justadded.jpg" }));
        const photolib::CleanScan s = photolib::scanUnused(dir, ref, undoRef, recent, now);
        QCOMPARE(s.unused, QStringList({ "photos/2024-10/unused.jpg" }));
        QCOMPARE(s.keptRecent, QStringList({ "photos/2024-10/fresh.jpg", "photos/2024-10/justadded.jpg" }));
        QCOMPARE(s.keptUndo, QStringList({ "photos/2024-10/inundo.jpg" }));
        QCOMPARE(s.orphanOriginals, QStringList({ "photos/.originals/2024-10/gone.jpg" }));
        QVERIFY(s.unusedBytes > 0);

        // 移到回收站：移动前再核对一次（这期间 unused.jpg 被用上了 → 不动）
        QStringList trashed;
        auto fake = [&trashed](const QString &abs, QString *) { trashed << QFileInfo(abs).fileName(); return QFile::remove(abs); };
        Value doc2 = doc;
        photolib::addRecords(doc2, tp::json::parse(QStringLiteral(R"([{"id":"f","file":"photos/2024-10/unused.jpg"}])")).asArray());
        photolib::TrashResult t = photolib::moveToTrash(dir, s.unused + s.orphanOriginals + QStringList { "photos/2024-10/used.jpg", "photos/../x" },
                                                        photolib::referencedFiles(doc2), undoRef, recent, now, fake);
        QCOMPARE(t.moved, 1);
        QCOMPARE(trashed, QStringList({ "gone.jpg" }));
        QCOMPARE(t.failed.size(), 3);
        QVERIFY(QFileInfo::exists(dir + "/photos/2024-10/unused.jpg"));
        QVERIFY(QFileInfo::exists(dir + "/photos/2024-10/used.jpg"));
        t = photolib::moveToTrash(dir, s.unused, ref, undoRef, recent, now, fake);
        QCOMPARE(t.moved, 1);
        QVERIFY(!QFileInfo::exists(dir + "/photos/2024-10/unused.jpg"));
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);   // QImage 读 WebP 要插件
    TestPhoto t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_photo.moc"
