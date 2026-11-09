// 地图的计算（0.8.0）：投影和相机、按范围缩放、最小缩放、飞行、昼夜阴影、"已解锁"上色、路线几何。
// 0.8.2：标记样式、合并（和 Electron 对照：tests/golden/marker-clusters.json，tools/claude-kit/marker-golden/gen.js 生成）、
// 合并圆圈的颜色、错开。
// 0.8.7：计划的排序、筛选、"此刻"和 Electron 对照（tests/golden/plans.json，tools/claude-kit/plans-golden/gen.js 生成）、改数据的几个操作。
// "已解锁"和 Electron 对照：tests/golden/unlock-colors.json 由 tools/claude-kit/unlock-golden/gen.js
// 用 Electron v6.3.0 map-app.js 里原样的代码算 testdata/data.js 得到；这里用 testdata/travel.json（同一份数据的原生格式）比较。
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QtTest>

#include "geo.h"
#include "globegeom.h"
#include "globeroutes.h"
#include "jsonio.h"
#include "markers.h"
#include "night.h"
#include "photomap.h"
#include "plancal.h"
#include "plankit.h"
#include "plans.h"
#include "planedit.h"
#include "planmoney.h"
#include <cmath>
#include "planweather.h"
#include "planpage.h"
#include "suncalc.h"
#include "rail.h"
#include "roads.h"
#include "tripdata.h"
#include "unlock.h"

using namespace tp::map;
using namespace tp::map::geo;

namespace {
QByteArray readFile(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

tp::json::Value parseJson(const QByteArray &bytes)
{
    const QString text = QString::fromUtf8(bytes);
    return tp::json::parse(text);
}

// 经度连续展开的环：试 lng、lng±360，任何一个在里面就算在里面
bool inUnwrappedRing(const std::vector<LatLng> &ring, double lat, double lng)
{
    for (double off : { 0.0, 360.0, -360.0, 720.0, -720.0 }) {
        const double x = lng + off;
        bool in = false;
        for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
            const double xi = ring[i].lng, yi = ring[i].lat, xj = ring[j].lng, yj = ring[j].lat;
            if (((yi > lat) != (yj > lat)) && x < (xj - xi) * (lat - yi) / (yj - yi) + xi) in = !in;
        }
        if (in) return true;
    }
    return false;
}
} // namespace

class TestMap : public QObject
{
    Q_OBJECT
private slots:
    void photoMapBasics();
    void plansLibrary();
    void tripTone();
    void altitudeRules();
    void weatherParsing();
    void elevEditing();
    void kitChecks();       // 0.10.4
    void kitEmergency();
    void kitDocuments();
    void kitJournal();
    void calShiftAndPlace();  // 0.11.2
    void calRoundTrip();
    void calUnplace();
    void calOverlapAndViews();
    void calWeekStart();      // 0.11.3
    void calWeekView();
    void calRamadan();
    void calSeasonTable();
    void visitedCodes();
    void journalPageCrud();
    void kitGuide();
    void moneyRates();
    void moneySpend();
    void moneySplit();
    void moneyLedger();
    void moneyEdit();
    void moneyBookingsView();
    // 0.9.0 地球
    void globeProjectPick();
    void globeZoomMapping();
    void globeSelectBasics();
    void globeSelectCullsBackside();
    void globeSelectBudget();
    void skySeededRand();
    // 0.9.5 地球上的内容
    void globePoleZoomSmooth();
    void globeLimbFadeAndScale();
    void globeOccludedLifted();
    void globeRoutesLiftAndModes();
    void globeScreenRunsCutAtLimb();
    void globeVehiclePlan();
    void globeSlerpAndLerp();
    void globeMorphMatchesFlat();   // 0.9.7
    void projectRoundTrip();
    void pixelForBasics();
    void pixelForWrapsToNearestCopy();
    void zoomAroundKeepsAnchor();
    void panByMovesContent();
    void constrainHeight();
    void minZoomForScale();
    void fitBoundsTight();
    void fitBoundsSinglePoint();
    void fitPaddingShrinks();
    void flyEndpoints();
    void flyZoomsOutOnLongTrips();
    void flySameCenterOnlyZooms();
    void greatCircleSampling();
    void nightStackMatchesElectron();
    void nightPolygonsCoverNight_data();
    void nightPolygonsCoverNight();
    void subsolarSanity();
    void nightGeoJsonShape();
    void nightStripesNoGapsNoSteps();
    void unlockMatchesElectron();
    void unlockIsDeterministic();
    void unlockManualHelpers();
    void prefetchMatchesElectron();
    // 0.8.6：卫星瓦片大小（固定 128）、任意瓦片大小的预取、省界输出的 geometry 由坐标重新拼（和原文一样）
    void satTileSizeByDpr();
    void prefetchAnyTileSize();
    void unlockGeometryRoundTrip();
    void tripDataStopsAndRoutes();
    void tripDataCache();
    void tripDataJsNumbers();
    void markerStyles();
    void markerGeometry();
    void clusterColors();
    void clusterTiers();
    void clustersMatchElectron();
    void clusterSeedNotCentroid();
    void spreadCircle();
    void spreadOnlyWhenZoomedIn();
    // 0.8.4：道路路线、路线分三份
    void roadsMatchElectron();
    void roadsPendingAndApply();
    void roadsParseOsrm();
    void tripRoutesSplit();
    void plansSortMatchesElectron();
    void plansNameOrder();
    void plansFilterMatchesElectron();
    void plansFilterVariantRoundTrip();
    void plansNowMatchesElectron();
    void plansSheetInfo();
    void plansEdits();
    void tripDataVisibleFilter();
    void plansListInfo();
    void plansCommandItems();
    // 0.8.9 站点编辑器
    void planEditRegions();
    void planEditValidate();
    void planEditEdits();
    void planEditKeepsUnknown();
    void planEditLegsAndStats();
    void planEditTodayStop();
    void sunAgainstElectron();
    void schedGrouping();
    void schedEdit();
    void pageDocWarnings();
    void pageSeasonAndDays();
    void tzFill();
    void railRoute();
};

void TestMap::projectRoundTrip()
{
    for (double lat : { -80.0, -33.9, 0.0, 22.3, 51.5, 84.9 })
        for (double lng : { -179.9, -73.9, 0.0, 114.17, 179.9 }) {
            const Pt p = project(lat, lng);
            const LatLng b = unproject(p.x, p.y);
            QVERIFY(std::abs(b.lat - lat) < 1e-9);
            QVERIFY(std::abs(b.lng - lng) < 1e-9);
        }
    // 赤道 / 本初子午线在世界正中；±85.0511° 在上下边
    QCOMPARE(project(0, 0).x, 0.5);
    QVERIFY(std::abs(project(0, 0).y - 0.5) < 1e-12);
    QVERIFY(std::abs(project(kMaxLat, 0).y) < 1e-9);
    QVERIFY(std::abs(project(-kMaxLat, 0).y - 1) < 1e-9);
}

void TestMap::pixelForBasics()
{
    Camera c;
    c.lat = 0; c.lng = 0; c.zoom = 1; c.width = 1000; c.height = 600;
    // 缩放 1：世界 1024 像素；经度 90° = 256 像素
    const Pt p = c.pixelFor(0, 90);
    QVERIFY(std::abs(p.x - (500 + 256)) < 1e-9);
    QVERIFY(std::abs(p.y - 300) < 1e-9);
    const LatLng back = c.coordinateFor(p.x, p.y);
    QVERIFY(std::abs(back.lng - 90) < 1e-9 && std::abs(back.lat) < 1e-9);
    // 北半球在上面
    QVERIFY(c.pixelFor(45, 0).y < 300);
}

void TestMap::pixelForWrapsToNearestCopy()
{
    Camera c;
    c.lat = 0; c.lng = 178; c.zoom = 3; c.width = 1400; c.height = 900;
    const double ws = c.worldSize();
    // -171 离 178 只有 11°（往东），应该画在中心右边
    const Pt p = c.pixelFor(0, -171);
    QVERIFY(std::abs(p.x - (700 + 11.0 / 360 * ws)) < 1e-6);
    // 不挪副本的版本在左边很远
    QVERIFY(c.pixelForRaw(0, -171).x < 0);
}

void TestMap::zoomAroundKeepsAnchor()
{
    Camera c;
    c.lat = 35; c.lng = 139; c.zoom = 4; c.width = 1200; c.height = 800;
    const LatLng anchor = c.coordinateFor(900, 200);
    c.zoomAround(7.3, 900, 200);
    const Pt p = c.pixelFor(anchor.lat, anchor.lng);
    QVERIFY2(std::abs(p.x - 900) < 1e-6 && std::abs(p.y - 200) < 1e-6, qPrintable(QStringLiteral("%1,%2").arg(p.x).arg(p.y)));
    QCOMPARE(c.zoom, 7.3);
}

void TestMap::panByMovesContent()
{
    Camera c;
    c.lat = 10; c.lng = 20; c.zoom = 5; c.width = 800; c.height = 600;
    const Pt before = c.pixelFor(12, 22);
    c.panBy(37, -21);
    const Pt after = c.pixelFor(12, 22);
    QVERIFY(std::abs(after.x - before.x - 37) < 1e-6);
    QVERIFY(std::abs(after.y - before.y + 21) < 1e-6);
}

void TestMap::constrainHeight()
{
    Camera c;
    c.width = 1400; c.height = 1100; c.zoom = 0.5; c.lat = 80; c.lng = 190;
    const Camera k = constrain(c, 0, 18);
    // 世界的高度至少等于窗口高度
    QVERIFY(k.worldSize() >= 1100 - 1e-6);
    // 上边不露出来：窗口顶边的纬度不超过 85.05°
    QVERIFY(k.coordinateFor(700, 0).lat <= kMaxLat + 1e-9);
    QVERIFY(std::abs(k.coordinateFor(700, 0).lat - kMaxLat) < 1e-6);
    QVERIFY(k.lng >= -180 && k.lng < 180);
    // 最大缩放
    c.zoom = 25;
    QCOMPARE(constrain(c, 0, 18).zoom, 18.0);
}

void TestMap::minZoomForScale()
{
    // Electron updateMinZoom：在最小缩放时，窗口正中 100 像素 = 1000 km × 2 × .97
    for (double lat : { 0.0, 35.0, 60.0 }) {
        Camera c;
        c.lat = lat; c.lng = 100; c.zoom = 4; c.width = 1400; c.height = 900;
        const double z = geo::minZoomForScale(c);
        c.zoom = z;
        const double mpp100 = c.metersPerPixelAtCenter() * 100;
        if (z > 0 && z < 6) QVERIFY2(std::abs(mpp100 - 1940000) / 1940000 < 0.002, qPrintable(QString::number(mpp100)));
    }
    // 高纬度时同样的比例尺需要的缩放更小（每像素米数小）
    Camera a, b;
    a.lat = 0; b.lat = 60;
    QVERIFY(geo::minZoomForScale(b) < geo::minZoomForScale(a));
}

void TestMap::fitBoundsTight()
{
    Camera c;
    c.width = 1400; c.height = 900;
    const Padding pad = fitPadding(1400, 900, 40, 40);
    // 欧洲：-10..30 经度、36..60 纬度
    const FitResult r = fitBounds(c, -10, 36, 30, 60, pad, 18);
    QVERIFY(r.ok);
    Camera k = c;
    k.lat = r.lat; k.lng = r.lng; k.zoom = r.zoom;
    const Pt nw = k.pixelFor(60, -10), se = k.pixelFor(36, 30);
    // 四个角都在留白以内，而且至少一个方向贴边
    QVERIFY(nw.x >= pad.left - 1e-6 && nw.y >= pad.top - 1e-6);
    QVERIFY(se.x <= 1400 - pad.right + 1e-6 && se.y <= 900 - pad.bottom + 1e-6);
    const bool tightX = std::abs(nw.x - pad.left) < 1e-6 && std::abs(se.x - (1400 - pad.right)) < 1e-6;
    const bool tightY = std::abs(nw.y - pad.top) < 1e-6 && std::abs(se.y - (900 - pad.bottom)) < 1e-6;
    QVERIFY(tightX || tightY);
    // maxZoom 生效
    QCOMPARE(fitBounds(c, 2.29, 48.85, 2.30, 48.86, pad, 5).zoom, 5.0);
    // 跨 180° 经线（east > 180）：中心在太平洋上
    const FitResult p = fitBounds(c, 170, -20, 200, 0, pad, 18);
    QVERIFY(std::abs(p.lng - (-175)) < 1e-6 || std::abs(p.lng - 185) < 1e-6);
}

void TestMap::fitBoundsSinglePoint()
{
    Camera c;
    c.width = 800; c.height = 600;
    const FitResult r = fitBounds(c, 116.4, 39.9, 116.4, 39.9, fitPadding(800, 600, 40, 40), 5);
    QVERIFY(r.ok);
    QCOMPARE(r.zoom, 5.0);
    QVERIFY(std::abs(r.lat - 39.9) < 1e-9 && std::abs(r.lng - 116.4) < 1e-9);
}

void TestMap::fitPaddingShrinks()
{
    const Padding a = fitPadding(1400, 900, 40, 40);
    QCOMPARE(a.left, 40.0); QCOMPARE(a.right, 40.0); QCOMPARE(a.top, 40.0); QCOMPARE(a.bottom, 40.0);
    // 窄窗口：左右按比例缩小；矮窗口：上下 (H-80)/2
    const Padding b = fitPadding(200, 100, 400, 200);
    QVERIFY(std::abs(b.left + b.right - 120) < 1e-9 && std::abs(b.left - 2 * b.right) < 1e-9);
    QCOMPARE(b.top, 10.0);
}

void TestMap::flyEndpoints()
{
    Camera c;
    c.lat = 31.2; c.lng = 121.5; c.zoom = 6; c.width = 1400; c.height = 900;
    FlyPath f(c, 48.86, 2.35, 9);
    double la, ln, z;
    f.at(0, &la, &ln, &z);
    QVERIFY(std::abs(la - 31.2) < 1e-9 && std::abs(ln - 121.5) < 1e-9 && std::abs(z - 6) < 1e-9);
    f.at(1, &la, &ln, &z);
    QVERIFY(std::abs(la - 48.86) < 1e-9 && std::abs(ln - 2.35) < 1e-9 && std::abs(z - 9) < 1e-12);
    QVERIFY(f.naturalDurationMs() > 500);
}

void TestMap::flyZoomsOutOnLongTrips()
{
    Camera c;
    c.lat = 31.2; c.lng = 121.5; c.zoom = 8; c.width = 1400; c.height = 900;
    FlyPath f(c, 40.7, -74.0, 8);   // 上海 → 纽约（跨 180° 经线更近）
    double minZ = 99, la, ln, z;
    double prevLng = 121.5;
    for (int i = 0; i <= 100; ++i) {
        f.at(i / 100.0, &la, &ln, &z);
        minZ = std::min(minZ, z);
        // 每一步经度变化不大（不会绕地球另一边）
        QVERIFY(std::abs(unwrapTo(ln, prevLng) - prevLng) < 30);
        prevLng = ln;
    }
    QVERIFY2(minZ < 5, qPrintable(QString::number(minZ)));
}

void TestMap::flySameCenterOnlyZooms()
{
    Camera c;
    c.lat = 10; c.lng = 10; c.zoom = 3; c.width = 800; c.height = 600;
    FlyPath f(c, 10, 10, 7);
    double la, ln, z, prev = 3;
    for (int i = 1; i <= 20; ++i) {
        f.at(i / 20.0, &la, &ln, &z);
        QVERIFY(std::abs(la - 10) < 1e-9 && std::abs(ln - 10) < 1e-9);
        QVERIFY(z >= prev - 1e-9);
        prev = z;
    }
    QCOMPARE(z, 7.0);
}

void TestMap::greatCircleSampling()
{
    // 近的两点直接连；远的每 1.5° 一个点
    QCOMPARE(int(greatCircle({ 39.9, 116.4 }, { 40.0, 116.5 }).size()), 2);
    const auto pts = greatCircle({ 31.2, 121.5 }, { 37.6, -122.4 });
    QVERIFY(pts.size() > 50);
    QVERIFY(std::abs(pts.front().lat - 31.2) < 1e-9 && std::abs(pts.back().lng - (-122.4)) < 1e-9);
    // 大圆弧往北拱
    double maxLat = 0;
    for (const LatLng &p : pts) maxLat = std::max(maxLat, p.lat);
    QVERIFY(maxLat > 45);
}

void TestMap::nightStackMatchesElectron()
{
    const auto bands = night::bands({ 10, 30 });
    QCOMPARE(int(bands.size()), night::kBands + 1);
    double worst = 0;
    for (double alt = 5; alt >= -30; alt -= 0.05) {
        const double want = night::targetAlpha(alt);
        const double got = night::stackedAlpha(bands, alt);
        worst = std::max(worst, std::abs(want - got));
    }
    // 12 层：每层之间最多差半层的变化（smoothstep 最陡处 1.5 × .62 / 12 / 2 ≈ .039）
    QVERIFY2(worst < 0.04, qPrintable(QString::number(worst)));
    QCOMPARE(night::stackedAlpha(bands, 1.0), 0.0);
    QVERIFY(std::abs(night::stackedAlpha(bands, -20) - night::kAlpha) < 1e-9);
}

void TestMap::nightPolygonsCoverNight_data()
{
    QTest::addColumn<double>("sunLat");
    QTest::addColumn<double>("sunLng");
    QTest::newRow("春分，反太阳点在 180° 经线") << 0.0 << 0.0;
    QTest::newRow("夏至（南极在夜里）") << 23.44 << 75.0;
    QTest::newRow("冬至（北极在夜里）") << -23.44 << -120.0;
    QTest::newRow("一般") << 8.0 << 170.0;
}

void TestMap::nightPolygonsCoverNight()
{
    QFETCH(double, sunLat);
    QFETCH(double, sunLng);
    const LatLng sun{ sunLat, sunLng };
    const auto bands = night::bands(sun, night::kBands, 360);
    int checked = 0, wrong = 0;
    for (const night::Band &b : bands) {
        for (double lat = -84; lat <= 84; lat += 3)
            for (double lng = -180; lng < 180; lng += 3) {
                const double alt = night::sunAltitude(sun, lat, lng);
                if (std::abs(alt - b.alt) < 1.2) continue;   // 边界附近（采样误差）不算
                const bool want = alt < b.alt;
                const bool got = inUnwrappedRing(b.ring, lat, lng);
                ++checked;
                if (want != got) ++wrong;
            }
    }
    QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 / %2 个点判断不对").arg(wrong).arg(checked)));
}

void TestMap::subsolarSanity()
{
    // 2026-06-21 12:00 UTC：太阳直射点纬度约 23.4°，经度约 0°（误差几度：时差）
    const QDateTime t(QDate(2026, 6, 21), QTime(12, 0), QTimeZone::UTC);
    const LatLng s = night::subsolarPoint(double(t.toMSecsSinceEpoch()));
    QVERIFY2(std::abs(s.lat - 23.44) < 0.3, qPrintable(QString::number(s.lat)));
    QVERIFY2(std::abs(s.lng) < 3, qPrintable(QString::number(s.lng)));
}

void TestMap::nightGeoJsonShape()
{
    // 0.8.0：Windows（MSVC）上 QJsonArray{ ring } 被当成复制，多边形少套一层，maplibre 拒收。检查结构：
    // features[i].geometry.coordinates = [ 外环 ]，外环 = [[lng, lat], …]（≥ 4 个点、首尾相同）
    const auto bands = night::bands({ 0, 0 });
    const QJsonObject fc = QJsonDocument::fromJson(night::toGeoJson(bands).toUtf8()).object();
    const QJsonArray feats = fc.value("features").toArray();
    QCOMPARE(int(feats.size()), int(bands.size()));
    for (const QJsonValue &fv : feats) {
        const QJsonObject g = fv.toObject().value("geometry").toObject();
        QCOMPARE(g.value("type").toString(), QStringLiteral("Polygon"));
        const QJsonArray rings = g.value("coordinates").toArray();
        QCOMPARE(int(rings.size()), 1);
        const QJsonArray ring = rings.at(0).toArray();
        QVERIFY(ring.size() >= 4);
        QVERIFY(ring.at(0).isArray() && ring.at(0).toArray().size() == 2);
        QCOMPARE(ring.first().toArray(), ring.last().toArray());
        QVERIFY(fv.toObject().value("properties").toObject().value("a").toDouble() > 0);
    }
}

void TestMap::unlockMatchesElectron()
{
    const QByteArray adm1 = readFile(QStringLiteral(TP_SOURCE_DIR "/assets/admin/adm1.geojson"));
    const QByteArray golden = readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/unlock-colors.json"));
    const QByteArray travel = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"));
    if (adm1.isEmpty() || golden.isEmpty() || travel.isEmpty()) QSKIP("缺 assets/admin/adm1.geojson、tests/golden/unlock-colors.json 或 testdata/travel.json");
    UnlockModel m;
    QVERIFY(m.load(adm1));
    TripData t;
    t.build(parseJson(travel));
    const UnlockModel::Result r = m.compute(t.completedStops(), t.manualRegions());
    const QJsonObject g = QJsonDocument::fromJson(golden).object();
    QCOMPARE(r.provinces, g.value("provinces").toInt());
    QCOMPARE(r.countries, g.value("countries").toInt());
    QStringList order;
    for (const QJsonValue &v : g.value("countryOrder").toArray()) order << v.toString();
    QCOMPARE(r.countryNames, order);
    QCOMPARE(int(r.dots.value("features").toArray().size()), g.value("dots").toInt());
    const QJsonObject colors = g.value("colors").toObject();
    QCOMPARE(int(r.colorById.size()), int(colors.size()));
    for (auto it = colors.begin(); it != colors.end(); ++it)
        QVERIFY2(r.colorById.value(it.key()) == it.value().toString(),
                 qPrintable(QStringLiteral("%1：原生 %2，Electron %3").arg(it.key(), r.colorById.value(it.key()), it.value().toString())));
}

void TestMap::unlockIsDeterministic()
{
    const QByteArray adm1 = readFile(QStringLiteral(TP_SOURCE_DIR "/assets/admin/adm1.geojson"));
    if (adm1.isEmpty()) QSKIP("缺 assets/admin/adm1.geojson");
    UnlockModel m;
    QVERIFY(m.load(adm1));
    // 两个计划同一天去了不同国家：颜色按数据里的先后分，每次一样
    QVector<UnlockModel::PlanStops> plans{ { "A", "2024-05-01", { { 35.68, 139.76 } } }, { "B", "2024-05-01", { { 1.29, 103.85 } } },
                                           { "C", "", { { 39.9, 116.4 } } } };
    QVector<UnlockModel::Manual> manual{ { QStringLiteral("NZL-3400"), 2019 } };
    const auto a = m.compute(plans, manual);
    for (int i = 0; i < 5; ++i) QVERIFY(m.compute(plans, manual).colorById == a.colorById);
    // 新西兰（手动标记 2019）最早 → 第一个色相 8；日本 / 新加坡同一天，日本在前 → 205、145；中国没日期（9999）最后
    QCOMPARE(a.countryNames.first(), QStringLiteral("新西兰"));
    QVERIFY(a.colorById.value(QStringLiteral("NZL-3400")).startsWith(QStringLiteral("hsl(8,")));
    // 新加坡是"很小的国家"：整块点亮，有圆点
    QVERIFY(a.dots.value("features").toArray().size() >= 1);
}

void TestMap::unlockManualHelpers()
{
    // 0.8.5 手动标记用到的：搜索、同一单位、副标题种类、外框中心（Electron unitKey / unitName / unitSub / renderUnlockList）
    const QByteArray adm1 = readFile(QStringLiteral(TP_SOURCE_DIR "/assets/admin/adm1.geojson"));
    if (adm1.isEmpty()) QSKIP("缺 assets/admin/adm1.geojson");
    UnlockModel m;
    QVERIFY(m.load(adm1));
    // 搜"四川"：有四川省，副标题是国家（中国）
    const QVector<int> sc = m.search(QStringLiteral("四川"));
    QVERIFY(!sc.isEmpty());
    QVERIFY(m.unitName(sc.first()).contains(QStringLiteral("四川")));
    QCOMPARE(m.unitSub(sc.first()), UnlockModel::Sub::Country);
    QCOMPARE(m.country(sc.first()), QStringLiteral("中国"));
    // 空的、只有空格的：什么都不返回
    QVERIFY(m.search(QStringLiteral("  ")).isEmpty());
    // 最多 120 个
    QCOMPARE(m.search(QStringLiteral("中国")).size(), 35);   // 省界数据里中国 32 个省级 + 港澳台各一行（整组只列一行）
    QCOMPARE(m.search(QStringLiteral("a")).size(), 120);     // 国家代码里带 a 的很多：最多 120 个
    QVERIFY(m.search(QStringLiteral("中国"), 7).size() == 7);
    // 香港：港澳台归中国，整块一个单位、副标题"中国"；整组只列一行
    const QVector<int> hk = m.search(QStringLiteral("香港"));
    QCOMPARE(hk.size(), 1);
    QCOMPARE(m.unitName(hk.first()), QStringLiteral("香港"));
    QCOMPARE(m.unitSub(hk.first()), UnlockModel::Sub::China);
    const QVector<int> hkAll = m.unitMembers(hk.first());
    QVERIFY(hkAll.size() >= 1);
    for (int i : hkAll) QVERIFY(m.sameUnit(i, hk.first()));
    QCOMPARE(int(m.unitFeatures(hk.first()).value("features").toArray().size()), int(hkAll.size()));
    // 新加坡：很小的国家，整块点亮，副标题"整个国家 / 地区"
    const QVector<int> sg = m.search(QStringLiteral("SGP"));
    QCOMPARE(sg.size(), 1);
    QCOMPARE(m.unitSub(sg.first()), UnlockModel::Sub::WholeCountry);
    // 不同省不是同一单位；外框中心落在中国范围里
    QVERIFY(!m.sameUnit(sc.first(), hk.first()));
    QVERIFY(!m.sameUnit(-1, hk.first()));
    const geo::LatLng c = m.unitCenter(sc.first());
    QVERIFY(c.lat > 26 && c.lat < 35 && c.lng > 97 && c.lng < 109);
    QCOMPARE(m.indexOf(m.unitId(sc.first())), sc.first());
    QCOMPARE(m.indexOf(QStringLiteral("没有这个")), -1);
}

void TestMap::satTileSizeByDpr()
{
    // PO 2026-11-05：卫星图要清楚，不管屏幕缩放比例都用 128（和 Electron 一样）
    for (double dpr : { 0.0, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0 }) QCOMPARE(geo::satTileSize(dpr), 128);
}

void TestMap::prefetchAnyTileSize()
{
    // 128 / 256 和 Electron 一样（prefetchMatchesElectron）；中间的大小：级数在两者之间，且 128 ≥ 它 ≥ 256
    const double w = 100, n = 30.5, e = 100.5, s = 30;   // 视野小，两级都不会因为超过 200 张被跳过
    for (double zoom : { 4.2, 5.5, 6.9, 8.3 }) {
        const auto t128 = geo::prefetchTiles(w, n, e, s, zoom, 128, 18);
        const auto t256 = geo::prefetchTiles(w, n, e, s, zoom, 256, 18);
        for (int ts : { 146, 171, 205 }) {
            const auto t = geo::prefetchTiles(w, n, e, s, zoom, ts, 18);
            QVERIFY(!t.empty());
            const int z = t.front().z, z128 = t128.empty() ? 99 : t128.front().z, z256 = t256.empty() ? -1 : t256.front().z;
            QVERIFY2(z >= z256 && z <= z128, qPrintable(QStringLiteral("缩放 %1，瓦片 %2：%3 级（256：%4，128：%5）").arg(zoom).arg(ts).arg(z).arg(z256).arg(z128)));
        }
    }
}

void TestMap::unlockGeometryRoundTrip()
{
    // 0.8.6 起不再留着原样的 geometry：输出（去过的省、悬停描边）由坐标重新拼，要和原文的 geometry 一模一样
    const QByteArray adm1 = readFile(QStringLiteral(TP_SOURCE_DIR "/assets/admin/adm1.geojson"));
    if (adm1.isEmpty()) QSKIP("缺 assets/admin/adm1.geojson");
    UnlockModel m;
    QVERIFY(m.load(adm1));
    const QJsonArray feats = QJsonDocument::fromJson(adm1).object().value("features").toArray();
    QCOMPARE(int(feats.size()), m.featureCount());
    int multi = 0, checked = 0;
    for (int i = 0; i < feats.size(); ++i) {
        const QJsonObject want = feats[i].toObject().value("geometry").toObject();
        const QString id = feats[i].toObject().value("properties").toObject().value("id").toString();
        const int k = m.indexOf(id);
        QVERIFY(k >= 0);
        // unitFeatures 给的是整组；按 id 找到这一块
        QJsonObject got;
        for (const QJsonValue &fv : m.unitFeatures(k).value("features").toArray())
            if (fv.toObject().value("properties").toObject().value("id").toString() == id) got = fv.toObject().value("geometry").toObject();
        if (want.value("type").toString() == QLatin1String("MultiPolygon")) ++multi;
        QVERIFY2(QJsonDocument(got).toJson(QJsonDocument::Compact) == QJsonDocument(want).toJson(QJsonDocument::Compact), qPrintable(id));
        ++checked;
    }
    QVERIFY(multi > 0);
    QCOMPARE(checked, int(feats.size()));
}

void TestMap::prefetchMatchesElectron()
{
    // tests/golden/prefetch.json 由 tools/claude-kit/tile-golden/gen.js 用 Electron 原样的 tileXY / prefetchTiles 生成
    const QByteArray golden = readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/prefetch.json"));
    if (golden.isEmpty()) QSKIP("缺 tests/golden/prefetch.json");
    const QJsonArray cases = QJsonDocument::fromJson(golden).array();
    QVERIFY(cases.size() >= 10);
    for (const QJsonValue &cv : cases) {
        const QJsonObject c = cv.toObject(), v = c.value("view").toObject();
        const auto tiles = geo::prefetchTiles(v.value("w").toDouble(), v.value("n").toDouble(), v.value("e").toDouble(), v.value("s").toDouble(),
                                              v.value("zoom").toDouble(), v.value("tileSize").toInt(), v.value("tileMaxZoom").toInt());
        QStringList got;
        for (const geo::TileXY &t : tiles) got << QStringLiteral("%1/%2/%3").arg(t.z).arg(t.x).arg(t.y);
        QStringList want;
        for (const QJsonValue &t : c.value("tiles").toArray()) want << t.toString();
        QCOMPARE(got, want);
        // tileXY 本身（7 级，视野的两个角）
        const QJsonArray xy = c.value("xy").toArray();
        const geo::TileXY a = geo::tileXY(v.value("w").toDouble(), v.value("n").toDouble(), 7);
        const geo::TileXY b = geo::tileXY(v.value("e").toDouble(), v.value("s").toDouble(), 7);
        QCOMPARE(a.x, xy[0].toArray()[0].toInt()); QCOMPARE(a.y, xy[0].toArray()[1].toInt());
        QCOMPARE(b.x, xy[1].toArray()[0].toInt()); QCOMPARE(b.y, xy[1].toArray()[1].toInt());
    }
}

void TestMap::tripDataStopsAndRoutes()
{
    const QByteArray travel = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"));
    if (travel.isEmpty()) QSKIP("缺 testdata/travel.json");
    TripData t;
    t.build(parseJson(travel));
    QVERIFY(t.plans().size() > 10);
    QVERIFY(t.stopCount() > 100);
    QVERIFY(t.routeCount() > 0 && t.legCount() > t.routeCount());
    // 每条路线：一条白边 + 每段一条彩线
    const QJsonArray f = t.routesGeoJson().value("features").toArray();
    int edges = 0, segs = 0;
    for (const QJsonValue &v : f) (v.toObject().value("properties").toObject().value("k").toString() == "c" ? edges : segs)++;
    QCOMPARE(edges, t.routeCount());
    QCOMPARE(segs, t.legCount());
    QCOMPARE(int(t.stopsGeoJson().value("features").toArray().size()), t.stopCount());
}

