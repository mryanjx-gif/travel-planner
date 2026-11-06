#version 440
// 地球的瓦片（0.9.0）：一块共用的网格（g 在 0..1），按实例的墨卡托范围摆到单位球面上。
// 要纹理数组（sampler2DArray），所以这两个着色器只出 GLSL 300 es / 330（CMakeLists.txt 单独一组 qt_add_shaders）
layout(location = 0) in vec2 g;
layout(location = 1) in vec4 merc;    // x0 y0 x1 y1（归一化墨卡托，y 向南）
layout(location = 2) in vec4 uvr;     // 在那一层里取哪一块（用上一级顶着时是其中的一小格）
layout(location = 3) in float layer;
layout(location = 0) out vec3 vPos;
layout(location = 1) out vec2 vUv;
layout(location = 2) flat out float vLayer;
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 sunDir;
    vec4 camPos;
    vec4 params;
    vec4 ocean;
    vec4 northCap;
    vec4 southCap;
    vec4 lightParams;   // 0.9.3：x = 画不画城市灯光，y = 亮度
    vec4 morph;   // 0.9.7 变形：x = 进度（0 球、1 平面），y / z = 屏幕中心的墨卡托坐标，w = 2π·cos(中心纬度)
    vec4 mC;      // 屏幕中心在球面上的点（切点）
    vec4 mE;      // 那里朝东的单位向量（= 相机的右）
    vec4 mN;      // 那里朝北的单位向量（= 相机的上）
};
const float PI = 3.14159265358979;

void main()
{
    float mx = mix(merc.x, merc.z, g.x);
    float my = mix(merc.y, merc.w, g.y);
    float lon = mx * 2.0 * PI - PI;
    float k = PI * (1.0 - 2.0 * my);
    float lat = atan(0.5 * (exp(k) - exp(-k)));   // atan(sinh(k))
    vec3 p = vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
    vPos = p;   // 光照、昼夜、城市灯光照旧按球面算（变形时由 CPU 把它们淡掉）
    vUv = mix(uvr.xy, uvr.zw, g);
    vLayer = layer;
    vec3 pos = p;
    if (morph.x > 0.0) {
        // 0.9.7 变形（C1）：往"以屏幕中心为切点、同一比例尺的墨卡托平面"上移。平面上的点到相机的深度都是 dist − 1，
        // 透视下是均匀缩放，所以终点和 maplibre 同一镜头的平面地图逐像素对齐。经度差按整块瓦片挑近的那一份（跨 180° 经线不拉长）
        float off = floor(0.5 * (merc.x + merc.z) - morph.y + 0.5);
        float dx = mx - morph.y - off;
        float dy = my - morph.z;
        vec3 q = mC.xyz + mE.xyz * (dx * morph.w) - mN.xyz * (dy * morph.w);
        pos = mix(p, q, morph.x);
    }
    gl_Position = mvp * vec4(pos, 1.0);
}
