#version 440
// 液态玻璃：一对一移植 Electron v6.3.0 liquidglass.js 的片元着色器——
// 连续圆角 SDF、smooth-min 粘连（k = 8）、四分之一圆高度场、两次折射 + 三色色散、mip LOD 磨砂、
// GGX 高光 × 4 个低角度光 + Schlick 菲涅尔、顶边亮线。
// 和 Electron 的差别：坐标是场景像素（Electron 的 uScale 恒为 1），每块面只带自己和够得着的泡泡（rects[0] 是自己）。
layout(location = 0) in vec2 vScene;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float lod;        // 面上磨砂的 mip 级别
    float n;          // rects 里有几个
    float plain;      // 1 = 标准玻璃
    vec2 sceneSize;
    vec4 tint;        // 底色 rgba（雾蓝 --g-map × .92 × 这块的 --lg-alpha）
    vec4 rects[8];    // x, y, w, h（场景像素）
    vec4 pars[8];     // 半径、种类（1 = 泡泡，2 = 大面板：连续圆角、不融合）
};
layout(binding = 1) uniform sampler2D source;

const float BEVEL = 22.0, IOR = 1.46, THICK = 40.0, DEPTH = 60.0, K = 8.0;

float sdBox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.)) + min(max(q.x, q.y), 0.) - r; }
// iOS 式连续圆角：转角用 4 次超椭圆代替圆弧
float sdSq(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; vec2 m = max(q, 0.); vec2 m2 = m * m; return pow(dot(m2, m2), .25) + min(max(q.x, q.y), 0.) - r; }
float smin(float a, float b, float k) { float h = clamp(.5 + .5 * (b - a) / k, 0., 1.); return mix(b, a, h) - k * h * (1. - h); }
int gi = 0;
float sdf(vec2 p)
{
    float d = 1e5;
    int cnt = int(n + .5);
    for (int i = 0; i < cnt; i++) {
        vec4 R = rects[i];
        float r = min(pars[i].x, min(R.z, R.w) * .5);
        vec2 c = p - R.xy - R.zw * .5;
        bool bubble = pars[i].y < 1.5;
        float di = bubble ? sdBox(c, R.zw * .5, r) : sdSq(c, R.zw * .5, r);
        if (di < d) gi = i;
        d = bubble ? smin(d, di, K) : min(d, di);
    }
    return d;
}
float height(float d) { float t = clamp(-d / BEVEL, 0., 1.); return sqrt(1. - (1. - t) * (1. - t)); }
vec3 bg(vec2 uv, float l) { return textureLod(source, clamp(uv, vec2(0.001), vec2(0.999)), l).rgb; }
float D_GGX(float NH, float a) { float a2 = a * a; float f = NH * NH * (a2 - 1.) + 1.; return a2 / (3.14159 * f * f); }

void main()
{
    vec2 p = vScene;
    float d = sdf(p);
    // 一个像素只由离它最近的那块面来画，不盖住相邻泡泡的内容
    if (d > 1.5 || gi != 0) { fragColor = vec4(0.); return; }
    if (plain > .5) {   // 标准玻璃（X-04）：同一个形状和粘连，只磨砂 + 底色 + 细亮线
        vec3 c0 = mix(bg(p / sceneSize, lod + 1.), tint.rgb, clamp(tint.a, 0., 1.));
        c0 += vec3(smoothstep(1.2, 0., abs(d + .9)) * .35);
        float a0 = 1. - smoothstep(-.75, .75, d);
        fragColor = vec4(c0 * a0, a0) * qt_Opacity;
        return;
    }
    float hx = height(sdf(p + vec2(1., 0.))) - height(sdf(p - vec2(1., 0.)));
    float hy = height(sdf(p + vec2(0., 1.))) - height(sdf(p - vec2(0., 1.)));
    vec3 n3 = normalize(vec3(-hx * BEVEL * .5, -hy * BEVEL * .5, 1.));
    float h = height(d);
    float bevel = 1. - smoothstep(0., 1., h);
    vec3 I = vec3(0., 0., -1.);
    vec3 col = vec3(0.);
    float eta[3] = float[3](1. / (IOR - .02), 1. / IOR, 1. / (IOR + .02));
    for (int c = 0; c < 3; c++) {
        vec3 r1 = refract(I, n3, eta[c]);
        vec3 r2 = refract(r1, vec3(0., 0., 1.), 1. / eta[c]);
        if (length(r2) < .5) r2 = r1;
        vec2 off = r1.xy / max(-r1.z, .2) * THICK * (1. + .6 * h) + r2.xy / max(-r2.z, .2) * DEPTH;
        vec2 uv = (p + off) / sceneSize;
        col[c] = bg(uv, mix(lod, lod * .2, bevel))[c];
    }
    float ta = clamp(tint.a, 0., 1.) * mix(1., .45, bevel);
    col = mix(col, tint.rgb, ta);
    vec2 g = vec2(hx, hy);
    float corner = 2. * abs(g.x) * abs(g.y) / max(dot(g, g), 1e-6);
    float cornerAtt = mix(1., .3, smoothstep(.25, .9, corner));
    vec3 V = vec3(0., 0., 1.);
    float NV = max(dot(n3, V), 0.);
    float F = .04 + .96 * pow(1. - NV, 5.);
    float spec = 0.;
    for (int i = 0; i < 4; i++) {
        float a = 0.785398 + float(i) * 1.5708;
        vec3 L = normalize(vec3(cos(a), sin(a), .28));
        vec3 H = normalize(L + V);
        float w = (i == 0 || i == 3) ? 1. : .55;
        spec += D_GGX(max(dot(n3, H), 0.), .3) * max(dot(n3, L), 0.) * w;
    }
    spec = (spec * .022 + F * .42 * bevel) * cornerAtt;
    col *= mix(1., .9, bevel * (1. - h));
    col += vec3(spec);
    float rim = smoothstep(1.2, 0., abs(d + .9)) * .5 * mix(1., .6, smoothstep(.25, .9, corner));
    col += vec3(rim * (0.5 + 0.5 * clamp(-hy * 6., 0., 1.)));
    float alpha = 1. - smoothstep(-.75, .75, d);
    fragColor = vec4(col * alpha, alpha) * qt_Opacity;
}