// 0.9.2：按计划缓存。第二次 build 不重算；改了一个计划只重算它；结果和一个新的 TripData 现算完全一样；
// "已解锁"的输入只在已完成的计划 / 手动标记变了时算变了；筛选只换要画的、不重算
void TestMap::tripDataCache()
{
    const QByteArray travel = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"));
    if (travel.isEmpty()) QSKIP("缺 testdata/travel.json");
    tp::json::Value root = parseJson(travel);
    const auto same = [](const TripData &a, const TripData &b) {
        if (a.routesGeoJson() != b.routesGeoJson() || a.stopsGeoJson() != b.stopsGeoJson()) return false;
        if (a.plans().size() != b.plans().size() || a.legCount() != b.legCount() || a.roadLegCount() != b.roadLegCount()) return false;
        if (a.routesJsonExcept(QString()) != b.routesJsonExcept(QString())) return false;
        for (int i = 0; i < a.plans().size(); ++i) {
            const TripPlan &x = a.plans()[i], &y = b.plans()[i];
            if (x.id != y.id || x.markerPos.size() != y.markerPos.size() || x.segments.size() != y.segments.size() || x.west != y.west || x.north != y.north)
                return false;
        }
        return true;
    };
    TripData t;
    t.build(root);
    const int n = int(t.plans().size());
    QCOMPARE(t.recomputed(), n);
    QVERIFY(t.unlockInputsChanged());
    t.build(root);
    QCOMPARE(t.recomputed(), 0);
    QVERIFY(!t.unlockInputsChanged());
    // 深比较相等但不是同一份（像撤销回来的数据）也命中
    t.build(parseJson(travel));
    QCOMPARE(t.recomputed(), 0);

    // 找一个想去的计划、一个已完成的计划
    int wishIdx = -1, doneIdx = -1;
    const tp::json::Array &arr = root[QStringLiteral("plans")].asArray();
    for (int i = 0; i < int(arr.size()); ++i) {
        const QString st = arr[size_t(i)][QStringLiteral("status")].toString();
        if (st == QLatin1String("wish") && wishIdx < 0) wishIdx = i;
        if (st == QLatin1String("completed") && doneIdx < 0 && !arr[size_t(i)][QStringLiteral("stops")].asArray().empty()) doneIdx = i;
    }
    QVERIFY(wishIdx >= 0 && doneIdx >= 0);
    // 改想去计划的名字：只重算 1 个，"已解锁"不变；结果和现算一样
    tp::json::Value r2 = root;
    r2.mutObject().find(QStringLiteral("plans"))->mutArray()[size_t(wishIdx)].mutObject().set(QStringLiteral("name"), tp::json::Value(QStringLiteral("改过的名字")));
    t.build(r2);
    QCOMPARE(t.recomputed(), 1);
    QVERIFY(!t.unlockInputsChanged());
    TripData fresh;
    fresh.build(r2);
    QVERIFY(same(t, fresh));
    // 挪已完成计划的第一个站点：重算 1 个，"已解锁"变了
    tp::json::Value r3 = r2;
    tp::json::Value &st0 = r3.mutObject().find(QStringLiteral("plans"))->mutArray()[size_t(doneIdx)].mutObject().find(QStringLiteral("stops"))->mutArray()[0];
    st0.mutObject().set(QStringLiteral("lat"), tp::json::Value(st0[QStringLiteral("lat")].toNumber() + 0.5));
    t.build(r3);
    QCOMPARE(t.recomputed(), 1);
    QVERIFY(t.unlockInputsChanged());
    TripData fresh3;
    fresh3.build(r3);
    QVERIFY(same(t, fresh3));
    // 筛选（只画已完成的）：不重算，结果和现算一样
    const auto onlyDone = [](const tp::json::Value &p) { return p[QStringLiteral("status")].toString() == QLatin1String("completed"); };
    t.build(r3, onlyDone);
    QCOMPARE(t.recomputed(), 0);
    QVERIFY(!t.unlockInputsChanged());
    TripData fresh4;
    fresh4.build(r3, onlyDone);
    QVERIFY(same(t, fresh4));
    QVERIFY(int(t.plans().size()) < n);
    // 删掉已完成的计划："已解锁"变了
    tp::json::Value r5 = r3;
    auto &plansArr = r5.mutObject().find(QStringLiteral("plans"))->mutArray();
    plansArr.erase(plansArr.begin() + doneIdx);
    t.build(r5);
    QCOMPARE(t.recomputed(), 0);
    QVERIFY(t.unlockInputsChanged());
}

void TestMap::tripDataJsNumbers()
{
    // Electron validStops：Number.isFinite(+v)。null → 0 算有效；没有这个键 / "abc" → 无效；"12.5" → 12.5
    const char *doc = R"({"plans":[{"id":"p","name":"x","type":"route","status":"planning","stops":[
        {"name":"a","lat":"12.5","lng":100},
        {"name":"b","lat":null,"lng":0},
        {"name":"c","lng":5},
        {"name":"d","lat":"abc","lng":5},
        {"name":"e","lat":13,"lng":101}]}],
      "visitedRegions":[{"id":"CHN-1810","year":2020},{"id":"X"},{"year":1}]})";
    TripData t;
    t.build(parseJson(QByteArray(doc)));
    QCOMPARE(t.plans().size(), 1);
    const auto &s = t.plans().first().stops;
    QCOMPARE(s.size(), 3);
    QCOMPARE(s[0].name, QStringLiteral("a"));
    QCOMPARE(s[0].lat, 12.5);
    QCOMPARE(s[1].name, QStringLiteral("b"));
    QCOMPARE(s[1].lat, 0.0);
    QCOMPARE(s[2].name, QStringLiteral("e"));
    QCOMPARE(t.manualRegions().size(), 2);
    QCOMPARE(t.manualRegions()[0].year, 2020);
    QCOMPARE(t.manualRegions()[1].year, 0);
}

void TestMap::markerStyles()
{
    TripPlan route;
    route.type = "route"; route.status = "planning";
    for (int i = 0; i < 3; ++i) route.stops.push_back(TripStop{});
    // 数字顺序（默认）：圆形，终点方块；文字是序号
    auto st = markers::stopStyle(route, 0, 3);
    QCOMPARE(st.shape, QStringLiteral("circle"));
    QCOMPARE(st.text, QStringLiteral("1"));
    QCOMPARE(st.color, QStringLiteral("#e8590c"));
    QCOMPARE(markers::stopStyle(route, 2, 3).shape, QStringLiteral("square"));
    // 指定了外形：终点不再变方块
    route.shape = "diamond";
    QCOMPARE(markers::stopStyle(route, 2, 3).shape, QStringLiteral("diamond"));
    // 圆点模式
    route.markerMode = "dot";
    st = markers::stopStyle(route, 1, 3);
    QVERIFY(st.dot);
    QVERIFY(st.text.isEmpty() && st.icon.isEmpty());
    // 图标模式：站点图标优先，其次计划图标
    route.markerMode = "icon";
    route.icon = "hiking";
    route.stops[1].icon = "hotel";
    QCOMPARE(markers::stopStyle(route, 0, 3).icon, QStringLiteral("hiking"));
    QCOMPARE(markers::stopStyle(route, 1, 3).icon, QStringLiteral("hotel"));
    // 单一目的地：默认水滴，没有文字；站点颜色覆盖计划颜色
    TripPlan single;
    single.type = "single"; single.status = "wish"; single.color = "#2a78d6";
    single.stops.push_back(TripStop{});
    single.stops[0].color = "#c2255c";
    st = markers::stopStyle(single, 0, 1);
    QCOMPARE(st.shape, QStringLiteral("pin"));
    QVERIFY(st.text.isEmpty());
    QCOMPARE(st.color, QStringLiteral("#c2255c"));
    // 只有一个站点的"路线"按单一目的地画（plan.type === 'route' && n > 1）
    TripPlan one = route;
    one.stops.resize(1);
    QCOMPARE(markers::stopStyle(one, 0, 1).shape, QStringLiteral("diamond"));
    QVERIFY(markers::stopStyle(one, 0, 1).text.isEmpty());
}

void TestMap::markerGeometry()
{
    markers::Style st;
    st.shape = "pin";
    auto g = markers::geomFor(st, true);
    QCOMPARE(g.w, 28.0); QCOMPARE(g.h, 38.0); QCOMPARE(g.ax, 14.0); QCOMPARE(g.ay, 37.0);
    st.shape = "circle"; st.text = "3";
    g = markers::geomFor(st, true);
    QCOMPARE(g.w, 22.0); QCOMPARE(g.ax, 11.0);
    st.icon = "hiking"; st.text.clear();
    QCOMPARE(markers::geomFor(st, true).w, 26.0);    // 有图标 26
    QCOMPARE(markers::geomFor(st, false).w, 22.0);   // 图标字体不能用时退回 22
    st.icon.clear(); st.dot = true;
    QCOMPARE(markers::geomFor(st, true).w, 14.0);
    QCOMPARE(markers::clusterSize(2), 26);
    QCOMPARE(markers::clusterSize(10), 28);
    QCOMPARE(markers::clusterSize(50), 32);
}

void TestMap::clusterColors()
{
    // 和 Electron 的几个固定样例一样（marker-clusters.json 的 colorSamples）
    const QByteArray golden = readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/marker-clusters.json"));
    if (golden.isEmpty()) QSKIP("缺 tests/golden/marker-clusters.json");
    const QJsonArray samples = QJsonDocument::fromJson(golden).object().value("colorSamples").toArray();
    QVERIFY(samples.size() >= 5);
    for (const QJsonValue &v : samples) {
        QStringList in;
        for (const QJsonValue &c : v.toObject().value("in").toArray()) in << c.toString();
        QCOMPARE(markers::averageColor(in), v.toObject().value("avg").toString());
        QCOMPARE(markers::isLightColor(markers::averageColor(in)), v.toObject().value("light").toBool());
    }
    QVERIFY(markers::isLightColor("#ffffff"));
    QVERIFY(!markers::isLightColor("#364152"));
    QVERIFY(!markers::isLightColor("bad"));
    double rgb[3];
    QVERIFY(markers::hexToRgb(" #ABC ", rgb));
    QVERIFY(!markers::hexToRgb("##abc", rgb));   // JS replace('#', '') 只去掉第一个
}

void TestMap::clusterTiers()
{
    QCOMPARE(markers::clusterTier(-0.5), 0);
    QCOMPARE(markers::clusterTier(0.99), 0);
    QCOMPARE(markers::clusterTier(1.0), 1);
    QCOMPARE(markers::clusterTier(5.999), 5);
    QCOMPARE(markers::clusterTier(5.9999999), -1);   // 加 1e-6 后算 6 级（Electron 一样）
    QCOMPARE(markers::clusterTier(6), -1);
    QCOMPARE(markers::clusterKm(0), 1400.0);
    QCOMPARE(markers::clusterKm(1), 1400.0);
    QCOMPARE(markers::clusterKm(2), 700.0);
    QCOMPARE(markers::clusterKm(3), 350.0);
    QCOMPARE(markers::clusterKm(4), 180.0);
    QCOMPARE(markers::clusterKm(5), 90.0);
}

void TestMap::clustersMatchElectron()
{
    const QByteArray golden = readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/marker-clusters.json"));
    const QByteArray travel = readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"));
    if (golden.isEmpty() || travel.isEmpty()) QSKIP("缺 tests/golden/marker-clusters.json 或 testdata/travel.json");
    TripData t;
    t.build(parseJson(travel));
    struct M { QString plan; int stop; LatLng base; QString color; markers::Style st; };
    QVector<M> all;
    for (const TripPlan &p : t.plans())
        for (int i = 0; i < p.stops.size(); ++i)
            all.push_back({ p.id, i, p.markerPos[i], TripData::stopColor(p, p.stops[i]), markers::stopStyle(p, i, int(p.stops.size())) });
    const QJsonObject g = QJsonDocument::fromJson(golden).object();
    // 样式：每个标记的外形、文字、圆点、图标、颜色
    const QJsonArray styles = g.value("styles").toArray();
    QCOMPARE(int(styles.size()), int(all.size()));
    for (int i = 0; i < all.size(); ++i) {
        const QJsonArray e = styles[i].toArray();
        QCOMPARE(all[i].plan, e[0].toString());
        QCOMPARE(all[i].stop, e[1].toInt());
        QCOMPARE(all[i].st.shape, e[2].toString());
        QCOMPARE(all[i].st.text, e[3].toString());
        QCOMPARE(all[i].st.dot, e[4].toBool());
        QCOMPARE(all[i].st.icon, e[5].toString());
        QCOMPARE(all[i].st.color, e[6].toString());
    }
    // 合并：0–6 级每一层的圆圈（成员、颜色、字色、中心）
    QVector<LatLng> pts;
    for (const M &m : all) pts.push_back(m.base);
    int checked = 0;
    for (const QJsonValue &tv : g.value("tiers").toArray()) {
        const QJsonObject to = tv.toObject();
        const int tier = markers::clusterTier(to.value("zoom").toDouble());
        QCOMPARE(tier, to.value("tier").isNull() ? -1 : to.value("tier").toInt());
        QVector<QVector<int>> mine;
        if (tier >= 0)
            for (const QVector<int> &mem : markers::cluster(pts, markers::clusterKm(tier)).members)
                if (mem.size() > 1) mine.push_back(mem);
        const QJsonArray theirs = to.value("clusters").toArray();
        QCOMPARE(int(mine.size()), int(theirs.size()));
        for (int k = 0; k < mine.size(); ++k) {
            const QJsonObject c = theirs[k].toObject();
            const QJsonArray mem = c.value("members").toArray();
            QCOMPARE(int(mine[k].size()), int(mem.size()));
            QStringList colors;
            std::vector<LatLng> cpts;
            for (int j = 0; j < mine[k].size(); ++j) {
                const M &m = all[mine[k][j]];
                QCOMPARE(m.plan, mem[j].toArray()[0].toString());
                QCOMPARE(m.stop, mem[j].toArray()[1].toInt());
                colors << m.color;
                cpts.push_back(m.base);
            }
            const QString avg = markers::averageColor(colors);
            QCOMPARE(avg, c.value("color").toString());
            QCOMPARE(markers::isLightColor(avg), c.value("light").toBool());
            const LatLng ctr = centerOf(cpts);
            QVERIFY(std::abs(ctr.lat - c.value("center").toObject().value("lat").toDouble()) < 1e-9);
            QVERIFY(std::abs(ctr.lng - c.value("center").toObject().value("lng").toDouble()) < 1e-9);
            ++checked;
        }
    }
    QVERIFY(checked > 100);
}

void TestMap::clusterSeedNotCentroid()
{
    // 和组的"种子"（第一个点）比距离，不是和中心比：A(0,0)、B(0,6°≈667 km)、C(0,12°)，700 km 时 A+B 一组，C 自己一组
    const QVector<LatLng> pts{ { 0, 0 }, { 0, 6 }, { 0, 12 } };
    const auto g = markers::cluster(pts, 700);
    QCOMPARE(g.members.size(), 2);
    QCOMPARE(g.members[0], (QVector<int>{ 0, 1 }));
    QCOMPARE(g.members[1], (QVector<int>{ 2 }));
    // 离两个种子都在范围内：并进最近的那个
    const QVector<LatLng> q{ { 0, 0 }, { 0, 10 }, { 0, 6 } };
    const auto h = markers::cluster(q, 700);
    QCOMPARE(h.members.size(), 2);
    QCOMPARE(h.groupOf[2], 1);
}

void TestMap::spreadCircle()
{
    Camera c;
    c.lat = 35.0; c.lng = 139.0; c.zoom = 15; c.width = 800; c.height = 600;
    // 同一地点 3 个标记（两个计划）+ 15 m 外 1 个 + 远处 1 个
    const LatLng p{ 35.0, 139.0 };
    const QVector<markers::SpreadItem> items{ { p, "b", 0 }, { p, "a", 1 }, { p, "a", 0 }, { { 35.0001, 139.0001 }, "c", 0 }, { { 35.01, 139.01 }, "d", 0 } };
    const auto r = markers::spread(items, c);
    QCOMPARE(r.dots.size(), 1);
    QCOMPARE(r.offset[4].x, 0.0);
    QCOMPARE(r.offset[4].y, 0.0);
    // 4 个一组：半径 max(22, 4·30/2π) = 22；顺序 a0、a1、b0、c0，从正上方顺时针
    const auto at = [&](int i) { const Pt b = c.pixelFor(items[i].base.lat, items[i].base.lng); return Pt{ b.x + r.offset[i].x, b.y + r.offset[i].y }; };
    const Pt ctr = c.pixelFor(p.lat, p.lng);
    QVERIFY(std::abs(at(2).x - ctr.x) < 1e-6 && std::abs(at(2).y - (ctr.y - 22)) < 1e-6);   // a0 在正上方
    QVERIFY(std::abs(at(1).x - (ctr.x + 22)) < 1e-6 && std::abs(at(1).y - ctr.y) < 1e-6);   // a1 在右边
    QVERIFY(std::abs(at(0).x - ctr.x) < 1e-6 && std::abs(at(0).y - (ctr.y + 22)) < 1e-6);   // b0 在下面
    QVERIFY(std::abs(at(3).x - (ctr.x - 22)) < 1e-6 && std::abs(at(3).y - ctr.y) < 1e-6);   // c0 在左边
    // 个数多了半径变大：20 个 → 20·30/2π ≈ 95.5
    QVector<markers::SpreadItem> many;
    for (int i = 0; i < 20; ++i) many.push_back({ p, "x", i });
    const auto r2 = markers::spread(many, c);
    QVERIFY(std::abs(std::hypot(r2.offset[0].x, r2.offset[0].y) - 20 * 30 / (2 * kPi)) < 1e-6);
}

void TestMap::spreadOnlyWhenZoomedIn()
{
    Camera c;
    c.lat = 35.0; c.lng = 139.0; c.zoom = 13.9; c.width = 800; c.height = 600;
    const LatLng p{ 35.0, 139.0 };
    const QVector<markers::SpreadItem> items{ { p, "a", 0 }, { p, "a", 1 } };
    auto r = markers::spread(items, c);
    QVERIFY(r.dots.isEmpty());
    QCOMPARE(r.offset[0].x, 0.0);
    c.zoom = 14;
    r = markers::spread(items, c);
    QCOMPARE(r.dots.size(), 1);
    // 视野外（再扩一半以外）的不错开
    c.lng = 140.0;
    r = markers::spread(items, c);
    QVERIFY(r.dots.isEmpty());
    // 跨 180° 经线：相机在 179.999，点在 -179.9995（在另一份世界副本上，但在视野里）
    c.lat = 0; c.lng = 179.999; c.zoom = 15;
    const QVector<markers::SpreadItem> w{ { { 0, -179.9995 }, "a", 0 }, { { 0, -179.9995 }, "b", 0 } };
    r = markers::spread(w, c);
    QCOMPARE(r.dots.size(), 1);
}

// ---------- 0.8.4：道路路线 ----------

void TestMap::roadsMatchElectron()
{
    // tests/golden/roads.json：Electron map-app.js 的原样代码跑出来的（tools/claude-kit/road-golden/gen.js）
    const QJsonObject g = QJsonDocument::fromJson(readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/roads.json"))).object();
    QVERIFY(!g.isEmpty());
    for (const QJsonValue &v : g.value("toFixed").toArray()) {
        const QJsonObject o = v.toObject();
        const double x = o.value("v").toDouble();
        QCOMPARE(roads::jsToFixed(x, 5), o.value("f5").toString());
        QCOMPARE(roads::jsToFixed(x, 6), o.value("f6").toString());
    }
    QCOMPARE(roads::jsToFixed(-0.0, 5), QStringLiteral("0.00000"));
    for (const QJsonValue &v : g.value("keys").toArray()) {
        const QJsonArray in = v.toObject().value("in").toArray();
        QCOMPARE(roads::legKeyOf("drive", in[0].toDouble(), in[1].toDouble(), in[2].toDouble(), in[3].toDouble()), v.toObject().value("drive").toString());
    }
    for (const QJsonValue &v : g.value("encEdge").toArray()) {
        QVector<LatLng> pts;
        for (const QJsonValue &p : v.toObject().value("pts").toArray()) pts.push_back({ p.toArray()[0].toDouble(), p.toArray()[1].toDouble() });
        QCOMPARE(roads::encodePolyline(pts), v.toObject().value("enc").toString());
    }
    int n = 0;
    for (const QJsonValue &v : g.value("cases").toArray()) {
        const QJsonObject o = v.toObject();
        const QVector<LatLng> pts = roads::decodePolyline(o.value("path").toString());
        QCOMPARE(int(pts.size()), o.value("n").toInt());
        QCOMPARE(pts.first().lat, o.value("first").toArray()[0].toDouble());
        QCOMPARE(pts.last().lng, o.value("last").toArray()[1].toDouble());
        QCOMPARE(roads::encodePolyline(pts), o.value("reencoded").toString());
        const QVector<LatLng> simp = roads::simplify(pts, 0.003);
        QCOMPARE(int(simp.size()), o.value("simplifiedN").toInt());
        QCOMPARE(roads::encodePolyline(simp), o.value("simplified").toString());
        ++n;
    }
    QVERIFY(n >= 20);
}

void TestMap::roadsPendingAndApply()
{
    tp::json::Value root = parseJson(readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json")));
    // 真实数据（从 Electron 导入）里的道路折线全部和坐标对得上（key 的写法和 Electron 一样）
    QCOMPARE(roads::pendingLegs(root, {}).size(), 0);
    // 去掉一段的折线 → 还没算 1 段；坐标挪了一点 → key 对不上，也算还没算
    QString planId; int stopIdx = -1; QString oldPath;
    for (tp::json::Value &pv : root.mutObject().find("plans")->mutArray()) {
        if (pv["type"].toString() != "route") continue;
        auto &arr = pv.mutObject().find("stops")->mutArray();
        for (int i = 0; i + 1 < int(arr.size()); ++i) {
            tp::json::Value *lv = arr[i].mutObject().find("leg");
            if (!lv || (*lv)["mode"].toString() != "drive" || (*lv)["path"].toString().isEmpty()) continue;
            oldPath = (*lv)["path"].toString();
            lv->mutObject().remove("path");
            planId = pv["id"].toString();
            stopIdx = i;
            break;
        }
        if (stopIdx >= 0) break;
    }
    QVERIFY(stopIdx >= 0);
    QVector<roads::Pending> pend = roads::pendingLegs(root, {});
    QCOMPARE(pend.size(), 1);
    QCOMPARE(pend[0].planId, planId);
    QCOMPARE(pend[0].stopIndex, stopIdx);
    QCOMPARE(pend[0].mode, QStringLiteral("drive"));
    // 记住算不出来的不再算进去
    QCOMPARE(roads::pendingLegs(root, { pend[0].key }).size(), 0);
    // 写回：leg 里其他的键（mode）保留，key 写上，之后不再是"还没算"
    QVERIFY(roads::applyLeg(root, pend[0], oldPath, 1234, 567));
    QCOMPARE(roads::pendingLegs(root, {}).size(), 0);
    // 写回时站点已经挪了：不写
    for (tp::json::Value &pv : root.mutObject().find("plans")->mutArray())
        if (pv["id"].toString() == planId) {
            tp::json::Value &st = pv.mutObject().find("stops")->mutArray()[stopIdx];
            QCOMPARE(st["leg"]["dist"].toNumber(), 1234.0);
            QCOMPARE(st["leg"]["mode"].toString(), QStringLiteral("drive"));
            st.mutObject().set("lat", tp::json::Value(st["lat"].toNumber() + 0.01));
        }
    const QVector<roads::Pending> moved = roads::pendingLegs(root, {});
    QCOMPARE(moved.size(), 1);   // 挪过的站点：折线和坐标对不上，又算"还没算"
    QVERIFY(!roads::applyLeg(root, pend[0], oldPath, 1, 1));   // 旧的 key 写不进去
    // 单一目的地、飞机 / 火车的段不算
    tp::json::Value tiny = parseJson(R"({"plans":[{"id":"a","type":"single","stops":[{"lat":1,"lng":2,"leg":{"mode":"drive"}},{"lat":3,"lng":4}]},
        {"id":"b","type":"route","stops":[{"lat":1,"lng":2,"leg":{"mode":"train"}},{"lat":3,"lng":4,"leg":{"mode":"walk"}},{"lat":"x","lng":4},{"lat":5,"lng":6}]}]})");
    pend = roads::pendingLegs(tiny, {});
    QCOMPARE(pend.size(), 1);   // 坐标无效的站点跳过：第 2 站 → 第 4 站
    QCOMPARE(pend[0].stopIndex, 1);
    QCOMPARE(pend[0].key, QStringLiteral("walk:3.00000,4.00000,5.00000,6.00000"));
    QCOMPARE(roads::osrmUrl("walk", 3, 4, 5, 6).toString(),
             QStringLiteral("https://routing.openstreetmap.de/routed-foot/route/v1/foot/4.000000,3.000000;6.000000,5.000000?overview=full&geometries=polyline&steps=false"));
    QVERIFY(roads::osrmUrl("drive", 3, 4, 5, 6).toString().startsWith("https://routing.openstreetmap.de/routed-car/route/v1/driving/"));
}

void TestMap::roadsParseOsrm()
{
    // 正常：抽稀、重新编码、距离和时间取整（Math.round）
    QVector<LatLng> line;
    for (int i = 0; i <= 100; ++i) line.push_back({ 35 + i * 0.001, 139 + i * 0.001 + (i % 2 ? 0.00001 : 0) });
    const QByteArray ok = QJsonDocument(QJsonObject{ { "code", "Ok" }, { "routes", QJsonArray{ QJsonObject{
        { "geometry", roads::encodePolyline(line) }, { "distance", 15234.5 }, { "duration", 1200.4 } } } } }).toJson();
    roads::OsrmResult r = roads::parseOsrm(ok, 200);
    QCOMPARE(r.kind, roads::OsrmResult::Ok);
    QCOMPARE(r.dist, 15235.0);
    QCOMPARE(r.time, 1200.0);
    QCOMPARE(roads::decodePolyline(r.path).size(), 2);   // 几乎是直线：只剩两头
    // 找不到路 / 太远：记住（permanent）；其他错误不记
    r = roads::parseOsrm(R"({"code":"NoRoute","message":"Impossible route"})", 400);
    QCOMPARE(r.kind, roads::OsrmResult::NoRoute);
    QVERIFY(r.permanent());
    r = roads::parseOsrm(R"({"code":"TooBig"})", 400);
    QVERIFY(r.permanent());
    r = roads::parseOsrm(R"({"code":"InvalidQuery","message":"Query string malformed"})", 400);
    QCOMPARE(r.kind, roads::OsrmResult::ServiceError);
    QCOMPARE(r.detail, QStringLiteral("Query string malformed"));
    QVERIFY(!r.permanent());
    r = roads::parseOsrm("<html>502 Bad Gateway</html>", 502);
    QCOMPARE(r.kind, roads::OsrmResult::BadReply);
    QCOMPARE(r.detail, QStringLiteral("502"));
}

void TestMap::tripRoutesSplit()
{
    TripData t;
    t.build(parseJson(readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json"))));
    QString route;
    for (const TripPlan &p : t.plans())
        if (p.isRoute()) { route = p.id; QVERIFY(!p.segments.isEmpty()); QVERIFY(p.west <= p.east); break; }
    QVERIFY(!route.isEmpty());
    const auto count = [](const QString &json) { return QJsonDocument::fromJson(json.toUtf8()).object().value("features").toArray().size(); };
    const int all = int(t.routesGeoJson().value("features").toArray().size());
    const int except = count(t.routesJsonExcept(route)), only = count(t.routesJsonOnly(route));
    QVERIFY(only >= 2);   // 白边 + 至少一段
    QCOMPARE(except + only, all);
    QCOMPARE(count(t.routesJsonExcept(QString())), all);
    QCOMPARE(count(t.routesJsonOnly(QString())), 0);
    QVERIFY(!t.routesJsonExcept(route).contains(route));
}

namespace {
QJsonObject plansGolden()
{
    return QJsonDocument::fromJson(readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/plans.json"))).object();
}
tp::json::Value plansInput(const QJsonObject &g)
{
    return parseJson(QJsonDocument(g.value("input").toObject()).toJson(QJsonDocument::Compact));
}
QSet<QString> setOf(const QJsonValue &v)
{
    QSet<QString> s;
    for (const QJsonValue &x : v.toArray()) s.insert(x.toString());
    return s;
}
} // namespace

void TestMap::plansSortMatchesElectron()
{
    const QJsonObject g = plansGolden();
    if (g.isEmpty()) QSKIP("缺 tests/golden/plans.json");
    const tp::json::Value root = plansInput(g);
    const tp::json::Array &arr = root[QStringLiteral("plans")].asArray();
    for (bool pin : { true, false }) {
        const QVector<int> idx = plans::sortedIndexes(root, pin);
        const QJsonArray want = g.value(pin ? "sortPin" : "sortNoPin").toArray();
        QCOMPARE(idx.size(), want.size());
        for (int i = 0; i < idx.size(); ++i)
            QVERIFY2(arr[idx[i]][QStringLiteral("id")].toString() == want[i].toString(),
                     qPrintable(QStringLiteral("%1 第 %2 个：%3 ≠ %4").arg(pin ? "置顶" : "不看置顶").arg(i).arg(arr[idx[i]][QStringLiteral("id")].toString(), want[i].toString())));
    }
}

void TestMap::nightStripesNoGapsNoSteps()
{
    // 0.8.7：互不重叠的细带。每个点只落在一条带里，带的序号和太阳高度对得上（边界上允许差一条），白天的点不在任何带里；
    // 相邻带的不透明度差 < 2%（看不出台阶）。几种太阳位置：赤道、北半球夏天、南半球夏天、经线 180° 附近
    for (const LatLng sun : { LatLng{ 0, 0 }, LatLng{ 23.4, 100 }, LatLng{ -23.4, -170 }, LatLng{ -16, 179.5 } }) {
        const auto st = night::stripes(sun);
        QCOMPARE(int(st.size()), night::kStripes + 1);
        for (size_t i = 1; i + 1 < st.size(); ++i) QVERIFY(std::abs(st[i].opacity - st[i - 1].opacity) < 0.02);
        int far = 0, total = 0;
        for (double lat = -83.7; lat <= 84; lat += 6)   // 不正好落在边上（边上的点射线法算不准）
            for (double lng = -179.3; lng < 180; lng += 6) {
                const double alt = night::sunAltitude(sun, lat, lng);
                const int got = night::stripeAt(st, lat, lng);
                if (alt > night::kDayAlt + 0.3) { QVERIFY2(got < 0, qPrintable(QStringLiteral("白天的点 %1,%2 落在带 %3").arg(lat).arg(lng).arg(got))); continue; }
                if (alt > night::kDayAlt - 0.3) continue;   // 昼夜分界线附近不算
                ++total;
                const int want = std::min(night::kStripes, int((night::kDayAlt - alt) / (night::kTwilight / night::kStripes)));
                QVERIFY2(got >= 0, qPrintable(QStringLiteral("夜里的点 %1,%2（太阳高度 %3）没被盖住").arg(lat).arg(lng).arg(alt)));
                if (std::abs(got - want) > 1) ++far;
            }
        QVERIFY2(far == 0, qPrintable(QStringLiteral("%1 / %2 个点差了不止一条带").arg(far).arg(total)));
    }
    const QJsonObject fc = QJsonDocument::fromJson(night::toGeoJson(night::stripes({ 0, 0 })).toUtf8()).object();
    QVERIFY(fc.value("features").toArray().size() > 40);
    QCOMPARE(fc.value("features").toArray().first().toObject().value("geometry").toObject().value("type").toString(), QStringLiteral("MultiPolygon"));
}

void TestMap::plansNameOrder()
{
    // Node（ICU）的 ['Zurich', '北京', …].sort((a, b) => a.localeCompare(b, 'zh')) 的结果；Windows 的系统排序规则会把拉丁字母排在汉字前面（0.8.7 验证）
    const QStringList want = { "", "!标点", "2号线", "阿坝", "北京", "弁天島", "東京", "乔戈里峰（K2）", "Acropolis", "Zurich", "Ак-Суу" };
    QStringList l = { "Zurich", "北京", "阿坝", "2号线", "!标点", "", "Acropolis", "弁天島", "乔戈里峰（K2）", "東京", "Ак-Суу" };
    std::stable_sort(l.begin(), l.end(), [](const QString &a, const QString &b) { return plans::compareNames(a, b) < 0; });
    QCOMPARE(l, want);
}

void TestMap::plansFilterMatchesElectron()
{
    const QJsonObject g = plansGolden();
    if (g.isEmpty()) QSKIP("缺 tests/golden/plans.json");
    const tp::json::Value root = plansInput(g);
    const tp::json::Array &arr = root[QStringLiteral("plans")].asArray();
    auto findPlan = [&arr](const QString &id) -> const tp::json::Value & {
        for (const tp::json::Value &p : arr) if (p[QStringLiteral("id")].toString() == id) return p;
        static tp::json::Value none; return none;
    };
    for (const QJsonValue &cv : g.value("filters").toArray()) {
        const QJsonObject c = cv.toObject(), fo = c.value("filter").toObject();
        plans::Filter f;
        f.status = setOf(fo.value("status")); f.type = setOf(fo.value("type")); f.region = setOf(fo.value("region")); f.hidden = setOf(fo.value("hidden"));
        QStringList onMap;
        for (const tp::json::Value &p : arr) if (f.onMap(p)) onMap << p[QStringLiteral("id")].toString();
        QStringList want;
        for (const QJsonValue &x : c.value("onMap").toArray()) want << x.toString();
        QCOMPARE(onMap, want);
        QCOMPARE(f.excludedCount(), c.value("excluded").toInt());
        for (const QJsonValue &rv : c.value("reveals").toArray()) {
            const QJsonObject r = rv.toObject();
            plans::Filter f2 = f;
            bool unhid = false;
            const QStringList opened = f2.reveal(findPlan(r.value("id").toString()), &unhid);
            QStringList wantOpened;
            for (const QJsonValue &x : r.value("opened").toArray()) wantOpened << x.toString();
            QCOMPARE(opened, wantOpened);
            QCOMPARE(unhid, r.value("unhid").toBool());
        }
    }
}

void TestMap::plansFilterVariantRoundTrip()
{
    plans::Filter f = plans::Filter::all();
    QCOMPARE(f.excludedCount(), 0);
    f.status.remove(QStringLiteral("completed"));
    f.region = { QStringLiteral("欧洲"), QStringLiteral("未分类") };
    f.hidden = { QStringLiteral("a"), QStringLiteral("gone") };
    const QVariantMap v = f.toVariant();
    const plans::Filter back = plans::Filter::fromVariant(v, { QStringLiteral("a"), QStringLiteral("b") });
    QCOMPARE(back.status, f.status);
    QCOMPARE(back.region, f.region);
    QCOMPARE(back.type, f.type);
    QCOMPARE(back.hidden, QSet<QString>{ QStringLiteral("a") });   // 已删除的计划丢掉
    // 没存过 = 全选；认不出来的取值丢掉；旧区域名换成新名
    QVERIFY(plans::Filter::fromVariant({}, {}) == plans::Filter::all());
    const plans::Filter odd = plans::Filter::fromVariant({ { "status", QVariantList{ "wish", "bogus" } }, { "region", QVariantList{ "西亚" } } }, {});
    QCOMPARE(odd.status, QSet<QString>{ QStringLiteral("wish") });
    QCOMPARE(odd.region, QSet<QString>{ QStringLiteral("西亚/中亚") });
}

void TestMap::plansNowMatchesElectron()
{
    const QJsonObject g = plansGolden();
    if (g.isEmpty()) QSKIP("缺 tests/golden/plans.json");
    tp::json::Value root = plansInput(g);
    for (const QJsonValue &cv : g.value("now").toArray()) {
        const QJsonObject c = cv.toObject();
        if (c.value("next").isNull()) root.mutObject().remove(QStringLiteral("nextTripId"));
        else root.mutObject().set(QStringLiteral("nextTripId"), tp::json::Value(c.value("next").toString()));
        const QString today = c.value("today").toString();
        const QVariantMap m = plans::nowInfo(root, today);
        const QString tag = today + u' ' + c.value("next").toString();
        QVERIFY2(m.value("kind").toString() == c.value("kind").toString(), qPrintable(tag + " kind " + m.value("kind").toString()));
        if (c.value("kind").toString() == QLatin1String("none")) continue;
        QCOMPARE(m.value("planId").toString(), c.value("planId").toString());
        if (c.contains("dayN")) {
            QCOMPARE(m.value("dayN").toInt(), c.value("dayN").toInt());
            QCOMPARE(m.value("dayTotal").toInt(), c.value("dayTotal").toInt());
            QCOMPARE(m.value("here").toString(), c.value("here").toString());
            QCOMPARE(m.value("todayItems").toList().size(), c.value("todayCount").toInt());
            const QVariantMap nx = m.value("next").toMap();
            if (c.contains("nextStop")) {
                const QJsonObject w = c.value("nextStop").toObject();
                QCOMPARE(nx.value("name").toString(), w.value("name").toString());
                QCOMPARE(nx.value("arriveDate").toString(), w.value("arriveDate").toString());
                QCOMPARE(nx.value("tomorrow").toBool(), w.value("tomorrow").toBool());
            } else QVERIFY2(nx.isEmpty(), qPrintable(tag));
        }
        if (c.contains("daysUntil")) QCOMPARE(m.value("daysUntil").toInt(), c.value("daysUntil").toInt());
        if (c.contains("daysAgo")) QCOMPARE(m.value("daysAgo").toInt(), c.value("daysAgo").toInt());
        QStringList todos, want;
        for (const QVariant &t : m.value("todos").toList()) todos << t.toMap().value("id").toString();
        for (const QJsonValue &x : c.value("todos").toArray()) want << x.toString();
        QVERIFY2(todos == want, qPrintable(tag + " todos " + todos.join(',')));
        QCOMPARE(m.value("todoOpen").toInt(), c.value("todoOpen").toInt());
        const QVariantList nt = m.value("notices").toList();
        const QJsonArray wn = c.value("notices").toArray();
        QCOMPARE(nt.size(), wn.size());
        for (int i = 0; i < nt.size(); ++i) {
            const QVariantMap a = nt[i].toMap();
            const QJsonObject b = wn[i].toObject();
            QCOMPARE(a.value("kind").toString(), b.value("kind").toString());
            if (b.contains("planId")) QCOMPARE(a.value("planId").toString(), b.value("planId").toString());
            if (b.contains("n")) QCOMPARE(a.value("n").toInt(), b.value("n").toInt());
            if (b.contains("label")) QCOMPARE(a.value("label").toString(), b.value("label").toString());
        }
    }
}

void TestMap::plansSheetInfo()
{
    const tp::json::Value root = parseJson(R"({"nextTripId":"r","plans":[{"id":"r","name":"关西","type":"route","status":"planning","regions":["东亚","西亚"],
        "cover":"ph1","pinnedAt":5,
        "stops":[{"id":"s1","name":"东京","lat":35.6,"lng":139.7,"leg":{"mode":"train","dist":513000,"time":8100}},
                 {"id":"s0","name":"坏的","lat":"x","lng":1},
                 {"id":"s2","name":"京都","lat":35.0,"lng":135.7,"schedule":[{"date":"2026-11-05","time":"09:30","text":"东福寺"}]},
                 {"id":"s3","name":"大阪","lat":34.7,"lng":135.5,"schedule":"老格式的文字"}]}],
        "photos":[{"id":"ph1","file":"photos/a.jpg","plan":"r"},{"id":"ph2","file":"photos/b.jpg","plan":"r","stop":"s2"},{"id":"ph3","file":"photos/c.jpg","plan":"x","stop":"s2"}]})");
    const QVariantMap m = plans::sheetInfo(root, QStringLiteral("r"));
    QCOMPARE(m.value("stopCount").toInt(), 3);   // 坐标无效的站点不算（和地图标记的下标一致）
    QCOMPARE(m.value("regions").toStringList(), (QStringList{ "东亚", "西亚/中亚" }));
    QVERIFY(m.value("pinned").toBool());
    QVERIFY(m.value("isNext").toBool());
    QCOMPARE(m.value("coverFile").toString(), QStringLiteral("photos/a.jpg"));
    const QVariantList st = m.value("stops").toList();
    QCOMPARE(st[0].toMap().value("leg").toMap().value("mode").toString(), QStringLiteral("train"));
    QCOMPARE(st[0].toMap().value("leg").toMap().value("time").toDouble(), 8100.0);
    QCOMPARE(st[1].toMap().value("name").toString(), QStringLiteral("京都"));
    QCOMPARE(st[1].toMap().value("photos").toStringList(), QStringList{ "photos/b.jpg" });
    QCOMPARE(st[1].toMap().value("leg").toMap().value("set").toBool(), false);   // 没设交通方式 = 默认飞机
    QVERIFY(!st[2].toMap().contains("leg"));                                       // 最后一站没有"去下一站"
    QCOMPARE(st[2].toMap().value("schedule").toList().first().toMap().value("text").toString(), QStringLiteral("老格式的文字"));
    QVERIFY(plans::sheetInfo(root, QStringLiteral("nope")).isEmpty());
}

