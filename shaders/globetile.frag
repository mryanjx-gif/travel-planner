#version 440
// 地球的瓦片（0.9.0）：纹理数组取色 + 昼夜 + 大气光边（和 globe.frag 一样）
// 0.9.3：夜里的城市灯光（NASA Black Marble 2016 灰度，等距圆柱投影，一个通道）：染成暖黄，按暮光带渐亮
layout(location = 0) in vec3 vPos;
layout(location = 1) in vec2 vUv;
layout(location = 2) flat in float vLayer;
layout(location = 0) out vec4 fragColor;
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
layout(binding = 1) uniform sampler2DArray tex;
layout(binding = 2) uniform sampler2D lights;
const float PI = 3.14159265358979;

// 城市灯光取样：经度在 ±180° 处跳一圈，直接用 fwidth 会选到最小的一级 mipmap、出一条缝；
// 算两套经度坐标（0..1 和 -0.5..0.5），哪套在这个像素附近不跳就用哪套的导数（Tarini 2012 的做法）
float sampleLights(vec3 n)
{
    float u = atan(n.y, n.x) / (2.0 * PI) + 0.5;
    float v = 0.5 - asin(clamp(n.z, -1.0, 1.0)) / PI;
    float u2 = fract(u + 0.5) - 0.5;
    vec2 dx = vec2(dFdx(u), dFdx(v)), dy = vec2(dFdy(u), dFdy(v));
    vec2 dx2 = vec2(dFdx(u2), dx.y), dy2 = vec2(dFdy(u2), dy.y);
    if (abs(dx2.x) + abs(dy2.x) < abs(dx.x) + abs(dy.x)) { dx = dx2; dy = dy2; }
    return textureGrad(lights, vec2(u, v), dx, dy).r;
}

void main()
{
    vec3 n = normalize(vPos);
    vec3 col = texture(tex, vec3(vUv, vLayer)).rgb;
    float s = dot(n, normalize(sunDir.xyz));
    float L = lightParams.x > 0.0 ? sampleLights(n) : 0.0;   // 导数要在分支外面算（同一组像素都要走到）
    if (params.x > 0.0) {
        float alt = degrees(asin(clamp(s, -1.0, 1.0)));
        float t = clamp((-0.8 - alt) / params.y, 0.0, 1.0);
        t = t * t * (3.0 - 2.0 * t);
        col = mix(col, vec3(4.0, 10.0, 32.0) / 255.0, params.x * t);
        // 城市灯光：灰度是亮度（图里已经是 sRGB 的观感），压一下暗部的噪点，染成钠灯那种暖黄；暮光带里随天黑渐亮
        float lum = max(L - 0.03, 0.0) / 0.97;
        col += vec3(1.0, 0.80, 0.50) * lum * t * lightParams.y;
    }
    // 大气光边（0.9.3 跟着太阳，PO 选 Q1 B）：白天那一侧亮蓝，晨昏线处带一点暖橙，夜里那一侧只剩很淡的一点
    vec3 V = normalize(camPos.xyz - vPos);
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);
    float lit = smoothstep(-0.25, 0.35, s);
    float warm = exp(-(s / 0.16) * (s / 0.16));
    col += (vec3(0.32, 0.55, 0.95) * mix(0.04, 0.6, lit) + vec3(1.0, 0.52, 0.28) * warm * 0.35) * rim * params.w;
    fragColor = vec4(col, 1.0);
}
