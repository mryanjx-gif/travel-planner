#version 440
// 地球后面的天空（0.9.3，SkyItem）：照 Electron map-app.js 的星空（buildSpaceTile / drawSpace），但贴在天球上（C2）。
// Electron 是一张 1600×1000 的星空图横竖铺开、跟着地图中心平移；这里按视线方向算，拖动地球时星空跟着转，
// 放多大都清楚，不占贴图显存。数量、大小、亮度、颜色按"屏幕上每个像素"和 Electron 一样（1080 高的窗口为准）：
//   底色 #070b1e；星云 4 种颜色的淡光雾；银河 = 一圈斜着的大圆上的光雾 + 密集的小星；全天普通星星（大多很小很暗，少数亮）；
//   闪烁的亮星（少数带十字光芒）；偶尔一颗流星（位置由 C++ 算好传进来）；暗角 + 中间一点蓝光。
// 地球外面那一圈大气光晕也在这里画（跟着太阳，PO 选 Q1 B）。
// 用了 uint 运算、纹理数组，所以和 globetile 一样只出 GLSL 300 es / 330 以上（CMakeLists.txt 的 tp_shaders_globe 一组）。
// 0.9.4：底色、星云、银河光雾（还有银河小星的疏密）变化很慢，由 C++ 进地球前算好一次（SkyItem::bakeBackground，
// 天球立方体 6 个面 × 256²，存在纹理数组 bg 里，a 通道是银河小星的疏密）；这里只取样。0.9.3 每个像素现算 6 组分形噪声，
// UHD 620 上拖地球一帧显卡要 25 ms（0.9.3 验证）。
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 viewProj;      // 校正 × 投影 × 视图（方向 → 屏幕）
    mat4 invViewProj;   // 逆矩阵（屏幕 → 视线方向）
    vec4 camPos;
    vec4 sunDir;
    vec4 params;        // x = 时间（秒），y = 动不动（星星闪、流星；0 = 不动），z = 设备像素比，w = 星星亮度（自检对比用，平时 1）
    vec4 viewport;      // x, y = 像素大小，z = NDC 的 y 是不是朝上（1 / 0）
    vec4 meteorA;       // 流星头的位置（设备像素，左上角为原点，y 向下）、方向
    vec4 meteorB;       // x = 尾巴长度（设备像素），y = 不透明度（0 = 没有流星），z = 线宽
    vec4 band;          // 银河所在大圆的法线
    vec4 seed;          // 0.9.4 起不用（底图在 C++ 里算）；留着保持 uniform 的布局
};
layout(binding = 1) uniform sampler2DArray bg;   // 0.9.4：算好的底色 + 星云 + 银河光雾，a = 银河小星的疏密 ÷ 1.2

const float PI = 3.14159265358979;

uvec4 pcg4d(uvec4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
    return v;
}
vec4 hash4(vec2 cell, float face, float layer)
{
    uvec4 h = pcg4d(uvec4(uint(cell.x), uint(cell.y), uint(face) + 7u, uint(layer) * 101u + 20261016u));
    return vec4(h) / 4294967295.0;
}

// 等角立方体：方向 → 第几个面、面上 -1..1 的坐标（按角度均分，格子在角上不会变得很小）
void toCube(vec3 d, out float face, out vec2 a)
{
    vec3 ad = abs(d);
    vec2 uv;
    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x > 0.0 ? 0.0 : 1.0; uv = d.yz / ad.x; }
    else if (ad.y >= ad.z) { face = d.y > 0.0 ? 2.0 : 3.0; uv = d.xz / ad.y; }
    else { face = d.z > 0.0 ? 4.0 : 5.0; uv = d.xy / ad.z; }
    a = atan(uv) * (4.0 / PI);
}
vec3 fromCube(float face, vec2 a)
{
    vec2 uv = tan(a * (PI / 4.0));
    if (face < 0.5) return normalize(vec3(1.0, uv));
    if (face < 1.5) return normalize(vec3(-1.0, uv));
    if (face < 2.5) return normalize(vec3(uv.x, 1.0, uv.y));
    if (face < 3.5) return normalize(vec3(uv.x, -1.0, uv.y));
    if (face < 4.5) return normalize(vec3(uv, 1.0));
    return normalize(vec3(uv, -1.0));
}