void TestMap::plansEdits()
{
    tp::json::Value root = parseJson(R"({"plans":[{"id":"a","status":"planning","todos":[{"id":"t1","text":"x","done":false}],"zz":1},{"id":"b","pinnedAt":3}],"keep":true})");
    QVERIFY(plans::setNextTrip(root, "a", true));
    QCOMPARE(root[QStringLiteral("nextTripId")].toString(), QStringLiteral("a"));
    QVERIFY(!plans::setNextTrip(root, "a", true));         // 没变化
    QVERIFY(!plans::setNextTrip(root, "nope", true));      // 没有这个计划
    QVERIFY(plans::setNextTrip(root, "a", false));
    QVERIFY(!root.asObject().contains(QStringLiteral("nextTripId")));
    QVERIFY(plans::setTodoDone(root, "a", "t1", true));
    QVERIFY(!plans::setTodoDone(root, "a", "t1", true));
    QVERIFY(!plans::setTodoDone(root, "a", "t9", true));
    QVERIFY(root[QStringLiteral("plans")].asArray()[0][QStringLiteral("todos")].asArray()[0][QStringLiteral("done")].toBool());
    QVERIFY(plans::togglePin(root, "a", 1234));
    QCOMPARE(root[QStringLiteral("plans")].asArray()[0][QStringLiteral("pinnedAt")].toNumber(), 1234.0);
    QVERIFY(plans::togglePin(root, "b", 99));
    QVERIFY(!root[QStringLiteral("plans")].asArray()[1].asObject().contains(QStringLiteral("pinnedAt")));
    QVERIFY(plans::markCompleted(root, "a"));
    QVERIFY(!plans::markCompleted(root, "a"));
    QCOMPARE(root[QStringLiteral("plans")].asArray()[0][QStringLiteral("zz")].toNumber(), 1.0);   // 不认识的字段原样保留
    QVERIFY(plans::deletePlan(root, "b"));
    QVERIFY(!plans::deletePlan(root, "b"));
    QCOMPARE(int(root[QStringLiteral("plans")].asArray().size()), 1);
    QVERIFY(root[QStringLiteral("keep")].toBool());
}

void TestMap::tripDataVisibleFilter()
{
    const tp::json::Value root = parseJson(R"({"plans":[
        {"id":"a","type":"route","status":"completed","stops":[{"lat":1,"lng":1},{"lat":2,"lng":2}]},
        {"id":"b","type":"single","status":"planning","stops":[{"lat":3,"lng":3}]}]})");
    TripData t;
    t.build(root, [](const tp::json::Value &p) { return p[QStringLiteral("id")].toString() == QLatin1String("b"); });
    QCOMPARE(t.plans().size(), 1);
    QCOMPARE(t.plans().first().id, QStringLiteral("b"));
    QCOMPARE(t.completedStops().size(), 1);   // "已解锁"不看筛选：被筛掉的已完成计划照样点亮
    QCOMPARE(t.allStops().size(), 1);
}

// 0.8.8 我的旅程：只看筛选（眼睛隐藏的照样列出、标为 hidden），顺序 = comparePlans；分组、每组数目、底部计数、天数
void TestMap::plansListInfo()
{
    tp::json::Value root = parseJson(R"({"nextTripId":"r1","plans":[
        {"id":"w1","name":"想去一","status":"wish","type":"single","stops":[{"lat":1,"lng":2}]},
        {"id":"c1","name":"完成一","status":"completed","type":"route","startDate":"2024-04-02","endDate":"2024-04-06","stops":[{"lat":1,"lng":2},{"lat":3,"lng":4},{"name":"无坐标"}]},
        {"id":"r1","name":"计划一","status":"planning","type":"route","startDate":"2026-11-20","endDate":"2026-11-27","stops":[{"lat":1,"lng":2}]},
        {"id":"p1","name":"置顶的","status":"completed","pinnedAt":5,"stops":[]},
        {"id":"x1","name":"没日期","status":"weird","startDate":"2026-01-01"}
    ]})");
    plans::Filter f = plans::Filter::all();
    f.hidden.insert(QStringLiteral("w1"));
    QVariantMap l = plans::listInfo(root, f);
    QVariantList rows = l.value(QStringLiteral("rows")).toList();
    QStringList ids;
    for (const QVariant &v : rows) ids << v.toMap().value(QStringLiteral("id")).toString();
    // 顺序和 sortedIndexes（地图页的 comparePlans）一样
    QStringList want;
    for (int i : plans::sortedIndexes(root, true)) want << root[QStringLiteral("plans")].asArray()[i][QStringLiteral("id")].toString();
    QCOMPARE(ids, want);
    QCOMPARE(ids.first(), QStringLiteral("p1"));   // 置顶的在最前
    const auto row = [&](const QString &id) { for (const QVariant &v : rows) if (v.toMap().value("id").toString() == id) return v.toMap(); return QVariantMap(); };
    QCOMPARE(row("p1").value("group").toString(), QStringLiteral("pin"));
    QCOMPARE(row("p1").value("groupCount").toInt(), 1);
    QCOMPARE(row("x1").value("group").toString(), QStringLiteral("planning"));   // 认不出来的状态算计划中
    QCOMPARE(row("r1").value("groupCount").toInt(), 2);
    QCOMPARE(row("w1").value("hidden").toBool(), true);                          // 眼睛隐藏的照样列出
    QCOMPARE(row("c1").value("stops").toInt(), 2);                               // 只数有坐标的站
    QCOMPARE(row("c1").value("days").toInt(), 5);                                // dayCount 含首尾
    QCOMPARE(row("x1").value("days").toInt(), -1);                               // 只有一个日期
    QCOMPARE(row("r1").value("isNext").toBool(), true);
    QCOMPARE(row("c1").value("isNext").toBool(), false);
    QCOMPARE(row("c1").value("type").toString(), QStringLiteral("route"));
    QCOMPARE(row("w1").value("type").toString(), QStringLiteral("single"));
    // 分组是连续的
    QStringList groups;
    for (const QVariant &v : rows) { const QString g = v.toMap().value("group").toString(); if (groups.isEmpty() || groups.last() != g) groups << g; }
    QCOMPARE(groups.size(), QSet<QString>(groups.begin(), groups.end()).size());
    QCOMPARE(l.value("total").toInt(), 5);
    QCOMPARE(l.value("planning").toInt(), 2);
    QCOMPARE(l.value("wish").toInt(), 1);
    QCOMPARE(l.value("completed").toInt(), 2);
    // 筛掉已完成：列表少了，底部计数不变（Electron sbFoot 用全部计划）
    f.status.remove(QStringLiteral("completed"));
    l = plans::listInfo(root, f);
    QCOMPARE(l.value("rows").toList().size(), 3);
    QCOMPARE(l.value("total").toInt(), 5);

    // 0.8.10 足迹（byYear）：按年份分组，日期新的在前，没有日期的放最后；置顶不单列
    tp::json::Value done = parseJson(R"({"plans":[
        {"id":"a","name":"甲","status":"completed","startDate":"2024-04-02","endDate":"2024-04-06"},
        {"id":"b","name":"乙","status":"completed","pinnedAt":9},
        {"id":"c","name":"丙","status":"completed","startDate":"2025-09-01"},
        {"id":"d","name":"丁","status":"completed","endDate":"2024-10-03"},
        {"id":"e","name":"戊","status":"completed","startDate":"坏日期"}
    ]})");
    l = plans::listInfo(done, plans::Filter::all(), true);
    QStringList order, grp;
    for (const QVariant &v : l.value("rows").toList()) { order << v.toMap().value("id").toString(); grp << v.toMap().value("group").toString(); }
    QCOMPARE(order.mid(0, 3), (QStringList{ "c", "d", "a" }));
    QCOMPARE(grp, (QStringList{ "y2025", "y2024", "y2024", "undated", "undated" }));
    QCOMPARE(l.value("rows").toList().at(1).toMap().value("groupCount").toInt(), 2);
    QVERIFY(order.mid(3).contains("b") && order.mid(3).contains("e"));
}

// 0.11.0 计划库、想去的分组、批量
void TestMap::plansLibrary()
{
    tp::json::Value root = parseJson(R"({"plans":[
        {"id":"a","name":"乙路线","status":"planning","type":"route","startDate":"2026-01-01","endDate":"2026-01-03","stops":[{"name":"京都","lat":35,"lng":135},{"lat":34,"lng":135.5},{"name":"无坐标"}]},
        {"id":"b","name":"甲目的地","status":"wish","type":"single","days":4.4,"seasonRef":"日本","cover":"ph2","stops":[{"lat":1,"lng":2}]},
        {"id":"c","name":"丙","status":"wish","type":"route","pinnedAt":5,"seasonRef":"没有这行","stops":[]},
        {"id":"d","name":"丁","status":"completed","stops":[{"lat":1,"lng":2},{"lat":3,"lng":4},{"lat":5,"lng":6}]},
        {"id":"e","name":"戊","status":"wish","seasonRef":"日本"}
    ],"photos":[{"id":"ph1","file":"photos/1.jpg","plan":"b"},{"id":"ph2","file":"photos/2.jpg","plan":"b"},{"id":"ph3","file":"photos/3.jpg","plan":"a"}],
    "seasonGuide":{"rows":[{"country":"日本","countryEn":"Japan","group":"东亚","spans":[{"from":3,"to":4,"kind":"best"},{"from":10,"to":11,"kind":"nice"},{"from":7,"to":8,"kind":"bad"}]}]}})");
    const auto ids = [](const QVariantMap &l) { QStringList o; for (const QVariant &v : l.value("rows").toList()) o << v.toMap().value("id").toString(); return o; };
    plans::LibraryQuery q;
    QVariantMap l = plans::libraryInfo(root, q);
    QCOMPARE(l.value("total").toInt(), 5);
    QCOMPARE(l.value("wish").toInt(), 3);
    QCOMPARE(ids(l).first(), QStringLiteral("c"));   // 置顶的最前
    const auto row = [&l](const QString &id) { for (const QVariant &v : l.value("rows").toList()) if (v.toMap().value("id").toString() == id) return v.toMap(); return QVariantMap(); };
    QCOMPARE(row("b").value("coverFile").toString(), QStringLiteral("photos/2.jpg"));   // 设的封面
    QCOMPARE(row("a").value("coverFile").toString(), QStringLiteral("photos/3.jpg"));   // 没设：第一张
    QCOMPARE(row("b").value("photos").toInt(), 2);
    QCOMPARE(row("b").value("estDays").toInt(), 4);
    QCOMPARE(row("a").value("days").toInt(), 3);
    QCOMPARE(row("a").value("stops").toInt(), 2);
    QCOMPARE(row("a").value("sketch").toList().size(), 2);
    // 搜索：计划名或站点名（包括没坐标的站点），不分大小写
    q.q = QStringLiteral("京都");
    QCOMPARE(ids(plans::libraryInfo(root, q)), QStringList{ "a" });
    q.q = QStringLiteral("无坐标");
    QCOMPARE(ids(plans::libraryInfo(root, q)), QStringList{ "a" });
    q.q.clear();
    q.status = QStringLiteral("completed");
    QCOMPARE(ids(plans::libraryInfo(root, q)), QStringList{ "d" });
    q.status = QStringLiteral("all");
    q.type = QStringLiteral("single");
    QVERIFY(!ids(plans::libraryInfo(root, q)).contains("a"));
    q.type = QStringLiteral("all");
    q.sort = QStringLiteral("stops");
    q.hidden = { QStringLiteral("d") };
    l = plans::libraryInfo(root, q);
    QCOMPARE(ids(l).last(), QStringLiteral("d"));   // 站点最多，但隐藏的在最后
    QCOMPARE(ids(l).at(1), QStringLiteral("a"));
    QVERIFY(row("d").value("hidden").toBool());
    q.sort = QStringLiteral("date");
    q.hidden.clear();
    QCOMPARE(ids(plans::libraryInfo(root, q)).at(1), QStringLiteral("a"));   // 有日期的在前（新的在前）

    // 收起的列表：隐藏的排到最后一组
    QVariantList rows = plans::listInfo(root, plans::Filter::all(), false, { QStringLiteral("a") }).value("rows").toList();
    QCOMPARE(rows.last().toMap().value("id").toString(), QStringLiteral("a"));
    QCOMPARE(rows.last().toMap().value("group").toString(), QStringLiteral("listHidden"));

    // 想去：普通分组（1.3.1 去掉月份条），每行带关联季节的 12 格，没关联的（含 seasonRef 指向没有的行）为空
    plans::Filter f = plans::Filter::all();
    f.status = { QStringLiteral("wish") };
    QVariantMap w = plans::wishInfo(root, f, {});
    QCOMPARE(ids(w).size(), 3);
    const auto grp = [](const QVariantMap &m, const QString &id) { for (const QVariant &v : m.value("rows").toList()) if (v.toMap().value("id").toString() == id) return v.toMap(); return QVariantMap(); };
    QCOMPARE(grp(w, "c").value("kinds").toStringList().size(), 0);
    QCOMPARE(grp(w, "b").value("kinds").toStringList().size(), 12);
    QVERIFY(!w.contains("months"));
    QCOMPARE(plans::seasonRows(root).at(0).toMap().value("group").toString(), QStringLiteral("东亚"));

    // 批量、开始计划、关联季节
    QVERIFY(plans::setPlansStatus(root, { "a", "b" }, "completed"));
    QCOMPARE(plans::statusOf(*plans::planById(root, "b")), QStringLiteral("completed"));
    QVERIFY(!plans::setPlansStatus(root, { "a", "b" }, "completed"));
    QVERIFY(!plans::setPlansStatus(root, { "a" }, "坏状态"));
    QVERIFY(plans::startPlan(root, "e", "2030-01-02", ""));
    QCOMPARE(plans::statusOf(*plans::planById(root, "e")), QStringLiteral("planning"));
    QCOMPARE((*plans::planById(root, "e"))[QStringLiteral("endDate")].toString(), QStringLiteral("2030-01-02"));
    QVERIFY(plans::setSeasonRef(root, "e", ""));
    QVERIFY(!plans::planById(root, "e")->asObject().find(QStringLiteral("seasonRef")));
    QVERIFY(plans::setSeasonRef(root, "e", "日本"));
    QVERIFY(!plans::setSeasonRef(root, "e", "日本"));
    QVERIFY(plans::deletePlans(root, { "a", "没有", "d" }));
    QCOMPARE(root[QStringLiteral("plans")].asArray().size(), size_t(3));
    QCOMPARE(root[QStringLiteral("photos")].asArray().size(), size_t(3));   // 照片不删
}

// 0.11.0 想去的颜色只影响这一次要画的，不进缓存
void TestMap::tripTone()
{
    tp::json::Value root = parseJson(R"({"plans":[{"id":"r","status":"wish","type":"route","color":"#204060","stops":[{"lat":1,"lng":2},{"lat":3,"lng":4}]}]})");
    tp::map::TripData t;
    t.build(root, {}, [](const tp::json::Value &) { return -1; });
    QCOMPARE(t.plans().at(0).tone, -1);
    QVERIFY(t.plans().at(0).color != QStringLiteral("#204060"));
    const QString faded = t.routesJsonOnly("r");
    QVERIFY(!faded.contains("#204060"));
    QVERIFY(faded.contains("#8f9faf"));
    t.build(root);
    QCOMPARE(t.plans().at(0).tone, 0);
    QCOMPARE(t.plans().at(0).color, QStringLiteral("#204060"));
    QVERIFY(t.routesJsonOnly("r").contains("#204060"));
}

// 0.8.8 Ctrl K 的条目：按原顺序，每个计划后面跟着它有名字的有效站点；站点下标是有效站点里的下标（和地图选中用的一样）
void TestMap::plansCommandItems()
{
    tp::json::Value root = parseJson(R"({"plans":[
        {"id":"a","name":"关西","status":"planning","startDate":"2026-11-20","endDate":"2026-11-27","stops":[{"name":"无坐标"},{"name":"京都","lat":35,"lng":135},{"lat":34,"lng":135.5},{"name":"奈良","lat":34.6,"lng":135.8}]},
        {"id":"b","name":"","status":"wish","color":"#123456"}
    ]})");
    const QVariantList it = plans::commandItems(root);
    QCOMPARE(it.size(), 4);
    QCOMPARE(it[0].toMap().value("kind").toString(), QStringLiteral("plan"));
    QCOMPARE(it[1].toMap().value("label").toString(), QStringLiteral("京都"));
    QCOMPARE(it[1].toMap().value("stop").toInt(), 0);
    QCOMPARE(it[2].toMap().value("label").toString(), QStringLiteral("奈良"));
    QCOMPARE(it[2].toMap().value("stop").toInt(), 2);        // 没名字的那一站不列，但下标照数
    QCOMPARE(it[2].toMap().value("planName").toString(), QStringLiteral("关西"));
    QCOMPARE(it[3].toMap().value("id").toString(), QStringLiteral("b"));
    QCOMPARE(it[3].toMap().value("color").toString(), QStringLiteral("#123456"));
    QCOMPARE(it[0].toMap().value("startDate").toString(), QStringLiteral("2026-11-20"));
}

// ---------------- 0.8.9 站点编辑器（planedit.*） ----------------

void TestMap::planEditRegions()
{
    // 和 Electron suggestRegion 逐点对照（tools/claude-kit/region-golden/gen.js 生成）
    const QJsonArray g = QJsonDocument::fromJson(readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/regions.json"))).array();
    QVERIFY(g.size() > 4000);
    int bad = 0;
    for (const QJsonValue &v : g) {
        const QJsonArray r = v.toArray();
        const QString want = r[2].isNull() ? QString() : r[2].toString();
        const QString got = planedit::suggestRegion(r[0].toDouble(), r[1].toDouble());
        if (got != want && ++bad <= 5) qWarning("区域不一致：%f,%f 期望 %s 实际 %s", r[0].toDouble(), r[1].toDouble(), qUtf8Printable(want), qUtf8Printable(got));
    }
    QCOMPARE(bad, 0);
    // 计划的建议 = 各站区域的并集，按 REGIONS 顺序
    tp::json::Value p = planedit::blankPlan(QStringLiteral("p_x"), 1);
    planedit::appendStop(p, planedit::newStop(QStringLiteral("s1"), QStringLiteral("Kyoto"), 35.01, 135.76));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("s2"), QStringLiteral("Hong Kong"), 22.32, 114.17));
    QCOMPARE(planedit::suggestPlanRegions(p), QStringList({ QStringLiteral("中国"), QStringLiteral("东亚") }));
}

void TestMap::planEditValidate()
{
    tp::json::Value p = planedit::blankPlan(QStringLiteral("p_x"), 1);
    planedit::Problems pr = planedit::validate(p);
    QVERIFY(pr.nameEmpty && pr.noStops && !pr.dateOrder && pr.badCoords.isEmpty());
    planedit::setField(p, QStringLiteral("name"), QStringLiteral("  "));
    QVERIFY(planedit::validate(p).nameEmpty);   // 只有空白也算空（Electron p.name.trim()）
    planedit::setField(p, QStringLiteral("name"), QStringLiteral("关西"));
    planedit::setField(p, QStringLiteral("startDate"), QStringLiteral("2026-11-27"));
    planedit::setField(p, QStringLiteral("endDate"), QStringLiteral("2026-11-20"));
    QVERIFY(planedit::validate(p).dateOrder);
    planedit::setField(p, QStringLiteral("endDate"), QStringLiteral("2026-11-28"));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("s1"), QStringLiteral("A"), 10, 20));
    QVERIFY(planedit::validate(p).ok());
    planedit::setStopLatLng(p, 0, 91, 20);   // 纬度超出范围
    pr = planedit::validate(p);
    QCOMPARE(pr.badCoords, QStringList({ QStringLiteral("s1") }));
    // 坐标不是数字（手动输入的文字）也算无效
    p.mutObject().find(QStringLiteral("stops"))->mutArray()[0].mutObject().set(QStringLiteral("lat"), tp::json::Value(QStringLiteral("abc")));
    QCOMPARE(planedit::validate(p).badCoords.size(), 1);
}

void TestMap::planEditEdits()
{
    tp::json::Value p = planedit::blankPlan(QStringLiteral("p_x"), 1);
    // newStop：6 位小数、经度归一（Electron addStop：+(+lat).toFixed(6)、normLng）
    const tp::json::Value s = planedit::newStop(QStringLiteral("s1"), QString(), 35.12345678, 190.0000004);
    QCOMPARE(s[QStringLiteral("lat")].toNumber(), 35.123457);
    QCOMPARE(s[QStringLiteral("lng")].toNumber(), -170.0);
    QVERIFY(!s.asObject().contains(QStringLiteral("order")) && !s.asObject().contains(QStringLiteral("photos")));
    planedit::appendStop(p, s);
    planedit::appendStop(p, planedit::newStop(QStringLiteral("s2"), QStringLiteral("B"), 1, 2));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("s3"), QStringLiteral("C"), 3, 4));
    // 改进 g：第二站时自动改成路线
    QVERIFY(planedit::autoRouteOnSecondStop(p));
    QCOMPARE(p[QStringLiteral("type")].toString(), QStringLiteral("route"));
    QVERIFY(!planedit::autoRouteOnSecondStop(p));
    // 挪站点（拖动排序：任意位置）
    QVERIFY(planedit::moveStop(p, 2, 0));
    QCOMPARE(p[QStringLiteral("stops")].asArray()[0][QStringLiteral("id")].toString(), QStringLiteral("s3"));
    QVERIFY(!planedit::moveStop(p, 0, 0));
    QVERIFY(planedit::removeStop(p, 1));
    QCOMPARE(int(p[QStringLiteral("stops")].asArray().size()), 2);
    // 样式：空 = 删掉键（站点）；计划的 color 空字符串照写（Electron p.color = v.color || ''）
    QVERIFY(planedit::setStyle(p, 0, QStringLiteral("#123456"), QStringLiteral("square"), QStringLiteral("hotel")));
    QVERIFY(planedit::setStyle(p, 0, {}, {}, {}));
    QVERIFY(!p[QStringLiteral("stops")].asArray()[0].asObject().contains(QStringLiteral("color")));
    planedit::setStyle(p, -1, {}, QStringLiteral("pin"), {});
    QVERIFY(p.asObject().contains(QStringLiteral("color")) && p[QStringLiteral("shape")].toString() == QStringLiteral("pin"));
    // markerMode：number 删掉
    QVERIFY(planedit::setField(p, QStringLiteral("markerMode"), QStringLiteral("icon")));
    QVERIFY(planedit::setField(p, QStringLiteral("markerMode"), QStringLiteral("number")));
    QVERIFY(!p.asObject().contains(QStringLiteral("markerMode")));
    // 备注空 = 删掉（站点）
    planedit::setStopField(p, 0, QStringLiteral("notes"), QStringLiteral("x"));
    planedit::setStopField(p, 0, QStringLiteral("notes"), QString());
    QVERIFY(!p[QStringLiteral("stops")].asArray()[0].asObject().contains(QStringLiteral("notes")));
    QVERIFY(!planedit::setStopField(p, 0, QStringLiteral("schedule"), QStringLiteral("x")));   // 编辑器不管的键不许改
    // 区域按 REGIONS 顺序、去掉"未分类"
    planedit::setRegions(p, { QStringLiteral("欧洲"), QStringLiteral("未分类"), QStringLiteral("中国") });
    QCOMPARE(tp::map::plans::regionsOf(p), QStringList({ QStringLiteral("中国"), QStringLiteral("欧洲") }));
}

void TestMap::planEditKeepsUnknown()
{
    // 真实数据里每个计划：复制 → 改名字 / 挪站点 → 写回，除了改的那几处，别的（交通住宿、消费、行程安排、未知键）都不变
    tp::json::Value root = parseJson(readFile(QStringLiteral(TP_SOURCE_DIR "/testdata/travel.json")));
    const tp::json::Array &ps = root[QStringLiteral("plans")].asArray();
    QVERIFY(ps.size() > 10);
    int checked = 0;
    for (int i = 0; i < int(ps.size()); ++i) {
        tp::json::Value r2 = root;
        tp::json::Value plan = ps[i];
        planedit::setField(plan, QStringLiteral("name"), QStringLiteral("改名测试"));
        const int n = int(plan[QStringLiteral("stops")].asArray().size());
        if (n > 1) planedit::moveStop(plan, 0, n - 1);
        QVERIFY(planedit::putPlan(r2, plan));
        int idx = -1;
        QVERIFY(planedit::findPlan(r2, plan[QStringLiteral("id")].toString(), &idx));
        QCOMPARE(idx, i);   // 位置不变
        // 把改的改回来，整份数据应该和原来一模一样
        tp::json::Value back = r2[QStringLiteral("plans")].asArray()[i];
        planedit::setField(back, QStringLiteral("name"), ps[i][QStringLiteral("name")].toString());
        if (n > 1) planedit::moveStop(back, n - 1, 0);
        planedit::putPlan(r2, back);
        QVERIFY2(r2 == root, qPrintable(ps[i][QStringLiteral("name")].toString()));
        ++checked;
    }
    QVERIFY(checked > 10);
    // 新计划加在最后；删掉
    tp::json::Value r3 = root;
    QVERIFY(planedit::putPlan(r3, planedit::blankPlan(QStringLiteral("p_new"), 1)));
    QCOMPARE(r3[QStringLiteral("plans")].asArray().back()[QStringLiteral("id")].toString(), QStringLiteral("p_new"));
    QVERIFY(planedit::removePlan(r3, QStringLiteral("p_new")));
    QVERIFY(r3 == root);
}

