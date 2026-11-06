#version 440
// 地球的底球（0.9.0）：比瓦片略小一点的球（params.z），瓦片没到的地方和两极露出来
layout(location = 0) in vec3 pos;
layout(location = 0) out vec3 vPos;
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 sunDir;    // 太阳直射点方向（世界坐标，x 指向 0°E，z 指向北极）
    vec4 camPos;
    vec4 params;    // x = 夜晚遮罩不透明度（0 = 不画昼夜），y = 暮光带宽度（度），z = 底球半径，w = 光边强度
    vec4 ocean;     // 瓦片没到的地方
    vec4 northCap;  // 北纬 85.05° 以北（墨卡托瓦片画不到）
    vec4 southCap;  // 南纬 85.05° 以南
    vec4 lightParams;   // 0.9.3：x = 画不画城市灯光，y = 亮度
    vec4 morph;   // 0.9.7 变形：x = 进度（0 球、1 平面），y / z = 屏幕中心的墨卡托坐标，w = 2π·cos(中心纬度)
    vec4 mC;      // 屏幕中心在球面上的点（切点）
    vec4 mE;      // 那里朝东的单位向量（= 相机的右）
    vec4 mN;      // 那里朝北的单位向量（= 相机的上）
};
void main()
{
    vPos = pos;
    gl_Position = mvp * vec4(pos * params.z, 1.0);
}
