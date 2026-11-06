#version 440
// 0.9.5 地球上的路线：圆头的线、圆头的虚线 / 点线（和 maplibre 的 line-cap: round + line-dasharray 一样，圆头伸进空白里），
// 边缘 1 像素抗锯齿
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec4 vGeom;
layout(location = 2) in vec2 vDash;
layout(location = 0) out vec4 fragColor;
void main()
{
    float along = vGeom.x, hw = vGeom.z, len = vGeom.w;
    float r = abs(vGeom.y) * (hw + 1.0);          // 离线中心的横向距离（四边形比线宽多 1 像素，给抗锯齿）
    float da = 0.0;
    if (vDash.y > 0.0) {
        float per = vDash.x + vDash.y;
        float m = mod(along, per);
        da = m <= vDash.x ? 0.0 : min(m - vDash.x, per - m);   // 离最近的一截虚线有多远
    }
    da = max(da, max(-along, along - len));       // 两头的圆头
    float d = length(vec2(da, r));
    float a = clamp(hw - d + 0.5, 0.0, 1.0);
    fragColor = vColor * a;
}