// 方向 → 屏幕上相对这个像素的位移（设备像素）。星星在无穷远：w = 0（只受旋转、投影影响）
bool toScreen(vec3 dir, out vec2 dpx)
{
    vec4 c = viewProj * vec4(dir, 0.0);
    dpx = vec2(1e4);
    if (c.w <= 1e-5) return false;
    dpx = (c.xy / c.w - vNdc) * 0.5 * viewport.xy;
    return true;
}

// 一颗圆点（Electron 的 arc + fill，带抗锯齿）：半径 r（设备像素）、离中心 dist 像素时盖住多少
float disc(float r, float dist)
{
    float cov = clamp(r + 0.5 - dist, 0.0, 1.0);
    return cov * min(1.0, 3.0 * r * r / ((r + 0.5) * (r + 0.5) * (r + 0.5)));   // 很小的星星按面积变暗（和画布一样）
}

vec3 tintOf(float h)
{
    // Electron tints：'255,255,255', '255,244,230', '220,232,255', '255,226,200', '200,220,255'
    int i = int(min(h * 5.0, 4.0));
    if (i == 0) return vec3(1.0);
    if (i == 1) return vec3(255.0, 244.0, 230.0) / 255.0;
    if (i == 2) return vec3(220.0, 232.0, 255.0) / 255.0;
    if (i == 3) return vec3(255.0, 226.0, 200.0) / 255.0;
    return vec3(200.0, 220.0, 255.0) / 255.0;
}

// 一层星星：每个格子最多一颗（概率 prob），在格子中间一半的范围里随机摆
// kind 0 = 全天普通星星，1 = 银河里的密集小星，2 = 闪烁的亮星
vec3 starLayer(vec3 d, float N, float prob, float layer, int kind, float dpr, float bandW)
{
    float face; vec2 a;
    toCube(d, face, a);
    vec2 cell = floor((a * 0.5 + 0.5) * N);
    vec4 h = hash4(cell, face, layer);
    vec4 h2 = hash4(cell, face, layer + 50.0);
    float p = prob;
    if (kind == 1) p *= bandW;
    if (h.x > p) return vec3(0.0);
    vec2 ac = (cell + 0.25 + 0.5 * h.yz) / N * 2.0 - 1.0;
    vec2 dpx;
    if (!toScreen(fromCube(face, ac), dpx)) return vec3(0.0);
    float dist = length(dpx);
    float r, al;
    vec3 tint;
    if (kind == 0) {
        float k = h.w;
        r = k > 0.985 ? 1.4 + h2.x * 0.8 : k > 0.9 ? 0.9 + h2.x * 0.4 : 0.35 + h2.x * 0.45;
        al = k > 0.9 ? 0.85 : 0.3 + h2.y * 0.5;
        tint = tintOf(h2.z);
    } else if (kind == 1) {
        r = 0.35 + h.w * 0.45;
        al = 0.25 + h2.x * 0.45;
        tint = vec3(225.0, 230.0, 255.0) / 255.0;
    } else {
        r = 0.9 + h.w * 1.1;
        float ph = h2.x * 2.0 * PI, sp = 0.6 + h2.y * 1.6;
        al = params.y > 0.5 ? 0.45 + 0.55 * (0.5 + 0.5 * sin(params.x * sp + ph)) : 0.8;
        tint = tintOf(h2.z);
        float rr = r * dpr;
        float v = disc(rr, dist) * al;
        if (h2.w > 0.75) {   // 少数亮星带一点十字光芒（长 4r，线宽 0.6，不透明度 ×0.35）
            float len = rr * 4.0, wl = 0.6 * dpr;
            float hx = clamp(wl * 0.5 + 0.5 - abs(dpx.y), 0.0, 1.0) * step(abs(dpx.x), len);
            float hy = clamp(wl * 0.5 + 0.5 - abs(dpx.x), 0.0, 1.0) * step(abs(dpx.y), len);
            v = max(v, max(hx, hy) * al * 0.35);
        }
        return tint * v;
    }
    return tint * disc(r * dpr, dist) * al;
}