void TestMap::planEditLegsAndStats()
{
    tp::json::Value p = planedit::blankPlan(QStringLiteral("p_x"), 1);
    planedit::setField(p, QStringLiteral("type"), QStringLiteral("route"));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("a"), QStringLiteral("A"), 35.0, 135.7));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("b"), QStringLiteral("B"), 34.7, 135.5));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("c"), QStringLiteral("C"), 34.68, 135.8));
    // 飞机：没有 leg；改成自驾 → leg.mode = drive
    QVERIFY(!p[QStringLiteral("stops")].asArray()[0].asObject().contains(QStringLiteral("leg")));
    QVERIFY(planedit::setLegMode(p, 0, QStringLiteral("drive")));
    QCOMPARE(p[QStringLiteral("stops")].asArray()[0][QStringLiteral("leg")][QStringLiteral("mode")].toString(), QStringLiteral("drive"));
    // 算好一段（写 path / dist / time / key），再改成火车：折线清掉
    QVector<planedit::LegRow> rows = planedit::legRows(p);
    QCOMPARE(rows.size(), 2);
    QVERIFY(rows[0].road && !rows[0].computed && !rows[0].key.isEmpty());
    {
        tp::json::Object &lo = p.mutObject().find(QStringLiteral("stops"))->mutArray()[0].mutObject().find(QStringLiteral("leg"))->mutObject();
        lo.set(QStringLiteral("path"), tp::json::Value(QStringLiteral("_p~iF~ps|U_ulLnnqC")));
        lo.set(QStringLiteral("dist"), tp::json::Value(50000.0));
        lo.set(QStringLiteral("time"), tp::json::Value(3600.0));
        lo.set(QStringLiteral("key"), tp::json::Value(rows[0].key));
    }
    rows = planedit::legRows(p);
    QVERIFY(rows[0].computed);
    planedit::Stats st = planedit::stats(p);
    QVERIFY(st.hasDrive && !st.approx && st.driveSec == 3600);
    QVERIFY(std::abs(st.km - (50 + rows[1].straightKm)) < 1e-9);
    // 挪了站点：key 对不上 → 没算好、算"约"
    planedit::setStopLatLng(p, 1, 34.6, 135.5);
    QVERIFY(!planedit::legRows(p)[0].computed);
    QVERIFY(planedit::stats(p).approx);
    // 改成飞机：mode 和折线都去掉，leg 空了就删掉
    QVERIFY(planedit::setLegMode(p, 0, QStringLiteral("flight")));
    QVERIFY(!p[QStringLiteral("stops")].asArray()[0].asObject().contains(QStringLiteral("leg")));
    QVERIFY(!planedit::setLegMode(p, 0, QStringLiteral("flight")));
    // leg 里别的键保留（以后 Electron / 原生加的）
    planedit::setLegMode(p, 0, QStringLiteral("train"));
    p.mutObject().find(QStringLiteral("stops"))->mutArray()[0].mutObject().find(QStringLiteral("leg"))->mutObject().set(QStringLiteral("note"), tp::json::Value(QStringLiteral("x")));
    planedit::setLegMode(p, 0, QStringLiteral("flight"));
    QCOMPARE(p[QStringLiteral("stops")].asArray()[0][QStringLiteral("leg")][QStringLiteral("note")].toString(), QStringLiteral("x"));
    // 大圆距离（Electron distKm）
    QVERIFY(std::abs(planedit::haversineKm(0, 0, 0, 1) - 111.195) < 0.01);
}

void TestMap::planEditTodayStop()
{
    tp::json::Value root = tp::json::Value::object();
    root.mutObject().set(QStringLiteral("nextTripId"), tp::json::Value(QStringLiteral("p1")));   // "此刻"看的是指定的下次出发
    tp::json::Value p = planedit::blankPlan(QStringLiteral("p1"), 1);
    planedit::setField(p, QStringLiteral("name"), QStringLiteral("在路上"));
    planedit::setField(p, QStringLiteral("type"), QStringLiteral("route"));
    planedit::setField(p, QStringLiteral("startDate"), QStringLiteral("2026-11-01"));
    planedit::setField(p, QStringLiteral("endDate"), QStringLiteral("2026-11-10"));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("a"), QStringLiteral("A"), 1, 1));
    planedit::appendStop(p, planedit::newStop(QStringLiteral("b"), QStringLiteral("B"), 2, 2));
    planedit::setStopField(p, 0, QStringLiteral("arriveDate"), QStringLiteral("2026-11-01"));
    planedit::setStopField(p, 1, QStringLiteral("arriveDate"), QStringLiteral("2026-11-05"));
    planedit::putPlan(root, p);
    QCOMPARE(planedit::todayStop(root, QStringLiteral("2026-11-03")).value("name").toString(), QStringLiteral("A"));
    QCOMPARE(planedit::todayStop(root, QStringLiteral("2026-11-06")).value("name").toString(), QStringLiteral("B"));
    QVERIFY(planedit::todayStop(root, QStringLiteral("2026-12-01")).isEmpty());   // 不在路上
}

// ---------------- 0.9.0 地球（globegeom） ----------------

void TestMap::globeProjectPick()
{
    using namespace tp::map::globe;
    View v; v.lat = 35; v.lng = 139; v.dist = 2.5; v.width = 1200; v.height = 800;
    const tp::map::globe::Camera cam(v);
    // 相机正下方的点在屏幕正中
    double sx, sy;
    QVERIFY(cam.project(toVec(35, 139), &sx, &sy));
    QVERIFY(std::abs(sx - 600) < 1e-6 && std::abs(sy - 400) < 1e-6);
    // 北边的点在屏幕上方，东边的在右边（上方向是地轴）
    QVERIFY(cam.project(toVec(40, 139), &sx, &sy)); QVERIFY(sy < 400);
    QVERIFY(cam.project(toVec(35, 145), &sx, &sy)); QVERIFY(sx > 600);
    // 投影 → 拾取 来回一致
    for (double lat : { 20.0, 35.0, 50.0 })
        for (double lng : { 125.0, 139.0, 155.0 }) {
            QVERIFY(cam.project(toVec(lat, lng), &sx, &sy));
            Vec3 p;
            QVERIFY(cam.pick(sx, sy, &p));
            double la, ln;
            toLatLng(p, &la, &ln);
            QVERIFY2(std::abs(la - lat) < 1e-6 && std::abs(ln - lng) < 1e-6, qPrintable(QStringLiteral("%1,%2 → %3,%4").arg(lat).arg(lng).arg(la).arg(ln)));
        }
    // 屏幕角上（球外）拾取不到；背面的点看不见
    Vec3 p;
    QVERIFY(!cam.pick(0, 0, &p));
    QVERIFY(!cam.facing(toVec(-35, -41)));
    QVERIFY(cam.facing(toVec(35, 139)));
    // 墨卡托换算来回一致
    for (double lat : { -80.0, -10.0, 0.0, 45.0, 85.0 }) QVERIFY(std::abs(latOfMercY(mercY(lat)) - lat) < 1e-9);
    QCOMPARE(mercX(-180), 0.0);
    QCOMPARE(lngOfMercX(0.5), 0.0);
}

void TestMap::globeZoomMapping()
{
    using namespace tp::map::globe;
    View v; v.height = 800;
    const double f = v.focal();
    // 来回一致
    for (double lat : { 0.0, 30.0, 60.0 })
        for (double z : { 1.0, 3.5, 6.0 }) QVERIFY(std::abs(zoomForDist(distForZoom(z, f, lat), f, lat) - z) < 1e-9);
    // 屏幕中心比例尺一致：地球上每像素的地面距离 = 平面同级别、同纬度的
    const double lat = 30, z = 4;
    const double d = distForZoom(z, f, lat);
    const double globeMpp = kEarthRadiusM * (d - 1) / f;
    const double flatMpp = 2 * M_PI * kEarthRadiusM * std::cos(lat * M_PI / 180) / (tp::map::geo::kTile * std::exp2(z));
    QVERIFY2(std::abs(globeMpp / flatMpp - 1) < 1e-9, qPrintable(QStringLiteral("%1 vs %2").arg(globeMpp).arg(flatMpp)));
    // 和 Electron 实测（MapLibre GL JS 5.24：球的半径 ≈ 146 × 1.82^(z-1) ÷ cos(纬度)）同一个量级：整个地球看起来的大小差不到 15%
    const double dz = distForZoom(2.0, f, 0);
    const double radiusPx = f * std::tan(std::asin(1.0 / dz));
    const double electron = 146 * std::pow(1.82, 1.0);
    QVERIFY2(std::abs(radiusPx / electron - 1) < 0.15, qPrintable(QStringLiteral("半径 %1 像素，Electron %2").arg(radiusPx).arg(electron)));
    // 最远：屏幕中心 100 像素最多约 2000 km（×0.97）；窗口越高越远，有上下限
    const double dmax = maxDistFor(f);
    QVERIFY(std::abs(kEarthRadiusM * (dmax - 1) / f * 100 - 1940000) < 1);
    QVERIFY(maxDistFor(10) == 1.6);
    QVERIFY(maxDistFor(1e6) == kMaxDistCap);
}

void TestMap::globeSelectBasics()
{
    using namespace tp::map::globe;
    View v; v.lat = 30; v.lng = 110; v.width = 1600; v.height = 1000;
    v.dist = distForZoom(2, v.focal(), v.lat);
    const Selection farS = selectTiles(v);
    QVERIFY(!farS.tiles.empty());
    QVERIFY(farS.maxZ <= 4);
    // 选出来的块互不重叠（没有一块是另一块的祖先）
    for (const TileId &a : farS.tiles)
        for (const TileId &b : farS.tiles) {
            if (a == b || a.z >= b.z) continue;
            const int d = b.z - a.z;
            QVERIFY(!((b.x >> d) == a.x && (b.y >> d) == a.y));
        }
    // 相机正下方那块一定在
    auto holds = [](const Selection &s, double lat, double lng) {
        for (const TileId &t : s.tiles) {
            const double n = std::exp2(t.z), mx = mercX(lng) * n - t.x, my = mercY(lat) * n - t.y;
            if (mx >= 0 && mx <= 1 && my >= 0 && my <= 1) return true;
        }
        return false;
    };
    QVERIFY(holds(farS, 30, 110));
    // 拉近到 6 级：最细的级别跟着升，正下方的块是最细的那一级附近
    v.dist = distForZoom(6, v.focal(), v.lat);
    const Selection nearS = selectTiles(v);
    QVERIFY2(nearS.maxZ >= 6, qPrintable(QString::number(nearS.maxZ)));
    QVERIFY(holds(nearS, 30, 110));
    // 每块在屏幕上不超过阈值（细分到最细一级的除外）：取正下方那块看边长
    const tp::map::globe::Camera cam(v);
    for (const TileId &t : nearS.tiles) {
        if (t.z == 12) continue;
        const double n = std::exp2(t.z);
        double ax, ay, bx, by;
        const Vec3 a = toVec(latOfMercY((t.y + 0.5) / n), lngOfMercX(t.x / n)), b = toVec(latOfMercY((t.y + 0.5) / n), lngOfMercX((t.x + 1) / n));
        if (!cam.facing(a) || !cam.facing(b) || !cam.project(a, &ax, &ay) || !cam.project(b, &bx, &by)) continue;
        QVERIFY2(std::hypot(bx - ax, by - ay) <= nearS.usedTilePx * 1.05, qPrintable(QStringLiteral("z%1 %2,%3：%4 像素").arg(t.z).arg(t.x).arg(t.y).arg(std::hypot(bx - ax, by - ay))));
    }
}

void TestMap::globeSelectCullsBackside()
{
    using namespace tp::map::globe;
    View v; v.lat = 0; v.lng = 0; v.width = 1600; v.height = 1000; v.dist = 3;
    const Selection s = selectTiles(v);
    // 背面（经度 180° 附近、赤道）那几块不选
    for (const TileId &t : s.tiles) {
        const double n = std::exp2(t.z);
        const double west = lngOfMercX(t.x / n), east = lngOfMercX((t.x + 1) / n);
        QVERIFY2(!(west >= 120 && east <= 180) && !(west >= -180 && east <= -120), qPrintable(QStringLiteral("z%1 %2,%3").arg(t.z).arg(t.x).arg(t.y)));
    }
    // 拉得很近时，屏幕外的块不选：块数远小于"整个半球都细分"
    v.dist = 1.08;
    const Selection nearS = selectTiles(v);
    QVERIFY(!nearS.tiles.empty());
    QVERIFY2(nearS.tiles.size() < 120, qPrintable(QString::number(nearS.tiles.size())));
}

void TestMap::globeSelectBudget()
{
    using namespace tp::map::globe;
    // 很大的屏幕（4K，设备像素）+ 拉近：超过预算时整体放粗，块数回到预算以内
    View v; v.lat = 45; v.lng = 10; v.width = 3840; v.height = 2160;
    v.dist = distForZoom(5, v.focal() / 2, v.lat);
    SelectOptions o; o.budget = 60;
    const Selection s = selectTiles(v, o);
    QVERIFY2(int(s.tiles.size()) <= 60, qPrintable(QString::number(s.tiles.size())));
    QVERIFY(s.usedTilePx > o.maxTilePx);
}

// ---- 0.9.5 地球上的内容 ----

void TestMap::globePoleZoomSmooth()
{
    // PO 11:02 报的抽搐：中心在 85°、鼠标在中心上方（往极点那边）滚轮放大。照 GlobeItem 的做法一帧帧走（按相机距离插值 +
    // zoomAnchorCenter），距离要一路变小、中心不乱跳；中心一直不超过 ±85°
    using namespace tp::map::globe;
    struct Case { double lat, ax, ay; };
    for (const Case &c : { Case{ 85, 1000, 150 }, Case{ 85, 800, 450 }, Case{ 85, 300, 700 }, Case{ 84, 1400, 100 }, Case{ -85, 600, 800 }, Case{ 30, 1300, 200 } }) {
        View v; v.lat = c.lat; v.lng = 10; v.dist = 2.2; v.width = 1600; v.height = 900;
        const double from = v.dist, to = 1.0 + (from - 1.0) * std::exp2(-0.5);
        double prevD = v.dist, prevLat = v.lat, prevLng = v.lng;
        std::vector<std::pair<double, double>> path{ { v.lat, v.lng } };
        for (int f = 1; f <= 12; ++f) {
            const double t = 1 - std::pow(1 - f / 12.0, 3);
            const double d = lerpDist(from, to, t);
            double lat = v.lat, lng = v.lng;
            zoomAnchorCenter(v, d, c.ax, c.ay, 85.0, &lat, &lng);
            v.dist = d; v.lat = lat; v.lng = lng;
            QVERIFY2(v.dist <= prevD + 1e-12, qPrintable(QStringLiteral("距离回头了：%1 → %2（%3°）").arg(prevD).arg(v.dist).arg(c.lat)));
            QVERIFY(std::abs(v.lat) <= 85.0 + 1e-9);
            QVERIFY2(std::abs(v.lat - prevLat) < 15.0, qPrintable(QStringLiteral("纬度跳了 %1°").arg(v.lat - prevLat)));
            // 中心在球面上每帧挪动的角度（极点附近经度本来就变得快，所以看球心角）
            const double moved = angleBetween(prevLat, prevLng, v.lat, v.lng) * 180 / M_PI;
            QVERIFY2(moved < 15.0, qPrintable(QStringLiteral("中心一帧挪了 %1°（%2, %3, %4）").arg(moved).arg(c.lat).arg(c.ax).arg(c.ay)));
            prevD = v.dist; prevLat = v.lat; prevLng = v.lng;
            path.push_back({ v.lat, v.lng });
        }
        // 中心一路朝终点走、不回头（0.9.4 以前在 85° 来回甩）
        for (size_t k = 1; k < path.size(); ++k)
            QVERIFY(angleBetween(path[k].first, path[k].second, v.lat, v.lng) <= angleBetween(path[k - 1].first, path[k - 1].second, v.lat, v.lng) + 1e-9);
        QVERIFY(std::abs(v.dist - to) < 1e-9);
    }
    // 不在极点：以鼠标处为中心，鼠标下的点换距离以后还在原处（误差 < 0.5 像素）
    View v; v.lat = 30; v.lng = 110; v.dist = 2.0; v.width = 1600; v.height = 900;
    Vec3 p0;
    QVERIFY(tp::map::globe::Camera(v).pick(1100, 300, &p0));
    double lat, lng;
    QVERIFY(zoomAnchorCenter(v, 1.6, 1100, 300, 85.0, &lat, &lng));
    View w = v; w.dist = 1.6; w.lat = lat; w.lng = lng;
    double sx, sy;
    QVERIFY(tp::map::globe::Camera(w).project(p0, &sx, &sy));
    QVERIFY2(std::hypot(sx - 1100, sy - 300) < 0.5, qPrintable(QStringLiteral("%1,%2").arg(sx).arg(sy)));
}

void TestMap::globeLimbFadeAndScale()
{
    using namespace tp::map::globe;
    View v; v.lat = 0; v.lng = 0; v.dist = 3.0; v.width = 1200; v.height = 800;
    const double hz = horizonAngle(3.0) * 180 / M_PI;   // 约 70.5°
    QVERIFY(std::abs(hz - 70.528779) < 1e-4);
    const Limb c = limbOf(v, 0, 0);
    QCOMPARE(c.fade, 1.0); QCOMPARE(c.scale, 1.0);
    const Limb edge = limbOf(v, 0, hz - 4);      // 离地平线 4°：一半透明、缩小
    QVERIFY(edge.fade > 0.3 && edge.fade < 0.7);
    QVERIFY(edge.scale > kLimbMinScale && edge.scale < 0.8);
    const Limb back = limbOf(v, 0, hz + 1);
    QCOMPARE(back.fade, 0.0); QCOMPARE(back.scale, kLimbMinScale);
    QVERIFY(limbOf(v, 0, hz - 8.01).fade > 0.999);
    QVERIFY(std::abs(degreesInside(v, 0, 30) - (hz - 30)) < 1e-6);
}

void TestMap::globeOccludedLifted()
{
    using namespace tp::map::globe;
    View v; v.lat = 0; v.lng = 0; v.dist = 3.0; v.width = 1200; v.height = 800;
    const tp::map::globe::Camera cam(v);
    QVERIFY(!cam.occluded(toVec(0, 0)));
    QVERIFY(cam.occluded(toVec(0, 120)));               // 背面
    QVERIFY(cam.occluded(toVec(0, 75)));                // 地平线 70.5° 以外的球面点：被挡住
    QVERIFY(!cam.occluded(toVec(0, 75) * 1.2));         // 抬到 1.2 倍半径：露出来（飞行弧线会探出边缘）
    QVERIFY(cam.occluded(toVec(0, 100) * 1.05));
}

void TestMap::globeRoutesLiftAndModes()
{
    using namespace tp::map::globe;
    QCOMPARE(liftHeight(0.0), 0.0);
    // 上海—东京约 1770 km：弧高 12% ≈ 212 km；很长的段最高 1500 km
    const double a = 1770.0 / (kEarthRadiusM / 1000.0);
    QVERIFY(std::abs(liftHeight(a) * kEarthRadiusM / 1000.0 - 212.4) < 0.5);
    QVERIFY(std::abs(liftHeight(2.0) * kEarthRadiusM / 1000.0 - 1500.0) < 1e-6);
    const QByteArray json = R"({"version":1,"plans":[
        {"id":"p1","status":"planning","type":"route","stops":[
            {"name":"上海","lat":31.23,"lng":121.47,"leg":{"mode":"flight"}},
            {"name":"东京","lat":35.68,"lng":139.69,"leg":{"mode":"train"}},
            {"name":"大阪","lat":34.69,"lng":135.50},
            {"name":"京都","lat":35.01,"lng":135.77}]},
        {"id":"p2","status":"completed","type":"single","stops":[{"name":"冰岛","lat":64.1,"lng":-21.9}]}]})";
    TripData t;
    t.build(parseJson(json));
    const auto r = buildRoutes(t);
    QCOMPARE(int(r->plans.size()), 1);   // 单一目的地没有线
    const RoutePlan &p = r->plans[0];
    QCOMPARE(p.id, QStringLiteral("p1"));
    QCOMPARE(p.dash, 1);
    QCOMPARE(int(p.legs.size()), 3);
    QCOMPARE(p.legs[0].mode, QStringLiteral("flight"));
    QVERIFY(p.legs[0].lifted);
    QCOMPARE(p.legs[1].mode, QStringLiteral("train"));
    QVERIFY(!p.legs[1].lifted);
    QCOMPARE(p.legs[2].mode, QStringLiteral("flight"));   // 没填交通方式 = 飞行（和 Electron 一样），也抬起，但很短抬得很低
    QVERIFY(p.legs[2].lifted);
    double maxR = 0;
    for (const Vec3 &v : p.legs[0].pts) maxR = std::max(maxR, v.length());
    QVERIFY(std::abs((maxR - 1) - liftHeight(p.legs[0].angle)) < 1e-3);
    for (const Vec3 &v : p.legs[1].pts) QVERIFY(std::abs(v.length() - 1) < 1e-9);   // 火车贴地
    // 两头在球面上；相邻两点不超过 1°
    QVERIFY(std::abs(p.legs[0].pts.front().length() - 1) < 1e-9 && std::abs(p.legs[0].pts.back().length() - 1) < 1e-9);
    for (size_t j = 1; j < p.legs[0].pts.size(); ++j)
        QVERIFY(std::acos(std::clamp(p.legs[0].pts[j - 1].normalized().dot(p.legs[0].pts[j].normalized()), -1.0, 1.0)) <= 1.0 * M_PI / 180 + 1e-9);
    QVERIFY(p.legs[0].t.front() == 0.0 && std::abs(p.legs[0].t.back() - 1.0) < 1e-12);
}

void TestMap::globeScreenRunsCutAtLimb()
{
    using namespace tp::map::globe;
    View v; v.lat = 0; v.lng = 0; v.dist = 3.0; v.width = 1200; v.height = 800;
    const tp::map::globe::Camera cam(v);
    // 赤道上从 0° 到 150°：只有地平线（70.5°）以内的那段看得见，最后一点正好在球的边上
    std::vector<Vec3> pts;
    for (int i = 0; i <= 150; ++i) pts.push_back(toVec(0, i));
    std::vector<std::vector<double>> ts;
    const auto runs = screenRuns(cam, pts, &ts);
    QCOMPARE(int(runs.size()), 1);
    QVERIFY(std::abs(ts[0].back() - 70.53) < 0.05);
    // 边上那一点到屏幕中心的距离 = 球在屏幕上的半径 f / √(d² − 1)
    const double R = v.focal() / std::sqrt(v.dist * v.dist - 1);
    QVERIFY(std::abs(std::hypot(runs[0].back().x - 600, runs[0].back().y - 400) - R) < 0.5);
    // 中间转到背面又转回来的线：分成两段
    std::vector<Vec3> loop;
    for (int i = -60; i <= 300; ++i) loop.push_back(toVec(0, i));
    QCOMPARE(int(screenRuns(cam, loop).size()), 2);
    // 碰线：线上一点距离 0，离开 10 像素距离 10
    double sx, sy;
    QVERIFY(cam.project(toVec(0, 20), &sx, &sy));
    QVERIFY(distToRuns(runs, sx, sy) < 1e-6);
    QVERIFY(std::abs(distToRuns(runs, sx, sy + 10) - 10) < 0.2);
}

void TestMap::globeVehiclePlan()
{
    using namespace tp::map::globe;
    // 屏幕长度：30 px（跳过）、80 px（最短 1.5 秒）、400 px（2.5 秒）、2000 px（最长 5 秒）
    const VehiclePlan vp = planVehicle({ 30, 80, 400, 2000 });
    QCOMPARE(int(vp.legs.size()), 3);
    QCOMPARE(vp.legs[0].leg, 1);
    QCOMPARE(vp.legs[0].ms, 1500.0);
    QCOMPARE(vp.legs[1].ms, 2500.0);
    QCOMPARE(vp.legs[2].ms, 5000.0);
    QCOMPARE(vp.totalMs, 1500.0 + 300 + 2500 + 300 + 5000 + 1000);
    int leg; double frac;
    QVERIFY(vehicleAt(vp, 750, &leg, &frac) && leg == 1 && std::abs(frac - 0.5) < 1e-9);
    QVERIFY(!vehicleAt(vp, 1600, &leg, &frac) && leg == 1 && frac == 1.0);   // 到站停 0.3 秒
    QVERIFY(vehicleAt(vp, 1800 + 100, &leg, &frac) && leg == 2 && frac > 0 && frac < 0.1);
    QVERIFY(!vehicleAt(vp, vp.totalMs - 10, &leg, &frac) && leg == 3);       // 最后停 1 秒
    QVERIFY(planVehicle({ 10, 20 }).legs.empty());
    // pointAlong：两头 / 中间
    RouteLeg l;
    l.pts = { toVec(0, 0), toVec(0, 10) * 1.1, toVec(0, 20) };
    l.t = { 0, 0.5, 1 };
    Vec3 ahead;
    const Vec3 mid = pointAlong(l, 0.5, &ahead);
    QVERIFY(std::abs(mid.length() - 1.1) < 1e-9);
    QVERIFY(ahead.y > mid.y);   // 往东走
}

// 0.9.7 变形过渡（C1）：变形到 1 时地球的投影和同一级的平面（geo::Camera，和 maplibre 一样的墨卡托）逐像素一样；
// 瓦片着色器里"切平面上的点 → 透视投影"的算法（这里照着用 CPU 算一遍）也落在同一个位置
void TestMap::globeMorphMatchesFlat()
{
    using namespace tp::map;
    const double W = 1600, H = 1000;
    for (const auto &c : { std::array<double, 3>{ 31.2, 121.5, 5.5 }, std::array<double, 3>{ -33.9, 151.2, 6.3 }, std::array<double, 3>{ 64.1, -21.9, 5.5 },
                           std::array<double, 3>{ 10.0, 179.6, 5.8 }, std::array<double, 3>{ 35.0, 120.0, 2.2 } }) {
        globe::View v;
        v.lat = c[0]; v.lng = c[1]; v.width = W; v.height = H;
        v.dist = globe::distForZoom(c[2], v.focal(), v.lat);
        v.morph = 1;
        const globe::Camera cam(v);
        geo::Camera fc;
        fc.lat = c[0]; fc.lng = c[1]; fc.zoom = globe::zoomForDist(v.dist, v.focal(), v.lat); fc.width = W; fc.height = H;
        // 着色器的平面：C + E·dx·w − N·dy·w（w = 2π·cos φ0），透视投影（morph = 0 的相机）
        globe::View v0 = v; v0.morph = 0;
        const globe::Camera cam0(v0);
        const double la = c[0] * M_PI / 180, lo = c[1] * M_PI / 180, w = 2 * M_PI * std::cos(la);
        const globe::Vec3 C = globe::toVec(c[0], c[1]), E{ -std::sin(lo), std::cos(lo), 0 }, N{ -std::sin(la) * std::cos(lo), -std::sin(la) * std::sin(lo), std::cos(la) };
        double worst = 0, worstShader = 0;
        for (int i = -4; i <= 4; ++i)
            for (int j = -4; j <= 4; ++j) {
                const double span = 40.0 / std::exp2(c[2] - 1);   // 大约一屏的范围（度）
                const double lat = std::clamp(c[0] + j * span / 8, -84.0, 84.0), lng = c[1] + i * span / 8;
                double gx, gy;
                QVERIFY(cam.project(globe::toVec(lat, lng), &gx, &gy));
                const geo::Pt fp = fc.pixelFor(lat, lng);
                worst = std::max(worst, std::hypot(gx - fp.x, gy - fp.y));
                double dx = globe::mercX(lng) - globe::mercX(c[1]);
                dx -= std::floor(dx + 0.5);
                const double dy = globe::mercY(lat) - globe::mercY(c[0]);
                const globe::Vec3 q = C + E * (dx * w) - N * (dy * w);
                double sx, sy;
                QVERIFY(cam0.project(q, &sx, &sy));
                worstShader = std::max(worstShader, std::hypot(sx - fp.x, sy - fp.y));
            }
        QVERIFY2(worst < 0.01, qPrintable(QStringLiteral("%1,%2 z%3：变形终点和平面差 %4 像素").arg(c[0]).arg(c[1]).arg(c[2]).arg(worst)));
        QVERIFY2(worstShader < 0.05, qPrintable(QStringLiteral("%1,%2 z%3：着色器的平面和平面差 %4 像素").arg(c[0]).arg(c[1]).arg(c[2]).arg(worstShader)));
        // 变形到一半：在球面和平面之间
        v.morph = 0.5;
        const globe::Camera ch(v);
        double hx, hy, gx0, gy0;
        QVERIFY(ch.project(globe::toVec(c[0] + 1, c[1] + 1), &hx, &hy));
        QVERIFY(cam0.project(globe::toVec(c[0] + 1, c[1] + 1), &gx0, &gy0));
        const geo::Pt fp = fc.pixelFor(c[0] + 1, c[1] + 1);
        QVERIFY(std::abs(hx - (gx0 + fp.x) / 2) < 1e-6 && std::abs(hy - (gy0 + fp.y) / 2) < 1e-6);
    }
}

void TestMap::globeSlerpAndLerp()
{
    using namespace tp::map::globe;
    QVERIFY(std::abs(lerpDist(3.0, 1.5, 0.0) - 3.0) < 1e-12 && std::abs(lerpDist(3.0, 1.5, 1.0) - 1.5) < 1e-12);
    QVERIFY(std::abs(lerpDist(5.0, 1.25, 0.5) - 2.0) < 1e-12);   // 高度 4 → 0.25 的对数中点 = 1
    double lat, lng;
    slerpCenter(0, 170, 0, -170, 0.5, &lat, &lng);           // 跨 180° 走近的那边
    QVERIFY(std::abs(lat) < 1e-9 && std::abs(std::abs(lng) - 180) < 1e-9);
    slerpCenter(10, 20, 10, 20, 0.3, &lat, &lng);
    QVERIFY(std::abs(lat - 10) < 1e-12 && std::abs(lng - 20) < 1e-12);
    QVERIFY(std::abs(angleBetween(0, 0, 0, 90) - M_PI / 2) < 1e-12);
}

