#version 440
// 地球的底球（0.9.0）：纯色 + 两极 + 昼夜阴影（照搬 Electron drawNight：太阳高度 < -0.8° 算夜，12° 暮光带，
// rgb(4,10,32) × .62）+ 大气光边。瓦片那一遍（globetile.frag）用同样的昼夜和光边（再加城市灯光）
layout(location = 0) in vec3 vPos;
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
const float MAX_LAT = 1.48442222974871;   // 85.0511°

void main()
{
    vec3 n = normalize(vPos);
    float lat = asin(clamp(n.z, -1.0, 1.0));
    vec3 col = ocean.rgb;
    col = mix(col, northCap.rgb, smoothstep(MAX_LAT - 0.004, MAX_LAT + 0.004, lat));
    col = mix(col, southCap.rgb, smoothstep(MAX_LAT - 0.004, MAX_LAT + 0.004, -lat));
    float s = dot(n, normalize(sunDir.xyz));          // = sin(太阳高度)
    if (params.x > 0.0) {
        float alt = degrees(asin(clamp(s, -1.0, 1.0)));
        float t = clamp((-0.8 - alt) / params.y, 0.0, 1.0);
        t = t * t * (3.0 - 2.0 * t);
        col = mix(col, vec3(4.0, 10.0, 32.0) / 255.0, params.x * t);
    }
    // 大气光边（0.9.3 跟着太阳，PO 选 Q1 B）：白天那一侧亮蓝，晨昏线处带一点暖橙，夜里那一侧只剩很淡的一点
    vec3 V = normalize(camPos.xyz - vPos);
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);
    float lit = smoothstep(-0.25, 0.35, s);
    float warm = exp(-(s / 0.16) * (s / 0.16));
    col += (vec3(0.32, 0.55, 0.95) * mix(0.04, 0.6, lit) + vec3(1.0, 0.52, 0.28) * warm * 0.35) * rim * params.w;
    fragColor = vec4(col, 1.0);
}
