#version 440
// 0.9.5 地球上的路线：顶点已经在 CPU 上投到屏幕、换成 NDC（y 朝上）；这里只按后端翻 y
layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 color;    // 预乘过透明度
layout(location = 2) in vec4 geom;     // x = 沿线距离（像素），y = 横向 -1..1，z = 半个线宽（像素），w = 这段的总长（像素）
layout(location = 3) in vec2 dash;     // 虚线：亮、暗的长度（像素）；暗 = 0 是实线
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec4 vGeom;
layout(location = 2) out vec2 vDash;
layout(std140, binding = 0) uniform buf {
    vec4 params;   // x = y 方向（1 或 -1）
};
void main()
{
    vColor = color;
    vGeom = geom;
    vDash = dash;
    gl_Position = vec4(pos.x, pos.y * params.x, 0.0, 1.0);
}