// 0.11.1 照片模式、相册的计算
void TestMap::photoMapBasics()
{
    namespace pm = tp::map::photomap;
    const QString js = QStringLiteral(R"({ "plans": [
      { "id": "a", "name": "旧趟", "status": "completed", "type": "route", "startDate": "2024-05-01", "stops": [
        { "id": "s1", "name": "一", "lat": 10, "lng": 20, "arriveDate": "2024-05-02" }, { "id": "s2", "name": "二", "lat": 30, "lng": 40 } ] },
      { "id": "b", "name": "新趟", "status": "planning", "startDate": "2026-01-01", "stops": [ { "id": "s3", "name": "三", "lat": -5, "lng": 100 } ] },
      { "id": "c", "name": "没照片", "stops": [] } ],
      "photos": [
        { "id": "p1", "file": "photos/x/1.jpg", "plan": "a", "stop": "s1" },
        { "id": "p2", "file": "photos/x/2.jpg", "plan": "a", "stop": "s1", "fav": true },
        { "id": "p3", "file": "photos/x/3.jpg", "plan": "a", "stop": "s2", "lat": 31, "lng": 41, "takenAt": "2023-03-04T05:06" },
        { "id": "p4", "file": "photos/x/4.heic", "plan": "b", "stop": "" },
        { "id": "p5", "file": "photos/x/5.jpg", "plan": "", "stop": "", "lat": 1, "lng": 2 },
        { "id": "p6", "file": "photos/x/6.jpg", "plan": "gone", "stop": "s9" } ] })");
    const tp::json::Value root = tp::json::parse(js);
    const pm::Library lib = pm::read(root);
    QCOMPARE(lib.recs.size(), 6);
    QVERIFY(lib.recs[0].hasPos && lib.recs[0].fromStop);      // 没有 GPS：站点的位置
    QCOMPARE(lib.recs[0].lat, 10.0);
    QVERIFY(lib.recs[2].hasPos && !lib.recs[2].fromStop);     // GPS 优先
    QCOMPARE(lib.recs[2].lat, 31.0);
    QVERIFY(!lib.recs[3].hasPos);                             // 只归到计划：没有位置
    QVERIFY(lib.recs[4].hasPos && lib.recs[4].plan.isEmpty()); // 未归类但有 GPS
    QVERIFY(lib.recs[5].plan.isEmpty());                      // 计划没了：算未归类
    QCOMPARE(lib.pinned, 4);
    QCOMPARE(lib.noPos, 2);
    QCOMPARE(lib.favs, 1);
    QCOMPARE(lib.inbox, 2);
    QCOMPARE(lib.heic, 1);
    QCOMPARE(lib.recs[0].day, QStringLiteral("2024-05-02"));  // 到站日期
    QCOMPARE(lib.recs[2].day, QStringLiteral("2023-03-04"));  // 拍摄时间优先
    QCOMPARE(lib.recs[3].day, QStringLiteral("2026-01-01"));  // 计划开始
    QCOMPARE(lib.trips.size(), 2);
    QCOMPARE(lib.trips[0].id, QStringLiteral("a"));           // 已完成的在前
    QCOMPARE(lib.trips[0].rep, 1);                            // 代表图优先精选

    // 合并：小比例尺全合在一起；放大后分开；同一站的永远在一起，代表图优先精选
    QVector<pm::Group> g = pm::cluster(lib.recs, 14);
    QCOMPARE(g.size(), 3);
    int same = -1;
    for (int i = 0; i < g.size(); ++i) if (g[i].members.size() == 2) same = i;
    QVERIFY(same >= 0);
    QVERIFY(g[same].samePlace);
    QCOMPARE(g[same].rep, 1);
    QVector<pm::Rec> closeBy;
    for (int i = 0; i < 2000; ++i) { pm::Rec r; r.hasPos = true; r.lat = 30 + (i % 50) * 0.01; r.lng = 110 + (i / 50) * 0.01; closeBy.push_back(r); }
    g = pm::cluster(closeBy, 3);
    QCOMPARE(g.size(), 1);
    QCOMPARE(g[0].members.size(), 2000);
    QVERIFY(!g[0].samePlace);
    const QVector<pm::Group> split = pm::cluster(closeBy, 16);
    QVERIFY(split.size() > 100);
    int sum = 0;
    for (const pm::Group &x : split) sum += int(x.members.size());
    QCOMPARE(sum, 2000);                                      // 一张不多一张不少

    // 行：整页按旅程
    const pm::RowLayout wide{ 100000, 100, 0 };
    auto kinds = [](const QVector<pm::Row> &rows) { QStringList k; for (const pm::Row &r : rows) k << r.kind; return k.join(u' '); };
    QCOMPARE(kinds(pm::rows(lib, QStringLiteral("trip"), QString(), false, wide)), QStringLiteral("plan stop photos stop photos plan stop photos"));
    QCOMPARE(kinds(pm::rows(lib, QStringLiteral("trip"), QStringLiteral("b"), false, wide)), QStringLiteral("plan stop photos"));
    // 窄列表：每趟一行 + 没有位置，只展开选中的
    QCOMPARE(kinds(pm::rows(lib, QStringLiteral("trip"), QString(), true, wide)), QStringLiteral("trip trip trip"));
    QCOMPARE(kinds(pm::rows(lib, QStringLiteral("trip"), QStringLiteral("a"), true, wide)), QStringLiteral("trip stop photos stop photos trip trip"));
    const QVector<pm::Row> np = pm::rows(lib, QStringLiteral("trip"), QString::fromLatin1(pm::kNoPos), true, wide);
    QCOMPARE(kinds(np), QStringLiteral("trip trip trip photos"));
    QCOMPARE(np.last().recs.size(), 2);
    // 窄到一行放一张时切两行，同一组
    const QVector<pm::Row> fav = pm::rows(lib, QStringLiteral("inbox"), QString(), false, pm::RowLayout{ 100, 100, 0 });
    QCOMPARE(fav.size(), 2);
    QCOMPARE(fav[0].group, fav[1].group);
    // 按年份：新的年在前，没有日期的最后
    QStringList titles;
    for (const pm::Row &r : pm::rows(lib, QStringLiteral("year"), QString(), false, wide)) if (r.kind == QLatin1String("year")) titles << r.title;
    QCOMPARE(titles.first(), QStringLiteral("2026"));
    QCOMPARE(titles.last(), QString());
    // 精选
    QCOMPARE(pm::rows(lib, QStringLiteral("fav"), QString(), false, wide).size(), 1);
    QCOMPARE(pm::rows(lib, QStringLiteral("fav"), QString(), false, wide).first().recs.first(), 1);
    // 0.12.9 等高行：整行铺满、比例不变、最后一行不拉满、窄窗口也不出错
    {
        pm::Library L;
        const double as[] = { 1.5, 0.67, 1.0, 1.78, 0.75, 1.33, 2.5, 0.2, 1.5, 1.5 };
        for (double a : as) { pm::Rec x; x.ratio = a; x.day = QStringLiteral("2026-01-01"); L.recs.push_back(x); }
        for (const double W : { 1000.0, 300.0, 60.0 }) {
            const pm::RowLayout lay{ W, 168, 8 };
            const QVector<pm::Row> rs = pm::rows(L, QStringLiteral("year"), QString(), false, lay);
            int seen = 0;
            QVector<const pm::Row *> ph;
            for (const pm::Row &x : rs) if (x.kind == QLatin1String("photos")) ph << &x;
            QVERIFY(!ph.isEmpty());
            for (const pm::Row *x : ph) {
                QCOMPARE(x->widths.size(), x->recs.size());
                seen += int(x->recs.size());
                double sum = 8.0 * (x->widths.size() - 1);
                for (double w : x->widths) sum += w;
                if (x != ph.last()) QVERIFY2(std::abs(sum - W) < 0.5, qPrintable(QString::number(sum)));
                else QVERIFY(sum <= W + 0.5);
                for (int k = 0; k + 1 < x->recs.size(); ++k) {   // 比例差不到 1 像素
                    const double a = std::clamp(L.recs[x->recs[k]].ratio, 0.2, 6.0);
                    QVERIFY(std::abs(x->widths[k] - a * x->height) <= 1.0);
                }
            }
            QCOMPARE(seen, 10);
            QVERIFY(ph.last()->height <= 168);
        }
    }
    // 归到计划
    tp::json::Value doc = root;
    QVERIFY(tp::map::plans::assignPhotos(doc, { QStringLiteral("p5") }, QStringLiteral("b"), QStringLiteral("s3")));
    QCOMPARE(pm::read(doc).recs[4].plan, QStringLiteral("b"));
    QVERIFY(!tp::map::plans::assignPhotos(doc, { QStringLiteral("nope") }, QStringLiteral("b"), QStringLiteral("s3")));
}

QTEST_GUILESS_MAIN(TestMap)
// ---------------- 0.9.3 星空 ----------------

// 固定种子的随机数和 Electron map-app.js seededRand(20261016) 的前 5 个一样（Node 跑出来的）
void TestMap::skySeededRand()
{
    const std::vector<double> got = tp::map::globe::mulberry32(20261016u, 5);
    const double want[5] = { 0.18689791089855134, 0.5267679621465504, 0.036956520518288016, 0.19033258804120123, 0.27266927040182054 };
    QCOMPARE(int(got.size()), 5);
    for (int i = 0; i < 5; ++i) QVERIFY2(got[size_t(i)] == want[i], qPrintable(QStringLiteral("#%1：%2 vs %3").arg(i).arg(got[size_t(i)], 0, 'g', 17).arg(want[i], 0, 'g', 17)));
}

// ---------------- 0.10.0 完整计划页 ----------------

void TestMap::sunAgainstElectron()
{
    const QJsonArray rows = QJsonDocument::fromJson(readFile(QStringLiteral(TP_SOURCE_DIR "/tests/golden/sun.json"))).array();
    QVERIFY(rows.size() >= 20);
    for (const QJsonValue &v : rows) {
        const QJsonObject c = v.toObject(), w = c[QStringLiteral("want")].toObject();
        const QString tz = c[QStringLiteral("tz")].toString(), name = tz + QLatin1Char(' ') + c[QStringLiteral("date")].toString();
        const sun::Times t = sun::info(c[QStringLiteral("lat")].toDouble(), c[QStringLiteral("lng")].toDouble(), c[QStringLiteral("date")].toString(), tz);
        QVERIFY2(t.ok, qPrintable(name));
        QCOMPARE(t.polar, w[QStringLiteral("polar")].toString());
        if (!t.polar.isEmpty()) continue;
        QVERIFY2(t.rise == w[QStringLiteral("rise")].toString(), qPrintable(name + QStringLiteral("：日出 %1 vs %2").arg(t.rise, w[QStringLiteral("rise")].toString())));
        QVERIFY2(t.set == w[QStringLiteral("set")].toString(), qPrintable(name + QStringLiteral("：日落 %1 vs %2").arg(t.set, w[QStringLiteral("set")].toString())));
        QCOMPARE(t.dayMin, w[QStringLiteral("dayMin")].toInt());
        QCOMPARE(t.approx, w[QStringLiteral("approx")].toBool());   // 这台电脑的时区库认得开发数据里用到的时区
    }
    QVERIFY(!sun::info(10, 20, QStringLiteral("2026-13-40"), QString()).ok);
    QVERIFY(!sun::info(10, 20, QString(), QString()).ok);
    QVERIFY2(sun::info(35, 135, QStringLiteral("2026-06-01"), QStringLiteral("Not/AZone")).approx, "认不出的时区退回按经度估算");
}

namespace {
tp::json::Value parseJson(const char *s)
{
    return tp::json::parse(QString::fromUtf8(s));
}
}

void TestMap::schedGrouping()
{
    const tp::json::Value stop = parseJson(R"({"schedule":[
        {"date":"","text":"随便"},
        {"date":"2026-11-23","time":"10:00","text":"岚山"},
        {"date":"2026-11-22","time":"14:00","end":"16:00","text":"稻荷"},
        {"date":"2026-11-22","time":"09:00","text":"清水寺"},
        {"date":"2026-11-22","text":"全天"}]})");
    const QVariantList d = plans::schedDays(stop, QStringLiteral("2026-11-22"));
    QCOMPARE(d.size(), 3);
    QCOMPARE(d[0].toMap().value("date").toString(), QStringLiteral("2026-11-22"));
    const QVariantList a = d[0].toMap().value("items").toList();
    QCOMPARE(a.size(), 3);
    QCOMPARE(a[2].toMap().value("text").toString(), QStringLiteral("全天"));   // 0.12.9 没写时间的放后面
    QCOMPARE(a[0].toMap().value("text").toString(), QStringLiteral("清水寺"));
    QCOMPARE(a[0].toMap().value("j").toInt(), 3);                              // 记着数组里的下标
    QVERIFY(a[0].toMap().value("today").toBool());
    QVERIFY(!d[1].toMap().value("items").toList()[0].toMap().value("today").toBool());
    QCOMPARE(d[2].toMap().value("date").toString(), QString());                // 没写日期的放最后
    // 旧格式的字符串
    const QVariantList s = plans::schedDays(parseJson(R"({"schedule":"老文字"})"), QString());
    QCOMPARE(s.size(), 1);
    QCOMPARE(s[0].toMap().value("items").toList()[0].toMap().value("text").toString(), QStringLiteral("老文字"));
    QVERIFY(plans::schedDays(parseJson("{}"), QString()).isEmpty());
}

void TestMap::schedEdit()
{
    tp::json::Value stop = parseJson(R"({"id":"s","schedule":[{"date":"2026-11-22","time":"09:00","text":"a","mine":1}]})");
    const tp::json::Value orig = stop;
    QCOMPARE(plans::putSchedule(stop, -1, "", "", "", "  "), QStringLiteral("empty"));
    QCOMPARE(plans::putSchedule(stop, -1, "", "", "10:00", "x"), QStringLiteral("endNoStart"));
    QCOMPARE(plans::putSchedule(stop, -1, "", "25:00", "", "x"), QStringLiteral("badTime"));
    QCOMPARE(plans::putSchedule(stop, -1, "", "abc", "", "x"), QStringLiteral("badTime"));
    QCOMPARE(plans::putSchedule(stop, -1, "2026-13-01", "", "", "x"), QStringLiteral("badDate"));
    QVERIFY(stop == orig);   // 有问题时什么都不改
    QCOMPARE(plans::putSchedule(stop, -1, "2026-11-23", "9:5", "", "x"), QStringLiteral("badTime"));
    QVERIFY(plans::putSchedule(stop, -1, "2026-11-23", "0930", "18:00", " 新 ").isEmpty());
    const tp::json::Array &arr = stop[QStringLiteral("schedule")].asArray();
    QCOMPARE(int(arr.size()), 2);
    QCOMPARE(arr[1][QStringLiteral("time")].toString(), QStringLiteral("09:30"));
    QCOMPARE(arr[1][QStringLiteral("text")].toString(), QStringLiteral("新"));
    // 修改：去掉时间、保留不认识的键
    QVERIFY(plans::putSchedule(stop, 0, "2026-11-22", "", "", "b").isEmpty());
    const tp::json::Value &it = stop[QStringLiteral("schedule")].asArray()[0];
    QVERIFY(!it.asObject().contains(QStringLiteral("time")));
    QCOMPARE(it[QStringLiteral("mine")].jsToNumber(), 1.0);
    QVERIFY(plans::removeSchedule(stop, 0));
    QVERIFY(!plans::removeSchedule(stop, 5));
    QCOMPARE(int(stop[QStringLiteral("schedule")].asArray().size()), 1);
    // 旧格式的字符串转成数组
    tp::json::Value old = parseJson(R"({"schedule":"老文字"})");
    QVERIFY(plans::putSchedule(old, -1, "", "", "", "新").isEmpty());
    QCOMPARE(int(old[QStringLiteral("schedule")].asArray().size()), 2);
}

void TestMap::pageDocWarnings()
{
    tp::json::Value root = parseJson(R"({"plans":[{"id":"p","status":"planning","startDate":"2026-08-31","endDate":"2026-08-31","stops":[]},
        {"id":"done","status":"completed","startDate":"2026-08-31","stops":[]}],
      "documents":[{"kind":"passport","expires":"2026-08-01","owner":"我"},
                   {"kind":"passport","expires":"2027-03-02"},
                   {"kind":"passport","expires":"2027-03-03"},
                   {"kind":"visa","expires":"2026-08-30","planId":"p"},
                   {"kind":"visa","expires":"2026-08-30","planId":"other"},
                   {"kind":"visa","expires":"不是日期","planId":"p"}]})");
    const QVariantList w = plans::docWarnings(root, *plans::planById(root, "p"));
    QCOMPARE(w.size(), 3);
    QCOMPARE(w[0].toMap().value("kind").toString(), QStringLiteral("passportExpired"));
    QCOMPARE(w[1].toMap().value("kind").toString(), QStringLiteral("passport6m"));   // 8 月 31 日 + 6 个月 = 3 月 3 日（和 JS 一样顺延），3 月 2 日不够
    QCOMPARE(w[2].toMap().value("kind").toString(), QStringLiteral("doc"));
    QVERIFY(plans::docWarnings(root, *plans::planById(root, "done")).isEmpty());
}

void TestMap::pageSeasonAndDays()
{
    tp::json::Value root = parseJson(R"({"seasonGuide":{"rows":[{"country":"日本","countryEn":"Japan","spans":[
        {"from":11,"to":2,"kind":"nice","text":"雪","textEn":"Snow"},{"from":3,"to":5,"kind":"best","text":"樱花"}]}]},
      "plans":[{"id":"p","status":"wish","name":"n","type":"route","seasonRef":"日本","startDate":"","endDate":"",
        "stops":[{"id":"a","name":"A","lat":35,"lng":135,"arriveDate":"2026-06-01","departDate":"2026-06-03","tz":"Asia/Tokyo","schedule":[]},
                 {"id":"bad","name":"坏","lat":"x","lng":0},
                 {"id":"b","name":"B","lat":36,"lng":136,"arriveDate":"2026-06-03","schedule":[{"date":"2026-06-03","text":"t"}]}]}]})");
    const QVariantMap s = plans::seasonInfo(root, "日本");
    QCOMPARE(s.value("nameEn").toString(), QStringLiteral("Japan"));
    const QVariantList c = s.value("cells").toList();
    QCOMPARE(c.size(), 12);
    QCOMPARE(c[0].toMap().value("kind").toString(), QStringLiteral("nice"));    // 跨年的一段
    QCOMPARE(c[3].toMap().value("kind").toString(), QStringLiteral("best"));
    QVERIFY(c[7].toMap().isEmpty());
    QVERIFY(plans::seasonInfo(root, "火星").isEmpty());

    const QVariantMap m = plans::pageInfo(root, "p", "2026-06-02");
    QVERIFY(m.value("season").toMap().contains("cells"));
    QCOMPARE(m.value("todayStop").toInt(), -1);   // 想去、没有日期：不算在路上
    const QVariantList st = m.value("stops").toList();
    QCOMPARE(st.size(), 2);                       // 坐标无效的不算
    QCOMPARE(st[0].toMap().value("sun").toMap().value("approx").toBool(), false);
    QCOMPARE(st[1].toMap().value("sun").toMap().value("approx").toBool(), true);
    QVERIFY(plans::pageInfo(root, "没有", "2026-06-02").isEmpty());

    // 预计天数
    QCOMPARE(plans::setDays(root, "p", "0"), QStringLiteral("badDays"));
    QCOMPARE(plans::setDays(root, "p", "1000"), QStringLiteral("badDays"));
    QCOMPARE(plans::setDays(root, "p", "abc"), QStringLiteral("badDays"));
    QVERIFY(plans::setDays(root, "p", "6.4").isEmpty());
    QCOMPARE(plans::pageInfo(root, "p", "2026-06-02").value("days").toInt(), 6);
    QVERIFY(plans::setDays(root, "p", " ").isEmpty());
    QCOMPARE(plans::pageInfo(root, "p", "2026-06-02").value("days").toInt(), 0);

    // 开始计划：只填出发时回来 = 出发
    QVERIFY(plans::startPlanning(root, "p", "2026-06-01", ""));
    const tp::json::Value *p = plans::planById(root, "p");
    QCOMPARE((*p)[QStringLiteral("status")].toString(), QStringLiteral("planning"));
    QCOMPARE((*p)[QStringLiteral("endDate")].toString(), QStringLiteral("2026-06-01"));
    // 这下在路上了：今天 = 最后一个到达日期 ≤ 今天的站
    QVERIFY(plans::startPlanning(root, "p", "2026-06-01", "2026-06-05"));
    const QVariantMap live = plans::pageInfo(root, "p", "2026-06-03");
    QVERIFY(live.value("live").toBool());
    QCOMPARE(live.value("todayStop").toInt(), 1);
    QVERIFY(live.value("stops").toList()[1].toMap().value("days").toList()[0].toMap().value("items").toList()[0].toMap().value("today").toBool());
    QVERIFY(!plans::pageInfo(root, "p", "2026-07-01").value("live").toBool());
}

void TestMap::tzFill()
{
    tp::json::Value root = parseJson(R"({"plans":[{"id":"p","stops":[
        {"id":"a","lat":39.47,"lng":75.99},{"id":"b","lat":43.8,"lng":87.6,"tz":"Asia/Urumqi"},{"id":"bad","lat":"x","lng":0},{"id":"c","lat":1,"lng":2,"tz":""}]}]})");
    const QVector<plans::TzStop> need = plans::stopsWithoutTz(*plans::planById(root, "p"));
    QCOMPARE(need.size(), 2);
    const QString url = plans::tzUrl(need).toString();
    QVERIFY2(url.startsWith("https://api.open-meteo.com/v1/forecast?") && url.contains("timezone=auto") && url.contains("39.470") && url.contains("forecast_days=1"), qPrintable(url));
    // 回复：数组、单个对象（只问一个地点时）、认不出的、坏 JSON
    QCOMPARE(plans::parseTz(R"([{"timezone":"Asia/Shanghai"},{"timezone":"<b>"}])", 2), QStringList({ "Asia/Shanghai", "" }));
    QCOMPARE(plans::parseTz(R"({"timezone":"UTC"})", 1), QStringList({ "UTC" }));
    QCOMPARE(plans::parseTz("坏的", 2), QStringList({ "", "" }));
    // 只写空的（手动填的、已经有的不动）
    QCOMPARE(plans::applyTz(root, "p", { "a", "b", "c", "没有" }, { "Asia/Shanghai", "Asia/Shanghai", "", "X/Y" }), 1);
    const tp::json::Array &st = (*plans::planById(root, "p"))[QStringLiteral("stops")].asArray();
    QCOMPARE(st[0][QStringLiteral("tz")].toString(), QStringLiteral("Asia/Shanghai"));
    QCOMPARE(st[1][QStringLiteral("tz")].toString(), QStringLiteral("Asia/Urumqi"));
    QCOMPARE(plans::applyTz(root, "没有", { "a" }, { "X/Y" }), 0);
}

// ---------------- 0.10.1 交通住宿、消费、汇率 ----------------
namespace {
QVariantMap vm(std::initializer_list<std::pair<QString, QVariant>> l) { QVariantMap m; for (const auto &kv : l) m.insert(kv.first, kv.second); return m; }
}

void TestMap::moneyRates()
{
    using namespace tp::map::plans;
    const Rates f = parseRates(R"({"base":"CNY","date":"2026-11-05","rates":{"AUD":0.2,"USD":0.14}})", "frankfurter", "CNY");
    QCOMPARE(f.date, QStringLiteral("2026-11-05"));
    QCOMPARE(f.table.size(), 2);
    const Rates e = parseRates(R"({"result":"success","time_last_update_unix":1793923351,"rates":{"AUD":0.25}})", "er-api", "CNY");
    QCOMPARE(e.date, QStringLiteral("2026-11-06"));
    QVERIFY(parseRates(R"({"result":"error","rates":{"AUD":0.25}})", "er-api", "CNY").table.isEmpty());
    QVERIFY(parseRates("不是 JSON", "frankfurter", "CNY").table.isEmpty());
    // 0.12.6 基准对不上的回复不能用；补缺只补没有的，基准不同不补，补完能换算
    QVERIFY(parseRates(R"({"base":"USD","rates":{"AUD":1.5}})", "frankfurter", "CNY").table.isEmpty());
    QVERIFY(parseRates(R"({"result":"success","base_code":"USD","rates":{"AUD":1.5}})", "er-api", "CNY").table.isEmpty());
    {
        Rates into = f;
        const Rates er = parseRates(R"({"result":"success","base_code":"CNY","rates":{"AUD":0.25,"TWD":5}})", "er-api", "CNY");
        QCOMPARE(mergeRates(into, er), 1);
        QVERIFY(qFuzzyCompare(into.table.value("AUD"), 0.2));   // Frankfurter 的不动
        QVERIFY(qFuzzyCompare(perMain(into, "CNY", "TWD"), 0.2));
        QVERIFY(qFuzzyCompare(perMain(into, "AUD", "TWD"), 0.04));   // 主货币不是基准时也对：1 TWD = 0.2 CNY = 0.04 AUD
        QCOMPARE(into.src, QStringLiteral("frankfurter + er-api"));
        const Rates other = parseRates(R"({"result":"success","rates":{"TWD":30}})", "er-api", "USD");
        Rates keep = f;
        QCOMPARE(mergeRates(keep, other), 0);
        QVERIFY(!keep.table.contains("TWD"));
        const tp::json::Value data = tp::json::parse(QStringLiteral(R"({"plans":[{"currency":"VND","budget":10,"expenses":[{"id":"a","amount":5,"currency":"TWD"},{"id":"b","amount":0,"currency":"EGP"}]},{"currency":"RUB"}]})"));
        const QStringList used = usedCurrencies(data);
        QVERIFY(used.contains("VND") && used.contains("TWD"));   // 有预算的计划币种、有金额的消费
        QVERIFY(!used.contains("EGP") && !used.contains("RUB"));   // 金额 0、没预算没花销的不算
    }
    // 1 AUD = 5 CNY；表的基准不是主货币时交叉换算；不知道时 < 0
    QVERIFY(qFuzzyCompare(perMain(f, "CNY", "AUD"), 5.0));
    QVERIFY(qFuzzyCompare(perMain(f, "CNY", "CNY"), 1.0));
    QVERIFY(qFuzzyCompare(perMain(f, "USD", "AUD"), 0.7));
    QVERIFY(perMain(f, "CNY", "ISK") < 0);
    QVERIFY(perMain(Rates(), "CNY", "AUD") < 0);
    QCOMPARE(currencyCodes().size(), 24);
}

void TestMap::moneySpend()
{
    using namespace tp::map::plans;
    const tp::json::Value plan = parseJson(R"({"currency":"AUD","budget":1000,"startDate":"2026-12-01","endDate":"2026-12-04",
      "bookings":[{"id":"b1","kind":"hotel","title":"H","start":"2026-12-01T15:00","cost":600},
                  {"id":"b2","kind":"flight","cost":300,"start":"2026-12-02"},
                  {"id":"b3","kind":"ticket","title":"没填费用"},
                  {"id":"b4","kind":"train","cost":"50"}],
      "expenses":[{"id":"e1","title":"机票","amount":300,"cat":"transport","date":"2026-12-02","bookingId":"b2"},
                  {"id":"e2","title":"退款","amount":-40,"cat":"food","date":"2026-12-03"},
                  {"id":"e3","title":"美元","amount":14,"currency":"USD","cat":"shop","date":"2026-12-03"},
                  {"id":"e4","title":"缺","amount":5,"currency":"ISK","date":""},
                  {"id":"e5","title":"孤儿","amount":7,"bookingId":"早没了","cat":"怪分类","date":"2026-12-04"}]})");
    const QVector<SpendRow> rows = spendRows(plan);
    QCOMPARE(rows.size(), 7);   // 5 笔消费 + b1、b4（b2 已经记到消费，b3 没费用）
    QCOMPARE(rows.first().date, QStringLiteral("2026-12-04"));   // 从新到旧
    QCOMPARE(rows.first().cat, QStringLiteral("other"));         // 认不出的分类
    QVERIFY(!rows.first().linked);
    int fromBooking = 0;
    for (const SpendRow &r : rows) {
        if (r.fromBooking) ++fromBooking;
        if (r.id == "e1") QVERIFY(r.linked);
        if (r.bookingId == "b1") { QCOMPARE(r.cat, QStringLiteral("stay")); QCOMPARE(r.date, QStringLiteral("2026-12-01")); QCOMPARE(r.id, QString()); }
    }
    QCOMPARE(fromBooking, 2);

    Rates t = parseRates(R"({"date":"d","rates":{"AUD":0.2,"USD":0.14}})", "frankfurter", "CNY");
    const QVariantMap m = spendInfo(plan, "CNY", t);
    // 预订 600 + 300 + 50 = 950；另外记账 -40 + 20（14 USD）+ 7 = -13；ISK 没汇率
    QVERIFY(qAbs(m.value("bookTotal").toDouble() - 950) < 1e-9);
    QVERIFY(qAbs(m.value("expTotal").toDouble() + 13) < 1e-9);
    QVERIFY(qAbs(m.value("total").toDouble() - 937) < 1e-9);
    QCOMPARE(m.value("missing").toStringList(), QStringList{ "ISK" });
    QCOMPARE(m.value("mainMissing").toStringList(), QStringList{ "ISK" });
    QVERIFY(qAbs(m.value("mainTotal").toDouble() - 937 * 5) < 1e-6);
    QVERIFY(!m.value("over").toBool());
    QVERIFY(qAbs(m.value("remain").toDouble() - 63) < 1e-9);
    QCOMPARE(m.value("days").toInt(), 4);
    // 退款让餐饮变负数：不进色条
    QStringList cats;
    for (const QVariant &c : m.value("cats").toList()) cats << c.toMap().value("k").toString();
    QCOMPARE(cats, QStringList({ "transport", "stay", "shop", "other" }));
    // 没有汇率表：外币都算缺，计划币种的照算
    const QVariantMap none = spendInfo(plan, "CNY", Rates());
    QVERIFY(none.value("missing").toStringList().contains("USD"));
    QVERIFY(qAbs(none.value("total").toDouble() - (950 - 40 + 7)) < 1e-9);
    // 主货币就是计划币种：不用汇率
    QVERIFY(spendInfo(parseJson(R"({"currency":"CNY","expenses":[{"amount":10}]})"), "CNY", Rates()).value("missing").toStringList().isEmpty());
    // 没写币种按 AUD，超预算
    const QVariantMap over = spendInfo(parseJson(R"({"budget":10,"bookings":[{"id":"x","cost":30}]})"), "CNY", Rates());
    QCOMPARE(over.value("currency").toString(), QStringLiteral("AUD"));
    QVERIFY(over.value("over").toBool());
    QVERIFY(qFuzzyCompare(over.value("pct").toDouble(), 1.0));
}

