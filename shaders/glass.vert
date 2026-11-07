#version 440
// 液态玻璃（0.12.0）：顶点在本地坐标，vScene 是同一点的场景坐标（像素，原点左上）
layout(location = 0) in vec4 qt_VertexPosition;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 vScene;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float lod;
    float n;
    vec2 sceneSize;
    vec4 tint;
    vec4 rects[8];
    vec4 pars[8];
};
void main()
{
    vScene = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * qt_VertexPosition;
}
