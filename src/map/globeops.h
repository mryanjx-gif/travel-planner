// 0.9.5：地球那一项交给 MapController 的相机接口（单独一个头文件：globeitem.h 不用包含整个 mapcontroller.h）。
#pragma once

#include "globegeom.h"

namespace tp::map {

// 0.9.5：地球那一项（GlobeItem）交给 MapController 的相机接口。在地球上时，定位、飞过去、框住、点标记平移、点圆圈放大
// 都转给它（MapController 不依赖 Qt Quick，GlobeItem 在程序里实现这个接口）
class GlobeOps {
public:
    virtual ~GlobeOps() = default;
    virtual globe::View globeView() const = 0;      // 逻辑像素
    virtual double globeZoom() const = 0;           // 换算成平面的缩放级别
    virtual bool globeMoving() const = 0;           // 在拖 / 在动画 / 在自转
    // 转过去（沿大圆）并缩放到 zoom（换算成平面的级别，< 0 = 距离不变；toDist > 0 时直接用这个相机距离）；ms ≤ 0 = 直接跳过去
    virtual void globeEaseTo(double lat, double lng, double zoom, int ms, double toDist = -1) = 0;
};

} // namespace tp::map