void TestMap::moneySplit()
{
    using namespace tp::map::plans;
    const auto rowsOf = [](const QVariantMap &m) { QHash<QString, QVariantMap> h; for (const QVariant &v : m.value("settle").toMap().value("rows").toList()) h.insert(v.toMap().value("name").toString(), v.toMap()); return h; };
    const auto trs = [](const QVariantMap &m) { QStringList l; for (const QVariant &v : m.value("settle").toMap().value("transfers").toList()) { const QVariantMap t = v.toMap(); l << t.value("from").toString() + ">" + t.value("to").toString() + ":" + QString::number(t.value("amount").toDouble()); } return l; };
    Rates t = parseRates(R"({"date":"2026-11-05","rates":{"AUD":0.2,"USD":0.14}})", "frankfurter", "CNY");

    // 平摊、只算几个人、按金额、部分写金额、退款、旧数据、外币、缺汇率；交通住宿不进结算
    const tp::json::Value plan = parseJson(R"({"currency":"CNY","people":[{"id":"a","name":"我"},{"id":"b","name":"王"},{"id":"c","name":"李"}],
      "bookings":[{"id":"h","kind":"hotel","cost":999}],
      "expenses":[{"id":"e1","amount":300,"paidBy":"a"},
                  {"id":"e2","amount":100,"paidBy":"b","split":[{"who":"a"},{"who":"b"}]},
                  {"id":"e3","amount":100,"paidBy":"c","split":[{"who":"a","amount":70},{"who":"c","amount":30}]},
                  {"id":"e4","amount":90,"paidBy":"a","split":[{"who":"a","amount":10},{"who":"b"},{"who":"c"}]},
                  {"id":"e5","amount":-30,"paidBy":"a"},
                  {"id":"e6","amount":50},
                  {"id":"e7","amount":14,"currency":"USD","paidBy":"b"},
                  {"id":"e8","amount":5,"currency":"ISK","paidBy":"b"},
                  {"id":"e9","amount":6,"paidBy":"不认识","split":[{"who":"早删了"}]}]})");
    const QVariantMap m = settleInfo(plan, "CNY", t);
    QVERIFY(m.value("multi").toBool());
    QCOMPARE(m.value("expenseCount").toInt(), 9);
    QCOMPARE(m.value("bookingOnly").toDouble(), 999.0);
    const auto h = rowsOf(m);
    // 付了：a = 300 + 90 − 30 + 50 + 6 = 416；b = 100 + 100（14 USD）= 200；c = 100
    QCOMPARE(h["我"].value("paid").toDouble(), 416.0);
    QCOMPARE(h["王"].value("paid").toDouble(), 200.0);
    QCOMPARE(h["李"].value("paid").toDouble(), 100.0);
    // 该出：a = 100 + 50 + 70 + 10 − 10 + 50 + 33.33 + 2 = 305.33；b = 100 + 50 + 40 − 10 + 33.33 + 2 = 215.33；c = 100 + 30 + 40 − 10 + 33.33 + 2 = 195.33
    QCOMPARE(h["我"].value("share").toDouble(), 305.33);
    QCOMPARE(h["王"].value("share").toDouble(), 215.33);
    QCOMPARE(h["李"].value("share").toDouble(), 195.33);
    QCOMPARE(h["我"].value("net").toDouble(), 110.67);
    QCOMPARE(m.value("settle").toMap().value("missing").toStringList(), QStringList{ "ISK" });
    const QVariantList used = m.value("settle").toMap().value("rates").toList();
    QCOMPARE(used.size(), 1);
    QCOMPARE(used[0].toMap().value("cur").toString(), QStringLiteral("USD"));
    QCOMPARE(trs(m), QStringList({ "李>我:95.33", "王>我:15.33" }));   // 最少转账

    // 最少转账：一个人欠两个人，两笔；账平了没有
    QCOMPARE(trs(settleInfo(parseJson(R"({"currency":"CNY","people":[{"id":"a","name":"A"},{"id":"b","name":"B"},{"id":"c","name":"C"}],
      "expenses":[{"amount":90,"paidBy":"a","split":[{"who":"c"}]},{"amount":60,"paidBy":"b","split":[{"who":"c"}]}]})"), "CNY", t)), QStringList({ "C>A:90", "C>B:60" }));
    QVERIFY(trs(settleInfo(parseJson(R"({"currency":"CNY","people":[{"id":"a","name":"A"},{"id":"b","name":"B"}],
      "expenses":[{"amount":10,"paidBy":"a"},{"amount":10,"paidBy":"b"}]})"), "CNY", t)).isEmpty());
    // 一个人：不算 multi；没有同行人：rows 空
    QVERIFY(!settleInfo(parseJson(R"({"people":[{"id":"a","name":"A"}]})"), "CNY", t).value("multi").toBool());
    QVERIFY(settleInfo(parseJson(R"({"expenses":[{"amount":10}]})"), "CNY", t).value("settle").toMap().value("rows").toList().isEmpty());

    // spendInfo 每笔带上怎么分
    QHash<QString, QVariantMap> rows;
    for (const QVariant &v : spendInfo(plan, "CNY", t).value("rows").toList()) rows.insert(v.toMap().value("id").toString(), v.toMap());
    QCOMPARE(rows["e1"].value("among").toStringList(), QStringList({ "a", "b", "c" }));
    QCOMPARE(rows["e1"].value("payerName").toString(), QStringLiteral("我"));
    QCOMPARE(rows["e2"].value("among").toStringList(), QStringList({ "a", "b" }));
    QVERIFY(rows["e3"].value("custom").toBool());
    QCOMPARE(rows["e3"].value("amounts").toMap().value("a").toDouble(), 70.0);
    QVERIFY(rows["e4"].value("custom").toBool());      // 只写了一部分金额也算按金额
    QCOMPARE(rows["e6"].value("among").toStringList(), QStringList{ "a" });   // 旧数据只算付款人自己
    QCOMPARE(rows["e6"].value("onlyName").toString(), QStringLiteral("我"));
    QCOMPARE(rows["e9"].value("payer").toString(), QStringLiteral("a"));      // 不认识的付款人退回第一个
    QCOMPARE(rows["e9"].value("among").toStringList(), QStringList({ "a", "b", "c" }));

    // putExpense：分账字段
    tp::json::Value p = parseJson(R"({"currency":"AUD","people":[{"id":"a","name":"我","x":1},{"id":"b","name":"王"}],"expenses":[{"id":"e","amount":10,"paidBy":"b","split":[{"who":"a"}],"keep":7}]})");
    const auto exp = [&](const QString &id) -> const tp::json::Value & { for (const tp::json::Value &x : p["expenses"].asArray()) if (x["id"].toString() == id) return x; static tp::json::Value none; return none; };
    QCOMPARE(putExpense(p, "e", { { "amount", "10" }, { "date", "" } }), QString());   // 没有 paidBy：分账字段不动
    QCOMPARE(exp("e")["paidBy"].toString(), QStringLiteral("b"));
    QCOMPARE(int(exp("e")["split"].asArray().size()), 1);
    QCOMPARE(putExpense(p, "e", { { "amount", "10" }, { "paidBy", "a" }, { "split", QVariantList() } }), QString());   // 所有人平摊：去掉 split
    QVERIFY(!exp("e").asObject().contains("split"));
    QCOMPARE(exp("e")["keep"].toNumber(), 7.0);
    const QVariantList bad{ QVariantMap{ { "who", "a" }, { "amount", "3" } }, QVariantMap{ { "who", "b" }, { "amount", "3" } } };
    QCOMPARE(putExpense(p, "e", { { "amount", "10" }, { "paidBy", "a" }, { "split", bad } }), QStringLiteral("badSplit"));
    const QVariantList good{ QVariantMap{ { "who", "a" }, { "amount", "7" } }, QVariantMap{ { "who", "b" }, { "amount", "3" } } };
    QCOMPARE(putExpense(p, "e", { { "amount", "10" }, { "paidBy", "a" }, { "split", good } }), QString());
    QCOMPARE(exp("e")["split"].asArray()[0]["amount"].toNumber(), 7.0);
    // 退款也能按金额分
    const QVariantList back{ QVariantMap{ { "who", "a" }, { "amount", "-7" } }, QVariantMap{ { "who", "b" }, { "amount", "-3" } } };
    QCOMPARE(putExpense(p, "e", { { "amount", "-10" }, { "paidBy", "a" }, { "split", back } }), QString());
    // 加人，新加一笔：默认第一个人付、所有人平摊
    QCOMPARE(addPerson(p, "  李  ", "我"), QString());
    QCOMPARE(addPerson(p, "李", "我"), QStringLiteral("dupName"));
    QCOMPARE(addPerson(p, "   ", "我"), QStringLiteral("emptyName"));
    QCOMPARE(putExpense(p, "", { { "amount", "5" }, { "title", "n" } }), QString());
    QCOMPARE(int(p["people"].asArray().size()), 3);
    QCOMPARE(p["people"].asArray()[2]["name"].toString(), QStringLiteral("李"));
    QCOMPARE(p["people"].asArray()[0]["x"].toNumber(), 1.0);   // 同行人不认识的键保留
    QCOMPARE(p["expenses"].asArray().back()["paidBy"].toString(), QStringLiteral("a"));

    // 同行人：改名、删除（用到的不能删；删空去掉 people）
    QCOMPARE(editPerson(p, "b", " 老王 ", false), QString());
    QCOMPARE(p["people"].asArray()[1]["name"].toString(), QStringLiteral("老王"));
    QCOMPARE(editPerson(p, "b", "", false), QString());
    QCOMPARE(p["people"].asArray()[1]["name"].toString(), QStringLiteral("老王"));   // 空名字不改
    QCOMPARE(editPerson(p, "没有", "x", false), QStringLiteral("gone"));
    QCOMPARE(editPerson(p, "b", "", true), QStringLiteral("used"));   // split 里用到
    QCOMPARE(editPerson(p, "a", "", true), QStringLiteral("used"));   // 付款人
    const QString liId = p["people"].asArray()[2]["id"].toString();
    QCOMPARE(editPerson(p, liId, "", true), QString());   // 只在全员平摊的笔里：能删
    QCOMPARE(int(p["people"].asArray().size()), 2);
    tp::json::Value lone = parseJson(R"({"people":[{"id":"z","name":"Z"}]})");
    QCOMPARE(editPerson(lone, "z", "", true), QString());
    QVERIFY(!lone.asObject().contains("people"));

    // 改旧的预订时勾"记到消费"：加一条，只加一次；有同行人时默认平摊
    tp::json::Value q = parseJson(R"({"currency":"CNY","people":[{"id":"a","name":"A"},{"id":"b","name":"B"}],"bookings":[{"id":"h","kind":"hotel","title":"H","cost":100,"start":"2026-12-01"}]})");
    QCOMPARE(settleInfo(q, "CNY", t).value("bookingOnly").toDouble(), 100.0);
    const QVariantMap f{ { "kind", "hotel" }, { "title", "H" }, { "startDate", "2026-12-01" }, { "cost", "100" }, { "toExp", true } };
    QCOMPARE(putBooking(q, "h", f), QString());
    QCOMPARE(putBooking(q, "h", f), QString());
    QCOMPARE(int(q["expenses"].asArray().size()), 1);
    QCOMPARE(q["expenses"].asArray()[0]["paidBy"].toString(), QStringLiteral("a"));
    QCOMPARE(settleInfo(q, "CNY", t).value("bookingOnly").toDouble(), 0.0);
    QCOMPARE(rowsOf(settleInfo(q, "CNY", t))["A"].value("net").toDouble(), 50.0);

    // ---- 0.11.5 加人时自动平摊 ----
    tp::json::Value a = parseJson(R"({"currency":"AUD","mystery":{"k":1},
      "bookings":[{"id":"b1","kind":"hotel","title":"H","cost":100,"start":"2026-12-01T15:00","z":1},{"id":"b2","kind":"train","title":"T","cost":40},{"id":"b3","kind":"bus","title":"没费用"}],
      "expenses":[{"id":"old","amount":30,"title":"旧","keep":1},{"id":"lnk","amount":40,"bookingId":"b2"}]})");
    QVERIFY(settleInfo(a, "AUD", t).value("people").toList().isEmpty());
    QCOMPARE(settleInfo(a, "AUD", t).value("shareCount").toInt(), 2);    // 预告：已有 2 笔
    QCOMPARE(settleInfo(a, "AUD", t).value("bookingCount").toInt(), 1);  // b1；b2 已关联，b3 没费用
    const double total0 = spendInfo(a, "AUD", t).value("total").toDouble();
    QCOMPARE(addPerson(a, "小王", "我"), QString());
    QCOMPARE(a["people"].asArray()[0]["name"].toString(), QStringLiteral("我"));
    const QString me = a["people"].asArray()[0]["id"].toString();
    const auto ex = [](const tp::json::Value &pl, const QString &id) -> tp::json::Value { for (const tp::json::Value &x : pl["expenses"].asArray()) if (x["id"].toString() == id) return x; return {}; };
    QCOMPARE(ex(a, "old")["paidBy"].toString(), me);                      // legacy 改写：我付
    QVERIFY(!ex(a, "old").asObject().contains("split"));                  // split 不写 = 全员平摊
    QCOMPARE(ex(a, "old")["keep"].toNumber(), 1.0);                       // 未知字段保留
    QCOMPARE(a["mystery"]["k"].toNumber(), 1.0);
    QCOMPARE(a["bookings"].asArray()[0]["z"].toNumber(), 1.0);
    QCOMPARE(int(a["expenses"].asArray().size()), 3);                     // b1 记一笔；b2 不重复；b3 没费用不记
    const tp::json::Value rec = a["expenses"].asArray().back();
    QCOMPARE(rec["bookingId"].toString(), QStringLiteral("b1"));
    QCOMPARE(rec["paidBy"].toString(), me);
    QCOMPARE(rec["date"].toString(), QStringLiteral("2026-12-01"));
    QCOMPARE(rec["cat"].toString(), QStringLiteral("stay"));
    QVERIFY(!rec.asObject().contains("split"));
    QCOMPARE(spendInfo(a, "AUD", t).value("total").toDouble(), total0);   // 合计不重复算
    QCOMPARE(settleInfo(a, "AUD", t).value("bookingOnly").toDouble(), 0.0);
    // 再加人：不重复记、不改写；全员平摊的自然跟着变；已写 split 的不动
    QCOMPARE(putExpense(a, "old", { { "amount", "30" }, { "paidBy", me }, { "split", QVariantList{ QVariantMap{ { "who", me } } } } }), QString());   // 不 A
    const tp::json::Value single = ex(a, "old");
    QCOMPARE(addPerson(a, "小李", "我"), QString());
    QCOMPARE(int(a["expenses"].asArray().size()), 3);
    QVERIFY(ex(a, "old") == single);
    QCOMPARE(rowsOf(settleInfo(a, "AUD", t))["小李"].value("share").toDouble(), 46.67);   // (40 + 100) / 3，结算保留两位
    // 新存一条有费用的交通住宿：有同行人时自动记，再存不重复
    const QVariantMap car{ { "kind", "car" }, { "title", "车" }, { "startDate", "2026-12-02" }, { "cost", "60" } };
    QCOMPARE(putBooking(a, "", car), QString());
    QCOMPARE(int(a["expenses"].asArray().size()), 4);
    const QString carId = a["bookings"].asArray().back()["id"].toString();
    QCOMPARE(putBooking(a, carId, car), QString());
    QCOMPARE(int(a["expenses"].asArray().size()), 4);
    QCOMPARE(a["expenses"].asArray().back()["paidBy"].toString(), me);
    // 只有一个人时不自动记（要勾）
    tp::json::Value solo = parseJson(R"({"people":[{"id":"z","name":"我"}],"expenses":[{"id":"l","amount":1}]})");
    QCOMPARE(putBooking(solo, "", car), QString());
    QCOMPARE(int(solo["expenses"].asArray().size()), 1);
    // 只加"我"自己：一个人，不改写
    tp::json::Value mine = parseJson(R"({"expenses":[{"id":"l","amount":1}]})");
    QCOMPARE(addPerson(mine, "我", "我"), QString());
    QCOMPARE(int(mine["people"].asArray().size()), 1);
    QVERIFY(!mine["expenses"].asArray()[0].asObject().contains("paidBy"));

    // 部分固定：固定的按金额，没写的人平分剩下的；保存时不写 amount 键
    tp::json::Value c = parseJson(R"({"currency":"AUD","people":[{"id":"a","name":"A"},{"id":"b","name":"B"},{"id":"c","name":"C"}],"expenses":[{"id":"e","amount":100,"paidBy":"a"}]})");
    const QVariantList part{ QVariantMap{ { "who", "a" }, { "amount", "" } }, QVariantMap{ { "who", "b" }, { "amount", "40" } }, QVariantMap{ { "who", "c" } } };
    QCOMPARE(putExpense(c, "e", { { "amount", "100" }, { "paidBy", "a" }, { "split", part } }), QString());
    const tp::json::Array sp = ex(c, "e")["split"].asArray();
    QVERIFY(!sp[0].asObject().contains("amount"));
    QCOMPARE(sp[1]["amount"].toNumber(), 40.0);
    QVERIFY(!sp[2].asObject().contains("amount"));
    QCOMPARE(rowsOf(settleInfo(c, "AUD", t))["B"].value("share").toDouble(), 40.0);
    QCOMPARE(rowsOf(settleInfo(c, "AUD", t))["C"].value("share").toDouble(), 30.0);
    QHash<QString, QVariantMap> cr;
    for (const QVariant &v : spendInfo(c, "AUD", t).value("rows").toList()) cr.insert(v.toMap().value("id").toString(), v.toMap());
    QVERIFY(cr["e"].value("custom").toBool());
    QCOMPARE(cr["e"].value("amounts").toMap().size(), 1);
    QCOMPARE(cr["e"].value("among").toStringList().size(), 3);
    // 校验：超过总额（退款按绝对值）、全都填了却不相等；对不上不动数据
    const tp::json::Value before = c;
    const auto split2 = [](const char *x, const char *y) { return QVariantList{ QVariantMap{ { "who", "a" }, { "amount", x } }, QVariantMap{ { "who", "b" } } , QVariantMap{ { "who", "c" }, { "amount", y } } }; };
    QCOMPARE(putExpense(c, "e", { { "amount", "100" }, { "paidBy", "a" }, { "split", split2("70", "40") } }), QStringLiteral("badSplit"));
    QCOMPARE(putExpense(c, "e", { { "amount", "-100" }, { "paidBy", "a" }, { "split", split2("-70", "-40") } }), QStringLiteral("badSplit"));
    QCOMPARE(putExpense(c, "e", { { "amount", "100" }, { "paidBy", "a" }, { "split", QVariantList{ QVariantMap{ { "who", "a" }, { "amount", "abc" } } } } }), QStringLiteral("badSplit"));
    QVERIFY(c == before);
    QCOMPARE(putExpense(c, "e", { { "amount", "-100" }, { "paidBy", "a" }, { "split", split2("-70", "-30") } }), QString());   // 正好等于：另一个人分 0
    // 往返不变：旧的按金额数据原样再存一遍
    tp::json::Value rt = parseJson(R"({"currency":"AUD","people":[{"id":"a","name":"A"},{"id":"b","name":"B"}],"expenses":[{"id":"e","date":"2026-12-01","cat":"food","title":"t","amount":10,"paidBy":"a","split":[{"who":"a","amount":7},{"who":"b","amount":3}],"keep":{"x":[1]}}]})");
    const tp::json::Value rt0 = rt;
    QCOMPARE(putExpense(rt, "e", { { "title", "t" }, { "amount", "10" }, { "cat", "food" }, { "date", "2026-12-01" }, { "paidBy", "a" },
                                   { "split", QVariantList{ QVariantMap{ { "who", "a" }, { "amount", "7" } }, QVariantMap{ { "who", "b" }, { "amount", "3" } } } } }), QString());
    QVERIFY(rt == rt0);

    // 删人：全员平摊不算用到；付过钱、写进 split 的不能删，说清哪几笔
    tp::json::Value d = parseJson(R"({"currency":"AUD","people":[{"id":"a","name":"A"},{"id":"b","name":"B"},{"id":"c","name":"C"},{"id":"e","name":"E"}],
      "expenses":[{"id":"1","title":"全员","amount":90,"paidBy":"a"},{"id":"2","title":"写进","amount":10,"paidBy":"a","split":[{"who":"b"}]},{"id":"3","title":"付过","amount":10,"paidBy":"c","split":[{"who":"a"}]}]})");
    QHash<QString, QVariantMap> who;
    for (const QVariant &v : settleInfo(d, "AUD", t).value("people").toList()) who.insert(v.toMap().value("id").toString(), v.toMap());
    QVERIFY(who["b"].value("used").toBool());
    QCOMPARE(who["b"].value("uses").toList().first().toMap().value("title").toString(), QStringLiteral("写进"));
    QCOMPARE(who["c"].value("uses").toList().first().toMap().value("title").toString(), QStringLiteral("付过"));
    QCOMPARE(who["a"].value("uses").toList().size(), 3);
    QVERIFY(!who["e"].value("used").toBool());
    QCOMPARE(editPerson(d, "b", "", true), QStringLiteral("used"));
    QCOMPARE(editPerson(d, "c", "", true), QStringLiteral("used"));
    QCOMPARE(editPerson(d, "e", "", true), QString());
    QCOMPARE(rowsOf(settleInfo(d, "AUD", t))["B"].value("share").toDouble(), 40.0);   // 90 / 3 + 10
}

void TestMap::moneyLedger()
{
    // 0.10.5 modeAlsoParts：旧设置没有这个键 = 都开；未知内容保留；只写 false 的算关
    QVERIFY(plans::overlayPartsOff(parseJson("{}")).isEmpty());
    QVERIFY(plans::overlayPartsOff(tp::json::Value()).isEmpty());
    const tp::json::Value partsOld = parseJson(R"({"wish":{"foot":false,"plan":"乱写","新键":1},"未来":{"x":1}})");
    const QHash<QString, QSet<QString>> partsOff = plans::overlayPartsOff(partsOld);
    QCOMPARE(partsOff.value("wish"), QSet<QString>{ "foot" });
    QVERIFY(partsOff.value("plan").isEmpty());
    const tp::json::Value partsNew = plans::overlayPartsJson(partsOld, partsOff);
    QCOMPARE(partsNew["未来"]["x"].toNumber(), 1.0);
    QCOMPARE(partsNew["wish"]["新键"].toNumber(), 1.0);
    QCOMPARE(partsNew["plan"]["foot"].toBool(), true);
    QCOMPARE(plans::overlayPartsOff(partsNew), partsOff);
    using namespace tp::map::plans;
    Rates t = parseRates(R"({"date":"d","rates":{"AUD":0.2,"USD":0.14}})", "frankfurter", "CNY");
    const tp::json::Value root = parseJson(R"({"plans":[
      {"id":"p1","name":"一","status":"completed","currency":"AUD","budget":100,"startDate":"2025-01-01","endDate":"2025-01-02",
       "bookings":[{"id":"b","kind":"hotel","cost":10,"start":"2025-01-01"},{"id":"b2","kind":"flight","cost":20,"start":"2024-12-30"}],
       "expenses":[{"id":"e","amount":20,"cat":"transport","date":"2024-12-30","bookingId":"b2"},{"id":"e2","amount":5,"cat":"food","currency":"ISK","date":"2025-01-01"}]},
      {"id":"p2","name":"二","status":"planning","currency":"CNY","expenses":[{"id":"x","amount":1000,"cat":"food","date":""},{"id":"y","amount":-100,"cat":"food","date":"2026-05-01"}]},
      {"id":"p3","name":"空","status":"wish","currency":"CNY"},
      {"id":"p4","name":"零","currency":"CNY","expenses":[{"id":"z","amount":0}]},
      "不是计划"]})");
    const QVariantMap m = ledgerInfo(root, "CNY", t, "2026-06-01");
    const QVariantList trips = m.value("trips").toList();
    QCOMPARE(trips.size(), 2);
    QCOMPARE(m.value("emptyCount").toInt(), 2);
    QCOMPARE(trips[0].toMap().value("id").toString(), QStringLiteral("p2"));   // 花得多的在前（900 > 150）
    const QVariantMap a = trips[1].toMap();
    QVERIFY(qAbs(a.value("spent").toDouble() - 150) < 1e-6);                  // (10 + 20) AUD × 5，不算两遍；ISK 没汇率
    QCOMPARE(a.value("days").toInt(), 2);
    QVERIFY(qAbs(a.value("budgetMain").toDouble() - 500) < 1e-6);
    QCOMPARE(m.value("missing").toStringList(), QStringList{ "ISK" });
    // 0.10.5 第 5 题 A：p1 已完成算已花；p2 计划中没有开始日期算还没去。平均、年份、类别只算已花
    QVERIFY(qAbs(m.value("total").toDouble() - 150) < 1e-6);
    QVERIFY(qAbs(m.value("future").toDouble() - 900) < 1e-6);
    QCOMPARE(m.value("spentCount").toInt(), 1);
    QCOMPARE(m.value("futureCount").toInt(), 1);
    QCOMPARE(trips[0].toMap().value("done").toBool(), false);
    QCOMPARE(trips[1].toMap().value("state").toString(), QStringLiteral("completed"));
    QVERIFY(qAbs(m.value("perTrip").toDouble() - 150) < 1e-6);
    QVERIFY(qAbs(m.value("perDay").toDouble() - 75) < 1e-6);                  // 只算有日期的那一趟
    QStringList ys;
    for (const QVariant &v : m.value("years").toList()) ys << v.toMap().value("y").toString();
    QCOMPARE(ys, QStringList({ "2025", "2024" }));
    QCOMPARE(m.value("cats").toList().size(), 2);                             // 住宿、交通（p2 的餐饮还没算）
    QCOMPARE(ledgerInfo(parseJson(R"({})"), "CNY", t, "2026-06-01").value("trips").toList().size(), 0);
    // 到了开始日期的计划中算已开始；开始日期还没到、想去的都算还没去
    const tp::json::Value r3 = parseJson(R"({"plans":[
      {"id":"s","status":"planning","startDate":"2026-06-01","currency":"CNY","expenses":[{"id":"a","amount":10}]},
      {"id":"f","status":"planning","startDate":"2026-06-02","currency":"CNY","expenses":[{"id":"b","amount":20}]},
      {"id":"w","status":"wish","startDate":"2020-01-01","currency":"CNY","expenses":[{"id":"c","amount":40}]}]})");
    const QVariantMap m3 = ledgerInfo(r3, "CNY", t, "2026-06-01");
    QVERIFY(qAbs(m3.value("total").toDouble() - 10) < 1e-6);
    QVERIFY(qAbs(m3.value("future").toDouble() - 60) < 1e-6);
    for (const QVariant &v : m3.value("trips").toList())
        if (v.toMap().value("id") == "s") QCOMPARE(v.toMap().value("state").toString(), QStringLiteral("live"));
    // 没有已花：平均是 0，不除以零
    const QVariantMap m0 = ledgerInfo(r3, "CNY", t, "2026-05-01");
    QCOMPARE(m0.value("total").toDouble(), 0.0);
    QCOMPARE(m0.value("perTrip").toDouble(), 0.0);
    QCOMPARE(m0.value("years").toList().size(), 0);
    // 没有汇率表：外币都算缺，和主货币一样的照算
    const QVariantMap none = ledgerInfo(root, "CNY", Rates(), "2026-06-01");
    QVERIFY(none.value("missing").toStringList().contains("AUD"));
    QVERIFY(qAbs(none.value("future").toDouble() - 900) < 1e-6);
    QVERIFY(none.value("trips").toList()[1].toMap().value("budgetMain").toDouble() < 0);
}

void TestMap::moneyEdit()
{
    using namespace tp::map::plans;
    tp::json::Value plan = parseJson(R"({"currency":"AUD","bookings":[{"id":"b1","kind":"flight","title":"旧","seat":"12A","from":"A","to":"B","cost":100}],
      "expenses":[{"id":"e1","title":"旧","amount":100,"bookingId":"b1","cat":"transport"},{"id":"e2","title":"分","amount":9,"paidBy":"p1","split":[{"who":"p1"}]}]})");
    const tp::json::Value orig = plan;
    // 错误：什么原因
    QCOMPARE(putBooking(plan, "b1", vm({ { "cost", "abc" } })), QStringLiteral("badCost"));
    QCOMPARE(putBooking(plan, "", vm({ { "startTime", "09:00" } })), QStringLiteral("badStart"));
    QCOMPARE(putBooking(plan, "", vm({ { "startDate", "2026-12-02" }, { "startTime", "25:00" } })), QStringLiteral("badStart"));
    QCOMPARE(putBooking(plan, "", vm({ { "endDate", "2026-13-02" } })), QStringLiteral("badEnd"));
    QCOMPARE(putBooking(plan, "", vm({ { "startDate", "2026-12-02" }, { "endDate", "2026-12-01" } })), QStringLiteral("endBeforeStart"));
    QCOMPARE(putBooking(plan, "", vm({ { "startDate", "2026-12-02" }, { "startTime", "15:00" }, { "endDate", "2026-12-02" }, { "endTime", "10:00" } })), QStringLiteral("endBeforeStart"));
    QCOMPARE(putBooking(plan, "没有", vm({ { "title", "x" } })), QStringLiteral("gone"));
    QVERIFY(plan == orig);
    { tp::json::Value c = plan;   // 当天退房没写时间：只比日期，不算早于入住
      QCOMPARE(putBooking(c, "", vm({ { "startDate", "2026-12-02" }, { "startTime", "15:00" }, { "endDate", "2026-12-02" } })), QString()); }
    // 改：费用同步消费，别的键保留，变成住宿时去掉出发地
    QCOMPARE(putBooking(plan, "b1", vm({ { "kind", "hotel" }, { "title", "新" }, { "startDate", "2026-12-02" }, { "startTime", "930" }, { "cost", "1,250.5" } })), QString());
    const tp::json::Value &b = plan[QStringLiteral("bookings")].asArray()[0];
    QCOMPARE(b[QStringLiteral("seat")].toString(), QStringLiteral("12A"));
    QCOMPARE(b[QStringLiteral("start")].toString(), QStringLiteral("2026-12-02T09:30"));
    QVERIFY(!b.asObject().contains(QStringLiteral("from")));
    QCOMPARE(b[QStringLiteral("cost")].toNumber(), 1250.5);
    const tp::json::Value &x = plan[QStringLiteral("expenses")].asArray()[0];
    QCOMPARE(x[QStringLiteral("amount")].toNumber(), 1250.5);
    QCOMPARE(x[QStringLiteral("title")].toString(), QStringLiteral("新"));
    // 清空费用：消费变 0（Electron 一样）
    QCOMPARE(putBooking(plan, "b1", vm({ { "kind", "hotel" }, { "title", "新" } })), QString());
    QVERIFY(!plan[QStringLiteral("bookings")].asArray()[0].asObject().contains(QStringLiteral("cost")));
    QCOMPARE(plan[QStringLiteral("expenses")].asArray()[0][QStringLiteral("amount")].toNumber(), 0.0);
    // 新加 + 同时记到消费；没有费用时不记
    QCOMPARE(putBooking(plan, "", vm({ { "kind", "train" }, { "cost", "20" }, { "startDate", "2026-12-03" }, { "toExp", true } })), QString());
    QCOMPARE(plan[QStringLiteral("expenses")].asArray().size(), size_t(3));
    const tp::json::Value &n = plan[QStringLiteral("expenses")].asArray()[2];
    QCOMPARE(n[QStringLiteral("date")].toString(), QStringLiteral("2026-12-03"));
    QCOMPARE(n[QStringLiteral("cat")].toString(), QStringLiteral("transport"));
    QCOMPARE(n[QStringLiteral("bookingId")].toString(), plan[QStringLiteral("bookings")].asArray()[1][QStringLiteral("id")].toString());
    QVERIFY(n[QStringLiteral("id")].toString().startsWith(QStringLiteral("ex_")));
    QCOMPARE(putBooking(plan, "", vm({ { "kind", "train" }, { "toExp", true } })), QString());
    QCOMPARE(plan[QStringLiteral("expenses")].asArray().size(), size_t(3));
    // 删除连带
    QCOMPARE(removeBooking(plan, "没有"), -1);
    QCOMPARE(removeBooking(plan, "b1"), 1);
    QCOMPARE(plan[QStringLiteral("bookings")].asArray().size(), size_t(2));
    // 消费：错误、保留分账的键、和计划币种一样不写币种
    QCOMPARE(putExpense(plan, "e2", vm({ { "amount", "" } })), QStringLiteral("badAmount"));
    QCOMPARE(putExpense(plan, "e2", vm({ { "amount", "1" }, { "date", "昨天" } })), QStringLiteral("badDate"));
    QCOMPARE(putExpense(plan, "没有", vm({ { "amount", "1" } })), QStringLiteral("gone"));
    QCOMPARE(putExpense(plan, "e2", vm({ { "amount", "-3" }, { "currency", "AUD" }, { "cat", "food" }, { "date", "2026-12-03" } })), QString());
    const tp::json::Value *e2 = nullptr;
    for (const tp::json::Value &e : plan[QStringLiteral("expenses")].asArray()) if (e[QStringLiteral("id")].toString() == "e2") e2 = &e;
    QVERIFY(e2);
    QCOMPARE((*e2)[QStringLiteral("paidBy")].toString(), QStringLiteral("p1"));
    QVERIFY((*e2)[QStringLiteral("split")].isArray());
    QCOMPARE((*e2)[QStringLiteral("amount")].toNumber(), -3.0);
    QVERIFY(!e2->asObject().contains(QStringLiteral("currency")));
    QVERIFY(removeExpense(plan, "e2"));
    QVERIFY(!removeExpense(plan, "e2"));
    // 没写币种的计划：记账时写上（Electron editExpense）
    tp::json::Value p2 = parseJson("{}");
    QCOMPARE(putExpense(p2, "", vm({ { "amount", "5" }, { "currency", "USD" } })), QString());
    QCOMPARE(p2[QStringLiteral("currency")].toString(), QStringLiteral("AUD"));
    QCOMPARE(p2[QStringLiteral("expenses")].asArray()[0][QStringLiteral("currency")].toString(), QStringLiteral("USD"));
    // 预算
    QCOMPARE(setBudget(p2, "-1", "AUD"), QStringLiteral("badBudget"));
    QCOMPARE(setBudget(p2, "abc", "AUD"), QStringLiteral("badBudget"));
    QCOMPARE(setBudget(p2, "2,000", "nzd"), QString());
    QCOMPARE(p2[QStringLiteral("budget")].toNumber(), 2000.0);
    QCOMPARE(p2[QStringLiteral("currency")].toString(), QStringLiteral("NZD"));
    QCOMPARE(setBudget(p2, "", "NZD"), QString());
    QVERIFY(!p2.asObject().contains(QStringLiteral("budget")));
}

void TestMap::moneyBookingsView()
{
    using namespace tp::map::plans;
    // 按天分组：从早到晚，没填时间的放最后；linked 标记
    const tp::json::Value plan = parseJson(R"({"status":"planning","startDate":"2026-12-01","endDate":"2026-12-08",
      "stops":[{"name":"A","lat":1,"lng":1,"arriveDate":"2026-12-01","departDate":"2026-12-04"},
               {"name":"B","lat":2,"lng":2,"arriveDate":"2026-12-04","departDate":"2026-12-06"},
               {"name":"C","lat":3,"lng":3,"arriveDate":"2026-12-07","departDate":"2026-12-08"}],
      "bookings":[{"id":"u","kind":"other"},
                  {"id":"f","kind":"flight","start":"2026-12-04T08:00","end":"2026-12-04T10:00"},
                  {"id":"h2","kind":"hotel","start":"2026-12-04T15:00","end":"2026-12-05T10:00"},
                  {"id":"h1","kind":"hotel","start":"2026-12-01T15:00","end":"2026-12-03T10:00"},
                  {"id":"n","kind":"train","start":"2026-12-05T21:00","end":"2026-12-06T07:00"},
                  {"id":"x","kind":"hotel","start":"2026-11-01"}],
      "expenses":[{"amount":1,"bookingId":"f"}]})");
    const QVariantList d = bookingDays(plan);
    QCOMPARE(d.size(), 5);
    QCOMPARE(d[0].toMap().value("day").toString(), QStringLiteral("2026-11-01"));
    QCOMPARE(d[1].toMap().value("day").toString(), QStringLiteral("2026-12-01"));
    QCOMPARE(d[2].toMap().value("rows").toList().size(), 2);
    QVERIFY(d[2].toMap().value("rows").toList()[0].toMap().value("linked").toBool());
    QCOMPARE(d[4].toMap().value("day").toString(), QString());

    // J2：换站那天，住宿算下一站、交通算这一站；不在任何一站的不挂
    const QVector<QVariantList> s = stopBookings(plan);
    QCOMPARE(s.size(), 3);
    const auto ids = [](const QVariantList &l) { QStringList o; for (const QVariant &v : l) o << v.toMap().value("id").toString(); return o; };
    QCOMPARE(ids(s[0]), QStringList({ "f", "h1" }));
    QCOMPARE(ids(s[1]), QStringList({ "h2", "n" }));
    QVERIFY(s[2].isEmpty());
    // 单一目的地只写了计划的日期
    const tp::json::Value one = parseJson(R"({"startDate":"2026-12-01","endDate":"2026-12-03","stops":[{"name":"A","lat":1,"lng":1}],"bookings":[{"id":"h","kind":"hotel","start":"2026-12-02"}]})");
    QCOMPARE(ids(stopBookings(one)[0]), QStringList{ "h" });

    // J1：要住 12-01…03、04、05、07；住了 01、02、04；12-05 坐夜车；缺 03、07。B→C 之间的 12-06 不在任何站里
    int undated = -1;
    QCOMPARE(missingNights(plan, "2026-11-01", &undated), QStringList({ "2026-12-03", "2026-12-07" }));
    QCOMPARE(undated, 0);
    QCOMPARE(missingNights(plan, "2026-12-04"), QStringList{ "2026-12-07" });   // 过去的晚上不提
    const tp::json::Value done = parseJson(R"({"status":"completed","startDate":"2026-12-01","endDate":"2026-12-03","stops":[]})");
    QVERIFY(missingNights(done, "2026-11-01").isEmpty());
    // 站点没写日期：按计划的日期；没填日期的住宿单独数
    const tp::json::Value noStops = parseJson(R"({"status":"planning","startDate":"2026-12-01","endDate":"2026-12-03","stops":[],"bookings":[{"id":"h","kind":"hotel"},{"id":"h9","kind":"hotel","start":"2026-12-01"}]})");
    QCOMPARE(missingNights(noStops, "2026-11-01", &undated), QStringList{ "2026-12-02" });   // 只写入住：住一晚
    QCOMPARE(undated, 1);
    QVERIFY(missingNights(parseJson(R"({"status":"planning"})"), "2026-11-01").isEmpty());
}


