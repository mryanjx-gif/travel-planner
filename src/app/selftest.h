// 开发版自检（0.7.2）：`TravelPlanner --selftest <报告.txt> --data <数据副本>`。
// 在 PO 的 Windows 上用真实显卡、真实数据（的副本）把一个版本里"手点才能验证"的流程自动走一遍，结果写成报告，
// 由 验证.bat 调用，Claude 读报告（不用 PO 一项项手点）。每个检查一行："✓ / ✗ 名称：说明"，最后一行总结；
// 有失败时程序退出码是失败的个数。
//
// 0.7.2 的检查（照片，0.7.0 的功能）：缩略图冷 / 热速度、导入、查重复、撤销重做、旋转（JPG / PNG / WebP / 不许转的 GIF）、
// 旋转后缩略图、清理（10 分钟保护 → 过了保护时间 → 真的移到回收站）、重新读取后照片还在。
// 0.8.0 的检查（地图）：投影（探针画出来的位置 = 算的位置，含飞行途中）、换底图不重新加载样式、中文路径里的样式文件、
// 昼夜阴影在 180° 经线两边都画了、"已解锁"颜色每次一样、改数据后地图跟着变。退出是否干净由 verify.ps1 查（退出码、崩溃转储、日志）。
// 0.8.2 的检查（标记）：图标字体、每个站点一个标记、合并 5 层、标记位置 = 投影、标记真的画出来了（取色）、选中的计划不合并、
// 聚焦和 ← → 切站、悬停渐亮 / 变淡 / 恢复、错开、点圆圈放大、没有数据的地方也画底色、"已解锁"的悬停提示框；缩略图失败时写出文件和错误。
// 0.8.3：底图瓦片画出来了（骨架样式里垫底图层在底图下面，屏幕取色不全是底色；没联网时这一项会失败）。
// 0.8.4：碰到线（命中、高亮层、其余变淡、和标记同步、离开 1 秒后恢复）、线条透明度立刻生效（没有 maplibre 自带的过渡）、
// 点线选中、碰到选中的计划的线不高亮；"有 N 段还没算"、批量计算（模拟的路线服务：写回、自动保存、撤销是一步）、
// 真的请求一次 OSRM（经统一请求通道；没联网时这一项会失败）、白名单和 User-Agent。
// 0.8.5：本机瓦片服务（底图走它、存进硬盘缓存、缓存命中不联网、路径检查、预取、连续 8 次失败的横幅和清零、离线切已解锁）；
// "已解锁"手动标记（打勾上色 + 自动保存、年份、地图上悬停描边 / 点省份、被计划点亮的只提示、撤销重做、搜索）。
// 0.8.7：排序和 Electron 一致（读 tests/golden/plans.json）、筛选（状态 / 类型 / 区域 / 隐藏 → 地图上画的计划、存进设置）、
// 定位被筛掉的计划时自动打开筛选、"此刻"（假装今天是某天）、勾待办 / 设为下次出发 / 置顶 / 标记为已完成 / 删除（自动保存、撤销是一步）、
// 简易计划面板跟着选中走、面板让开（飞过去时计划在面板左边）。各部分在 selftest_*.cpp（0.8.7 拆开）。
// 0.8.9：站点编辑器（新建 / 编辑 / 边改边存 / 放弃 / 删除 / 撤销一步）、草稿和拖动、编辑时算单段（模拟的路线服务）、
// 地名搜索（模拟的服务 + 缓存 + 署名条件）、点选反查、我的位置（定位来源、模拟拿到 / 拿不到时的退回）、比例尺、窄窗口的布局（selftest_editor.cpp）。
// 以后每个阶段把新功能的检查加在这里（只用 C++ 对象，不点界面；界面靠截图）。
// 只在开发版有效（打包版忽略 --selftest）。会改数据：一定要给 --data 一份副本。
#pragma once

#include <QString>

namespace tp::data { class ThumbCache; }
namespace tp::map { class MapController; }
namespace tp::services { class NetClient; class RoadRouter; class TileServer; class TileCache; class Geocoder; }
class QQuickWindow;

namespace tp::app {

class AppController;
class PhotoController;
class PlanController;
class PlanEditor;
class Locator;
class FrameStats;
class Settings;
class PlanPageController;
class NavHistory;
class RatesController;
class NetController;
class WeatherController;
class AiController;
class ExportController;

class SelfTest
{
public:
    // 0.8.7：地图页的计划（筛选、此刻、简易面板），在 start 之前给
    static void setPlans(PlanController *p);
    // 0.8.9：站点编辑器、我的位置、地名搜索
    static void setEditor(PlanEditor *e, Locator *l, services::Geocoder *g);
    // 0.10.0：完整计划页
    static void setPlanPage(PlanPageController *p, NavHistory *n, RatesController *rates);
    static void setNetCtl(NetController *n);
    static void setAi(AiController *a);   // 0.13.0
    static void setExporter(ExportController *e);   // 0.14.0
    static void setWeather(WeatherController *w);
    // 0.9.1：每一帧的时间（没挂时为空）、设置（量写 settings.json 要多久，用它的一份副本）
    static void setPerf(FrameStats *frames, Settings *settings);
    // 窗口显示后调用；做完写报告并 QCoreApplication::exit(失败个数)
    static void start(const QString &reportPath, AppController *app, PhotoController *photos, map::MapController *mapc, QQuickWindow *win,
                      const QString &backendInfo, services::RoadRouter *roads, services::NetClient *net,
                      services::TileServer *tiles, services::TileCache *tileCache);
};

} // namespace tp::app
