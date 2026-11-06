#version 440
// 背景（星空 + 大气外光）：一个盖满屏幕的三角形，直接给 NDC
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec2 vNdc;
void main()
{
    vNdc = ndc;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