void TestMap::altitudeRules()
{
    using namespace tp::map::plans;
    const auto warns = [](const tp::json::Value &p) { return altitudeInfo(p).value("warnings").toList(); };
    const auto tags = [](const tp::json::Value &p) { return altitudeInfo(p).value("tags").toStringList(); };
    const auto stop = [](double e, const QString &a, const QString &b) {
        return QStringLiteral(R"({"name":"S","lat":1,"lng":1,"elev":%1,"arriveDate":"%2","departDate":"%3"})").arg(e).arg(a, b);
    };
    const auto plan = [](const QStringList &stops) { return parseJson(QStringLiteral(R"({"stops":[%1]})").arg(stops.join(',')).toUtf8()); };

    // 都不到 2500：什么都没有
    QVERIFY(warns(plan({ stop(1000, "2026-12-01", "2026-12-02"), stop(2400, "2026-12-02", "2026-12-03") })).isEmpty());
    // 单站
    const auto one = plan({ stop(3500, "2026-12-01", "2026-12-03") });
    QVERIFY(warns(one).isEmpty());
    QCOMPARE(tags(one), QStringList{ "high" });
    // 跨 3000 边界：3000 算到了；2999 不算
    auto w = warns(plan({ stop(2000, "2026-12-01", "2026-12-02"), stop(3000, "2026-12-02", "2026-12-03") }));
    QCOMPARE(w.size(), 1);
    QCOMPARE(w[0].toMap().value("gain").toInt(), 1000);
    QVERIFY(warns(plan({ stop(2000, "2026-12-01", "2026-12-02"), stop(2999, "2026-12-02", "2026-12-03") })).isEmpty());
    // 正好 500 不算超；多天按天数放宽
    QVERIFY(warns(plan({ stop(3000, "2026-12-01", "2026-12-02"), stop(3500, "2026-12-02", "2026-12-03") })).isEmpty());
    QVERIFY(warns(plan({ stop(3000, "2026-12-01", "2026-12-03"), stop(3900, "2026-12-03", "2026-12-04") })).isEmpty());
    w = warns(plan({ stop(3000, "2026-12-01", "2026-12-03"), stop(4100, "2026-12-03", "2026-12-04") }));
    QCOMPARE(w.size(), 1);
    QCOMPARE(w[0].toMap().value("days").toInt(), 2);
    // 没海拔的站跳过，比较前后有海拔的两站；站号是原来的
    const auto gap = plan({ stop(2000, "2026-12-01", "2026-12-02"), R"({"name":"N","lat":1,"lng":1,"arriveDate":"2026-12-02","departDate":"2026-12-03"})", stop(3800, "2026-12-03", "2026-12-04") });
    w = warns(gap);
    QCOMPARE(w.size(), 1);
    QCOMPARE(w[0].toMap().value("from").toInt(), 1);
    QCOMPARE(w[0].toMap().value("to").toInt(), 3);
    QCOMPARE(tags(gap), QStringList({ "", "", "rise" }));
    // 当天走的站（0 晚）不算睡觉的海拔
    w = warns(plan({ stop(3000, "2026-12-01", "2026-12-02"), stop(4900, "2026-12-02", "2026-12-02"), stop(3400, "2026-12-02", "2026-12-03") }));
    QVERIFY(w.isEmpty());
    // 连续 4 晚往上（每晚 +300，不超过每天 500）
    QStringList up;
    for (int i = 0; i < 5; ++i) up << stop(3100 + i * 300, QStringLiteral("2026-12-%1").arg(i + 1, 2, 10, QChar('0')), QStringLiteral("2026-12-%1").arg(i + 2, 2, 10, QChar('0')));
    w = warns(plan(up));
    QCOMPARE(w.size(), 1);
    QCOMPARE(w[0].toMap().value("kind").toString(), QString("run"));
    QCOMPARE(w[0].toMap().value("nights").toInt(), 4);
    // 中间休一晚（这站住两晚）就不算连续
    up[2] = stop(3700, "2026-12-03", "2026-12-05");
    up[3] = stop(4000, "2026-12-05", "2026-12-06");
    up[4] = stop(4300, "2026-12-06", "2026-12-07");
    QVERIFY(warns(plan(up)).isEmpty());
    // 没写日期：按每站一晚估算，标 approx
    const auto nd = parseJson(R"({"stops":[{"lat":1,"lng":1,"elev":2000},{"lat":2,"lng":2,"elev":3200}]})");
    QVERIFY(altitudeInfo(nd).value("approx").toBool());
    QCOMPARE(warns(nd).size(), 1);
    // null、空字符串算没有海拔
    QVERIFY(altitudeInfo(parseJson(R"({"stops":[{"lat":1,"lng":1,"elev":null},{"lat":2,"lng":2,"elev":""}]})")).value("warnings").toList().isEmpty());
}

void TestMap::weatherParsing()
{
    using namespace tp::map::plans;
    // 窗口：16 天内、没过完、没完成
    const auto w = [](const char *j) { return parseJson(j); };
    QVERIFY(inForecastWindow(w(R"({"startDate":"2026-11-21","endDate":"2026-11-25"})"), "2026-11-06"));
    QVERIFY(!inForecastWindow(w(R"({"startDate":"2026-11-22","endDate":"2026-11-25"})"), "2026-11-06"));
    QVERIFY(!inForecastWindow(w(R"({"startDate":"2026-10-01","endDate":"2026-11-05"})"), "2026-11-06"));
    QVERIFY(inForecastWindow(w(R"({"startDate":"2026-10-01","endDate":"2026-11-06"})"), "2026-11-06"));
    QVERIFY(!inForecastWindow(w(R"({"status":"completed","startDate":"2026-11-07"})"), "2026-11-06"));
    QVERIFY(!inForecastWindow(w(R"({"status":"planning"})"), "2026-11-06"));
    // 这一站用哪天
    QCOMPARE(stopWxDate(w(R"({"arriveDate":"2026-11-08","departDate":"2026-11-10"})"), "2026-11-06"), QString("2026-11-08"));
    QCOMPARE(stopWxDate(w(R"({"arriveDate":"2026-11-05","departDate":"2026-11-10"})"), "2026-11-06"), QString("2026-11-06"));
    QCOMPARE(stopWxDate(w(R"({})"), "2026-11-06"), QString());
    // WMO
    QCOMPARE(wmoKind(0), QString("clear"));
    QCOMPARE(wmoKind(3), QString("cloudy"));
    QCOMPARE(wmoKind(63), QString("rain"));
    QCOMPARE(wmoKind(86), QString("snowShowers"));
    QCOMPARE(wmoKind(99), QString("storm"));
    QCOMPARE(wmoKind(44), QString("unknown"));
    // 一个地点时回复不是数组；出错的留空
    const QByteArray single = R"({"daily":{"time":["2026-11-06","2026-11-07"],"weather_code":[61,0],"temperature_2m_max":[18.4,null],"temperature_2m_min":[11.6,3],"precipitation_probability_max":[70,null]}})";
    QVector<QJsonObject> d = parseDaily(single, 1);
    QCOMPARE(d.size(), 1);
    QVariantMap day = wxDay(d[0], "2026-11-06");
    QCOMPARE(day.value("kind").toString(), QString("rain"));
    QCOMPARE(day.value("pop").toInt(), 70);
    QVERIFY(wxDay(d[0], "2026-11-07").isEmpty());    // 温度是 null
    QVERIFY(wxDay(d[0], "2026-11-08").isEmpty());
    QVERIFY(wxDay(d[0], "").isEmpty());
    d = parseDaily(R"([{"error":true},{"daily":{"time":["2026-11-06"],"weather_code":[0],"temperature_2m_max":[1],"temperature_2m_min":[0]}}])", 2);
    QVERIFY(d[0].isEmpty());
    QVERIFY(!wxDay(d[1], "2026-11-06").contains("pop"));
    QVERIFY(parseDaily("not json", 2)[0].isEmpty());
    // URL 有所有坐标
    const QUrl u = forecastUrl({ { "a", 1.23456, 2.5 }, { "b", -3, 4 } });
    QCOMPARE(u.host(), QString("api.open-meteo.com"));
    QVERIFY(u.toString().contains("latitude=1.235,-3.000"));
    QVERIFY(u.toString().contains("forecast_days=16"));
    // 气候的年份：上个月刚过完 6 天以内不算；1 月算去年的 12 月
    QCOMPARE(climYears(10, "2026-11-06").last(), 2025);
    QCOMPARE(climYears(10, "2026-11-07").last(), 2026);
    QCOMPARE(climYears(9, "2026-11-01").last(), 2026);
    QCOMPARE(climYears(12, "2027-01-20").last(), 2026);
    QCOMPARE(climYears(11, "2026-11-20").last(), 2025);
    QCOMPARE(climYears(12, "2026-11-20").size(), 10);
    // 合并：约 30 km 内同月算一个点；不同月分开；最多 12 个点
    const auto cp = climPlan(w(R"({"stops":[{"lat":10,"lng":20,"arriveDate":"2026-03-01"},{"lat":10.1,"lng":20.1,"arriveDate":"2026-03-05"},{"lat":10,"lng":20,"arriveDate":"2026-04-01"},{"lat":40,"lng":20,"arriveDate":"2026-03-07"}]})"), "2026-11-06");
    QCOMPARE(cp.points.size(), 3);
    QCOMPARE(cp.stopKeys[0], cp.stopKeys[1]);
    QVERIFY(cp.stopKeys[0] != cp.stopKeys[2]);
    QVERIFY(cp.stopKeys[0].endsWith("@3"));
    QVERIFY(cp.stopKeys[2].endsWith("@4"));
    QString many = "{\"stops\":[";
    for (int i = 0; i < 14; ++i) many += QStringLiteral("%1{\"lat\":%2,\"lng\":0,\"arriveDate\":\"2026-03-01\"}").arg(i ? "," : "").arg(i * 3);
    const auto cm = climPlan(parseJson((many + "]}").toUtf8()), "2026-11-06");
    QCOMPARE(cm.points.size(), 12);
    QVERIFY(cm.stopKeys[13].isEmpty());
    // 累加：两年，每个点平均；null 跳过；≥ 1 mm 算下雨
    QVector<ClimAcc> acc(2);
    climAdd(acc, R"([{"daily":{"temperature_2m_max":[30,null],"temperature_2m_min":[20],"precipitation_sum":[0.9,1,null]}},{"error":true}])");
    climAdd(acc, R"([{"daily":{"temperature_2m_max":[20],"temperature_2m_min":[10],"precipitation_sum":[4]}},{"error":true}])");
    const QJsonObject r0 = climResult(acc[0], { 2016, 2025 });
    QCOMPARE(r0.value("hi").toDouble(), 25.0);
    QCOMPARE(r0.value("lo").toDouble(), 15.0);
    QCOMPARE(r0.value("rainDays").toDouble(), 1.0);
    QCOMPARE(r0.value("mm").toDouble(), 2.95);
    QCOMPARE(r0.value("years").toString(), QStringLiteral("2016–2025"));
    QVERIFY(climResult(acc[1], { 2016, 2025 }).isEmpty());
}

void TestMap::kitChecks()
{
    using namespace tp::map::plans;
    tp::json::Value root = parseJson(R"({"plans":[{"id":"p","status":"planning","startDate":"2026-12-01","packing":[
        {"id":"a","text":"护照","done":true,"x":1},{"id":"b","text":"雨具","done":false}],
        "todos":[{"id":"t1","text":"甲","done":false,"due":"2026-11-08"},{"id":"t2","text":"乙","done":false,"due":"2026-11-01"},
                 {"id":"t3","text":"丙","done":true,"due":"2026-11-01"},{"id":"t4","text":"丁","done":false,"due":"2026-12-01"}]},
        {"id":"w","status":"wish","todos":[{"id":"t","text":"x","done":false,"due":"2020-01-01"}]}]})");
    tp::json::Value *p = mutPlan(root, "p");
    QVariantMap k = kitInfo(root, *p, "2026-11-06");
    // M1：显示顺序没勾的在前，数据顺序不变
    QCOMPARE(k["packing"].toList()[0].toMap()["id"].toString(), QStringLiteral("b"));
    QCOMPARE((*p)["packing"].asArray()[0]["id"].toString(), QStringLiteral("a"));
    QCOMPARE(k["packDone"].toInt(), 1);
    QCOMPARE(k["doneAll"].toInt(), 2);
    QCOMPARE(k["totalAll"].toInt(), 6);
    // M2：3 天内、已过期；勾了的不算
    QCOMPARE(k["dueSoon"].toInt(), 1);
    QCOMPARE(k["dueOver"].toInt(), 1);
    QCOMPARE(k["todos"].toList()[0].toMap()["dueState"].toString(), QStringLiteral("soon"));
    QCOMPARE(k["todos"].toList()[1].toMap()["dueState"].toString(), QStringLiteral("over"));
    QCOMPARE(k["todos"].toList()[2].toMap()["dueState"].toString(), QString());
    // 只在计划中的计划上提醒
    QCOMPARE(kitInfo(root, *planById(root, "w"), "2026-11-06")["dueOver"].toInt(), 0);
    // M4
    QCOMPARE(splitLines("- 徒步杖\r\n\n  * 三季徒步鞋 \n1. 头灯\n[ ] 地图\n- [x] 水壶\n1.5L 水\n   "),
             QStringList({ "徒步杖", "三季徒步鞋", "头灯", "地图", "水壶", "1.5L 水" }));
    QCOMPARE(addChecks(*p, "packing", "x\ny"), 2);
    QCOMPARE(addChecks(*p, "packing", "  \n "), 0);
    QCOMPARE(addChecks(*p, "other", "z"), 0);
    QCOMPARE((*p)["packing"].asArray().size(), size_t(4));
    QVERIFY((*p)["packing"].asArray()[3]["id"].toString().startsWith("pk_"));
    QCOMPARE((*p)["packing"].asArray()[3]["done"], tp::json::Value(false));
    // 打勾、日期、删除：只动那个键
    QVERIFY(setCheckDone(*p, "packing", "a", false));
    QCOMPARE((*p)["packing"].asArray()[0]["x"].toNumber(), 1.0);
    QVERIFY(!setCheckDone(*p, "packing", "nope", true));
    QVERIFY(setCheckDue(*p, "t4", "2026-12-02"));
    QVERIFY(!setCheckDue(*p, "t4", "明天"));
    QVERIFY(setCheckDue(*p, "t4", ""));
    QVERIFY(!(*p)["todos"].asArray()[3].asObject().contains("due"));
    QVERIFY(removeCheck(*p, "todos", "t4"));
    QVERIFY(!removeCheck(*p, "todos", "t4"));
    // 模板：默认 11 项、去重；存了以后用存的
    root = parseJson(R"({"plans":[{"id":"p","packing":[{"id":"a","text":"雨具","done":true}]},{"id":"q"}]})");
    QCOMPARE(defaultPacking().size(), 11);
    QCOMPARE(addFromTemplate(root, "p"), 10);
    QCOMPARE(addFromTemplate(root, "p"), 0);
    QCOMPARE(saveTemplate(root, "q"), 0);
    QVERIFY(!root.asObject().contains("packingTemplate"));
    QCOMPARE(saveTemplate(root, "p"), 11);
    QCOMPARE(addFromTemplate(root, "q"), 11);
    QCOMPARE((*planById(root, "q"))["packing"].asArray()[0]["text"].toString(), QStringLiteral("雨具"));
}

void TestMap::kitDocuments()
{
    using namespace tp::map::plans;
    // Electron 存的样子（travelkit.js editDoc），带一个不认识的键、一个坏条目、一个没有 id 的
    const QString src = QString::fromUtf8(R"({"keep":1,"plans":[{"id":"p1","name":"日本","status":"planning"},{"id":"p2","name":"旧的","status":"completed"}],
      "documents":[{"kind":"visa","owner":"妈妈","label":"单次","expires":"2030-01-01","planId":"p1","notes":"n","id":"doc_a","future":{"x":1}},
                   7,{"kind":"passport","expires":"2020-01-01","id":"doc_b"},{"kind":"other","label":"没有 id"},{"kind":"weird","planId":"p2","id":"doc_d"}]})");
    tp::json::Value root = parseJson(src.toUtf8());
    const tp::json::Value orig = root;
    const QVariantList l = documentsInfo(root, "2026-11-08");
    QCOMPARE(l.size(), 4);
    QCOMPARE(l[0].toMap().value("expires").toString(), QString());   // 没填的在前面
    QCOMPARE(l[2].toMap().value("id").toString(), QStringLiteral("doc_b"));
    QVERIFY(l[2].toMap().value("expired").toBool());
    QCOMPARE(l[3].toMap().value("index").toInt(), 0);
    QVERIFY(!l[3].toMap().value("expired").toBool());
    QCOMPARE(l[3].toMap().value("planName").toString(), QStringLiteral("日本"));
    // 不改内容地存一遍 = 往返不变（含不认识的键、键的顺序）
    const QVariantMap same{ { "kind", "visa" }, { "owner", "妈妈" }, { "label", "单次" }, { "expires", "2030-01-01" }, { "planId", "p1" }, { "notes", "n" } };
    QCOMPARE(putDocument(root, 0, "doc_a", same), QString());
    QCOMPARE(tp::json::stringify(root, 0), tp::json::stringify(orig, 0));
    // 改：清空的键去掉，不认识的留着；没有 id 的也能改，不会补 id
    QCOMPARE(putDocument(root, 0, "doc_a", { { "kind", "visa" }, { "expires", "2031-02-03" } }), QString());
    const tp::json::Value &a = root["documents"].asArray()[0];
    QCOMPARE(a["expires"].toString(), QStringLiteral("2031-02-03"));
    QVERIFY(!a.asObject().contains("owner"));
    QCOMPARE(a["future"]["x"].toNumber(), 1.0);
    QCOMPARE(putDocument(root, 3, "", { { "kind", "other" }, { "label", "改了" } }), QString());
    QCOMPARE(root["documents"].asArray()[3]["label"].toString(), QStringLiteral("改了"));
    QVERIFY(!root["documents"].asArray()[3].asObject().contains("id"));
    // 列表变过：对不上就不动
    const QString beforeStale = tp::json::stringify(root, 0);
    QCOMPARE(putDocument(root, 0, "别的", same), QStringLiteral("gone"));
    QCOMPARE(removeDocument(root, 1, ""), QStringLiteral("gone"));   // 坏条目
    QCOMPARE(removeDocument(root, 99, "x"), QStringLiteral("gone"));
    QCOMPARE(tp::json::stringify(root, 0), beforeStale);
    // 新加：id 的样子同 Electron，键的顺序同 editDoc
    QCOMPARE(putDocument(root, -1, "", { { "kind", "passport" }, { "owner", " 我 " }, { "expires", "2028-01-01" } }), QString());
    const tp::json::Value &n = root["documents"].asArray().back();
    QVERIFY(n["id"].toString().startsWith("doc_"));
    QCOMPARE(n["owner"].toString(), QStringLiteral("我"));
    // 关联到已完成或已删的计划：选项里留着，改证件时不丢
    QCOMPARE(documentPlanOptions(root, "").size(), 1);
    const QVariantList keep = documentPlanOptions(root, "p2");
    QCOMPARE(keep.size(), 2);
    QCOMPARE(keep[1].toMap().value("text").toString(), QStringLiteral("旧的"));
    QCOMPARE(documentPlanOptions(root, "gone").last().toMap().value("text").toString(), QString());
    // 删；删空留 []；没有 documents 时新加能建出来；顶层别的键不动
    tp::json::Value one = parseJson(R"({"keep":1,"documents":[{"id":"a"}]})");
    QCOMPARE(removeDocument(one, 0, "a"), QString());
    QCOMPARE(tp::json::stringify(one, 0), QStringLiteral("{\"keep\":1,\"documents\":[]}"));
    tp::json::Value none = parseJson(R"({"keep":1})");
    QVERIFY(documentsInfo(none, "2026-11-08").isEmpty());
    QCOMPARE(putDocument(none, -1, "", { { "kind", "visa" } }), QString());
    QCOMPARE(none["documents"].asArray().size(), size_t(1));
    QCOMPARE(none["keep"].toNumber(), 1.0);
}

void TestMap::kitEmergency()
{
    using namespace tp::map::plans;
    tp::json::Value root = parseJson(R"({"keep":1,"emergency":{"insurer":"A","future":"留着","contacts":[{"name":"甲","phone":"1","extra":"留"},
        {"name":"乙"},{"name":"丙"},{"name":"丁"}]},"plans":[{"id":"p","localEmergency":[{"label":"报警","phone":"110"},{"label":"旧的一段文字","phone":""}]}]})");
    tp::json::Value *p = mutPlan(root, "p");
    setLocalEmergency(*p, { { "急救", " 120 " } });
    const tp::json::Array &l = (*p)["localEmergency"].asArray();
    QCOMPARE(l.size(), size_t(2));     // 报警清空了；导入来的一段文字留着
    QCOMPARE(l[0]["label"].toString(), QStringLiteral("急救"));
    QCOMPARE(l[0]["phone"].toString(), QStringLiteral("120"));
    QCOMPARE(l[1]["label"].toString(), QStringLiteral("旧的一段文字"));
    tp::json::Value q = parseJson(R"({"localEmergency":[{"label":"报警","phone":"110"}]})");
    setLocalEmergency(q, {});
    QVERIFY(!q.asObject().contains("localEmergency"));

    setEmergency(root, { { "policyNo", "P1" }, { "n0", "甲" }, { "p0", "9" }, { "n2", "新" } });
    const tp::json::Value &e = root["emergency"];
    QVERIFY(!e.asObject().contains("insurer"));
    QCOMPARE(e["future"].toString(), QStringLiteral("留着"));
    QCOMPARE(e["policyNo"].toString(), QStringLiteral("P1"));
    const tp::json::Array &c = e["contacts"].asArray();
    QCOMPARE(c.size(), size_t(3));     // 甲、新、丁（第 2 个空了；第 4 个表单里没有，留着）
    QCOMPARE(c[0]["extra"].toString(), QStringLiteral("留"));
    QCOMPARE(c[0]["phone"].toString(), QStringLiteral("9"));
    QCOMPARE(c[1]["name"].toString(), QStringLiteral("新"));
    QCOMPARE(c[2]["name"].toString(), QStringLiteral("丁"));
    tp::json::Value blank = parseJson(R"({"emergency":{"insurer":"A"}})");
    setEmergency(blank, {});
    QVERIFY(!blank.asObject().contains("emergency"));
}

namespace {
// 带所有日期字段的计划：站点两个（一个日程带日期、一个不带）、一条交通、一笔消费、一条待办，和不认识的键
const char *kCalRoot = R"({"plans":[
  {"id":"a","name":"甲","status":"planning","startDate":"2026-12-28","endDate":"2027-01-03","days":99,"future":{"x":1},
   "stops":[{"id":"s1","name":"一","lat":1,"lng":2,"arriveDate":"2026-12-28","departDate":"2026-12-31","extra":"留着",
             "schedule":[{"date":"2026-12-29","time":"09:00","text":"早"},{"time":"10:00","text":"没日期"},{"date":"随便写的","text":"不是日期"}]},
            {"id":"s2","name":"二","lat":3,"lng":4,"arriveDate":"2026-12-31","departDate":"2027-01-03"},
            {"id":"s3","name":"没日期","lat":5,"lng":6}],
   "bookings":[{"id":"b1","start":"2026-12-28T08:00","end":"2026-12-28T12:00"},{"id":"b2","start":""}],
   "expenses":[{"id":"e1","date":"2026-12-29","amount":3}],"todos":[{"id":"t1","text":"办","due":"2026-12-20"}]},
  {"id":"b","name":"乙","status":"wish","stops":[{"id":"s","lat":1,"lng":2,"arriveDate":"2025-03-01","departDate":"2025-03-04"}]},
  {"id":"c","name":"丙","status":"wish","days":5},
  {"id":"d","name":"丁","status":"completed"},
  {"id":"e","name":"戊","status":"planning","startDate":"2027-01-02","endDate":"2027-01-02"}
]})";
QString stopDate(const tp::json::Value &plan, int k, const char *key) { return plan["stops"].asArray()[size_t(k)][QString::fromLatin1(key)].toString(); }
}

void TestMap::calShiftAndPlace()
{
    using namespace tp::map::plans;
    tp::json::Value root = parseJson(kCalRoot);
    tp::json::Value *a = mutPlan(root, "a"), *b = mutPlan(root, "b"), *c = mutPlan(root, "c");
    QCOMPARE(planDays(*a), 7);       // 有起止
    QCOMPARE(planDays(*b), 4);       // 站点跨度
    QCOMPARE(planDays(*c), 5);       // days
    QCOMPARE(planDays(*mutPlan(root, "d")), 1);
    QCOMPARE(datedBookings(*a), 1);
    // 跨年、跨月：挪到 2027-02-26（经过闰年以外的 2 月底）
    QVERIFY(placePlan(*a, "2027-02-26", planDays(*a)));
    QCOMPARE((*a)["startDate"].toString(), QStringLiteral("2027-02-26"));
    QCOMPARE((*a)["endDate"].toString(), QStringLiteral("2027-03-04"));
    QCOMPARE(stopDate(*a, 0, "arriveDate"), QStringLiteral("2027-02-26"));
    QCOMPARE(stopDate(*a, 0, "departDate"), QStringLiteral("2027-03-01"));
    QCOMPARE(stopDate(*a, 1, "departDate"), QStringLiteral("2027-03-04"));
    const tp::json::Array &sc = (*a)["stops"].asArray()[0]["schedule"].asArray();
    QCOMPARE(sc[0]["date"].toString(), QStringLiteral("2027-02-27"));
    QVERIFY(sc[1]["date"].isNull() || sc[1]["date"].toString().isEmpty());
    QCOMPARE(sc[2]["date"].toString(), QStringLiteral("随便写的"));
    QVERIFY(!(*a)["stops"].asArray()[2].asObject().contains("arriveDate"));   // 没写的不凭空加
    // 交通住宿、消费、待办不动；不认识的键还在；days 不动
    QCOMPARE((*a)["bookings"].asArray()[0]["start"].toString(), QStringLiteral("2026-12-28T08:00"));
    QCOMPARE((*a)["expenses"].asArray()[0]["date"].toString(), QStringLiteral("2026-12-29"));
    QCOMPARE((*a)["todos"].asArray()[0]["due"].toString(), QStringLiteral("2026-12-20"));
    QCOMPARE((*a)["future"]["x"].toNumber(), 1.0);
    QCOMPARE((*a)["days"].toNumber(), 99.0);
    QCOMPARE((*a)["stops"].asArray()[0]["extra"].toString(), QStringLiteral("留着"));
    // 没定日期、站点有日期：站点平移到新的开始
    QVERIFY(placePlan(*b, "2026-06-10", planDays(*b)));
    QCOMPARE((*b)["endDate"].toString(), QStringLiteral("2026-06-13"));
    QCOMPARE(stopDate(*b, 0, "arriveDate"), QStringLiteral("2026-06-10"));
    QCOMPARE(stopDate(*b, 0, "departDate"), QStringLiteral("2026-06-13"));
    // 没定日期、站点也没有：按 days
    QVERIFY(placePlan(*c, "2028-02-27", planDays(*c)));
    QCOMPARE((*c)["endDate"].toString(), QStringLiteral("2028-03-02"));   // 2028 闰年
    QVERIFY(!placePlan(*c, "不是日期", 3));
}

void TestMap::calRoundTrip()
{
    using namespace tp::map::plans;
    const tp::json::Value orig = parseJson(kCalRoot);
    // 拖过去再拖回来：完全一样（站点、日程、键的顺序都不变）。覆盖跨月、跨年、倒回去
    for (const char *day : { "2027-01-01", "2027-01-31", "2028-03-01", "2020-02-29", "2026-12-29" }) {
        tp::json::Value root = orig;
        tp::json::Value *a = mutPlan(root, "a");
        const QString back = (*a)["startDate"].toString();
        QVERIFY(placePlan(*a, QString::fromLatin1(day), planDays(*a)));
        QVERIFY(root != orig);
        QVERIFY(placePlan(*a, back, planDays(*a)));
        QVERIFY2(root == orig, day);
    }
}

void TestMap::calUnplace()
{
    using namespace tp::map::plans;
    tp::json::Value root = parseJson(kCalRoot);
    tp::json::Value *a = mutPlan(root, "a");
    unplacePlan(*a);
    QCOMPARE((*a)["days"].toNumber(), 7.0);
    QCOMPARE((*a)["startDate"].toString(), QString());
    QCOMPARE((*a)["endDate"].toString(), QString());
    QCOMPARE(stopDate(*a, 0, "arriveDate"), QString());
    QVERIFY(!(*a)["stops"].asArray()[0]["schedule"].asArray()[0].asObject().contains("date"));
    QCOMPARE((*a)["bookings"].asArray()[0]["start"].toString(), QStringLiteral("2026-12-28T08:00"));
    QCOMPARE(planDays(*a), 7);   // 去掉日期后天数记在 days 里，再放回去还是 7 天
    QVERIFY(placePlan(*a, "2027-05-01", planDays(*a)));
    QCOMPARE((*a)["endDate"].toString(), QStringLiteral("2027-05-07"));
    // 右边的分组：去掉日期的回到"计划中"
    const QVariantMap side = calSide(root, { "e" });
    QCOMPARE(side["planning"].toList().size(), 0);    // a 又放回去了，e 隐藏
    QCOMPARE(side["wish"].toList().size(), 2);        // b、c；已完成的 d 不算
    QCOMPARE(side["hidden"].toList().size(), 1);
}

void TestMap::calOverlapAndViews()
{
    using namespace tp::map::plans;
    const tp::json::Value root = parseJson(kCalRoot);
    QCOMPARE(overlapCount(root, "a", "2026-12-28", "2027-01-03", {}), 1);        // 戊
    QCOMPARE(overlapCount(root, "a", "2026-12-28", "2027-01-03", { "e" }), 0);   // 隐藏的不算
    QCOMPARE(overlapCount(root, "e", "2027-01-03", "2027-01-03", {}), 1);        // 边上一天也算
    QCOMPARE(overlapCount(root, "e", "2027-01-04", "2027-01-05", {}), 0);
    // 年视图：跨年的计划；窄条名字写旁边、一起排行，不和戊压在一行
    const QVariantMap y = calYear(root, {}, 2026, 11, "2026-11-07");
    QCOMPARE(y["spanDays"].toInt(), 365);
    QVERIFY(y["today"].toDouble() > 0 && y["today"].toDouble() < 0.03);
    QCOMPARE(y["months"].toList().size(), 12);
    QCOMPARE(y["months"].toList()[0].toMap()["cur"].toBool(), true);
    QCOMPARE(y["months"].toList()[2].toMap()["y"].toInt(), 2027);
    const QVariantList bars = y["bars"].toList();
    QCOMPARE(bars.size(), 2);
    QCOMPARE(bars[0].toMap()["id"].toString(), QStringLiteral("a"));
    QVERIFY(bars[0].toMap()["narrow"].toBool());
    QVERIFY(!bars[0].toMap()["labelLeft"].toBool());
    QCOMPARE(y["lanes"].toInt(), 2);
    QCOMPARE(calYear(root, {}, 2030, 1, "2026-11-07")["bars"].toList().size(), 0);
    QCOMPARE(calYear(root, {}, 2030, 1, "2026-11-07")["today"].toDouble(), -1.0);
    // 月视图：2027 年 1 月从 12-28（周一）开始；甲在第一周占周一到周日，后面一周周日结束
    const QVariantMap m = calMonth(root, {}, 2027, 1, "2027-01-02");
    const QVariantList weeks = m["weeks"].toList();
    QCOMPARE(weeks[0].toMap()["start"].toString(), QStringLiteral("2026-12-28"));
    QCOMPARE(weeks[0].toMap()["days"].toList()[0].toMap()["out"].toBool(), true);
    QCOMPARE(weeks[0].toMap()["days"].toList()[5].toMap()["today"].toBool(), true);
    const QVariantList w0 = weeks[0].toMap()["bars"].toList();
    QCOMPARE(w0.size(), 2);
    QCOMPARE(w0[0].toMap()["a"].toInt(), 0);
    QCOMPARE(w0[0].toMap()["b"].toInt(), 6);
    QCOMPARE(w0[1].toMap()["lane"].toInt(), 1);
    QCOMPARE(weeks.size(), 5);
    // 选年月
    const QVariantMap pk = calPicker(root, {});
    QCOMPARE(pk["years"].toList().size(), 2);
    QCOMPARE(pk["counts"].toMap()["2027-01"].toList().size(), 2);
}

