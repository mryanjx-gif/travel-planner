#version 440
// 液态玻璃（一块面板）：一对一移植 Electron v6.3.0 liquidglass.js 的片元着色器——
// 连续圆角 SDF、四分之一圆高度场、两次折射 + 三色色散、mip LOD 磨砂、GGX 高光 × 4 个低角度光 + Schlick 菲涅尔、顶边亮线。
// 背景来自 ShaderEffectSource（地图 / 地球那一层，mipmap 打开）。坐标：场景像素，原点左上。
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 itemPos;     // 本 ShaderEffect 在场景里的左上角（像素）
    vec2 itemSize;    // 本 ShaderEffect 的大小（像素）
    vec2 sceneSize;   // 背景纹理对应的场景大小（像素）
    vec4 rect;        // 玻璃块 x, y, w, h（场景像素）
    float radius;     // 圆角半径
    float lod;        // 面上磨砂的 mip 级别（Electron 2.3 × 缩放）
    float scale;      // 设备像素比（Electron 限制在 1.25 以内）
    float merge;      // 2 = 连续圆角（大面板），其他 = 普通圆角
    vec4 tint;        // 底色 rgba（雾蓝 --g-map rgba(250,252,253,.92) × .92）
};
layout(binding = 1) uniform sampler2D source;

const float BEVEL = 22.0, IOR = 1.46, THICK = 40.0, DEPTH = 60.0;

float sdBox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.)) + min(max(q.x, q.y), 0.) - r; }
// iOS 式连续圆角：转角用 4 次超椭圆代替圆弧
float sdSq(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; vec2 m = max(q, 0.); vec2 m2 = m * m; return pow(dot(m2, m2), .25) + min(max(q.x, q.y), 0.) - r; }
float sdf(vec2 p)
{
    float r = min(radius, min(rect.z, rect.w) * .5);
    vec2 c = p - rect.xy - rect.zw * .5;
    return merge > 1.5 ? sdSq(c, rect.zw * .5, r) : sdBox(c, rect.zw * .5, r);
}
float height(float d) { float t = clamp(-d / (BEVEL * scale), 0., 1.); return sqrt(1. - (1. - t) * (1. - t)); }
vec3 bg(vec2 uv, float l) { return textureLod(source, clamp(uv, vec2(0.001), vec2(0.999)), l).rgb; }
float D_GGX(float NH, float a) { float a2 = a * a; float f = NH * NH * (a2 - 1.) + 1.; return a2 / (3.14159 * f * f); }

void main()
{
    vec2 p = itemPos + qt_TexCoord0 * itemSize;
    float d = sdf(p);
    if (d > 1.5) { fragColor = vec4(0.); return; }
    float e = scale;
    float hx = height(sdf(p + vec2(e, 0.))) - height(sdf(p - vec2(e, 0.)));
    float hy = height(sdf(p + vec2(0., e))) - height(sdf(p - vec2(0., e)));
    vec3 n = normalize(vec3(-hx * BEVEL * scale * .5, -hy * BEVEL * scale * .5, 1.));
    float h = height(d);
    float bevel = 1. - smoothstep(0., 1., h);
    vec3 I = vec3(0., 0., -1.);
    vec3 col = vec3(0.);
    float eta[3] = float[3](1. / (IOR - .02), 1. / IOR, 1. / (IOR + .02));
    for (int c = 0; c < 3; c++) {
        vec3 r1 = refract(I, n, eta[c]);
        vec3 r2 = refract(r1, vec3(0., 0., 1.), 1. / eta[c]);
        if (length(r2) < .5) r2 = r1;
        vec2 off = r1.xy / max(-r1.z, .2) * THICK * scale * (1. + .6 * h) + r2.xy / max(-r2.z, .2) * DEPTH * scale;
        vec2 uv = (p + off) / sceneSize;
        float l = mix(lod, lod * .2, bevel);
        col[c] = bg(uv, l)[c];
    }
    float ta = clamp(tint.a, 0., 1.) * mix(1., .45, bevel);
    col = mix(col, tint.rgb, ta);
    vec2 g = vec2(hx, hy);
    float corner = 2. * abs(g.x) * abs(g.y) / max(dot(g, g), 1e-6);
    float cornerAtt = mix(1., .3, smoothstep(.25, .9, corner));
    vec3 V = vec3(0., 0., 1.);
    float NV = max(dot(n, V), 0.);
    float F = .04 + .96 * pow(1. - NV, 5.);
    float spec = 0.;
    for (int i = 0; i < 4; i++) {
        float a = 0.785398 + float(i) * 1.5708;
        vec3 L = normalize(vec3(cos(a), sin(a), .28));
        vec3 H = normalize(L + V);
        float w = (i == 0 || i == 3) ? 1. : .55;
        spec += D_GGX(max(dot(n, H), 0.), .3) * max(dot(n, L), 0.) * w;
    }
    spec = (spec * .022 + F * .42 * bevel) * cornerAtt;
    col *= mix(1., .9, bevel * (1. - h));
    vec3 hi = vec3(1.);
    col += hi * spec;
    float rim = smoothstep(1.2 * scale, 0., abs(d + .9 * scale)) * .5 * mix(1., .6, smoothstep(.25, .9, corner));
    col += hi * rim * (0.5 + 0.5 * clamp(-hy * 6., 0., 1.));
    float alpha = 1. - smoothstep(-.75 * scale, .75 * scale, d);
    fragColor = vec4(col * alpha, alpha) * qt_Opacity;
}