void main()
{
    vec4 w = invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w - camPos.xyz);
    float dpr = params.z;
    // 屏幕坐标（左上角为原点、y 向下，设备像素）
    vec2 scr = vec2((vNdc.x * 0.5 + 0.5) * viewport.x, (viewport.z > 0.5 ? 0.5 - vNdc.y * 0.5 : 0.5 + vNdc.y * 0.5) * viewport.y);

    // 底色 #070b1e + 星云 + 银河光雾（算好的，见文件头）
    float bface; vec2 ba;
    toCube(dir, bface, ba);
    vec4 b = texture(bg, vec3(ba * 0.5 + 0.5, bface));
    vec3 col = b.rgb;
    float starW = b.a * 1.2;

    // 星星（params.w：自检时可以关掉看底色）
    vec3 stars = starLayer(dir, 96.0, 0.374, 1.0, 0, dpr, 0.0)     // 全天普通星星：约 2.1 万颗
               + starLayer(dir, 200.0, 1.0, 2.0, 1, dpr, starW);   // 银河里的小星：约 0.9 万颗
    col += stars * params.w;

    // 暗角 + 地球周围一圈很淡的蓝光（Electron：0.15R rgba(60,90,170,.16) → 0.45R 透明 → R rgba(0,0,8,.55)）
    float R = length(viewport.xy) * 0.5;
    float rr = length(scr - viewport.xy * 0.5) / R;
    if (rr < 0.45) col = mix(col, vec3(60.0, 90.0, 170.0) / 255.0, 0.16 * (1.0 - smoothstep(0.15, 0.45, rr)));
    else col = mix(col, vec3(0.0, 0.0, 8.0) / 255.0, 0.55 * smoothstep(0.45, 1.0, rr));

    // 闪烁的亮星（Electron 每帧单独画，画在暗角上面）：约 1600 颗
    col += starLayer(dir, 40.0, 0.168, 3.0, 2, dpr, 0.0) * params.w;

    // 流星：一条从头往尾渐隐的线
    if (meteorB.y > 0.0) {
        vec2 hd = meteorA.xy, md = meteorA.zw;
        vec2 rel = scr - hd;
        float t = -dot(rel, md);                         // 沿着尾巴方向（往后）的距离
        float side = abs(rel.x * md.y - rel.y * md.x);   // 离线的距离
        if (t > -1.0 && t < meteorB.x) {
            float a = (1.0 - clamp(t / meteorB.x, 0.0, 1.0)) * clamp(meteorB.z * 0.5 + 0.5 - side, 0.0, 1.0);
            col += vec3(1.0) * a * meteorB.y;
        }
    }

    // 大气光晕（地球外面）：视线离球心最近的距离略大于 1 时发光。跟着太阳：白天那一侧亮蓝，晨昏线处一点暖橙，夜里那一侧很淡
    float tca = max(-dot(camPos.xyz, dir), 0.0);
    vec3 P = camPos.xyz + dir * tca;
    float r = length(P);
    // 球的边缘：地球那一层的网格是折线，比真正的球略小一点，中间漏出来的几个像素也要有光晕（往里多画 1.5%，里面反正被地球盖住）
    if (r > 0.985) {
        float s = dot(P / r, normalize(sunDir.xyz));
        float lit = smoothstep(-0.30, 0.45, s);
        float warm = exp(-(s / 0.18) * (s / 0.18));
        float fall = exp(-max(r - 1.0, 0.0) * 26.0);
        col += (vec3(0.30, 0.52, 0.95) * mix(0.02, 0.75, lit) + vec3(1.0, 0.55, 0.30) * warm * 0.5) * fall;
    }
    // 抖动半级灰度：暗的星云是 8 位存的，不抖会看出一圈圈的色阶
    vec4 dh = hash4(floor(gl_FragCoord.xy), 9.0, 0.0);
    col += (dh.x - 0.5) / 255.0;
    fragColor = vec4(col, 1.0);
}
