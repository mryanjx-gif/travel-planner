// 自检的公共部分（0.8.7 从 selftest.cpp 拆出来，不改行为）：报告、等待、截图取色，以及各部分检查的入口。
// 只给 selftest*.cpp 用。每部分一个文件：selftest_map.cpp（地图、标记）、selftest_routes.cpp（路线、瓦片）、
// selftest_unlock.cpp（"已解锁"手动标记、内存）、selftest_plans.cpp（0.8.7 筛选、此刻、简易面板），照片在 selftest.cpp。
#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

class QQuickWindow;
class QQuickItem;
class GlobeItem;
namespace tp::map { class MapController; }
namespace tp::services { class NetClient; class RoadRouter; class TileServer; class TileCache; class Geocoder; }

namespace tp::app {
class AppController;
class PhotoController;
class PlanController;
class PlanEditor;
class PlanPageController;
class NavHistory;
class RatesController;
class NetController;
class WeatherController;
class Locator;
class FrameStats;
class Settings;
}

namespace tp::app::selftest {

struct Report
{
    QStringList lines;
    int fails = 0, passes = 0;
    void section(const QString &s) { lines << QString() << QStringLiteral("## ") + s; }
    void info(const QString &s) { lines << QStringLiteral("  ") + s; }
    bool check(bool ok, const QString &name, const QString &detail = {})
    {
        lines << QStringLiteral("%1 %2%3").arg(ok ? QStringLiteral("✓") : QStringLiteral("✗"), name, detail.isEmpty() ? QString() : QStringLiteral("：") + detail);
        (ok ? passes : fails)++;
        if (!ok) qWarning("自检失败：%s %s", qUtf8Printable(name), qUtf8Printable(detail));
        return ok;
    }
};

// 等到 pred 为真（期间照常处理事件，不卡界面、不触发卡住检测）。超时返回 false
bool waitFor(const std::function<bool()> &pred, int timeoutMs);
void pause(int ms);
// 0.9.7（PO：减少"等得不够久"的假失败）：按状态等，不按固定时长
// 窗口再画 n 帧（先请求重画）：画面跟上了
bool waitFrames(QQuickWindow *win, int n = 2, int timeoutMs = 2000);
// 平面地图的动画停下来，再画两帧
bool waitMapIdle(QQuickWindow *win, tp::map::MapController *mapc, int timeoutMs = 3000);
// 平面地图停下来、地图库要的瓦片都回来了（最多 timeoutMs），再画三帧（maplibre 画好了）
bool waitRendered(QQuickWindow *win, tp::map::MapController *mapc, int timeoutMs = 3000);
// 地球停下来（不在动画、不在变形）、这一屏的瓦片到齐（到不齐等到超时），再画两帧
bool waitGlobeIdle(QQuickWindow *win, GlobeItem *globe, int timeoutMs = 3000);
// 一个界面元素停稳了（位置、大小连续 3 次检查没变；面板滑进滑出、宽度动画）
bool waitStill(QQuickItem *it, int timeoutMs = 2000);
// 截一张窗口图，在 (x, y)（逻辑像素）附近 24 像素内找品红探针的中心；找不到返回 false
bool findProbe(QQuickWindow *win, double x, double y, double *fx, double *fy, QImage *shot = nullptr);
double luminanceAt(const QImage &img, QQuickWindow *win, double x, double y);
// 窗口截图里 (x, y)（逻辑像素）的颜色
QColor colorAt(const QImage &img, QQuickWindow *win, double x, double y);
double colorDiff(const QColor &a, const QColor &b);

void runMap(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
void runMarkers(Report &r, map::MapController *mapc, QQuickWindow *win);
void runRoutes(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, services::RoadRouter *roads, services::NetClient *net);
void runTiles(Report &r, map::MapController *mapc, QQuickWindow *win, services::NetClient *net, services::TileServer *ts, services::TileCache *cache);
void runUnlockManual(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
void runMemory(Report &r, map::MapController *mapc, QQuickWindow *win);
// 0.9.0：地球——按视野加载瓦片、卫星图 / 已解锁、相机、离开时释放、去掉浅灰（selftest_globe.cpp）
void runGlobe(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, services::TileServer *ts);
// 0.8.7（PlanController 由 SelfTest::setPlans 给）
PlanController *plansCtl();
void runPlans(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
// 0.8.8：顶栏、⋯ 泡泡、我的旅程、Ctrl K、前进 / 后退、Esc 层级（selftest_shell.cpp）
void runShell(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
// 0.8.10：页面结构——⋯ 的 8 颗泡泡、地图的四种模式、整页占位（selftest_pages.cpp）
void runPages(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
// 0.8.9：站点编辑器、地名搜索、我的位置、地图控件、窄窗口（selftest_editor.cpp）
PlanEditor *editorCtl();
Locator *locatorCtl();
services::Geocoder *geocoderCtl();
void runEditor(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, services::RoadRouter *roads);
// 0.10.0：完整计划页（selftest_planpage.cpp）
PlanPageController *planPageCtl();
NavHistory *navCtl();
void runPlanPage(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, PlanPageController *page, NavHistory *nav);
// 0.10.1：交通住宿、消费、汇率（selftest_money.cpp）
RatesController *ratesCtl();
void runMoney(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, RatesController *rates);
void runSplit(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, RatesController *rates);
// 0.10.3：天气、气候、海拔、高反、此刻卡（selftest_weather.cpp）
WeatherController *weatherCtl();
void runWeather(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, WeatherController *weather);
// 0.10.4：行李与待办、紧急信息、游记、攻略（selftest_kit.cpp）
void runKit(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav);
// 0.12.4：设置的货币、证件、紧急信息，此刻卡"看证件"（selftest_kit.cpp）
void runDocs(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, RatesController *rates);
// 0.12.5：设置的联网开关、缓存清空、关于和更新日志（selftest_kit.cpp）
NetController *netCtl();
void runNetAbout(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, NavHistory *nav, services::NetClient *net, RatesController *rates, WeatherController *weather, PhotoController *photos);
// 0.10.5：照片标签、看大图、编辑器的照片和封面（selftest_photos_tab.cpp）
void runPlanPhotos(Report &r, AppController *app, QQuickWindow *win, PlanPageController *page, NavHistory *nav, PhotoController *photos);
// 0.11.0：计划库、想去、足迹（selftest_library.cpp）
void runLibrary(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
// 0.11.4：阶段 ⑤ 总检查补测（selftest_audit.cpp）。分组 audit；thumbs1000 只在点名时跑
void runAudit(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, PhotoController *photos);
void runThumbs1000(Report &r, PhotoController *photos, const QString &dataDir);
// 0.11.1：照片模式、相册、看大图、HEIC（selftest_photomode.cpp）
void runPhotoMode(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win, PhotoController *photos);
// 0.11.2：日历、游记（selftest_calendar.cpp）。分组 calendar、journal
void runCalendar(Report &r, AppController *app, QQuickWindow *win);
void runCalendarWeek(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
void runJournal(Report &r, AppController *app, QQuickWindow *win);
// 0.9.1：性能——启动各步、拖地图每帧、换模式、数据重建、写设置、玻璃和标记各占多少（selftest_perf.cpp）
FrameStats *frameStats();
Settings *settingsObj();
void runGlass(Report &r, AppController *app, QQuickWindow *win);
void runPerf(Report &r, AppController *app, map::MapController *mapc, QQuickWindow *win);
// 0.9.1：显卡内存分配器的统计（Vulkan / D3D12 才有；在渲染线程上读）
struct RhiMem { bool ok = false; quint32 blocks = 0, allocs = 0; double usedMB = 0, unusedMB = 0; QString text() const; };
RhiMem readRhiMem(QQuickWindow *win);

} // namespace tp::app::selftest