void TestMap::calWeekStart()
{
    using namespace tp::map::plans;
    const tp::json::Value root = parseJson(kCalRoot);
    QCOMPARE(weekStartOf("2026-11-07", 1), QStringLiteral("2026-11-02"));   // 周六
    QCOMPARE(weekStartOf("2026-11-07", 0), QStringLiteral("2026-11-01"));
    QCOMPARE(weekStartOf("2026-11-08", 1), QStringLiteral("2026-11-02"));   // 周日：周一开始时算前一周
    QCOMPARE(weekStartOf("2026-11-08", 0), QStringLiteral("2026-11-08"));
    QCOMPARE(weekStartOf("2026-11-09", 1), QStringLiteral("2026-11-09"));
    // 2027 年 1 月：1 号周五；周日开始时第一周从 2026-12-27 起，甲（12-28 起）在第一周占第 1–6 格
    const QVariantList weeks = calMonth(root, {}, 2027, 1, "2027-01-02", 0)["weeks"].toList();
    QCOMPARE(weeks[0].toMap()["start"].toString(), QStringLiteral("2026-12-27"));
    const QVariantMap bar = weeks[0].toMap()["bars"].toList()[0].toMap();
    QCOMPARE(bar["a"].toInt(), 1);
    QCOMPARE(bar["b"].toInt(), 6);
    QCOMPARE(weeks[0].toMap()["days"].toList()[6].toMap()["today"].toBool(), true);   // 1 月 2 日周六是最后一格
    QCOMPARE(weeks[1].toMap()["bars"].toList()[0].toMap()["b"].toInt(), 0);           // 甲 1 月 3 日周日结束，第二周第一格
    // 月份从周日开始正好是第一天（2026 年 11 月 1 日周日）：没有上月的格子
    const QVariantList nov = calMonth(root, {}, 2026, 11, "", 0)["weeks"].toList();
    QCOMPARE(nov[0].toMap()["start"].toString(), QStringLiteral("2026-11-01"));
    QCOMPARE(nov[0].toMap()["days"].toList()[0].toMap()["out"].toBool(), false);
}

void TestMap::calWeekView()
{
    using namespace tp::map::plans;
    const tp::json::Value root = parseJson(R"({"plans":[
      {"id":"a","name":"甲","type":"route","status":"planning","startDate":"2027-03-01","endDate":"2027-03-10",
       "stops":[{"id":"s1","name":"一","lat":1,"lng":2,"arriveDate":"2027-03-01","departDate":"2027-03-03",
                 "schedule":[{"date":"2027-03-02","time":"09:00","end":"10:30","text":"早市\n第二行"},{"date":"2027-03-02","time":"10:00","text":"重叠"},{"date":"2027-03-02","text":"没时间"},{"time":"11:00","text":"多天的站没写日期"},
                             {"date":"2027-03-03","time":"23:30","end":"01:00","text":"跨午夜"}]},
                {"id":"s2","name":"二","lat":3,"lng":4,"color":"#123456","arriveDate":"2027-03-04","departDate":"2027-03-04","schedule":[{"time":"06:00","text":"单天的站"}]},
                {"id":"s3","name":"没坐标","arriveDate":"2027-03-04"}],
       "bookings":[{"id":"f","kind":"flight","title":"QF1","start":"2027-03-05T22:00","end":"2027-03-06T02:00","from":"甲地","to":"乙地"},
                   {"id":"h","kind":"hotel","title":"旅馆","start":"2027-03-01T15:00","end":"2027-03-03T10:00"},
                   {"id":"n","kind":"train","title":"没时间","start":"2027-03-02"}]},
      {"id":"b","name":"隐藏的","status":"planning","startDate":"2027-03-01","endDate":"2027-03-02"}]})");
    const QVariantMap w = calWeek(root, { "b" }, "2027-03-01", "2027-03-02");
    QCOMPARE(w["start"].toString(), QStringLiteral("2027-03-01"));
    QCOMPARE(w["days"].toList()[1].toMap()["today"].toBool(), true);
    QCOMPARE(w["days"].toList()[0].toMap()["wd"].toInt(), 1);
    // 全天：计划一条（后面还有，右边切开）+ 住宿一条（住到退房前一晚：3 月 1–2 日）
    const QVariantList bars = w["bars"].toList();
    QCOMPARE(bars.size(), 2);
    QCOMPARE(bars[0].toMap()["kind"].toString(), QStringLiteral("plan"));
    QCOMPARE(bars[0].toMap()["cutR"].toBool(), true);
    QCOMPARE(bars[1].toMap()["kind"].toString(), QStringLiteral("hotel"));
    QCOMPARE(bars[1].toMap()["b"].toInt(), 1);
    QCOMPARE(bars[1].toMap()["lane"].toInt(), 1);
    // 当天的站：路线有序号；没写时间的日程跟着；没坐标的不算
    const QVariantList stops = w["stops"].toList();
    QCOMPARE(stops[1].toList().size(), 1);
    QCOMPARE(stops[1].toList()[0].toMap()["n"].toInt(), 1);
    QVERIFY(stops[1].toList()[0].toMap()["loose"].toStringList().contains(QStringLiteral("没时间")));
    QCOMPARE(stops[3].toList().size(), 1);
    QCOMPARE(stops[3].toList()[0].toMap()["color"].toString(), QStringLiteral("#123456"));
    QCOMPARE(stops[4].toList().size(), 0);
    // 事件：3 月 2 日两个重叠的并排（早的在左）；多天的站没写日期不进时间轴；只取第一行
    const QVariantList evs = w["events"].toList();
    const QVariantList d2 = evs[1].toList();
    QCOMPARE(d2.size(), 2);
    QCOMPARE(d2[0].toMap()["title"].toString(), QStringLiteral("早市"));
    QCOMPARE(d2[0].toMap()["a"].toInt(), 540);
    QCOMPARE(d2[0].toMap()["b"].toInt(), 630);
    QCOMPARE(d2[0].toMap()["n"].toInt(), 2);
    QCOMPARE(d2[1].toMap()["col"].toInt(), 1);
    QCOMPARE(d2[1].toMap()["b"].toInt(), 660);   // 没写结束算 1 小时
    // 跨午夜：3 月 3 日 23:30–24:00 + 3 月 4 日 0:00–1:00
    QCOMPARE(evs[2].toList()[0].toMap()["b"].toInt(), 1440);
    const QVariantList d4 = evs[3].toList();
    QCOMPARE(d4.size(), 2);
    QCOMPARE(d4[0].toMap()["a"].toInt(), 0);
    QCOMPARE(d4[0].toMap()["b"].toInt(), 60);
    QCOMPARE(d4[1].toMap()["title"].toString(), QStringLiteral("单天的站"));
    // 机票跨午夜：两段，交通住宿标签
    QCOMPARE(evs[4].toList()[0].toMap()["a"].toInt(), 1320);
    QCOMPARE(evs[4].toList()[0].toMap()["b"].toInt(), 1440);
    QCOMPARE(evs[5].toList()[0].toMap()["b"].toInt(), 120);
    QCOMPARE(evs[5].toList()[0].toMap()["tab"].toString(), QStringLiteral("bookings"));
    QCOMPARE(evs[5].toList()[0].toMap()["sub"].toString(), QStringLiteral("甲地 → 乙地"));
    // 滚到最早的事件（0:00 的段）
    QCOMPARE(w["first"].toInt(), 0);
    // 周日开始：同一个计划，列跟着变
    const QVariantMap sun = calWeek(root, { "b" }, weekStartOf("2027-03-02", 0), "");
    QCOMPARE(sun["start"].toString(), QStringLiteral("2027-02-28"));
    QCOMPARE(sun["bars"].toList()[0].toMap()["a"].toInt(), 1);
    QCOMPARE(sun["days"].toList()[0].toMap()["wd"].toInt(), 0);
    // 没安排的一周：7 点
    const QVariantMap none = calWeek(root, {}, "2030-01-07", "");
    QCOMPARE(none["empty"].toBool(), true);
    QCOMPARE(none["first"].toInt(), 420);
    // 这个计划拖过去再撤销由自检走真实鼠标；这里确认周视图是只读的
    tp::json::Value copy = root;
    calWeek(copy, {}, "2027-03-01", "");
    QVERIFY(copy == root);
}

void TestMap::calRamadan()
{
    using namespace tp::map::plans;
    const auto has = [](int y, const char *a, const char *b) {
        for (const RamadanSpan &s : ramadanSpans(y)) if (QLatin1String(s.a) == QLatin1String(a) && QLatin1String(s.b) == QLatin1String(b)) return true;
        return false;
    };
    QCOMPARE(ramadanSpans(2027).size(), 1);
    QVERIFY(has(2027, "2027-02-08", "2027-03-08"));
    // 2030：1 月和 12 月底两段，12 月底那段跨到 2031 年，连起来算
    QCOMPARE(ramadanSpans(2030).size(), 2);
    QVERIFY(has(2030, "2030-01-05", "2030-02-03"));
    QVERIFY(has(2030, "2030-12-26", "2031-01-23"));
    QVERIFY(has(2031, "2030-12-26", "2031-01-23"));
    // 2033：1 月初（2032 年 12 月开始那段的尾巴）和年底两段
    QCOMPARE(ramadanSpans(2033).size(), 2);
    QVERIFY(has(2033, "2032-12-04", "2033-01-02"));
    QVERIFY(has(2033, "2033-11-23", "2033-12-22"));
    QVERIFY(has(2032, "2032-12-04", "2033-01-02"));
    // 表的范围：2024–2075 每年至少一段，每段 29 或 30 天；表外没有
    for (int y = 2024; y <= 2075; ++y) {
        const QVector<RamadanSpan> l = ramadanSpans(y);
        QVERIFY2(!l.isEmpty(), qPrintable(QString::number(y)));
        for (const RamadanSpan &s : l) {
            const int n = days(QString::fromLatin1(s.a), QString::fromLatin1(s.b)) + 1;
            QVERIFY2(n == 29 || n == 30, s.a);
        }
    }
    QVERIFY(ramadanSpans(2023).isEmpty());
    QVERIFY(ramadanSpans(2076).isEmpty());
}

void TestMap::calSeasonTable()
{
    using namespace tp::map::plans;
    const tp::json::Value root = parseJson(R"({"plans":[{"id":"p1","name":"去日本","status":"wish","seasonRef":"日本"},{"id":"p2","name":"别的","status":"planning"}],
      "seasonGuide":{"rows":[
        {"country":"日本","countryEn":"Japan","group":"日韩","iso":["JPN"],"spans":[{"from":3,"to":5,"kind":"best","text":"樱花","textEn":"Blossoms"},{"from":6,"to":6,"kind":"best","text":"梅雨前"},{"from":11,"to":2,"kind":"nice","text":"雪"}]},
        {"country":"俄罗斯","countryEn":"Russia","iso":["RUS"],"spans":[]},
        {"country":"土耳其","region":"西亚/中亚","ramadan":true,"iso":"TUR","spans":[]},
        {"country":"某地","group":"未知的组","spans":[]}]}})");
    const QVariantMap s = calSeason(root, 2030, nullptr, {});
    QCOMPARE(s["ready"].toBool(), false);
    QCOMPARE(s["groups"].toStringList(), QStringList({ "东亚", "西亚/中亚", "欧洲", "其他" }));   // REGIONS 的顺序，其他在最后
    const QVariantList rows = s["rows"].toList();
    QCOMPARE(rows.size(), 9);   // 4 组头 + 5 行（俄罗斯在东亚、欧洲各一次）
    QCOMPARE(rows[0].toMap()["head"].toBool(), true);
    QCOMPARE(rows[0].toMap()["count"].toInt(), 2);
    const QVariantMap jp = rows[1].toMap();
    QCOMPARE(jp["nameEn"].toString(), QStringLiteral("Japan"));
    QCOMPARE(jp["plans"].toList().size(), 1);
    QCOMPARE(jp["plans"].toList()[0].toMap()["wish"].toBool(), true);
    // 分段：1–2 月 nice（跨年的 span）、3–6 best 连成一段、7–10 没写、11–12 nice；英文没写的退回中文
    const QVariantList segs = jp["segs"].toList();
    QCOMPARE(segs.size(), 4);
    QCOMPARE(segs[1].toMap()["from"].toInt(), 3);
    QCOMPARE(segs[1].toMap()["to"].toInt(), 6);
    QCOMPARE(segs[1].toMap()["text"].toString(), QStringLiteral("樱花；梅雨前"));
    QCOMPARE(segs[1].toMap()["textEn"].toString(), QStringLiteral("Blossoms; 梅雨前"));
    QCOMPARE(segs[2].toMap()["kind"].toString(), QStringLiteral("none"));
    // 斋月：只有标了的行；2030 两段，第二段画到年底、日期写完整、标签靠左放
    QVERIFY(jp["ram"].toList().isEmpty());
    const QVariantList ram = rows[4].toMap()["ram"].toList();
    QCOMPARE(ram.size(), 2);
    QCOMPARE(ram[1].toMap()["b"].toString(), QStringLiteral("2031-01-23"));
    QCOMPARE(ram[1].toMap()["r"].toDouble(), 1.0);
    QCOMPARE(ram[1].toMap()["labelLeft"].toBool(), true);
    QVERIFY(ram[0].toMap()["l"].toDouble() < 0.02);
    // 2033 年 1 月初那段：从 0 开始，到 1 月 2 日结束
    const QVariantList ram33 = calSeason(root, 2033, nullptr, {})["rows"].toList()[4].toMap()["ram"].toList();
    QCOMPARE(ram33.size(), 2);
    QCOMPARE(ram33[0].toMap()["l"].toDouble(), 0.0);
    QVERIFY(ram33[0].toMap()["r"].toDouble() < 0.02);
    QVERIFY(calSeason(root, 2099, nullptr, {})["rows"].toList()[4].toMap()["ram"].toList().isEmpty());
    // 去过：行里的 iso（字符串、数组）；老数据没 iso 按国家名；组头数
    const QSet<QString> visited{ "JPN", "TUR" };
    const QVariantMap v = calSeason(root, 2030, &visited, {});
    QCOMPARE(v["ready"].toBool(), true);
    QCOMPARE(v["rows"].toList()[1].toMap()["visited"].toBool(), true);
    QCOMPARE(v["rows"].toList()[2].toMap()["visited"].toBool(), false);
    QCOMPARE(v["rows"].toList()[0].toMap()["visited"].toInt(), 1);
    // 折叠：只留组头
    const QVariantMap closed = calSeason(root, 2030, nullptr, { "东亚" });
    QCOMPARE(closed["rows"].toList().size(), 7);
    QCOMPARE(closed["rows"].toList()[0].toMap()["closed"].toBool(), true);
}

void TestMap::visitedCodes()
{
    const QByteArray adm1 = readFile(QStringLiteral(TP_SOURCE_DIR "/assets/admin/adm1.geojson"));
    if (adm1.isEmpty()) QSKIP("缺 assets/admin/adm1.geojson");
    UnlockModel m;
    QVERIFY(m.load(adm1));
    UnlockModel::PlanStops p;
    p.stops.push_back({ 35.68, 139.76 });    // 东京
    p.stops.push_back({ 39.9, 116.4 });      // 北京：中国 + 华北
    p.stops.push_back({ 30.65, 101.0 });     // 四川西部：川西
    p.stops.push_back({ 22.3, 114.17 });     // 香港：算中国和香港
    p.stops.push_back({ 0.0, -160.0 });      // 太平洋中间：哪儿也不算
    const QSet<QString> s = m.visitedCodes({ p }, {});
    for (const char *c : { "JPN", "CHN", "CN-N", "CN-WSC", "HKG" }) QVERIFY2(s.contains(QString::fromLatin1(c)), c);
    QVERIFY(!s.contains(QStringLiteral("CN-SW")));
    QVERIFY(!s.contains(QStringLiteral("USA")));
    // 手动标记：只有省份，跨区的省几个区都算
    int sichuan = -1;
    for (int i = 0; i < m.featureCount() && sichuan < 0; ++i) if (m.unitName(i) == QStringLiteral("四川省")) sichuan = i;
    QVERIFY(sichuan >= 0);
    const QSet<QString> manual = m.visitedCodes({}, { { m.unitId(sichuan), 2020 } });
    QVERIFY(manual.contains(QStringLiteral("CN-WSC")) && manual.contains(QStringLiteral("CN-SW")) && manual.contains(QStringLiteral("CHN")));
    QVERIFY(m.visitedCodes({}, {}).isEmpty());
}

void TestMap::journalPageCrud()
{
    using namespace tp::map::plans;
    tp::json::Value root = parseJson(R"({"photos":[{"id":"ph1","file":"photos/2026-01/a.jpg"}],"plans":[{"id":"p","name":"甲","stops":[{"id":"s1","name":"一","lat":1,"lng":2}]},{"id":"q","name":"乙"}],"journal":[]})");
    const tp::json::Value orig = root;
    QVERIFY(!putJournal(root, "j0", { { "title", " " }, { "body", "" }, { "date", "2026-01-01" } }));   // 什么都没写不新建
    QVERIFY(root == orig);
    QVERIFY(putJournal(root, "j1", { { "title", "标题" }, { "date", "2026-02-01" }, { "plan", "p" }, { "stop", "s1" }, { "body", "# 大\n![说明](photo:ph1)\n" + QString(200, QLatin1Char('x')) } }));
    QVERIFY(putJournal(root, "j2", { { "title", "" }, { "date", "2026-03-01" }, { "plan", "" }, { "body", "没关联" } }));
    QVERIFY(!root["journal"].asArray()[1].asObject().contains("plan"));
    // 不认识的键留着；只改给的字段
    root.mutObject().find("journal")->mutArray()[0].mutObject().set("future", tp::json::Value("留着"));
    QVERIFY(putJournal(root, "j1", { { "title", "新标题" }, { "date", "2026-02-01" }, { "plan", "p" }, { "stop", "" }, { "body", "b" } }));
    QCOMPARE(root["journal"].asArray()[0]["future"].toString(), QStringLiteral("留着"));
    QVERIFY(!root["journal"].asArray()[0].asObject().contains("stop"));
    QVERIFY(putJournal(root, "j1", { { "title", "标题" }, { "date", "2026-02-01" }, { "plan", "p" }, { "stop", "s1" }, { "body", "# 大\n![说明](photo:ph1)\n" + QString(200, QLatin1Char('x')) } }));
    QVariantMap pg = journalPage(root, "all");
    QCOMPARE(pg["total"].toInt(), 2);
    QCOMPARE(pg["hasNone"].toBool(), true);
    QCOMPARE(pg["plans"].toList().size(), 1);
    const QVariantList rows = pg["rows"].toList();
    QCOMPARE(rows[0].toMap()["id"].toString(), QStringLiteral("j2"));   // 新的在前
    QCOMPARE(rows[1].toMap()["file"].toString(), QStringLiteral("photos/2026-01/a.jpg"));
    QCOMPARE(rows[1].toMap()["text"].toString().size(), 110);
    QCOMPARE(journalPage(root, "p")["rows"].toList().size(), 1);
    QCOMPARE(journalPage(root, "none")["rows"].toList().size(), 1);
    QVERIFY(journalPage(root, "q")["filterGone"].toBool());
    const QVariantMap e = journalEntry(root, "j1");
    QCOMPARE(e["planName"].toString(), QStringLiteral("甲"));
    QCOMPARE(e["stopName"].toString(), QStringLiteral("一"));
    QVERIFY(journalEntry(root, "没有").isEmpty());
    QCOMPARE(journalStopOptions(root, "p").size(), 1);
    QCOMPARE(journalPlanOptions(root).size(), 2);
    // 写了又全删掉：离开时删；正在写的留着
    QVERIFY(putJournal(root, "j2", { { "title", "" }, { "date", "2026-03-01" }, { "body", "  " } }));
    QCOMPARE(dropEmptyJournal(root, "j2"), 0);
    QCOMPARE(dropEmptyJournal(root, ""), 1);
    QCOMPARE(int(root["journal"].asArray().size()), 1);
    QVERIFY(removeJournal(root, "j1"));
    QVERIFY(!removeJournal(root, "j1"));
    QCOMPARE(int(root["journal"].asArray().size()), 0);
}

void TestMap::kitJournal()
{
    using namespace tp::map::plans;
    const QString longBody = QString(200, QLatin1Char('x'));
    tp::json::Object j1;
    j1.set("id", tp::json::Value("old")); j1.set("plan", tp::json::Value("p")); j1.set("date", tp::json::Value("2026-01-01")); j1.set("body", tp::json::Value(longBody));
    tp::json::Value root = parseJson(R"({"photos":[{"id":"ph1","file":"photos/2026-01/a.jpg"}],"plans":[{"id":"p"}],
        "journal":[{"id":"n","plan":"p","title":"新","date":"2026-02-01","body":"# 标题\n![说明](photo:ph1)\n**加粗** 的 `字`"},
                   {"id":"other","plan":"q","date":"2030-01-01","body":"别的"}]})");
    root.mutObject().find("journal")->mutArray().push_back(tp::json::Value(std::move(j1)));
    const QVariantList l = journalList(root, "p");
    QCOMPARE(l.size(), 2);
    QCOMPARE(l[0].toMap()["id"].toString(), QStringLiteral("n"));
    QCOMPARE(l[0].toMap()["file"].toString(), QStringLiteral("photos/2026-01/a.jpg"));
    QCOMPARE(l[0].toMap()["text"].toString(), QStringLiteral("标题\n\n加粗 的 字"));
    QCOMPARE(l[1].toMap()["text"].toString().size(), 120);
    QCOMPARE(l[1].toMap()["file"].toString(), QString());
}

void TestMap::kitGuide()
{
    using namespace tp::map::plans;
    const tp::json::Value root = parseJson(R"({"photos":[{"id":"ph1","file":"photos/a.jpg","width":400,"height":300}]})");
    const QString text = QStringLiteral(
        "# 交通\n第一行 <b>不是标签</b>\n**粗** 看 https://a.com/x?a=1&b=2，然后\n\n## 小 **标题**\n> 引用\n- 列表\n- [ ] 没勾\n- [x] 勾了\n* [X] 也勾了\n"
        "![海边](photo:ph1)\n![](photo:gone)\n[文字](https://b.com) 和 [不是网址](javascript:alert(1))\n#不是标题\n### 三级");
    const QVariantMap g = guideBlocks(root, text);
    const QVariantList b = g["blocks"].toList();
    QStringList types;
    for (const QVariant &v : b) types << v.toMap()["type"].toString();
    QCOMPARE(types, QStringList({ "h1", "p", "h2", "quote", "li", "task", "task", "task", "img", "img", "p", "h2" }));
    const QString p = b[1].toMap()["html"].toString();
    QVERIFY(p.contains("&lt;b&gt;不是标签&lt;/b&gt;<br><b>粗</b>"));      // 转义；多行用 <br>
    QVERIFY(p.contains("<a href=\"https://a.com/x?a=1&amp;b=2\">"));
    QVERIFY(p.contains("</a>，然后"));                                    // 中文标点不算进网址
    QCOMPARE(b[5].toMap()["line"].toInt(), 7);
    QCOMPARE(b[5].toMap()["done"].toBool(), false);
    QCOMPARE(b[6].toMap()["done"].toBool(), true);
    QCOMPARE(b[8].toMap()["file"].toString(), QStringLiteral("photos/a.jpg"));
    QCOMPARE(b[8].toMap()["caption"].toString(), QStringLiteral("海边"));
    QCOMPARE(b[8].toMap()["width"].toDouble(), 400.0);
    QCOMPARE(b[9].toMap()["file"].toString(), QString());
    const QString tail = b[10].toMap()["html"].toString();
    QVERIFY(tail.contains("<a href=\"https://b.com\">文字</a>"));
    QVERIFY(!tail.contains("href=\"javascript"));                         // 只认 http(s)
    const QVariantList heads = g["heads"].toList();
    QCOMPARE(heads.size(), 3);
    QCOMPARE(heads[1].toMap()["text"].toString(), QStringLiteral("小 标题"));
    QCOMPARE(heads[1].toMap()["block"].toInt(), 2);
    QCOMPARE(heads[2].toMap()["level"].toInt(), 3);
    QVERIFY(guideBlocks(root, "")["blocks"].toList().isEmpty());
    // 打勾写回那一行，别的不动
    const QString t = QStringLiteral("a\n- [ ] 甲\n  * [x] 乙\n- 丙");
    QCOMPARE(toggleTaskLine(t, 1, true), QStringLiteral("a\n- [x] 甲\n  * [x] 乙\n- 丙"));
    QCOMPARE(toggleTaskLine(t, 2, false), QStringLiteral("a\n- [ ] 甲\n  * [ ] 乙\n- 丙"));
    QCOMPARE(toggleTaskLine(t, 3, true), t);
    QCOMPARE(toggleTaskLine(t, 99, true), t);
    tp::json::Value plan = parseJson(R"({"guide":"x"})");
    setGuide(plan, " \n ");
    QVERIFY(!plan.asObject().contains("guide"));
    setGuide(plan, "y");
    QCOMPARE(plan["guide"].toString(), QStringLiteral("y"));
    // 插入照片的列表：站点顺序，没坐标的站不算
    const tp::json::Value r2 = parseJson(R"({"plans":[{"id":"p","stops":[{"id":"s1","name":"一","lat":1,"lng":1},{"id":"s2","name":"二","lat":2,"lng":2},{"id":"x","name":"无坐标"}]}],
        "photos":[{"id":"b","file":"f/b","plan":"p","stop":"s2"},{"id":"a","file":"f/a","plan":"p","stop":"s1"},{"id":"c","file":"f/c","plan":"p","stop":"x"},{"id":"d","file":"f/d","plan":"q","stop":"s1"}]})");
    const QVariantList ph = planPhotos(r2, "p");
    QCOMPARE(ph.size(), 2);
    QCOMPARE(ph[0].toMap()["id"].toString(), QStringLiteral("a"));
    QCOMPARE(ph[1].toMap()["stop"].toString(), QStringLiteral("二"));
    QCOMPARE(ph[1].toMap()["stopNo"].toInt(), 2);
    // 0.10.5 精选、说明、封面、移除（不动文件路径，变成未归类；是封面就去掉封面）
    tp::json::Value r4 = r2;
    QVERIFY(setPhotoFav(r4, "a", true));
    QCOMPARE(planPhotos(r4, "p")[0].toMap()["fav"].toBool(), true);
    QVERIFY(setPhotoFav(r4, "a", false));
    QVERIFY(!r4["photos"].asArray()[1].asObject().contains("fav"));
    QVERIFY(setPhotoCaption(r4, "a", " 海边 "));
    QCOMPARE(planPhotos(r4, "p")[0].toMap()["caption"].toString(), QStringLiteral("海边"));
    QVERIFY(setPlanCover(r4, "p", "a"));
    QCOMPARE((*planById(r4, "p"))["cover"].toString(), QStringLiteral("a"));
    QVERIFY(!setPlanCover(r4, "p", "没有这张"));
    QVERIFY(!removePhotoFromPlan(r4, "q", "a"));                               // 不是这个计划的不动
    QVERIFY(removePhotoFromPlan(r4, "p", "a"));
    QCOMPARE(planPhotos(r4, "p").size(), 1);
    QVERIFY(!(*planById(r4, "p")).asObject().contains("cover"));
    QCOMPARE(r4["photos"].asArray()[1]["file"].toString(), QStringLiteral("f/a"));
    QVERIFY(!r4["photos"].asArray()[1].asObject().contains("plan"));
}

void TestMap::elevEditing()
{
    using namespace tp::map::plans;
    tp::json::Value root = parseJson(R"({"plans":[{"id":"p","stops":[
        {"id":"a","lat":1,"lng":1,"elev":100},{"id":"b","lat":2,"lng":2},{"id":"c","lat":3,"lng":3,"elev":null},
        {"id":"d","lat":4,"lng":4,"elev":""},{"id":"x"},{"id":"e","lat":5,"lng":5,"elev":"山顶"}]}]})");
    const tp::json::Value *p = planById(root, "p");
    const QVector<TzStop> need = stopsWithoutElev(*p);
    QStringList ids;
    for (const TzStop &s : need) ids << s.id;
    QCOMPARE(ids, QStringList({ "b", "c", "d" }));   // 只补空的；手写的不动；没坐标的不问
    QVERIFY(elevUrl(need).toString().contains("latitude=2.00000,3.00000,4.00000"));
    const QVector<double> v = parseElev(R"({"elevation":[1234.5,null,5]})", 3);
    QCOMPARE(v[0], 1234.5);
    QVERIFY(std::isnan(v[1]));
    QCOMPARE(v[2], 5.0);
    QVERIFY(std::isnan(parseElev("oops", 1)[0]));
    QCOMPARE(applyElev(root, "p", ids, v), 2);
    double e = 0;
    p = planById(root, "p");
    const tp::json::Array &st = (*p)["stops"].asArray();
    QVERIFY(stopElev(st[1], &e));
    QCOMPARE(e, 1235.0);
    QVERIFY(!stopElev(st[2], &e));
    QVERIFY(stopElev(st[3], &e));
    QCOMPARE(e, 5.0);
    QVERIFY(!stopElev(st[5], &e));   // "山顶"
    QCOMPARE((st[5])["elev"].toString(), QString("山顶"));
    // 已有的不覆盖
    QCOMPARE(applyElev(root, "p", { "a" }, { 999 }), 0);
    // 手动改：下标是有效站点的下标（没坐标的 x 不算）
    tp::json::Value *mp = mutPlan(root, "p");
    QCOMPARE(setStopElev(*mp, 4, "2000.4"), QString());
    QVERIFY(stopElev((*planById(root, "p"))["stops"].asArray()[5], &e));
    QCOMPARE(e, 2000.0);
    QCOMPARE(setStopElev(*mp, 0, "abc"), QString("badElev"));
    QCOMPARE(setStopElev(*mp, 0, "9001"), QString("badElev"));
    QCOMPARE(setStopElev(*mp, 0, "-501"), QString("badElev"));
    QCOMPARE(setStopElev(*mp, 9, "1"), QString("gone"));
    QVERIFY(stopElev((*planById(root, "p"))["stops"].asArray()[0], &e));
    QCOMPARE(e, 100.0);
    QCOMPARE(setStopElev(*mp, 0, "  "), QString());
    QVERIFY(!(*planById(root, "p"))["stops"].asArray()[0].asObject().find("elev"));
}

// 1.3.0 火车类沿离线线路数据（assets/rail/）：悉尼 Central → Town Hall 沿线路、长度合理；没有数据的城市回落直线
void TestMap::railRoute()
{
    namespace rail = tp::map::rail;
    rail::setDataDir(QStringLiteral(TP_SOURCE_DIR "/assets/rail"));
    const rail::Result r = rail::route(-33.8832, 151.2063, -33.8731, 151.2069);   // Central → Town Hall，直线约 1.1 km
    QVERIFY2(r.ok(), qPrintable(r.reason));
    QVERIFY(r.pts.size() >= 2);
    QVERIFY2(r.distM > 900 && r.distM < 1800, qPrintable(QString::number(r.distM)));
    // 起止点就在车站附近（吸附点离站 < 300 米）
    QVERIFY(tp::map::geo::distanceMeters(r.pts.first(), { -33.8832, 151.2063 }) < 300);
    QVERIFY(tp::map::geo::distanceMeters(r.pts.last(), { -33.8731, 151.2069 }) < 300);
    // 悉尼 Central → 机场国内航站楼（Airport Line，直线约 8 km）
    const rail::Result r2 = rail::route(-33.8832, 151.2063, -33.9350, 151.1658);
    QVERIFY2(r2.ok(), qPrintable(r2.reason));
    QVERIFY2(r2.distM > 7000 && r2.distM < 14000, qPrintable(QString::number(r2.distM)));
    qInfo("Central → Town Hall %.0f 米（%lld 点）；Central → 机场 %.0f 米", r.distM, qlonglong(r.pts.size()), r2.distM);
    // 其他城市抽查：东京站 → 新宿（JR 中央线约 10 km）、伦敦 King's Cross → Victoria（维多利亚线约 4 km）
    const rail::Result r3 = rail::route(35.6812, 139.7671, 35.6896, 139.7006);
    QVERIFY2(r3.ok() && r3.distM > 6000 && r3.distM < 16000, qPrintable(r3.reason + u' ' + QString::number(r3.distM)));
    const rail::Result r4 = rail::route(51.5308, -0.1238, 51.4952, -0.1441);
    QVERIFY2(r4.ok() && r4.distM > 3500 && r4.distM < 8000, qPrintable(r4.reason + u' ' + QString::number(r4.distM)));
    qInfo("东京站 → 新宿 %.0f 米；King's Cross → Victoria %.0f 米", r3.distM, r4.distM);
    // 没有数据的城市（雷克雅未克）→ 直线
    QCOMPARE(rail::route(64.1466, -21.9426, 64.1355, -21.8954).reason, QString("nodata"));
    // 一站在悉尼、一站在墨尔本 → 超出范围
    QCOMPARE(rail::route(-33.8832, 151.2063, -37.8183, 144.9671).reason, QString("out"));
    // 悉尼市区但离线路远（海上）→ far
    QCOMPARE(rail::route(-33.8832, 151.2063, -33.85, 151.33).reason, QString("far"));
    // 编辑器那一行：火车段给出 rail = ok 和沿线路的米数
    tp::json::Value plan = parseJson(QByteArray(R"({"id":"p","type":"route","stops":[
        {"id":"a","lat":-33.8832,"lng":151.2063,"leg":{"mode":"train"}},{"id":"b","lat":-33.8731,"lng":151.2069}]})"));
    const auto rows = planedit::legRows(plan);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].rail, QString("ok"));
    QVERIFY(qAbs(rows[0].dist - r.distM) < 1);
    QVERIFY(r.pts.size() >= 3);   // 真的沿着轨道拐，不只是两个吸附点
    rail::setDataDir(QStringLiteral(":/rail"));
}

#include "tst_map.moc"
