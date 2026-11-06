#include "markerimages.h"

#include "msicons.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStringList>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace tp::map {

namespace {
QString g_textFamily = QStringLiteral("Noto Serif SC");
QString g_iconFamily;

const QColor kPaper(0xff, 0xfd, 0xf8);   // #fffdf8（标记白边、想去的底色）

QString enc(const QString &s) { return s.isEmpty() ? QStringLiteral("-") : QString::fromLatin1(QUrl::toPercentEncoding(s)); }
QString dec(const QString &s) { return s == QLatin1String("-") ? QString() : QUrl::fromPercentEncoding(s.toLatin1()); }
QString dprStr(double dpr) { return QString::number(std::clamp(dpr, 0.5, 4.0), 'f', 2); }

// CSS 颜色：认不出时 CSS 不画背景（透明），这里也一样
QColor cssColor(const QString &c)
{
    QColor q = QColor::fromString(c.trimmed());
    return q.isValid() ? q : QColor(Qt::transparent);
}

// 一维高斯核（σ 以图片像素计）
std::vector<float> kernel(double sigma)
{
    const int r = std::max(1, int(std::ceil(sigma * 3)));
    std::vector<float> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; ++i) { k[i + r] = float(std::exp(-(i * i) / (2 * sigma * sigma))); sum += k[i + r]; }
    for (float &v : k) v = float(v / sum);
    return k;
}

// 把 img（ARGB32 预乘）的 alpha 做成阴影：颜色 c、偏移 (dx, dy)、模糊 σ（都以图片像素计）
QImage shadowOf(const QImage &img, const QColor &c, double dx, double dy, double sigma)
{
    const int W = img.width(), H = img.height();
    std::vector<float> a(size_t(W) * H), t(size_t(W) * H);
    for (int y = 0; y < H; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < W; ++x) a[size_t(y) * W + x] = qAlpha(row[x]) / 255.f;
    }
    if (sigma > 0.05) {
        const std::vector<float> k = kernel(sigma);
        const int r = int(k.size() / 2);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float s = 0;
                for (int i = -r; i <= r; ++i) { const int xx = x + i; if (xx >= 0 && xx < W) s += a[size_t(y) * W + xx] * k[i + r]; }
                t[size_t(y) * W + x] = s;
            }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float s = 0;
                for (int i = -r; i <= r; ++i) { const int yy = y + i; if (yy >= 0 && yy < H) s += t[size_t(yy) * W + x] * k[i + r]; }
                a[size_t(y) * W + x] = s;
            }
    }
    QImage out(W, H, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const int ox = int(std::lround(dx)), oy = int(std::lround(dy));
    for (int y = 0; y < H; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        const int sy = y - oy;
        if (sy < 0 || sy >= H) continue;
        for (int x = 0; x < W; ++x) {
            const int sx = x - ox;
            if (sx < 0 || sx >= W) continue;
            const float al = std::clamp(a[size_t(sy) * W + sx] * float(c.alphaF()), 0.f, 1.f);
            const int A = int(al * 255 + 0.5f);
            row[x] = qRgba(c.red() * A / 255, c.green() * A / 255, c.blue() * A / 255, A);
        }
    }
    return out;
}

QImage blank(double w, double h, double dpr)
{
    QImage img(int(std::ceil(w * dpr)), int(std::ceil(h * dpr)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

// CSS line-height: 1 时文字的基线：盒子中线 + (ascent - descent) / 2（和浏览器的半行距算法一样）
void drawCentered(QPainter &p, const QFont &f, const QString &s, QPointF center, const QColor &c)
{
    p.setFont(f);
    p.setPen(c);
    const QFontMetricsF fm(f, p.device());
    const double w = fm.horizontalAdvance(s);
    p.drawText(QPointF(center.x() - w / 2, center.y() + (fm.ascent() - fm.descent()) / 2), s);
}

QFont textFont()
{
    QFont f(g_textFamily);
    f.setPixelSize(11);
    f.setWeight(QFont::Bold);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// Material Symbols：字形画在 1em 见方的格子里、基线在格子底边（font-size px、line-height 1）
void drawIcon(QPainter &p, char32_t cp, double px, QPointF center, const QColor &c)
{
    QFont f(g_iconFamily);
    f.setPixelSize(int(std::lround(px)));
    f.setHintingPreference(QFont::PreferNoHinting);
    p.setFont(f);
    p.setPen(c);
    const QString s = QString::fromUcs4(&cp, 1);
    const QFontMetricsF fm(f, p.device());
    const double w = fm.horizontalAdvance(s);
    p.drawText(QPointF(center.x() - w / 2, center.y() + px / 2), s);
}

QPainterPath pinPath()
{
    // <path d="M14 1.2C7 1.2 1.3 6.8 1.3 13.8 1.3 23.5 14 36.8 14 36.8s12.7-13.3 12.7-23C26.7 6.8 21 1.2 14 1.2z"/>
    QPainterPath path;
    path.moveTo(14, 1.2);
    path.cubicTo(7, 1.2, 1.3, 6.8, 1.3, 13.8);
    path.cubicTo(1.3, 23.5, 14, 36.8, 14, 36.8);
    path.cubicTo(14, 36.8, 26.7, 23.5, 26.7, 13.8);   // s 12.7-13.3 12.7-23（第一个控制点 = 上一段第二个控制点的镜像）
    path.cubicTo(26.7, 6.8, 21, 1.2, 14, 1.2);
    path.closeSubpath();
    return path;
}

// 圆角矩形 / 圆（border-box）：外圈是边框色，里面缩进 bw 填底色（内圆角 = 外圆角 - bw）
// 编辑中的标记（Electron .mk.draft .mk-b { border-style: dashed }）：先整块填底色，再画一圈虚线边
thread_local bool g_draft = false;   // 每个线程一份（图片提供者可能在加载线程里画）
QPen draftPen(const QColor &c, double w)
{
    QPen pen(c, w, Qt::CustomDashLine, Qt::FlatCap, Qt::MiterJoin);
    pen.setDashPattern({ 1.5, 1.0 });   // 以线宽为单位：3 px 实、2 px 空（Chrome 的 2 px dashed 约是这样）
    return pen;
}

void boxShape(QPainter &p, const QRectF &r, double radius, double bw, const QColor &border, const QColor &fill)
{
    if (g_draft) {
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, radius, radius);
        p.setBrush(Qt::NoBrush);
        p.setPen(draftPen(border, bw));
        const double h = bw / 2;
        p.drawRoundedRect(r.adjusted(h, h, -h, -h), std::max(0.0, radius - h), std::max(0.0, radius - h));
        return;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(border);
    p.drawRoundedRect(r, radius, radius);
    p.setBrush(fill);
    const double ir = std::max(0.0, radius - bw);
    p.drawRoundedRect(r.adjusted(bw, bw, -bw, -bw), ir, ir);
}

QImage compose(const QImage &shadow, const QImage &fg)
{
    QImage out = shadow;
    QPainter p(&out);
    p.drawImage(0, 0, fg);
    return out;
}
} // namespace

MarkerImages::MarkerImages() : QQuickImageProvider(QQuickImageProvider::Image) {}

void MarkerImages::setFonts(const QString &textFamily, const QString &iconFamily)
{
    if (!textFamily.isEmpty()) g_textFamily = textFamily;
    g_iconFamily = iconFamily;
}
QString MarkerImages::iconFamily() { return g_iconFamily; }
bool MarkerImages::iconReady() { return !g_iconFamily.isEmpty(); }

QString MarkerImages::stopId(const markers::Style &st, bool wish, double dpr, bool draft)
{
    if (draft) return stopId(st, wish, dpr, false) + QStringLiteral("/d");
    const char32_t cp = markers::showsIcon(st, iconReady()) ? msicons::codepoint(st.icon.toUtf8().constData()) : 0;
    return QStringList{ QStringLiteral("s"), enc(st.shape), enc(st.color), wish ? QStringLiteral("1") : QStringLiteral("0"),
                        st.dot ? QStringLiteral("1") : QStringLiteral("0"), cp ? QString::number(uint(cp), 16) : QStringLiteral("-"),
                        enc(st.text), dprStr(dpr) }.join(QLatin1Char('/'));
}

QString MarkerImages::clusterId(int size, const QString &bg, const QString &fg, int n, double dpr)
{
    return QStringList{ QStringLiteral("c"), QString::number(size), enc(bg), enc(fg), QString::number(n), dprStr(dpr) }.join(QLatin1Char('/'));
}

QString MarkerImages::glowId(double w, double h, bool pin, double dpr)
{
    return QStringList{ QStringLiteral("g"), QString::number(w), QString::number(h), pin ? QStringLiteral("1") : QStringLiteral("0"), dprStr(dpr) }
        .join(QLatin1Char('/'));
}

QString MarkerImages::vehicleId(const QString &mode, const QString &color, double dpr)
{
    return QStringList{ QStringLiteral("v"), enc(mode), enc(color), dprStr(dpr) }.join(QLatin1Char('/'));
}

QImage MarkerImages::paintVehicle(const QString &mode, const QString &color, double dpr)
{
    dpr = std::clamp(dpr > 0 ? dpr : 1.0, 0.5, 4.0);
    if (mode == QLatin1String("flight")) {
        // 俯视的飞机剪影，机头朝右（QML 按前进方向转）：白底、深色描边，在卫星图和夜里都看得清
        const double S = kVehicleSize;
        QImage img = blank(S, S, dpr);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.translate(S / 2, S / 2);
        p.scale(1.15, 1.15);
        QPainterPath path;
        const double pts[][2] = { { 9, 0 }, { 3, -1.3 }, { -1, -8 }, { -3, -8 }, { -0.5, -1.4 }, { -6, -1.2 }, { -8, -4 }, { -9.5, -4 },
                                  { -8.3, 0 }, { -9.5, 4 }, { -8, 4 }, { -6, 1.2 }, { -0.5, 1.4 }, { -3, 8 }, { -1, 8 }, { 3, 1.3 } };
        path.moveTo(pts[0][0], pts[0][1]);
        for (size_t i = 1; i < sizeof(pts) / sizeof(pts[0]); ++i) path.lineTo(pts[i][0], pts[i][1]);
        path.closeSubpath();
        p.setPen(QPen(QColor(20, 30, 45, 220), 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::white);
        p.drawPath(path);
        return img;
    }
    // 火车 / 自驾 / 步行 / 轮渡：白色小圆牌（计划颜色的细边）+ 计划颜色的图标（Material Symbols，字体子集里都有）
    const double S = kBadgeSize;
    QImage img = blank(S, S, dpr);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(dpr, dpr);
    const QColor c = cssColor(color);
    p.setPen(QPen(c, 1.6));
    p.setBrush(kPaper);
    p.drawEllipse(QRectF(1, 1, S - 2, S - 2));
    const char *name = mode == QLatin1String("train") ? "train" : mode == QLatin1String("ferry") ? "directions_boat"
                     : mode == QLatin1String("walk") ? "directions_walk" : "directions_car";
    const char32_t cp = msicons::codepoint(name);
    if (cp && iconReady()) drawIcon(p, cp, 16, QPointF(S / 2, S / 2), c);
    else { p.setPen(Qt::NoPen); p.setBrush(c); p.drawEllipse(QPointF(S / 2, S / 2), 4, 4); }
    return img;
}

QImage MarkerImages::requestImage(const QString &id, QSize *size, const QSize &)
{
    const QStringList f = id.split(QLatin1Char('/'));
    QImage img;
    if ((f.size() == 8 || (f.size() == 9 && f[8] == QLatin1String("d"))) && f[0] == QLatin1String("s")) {
        markers::Style st;
        st.shape = dec(f[1]);
        st.color = dec(f[2]);
        st.dot = f[4] == QLatin1String("1");
        st.text = dec(f[6]);
        // 图标：描述里已经是码位（stopId 只在字体能用、名字认得时写进去）
        bool ok = false;
        const uint cp = f[5] == QLatin1String("-") ? 0 : f[5].toUInt(&ok, 16);
        if (ok && cp) st.icon = QStringLiteral("#cp:") + QString::number(cp, 16);
        img = paintStop(st, f[3] == QLatin1String("1"), f[7].toDouble(), f.size() == 9);
    } else if (f.size() == 6 && f[0] == QLatin1String("c")) {
        img = paintCluster(f[1].toInt(), dec(f[2]), dec(f[3]), f[4].toInt(), f[5].toDouble());
    } else if (f.size() == 5 && f[0] == QLatin1String("g")) {
        img = paintGlow(f[1].toDouble(), f[2].toDouble(), f[3] == QLatin1String("1"), f[4].toDouble());
    } else if (f.size() == 4 && f[0] == QLatin1String("v")) {
        img = paintVehicle(dec(f[1]), dec(f[2]), f[3].toDouble());
    }
    if (img.isNull()) { img = QImage(1, 1, QImage::Format_ARGB32_Premultiplied); img.fill(Qt::transparent); }
    if (size) *size = img.size();
    return img;
}

QImage MarkerImages::paintStop(const markers::Style &stIn, bool wish, double dpr, bool draft)
{
    struct DraftFlag { explicit DraftFlag(bool on) { g_draft = on; } ~DraftFlag() { g_draft = false; } } draftFlag(draft);
    dpr = std::clamp(dpr > 0 ? dpr : 1.0, 0.5, 4.0);
    markers::Style st = stIn;
    // 图标码位：requestImage 传进来的是 "#cp:<十六进制>"；直接调用（测试）时是图标名
    char32_t cp = 0;
    if (st.icon.startsWith(QLatin1String("#cp:"))) cp = char32_t(st.icon.mid(4).toUInt(nullptr, 16));
    else if (markers::showsIcon(st, iconReady())) cp = msicons::codepoint(st.icon.toUtf8().constData());
    if (!cp) st.icon.clear();
    const bool icon = cp != 0 && iconReady();
    const markers::Geom g = markers::geomFor(st, icon);
    const double P = kPad;
    QImage fg = blank(g.w + 2 * P, g.h + 2 * P, dpr);
    const QColor color = cssColor(st.color);
    const QColor ink = wish ? color : QColor(Qt::white);
    {
        QPainter p(&fg);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(dpr, dpr);
        p.translate(P, P);
        if (st.shape == QLatin1String("pin")) {
            const QPainterPath path = pinPath();
            p.setBrush(wish ? kPaper : color);
            p.setPen(draft ? draftPen(wish ? color : kPaper, wish ? 2.4 : 2.0) : QPen(wish ? color : kPaper, wish ? 2.4 : 2.0));
            p.drawPath(path);
            const QPointF c(14, 14);   // .mk-in：left 0、top 4、28×20
            if (icon) drawIcon(p, cp, 15, c, ink);
            else if (!st.text.isEmpty()) drawCentered(p, textFont(), st.text, c, ink);
            else { p.setPen(Qt::NoPen); p.setBrush(wish ? color : QColor(Qt::white)); p.drawEllipse(c, 4.8, 4.8); }
        } else {
            const double s = g.w;
            const QRectF box(0, 0, s, s);
            const QColor fill = wish ? kPaper : color;
            const QColor border = wish ? color : kPaper;
            const double bw = wish ? 2.5 : 2.0;
            const QPointF c(s / 2, s / 2);
            double contentScale = 1;
            if (st.shape == QLatin1String("diamond")) {
                // transform: rotate(45deg) scale(.84)；圆角 4px；里面的内容转回来（缩放留着）
                p.save();
                p.translate(c);
                p.rotate(45);
                p.scale(0.84, 0.84);
                p.translate(-c);
                boxShape(p, box, 4, bw, border, fill);
                p.restore();
                contentScale = 0.84;
            } else {
                const double radius = st.shape == QLatin1String("square") ? 5 : s / 2;   // 认不出的外形按圆形（.mk-<外形> 没有样式时是方角，极少见）
                boxShape(p, box, radius, bw, border, fill);
            }
            p.save();
            p.translate(c);
            p.scale(contentScale, contentScale);
            p.translate(-c);
            if (icon) drawIcon(p, cp, 16, c, ink);
            else if (!st.text.isEmpty()) drawCentered(p, textFont(), st.text, c, ink);
            p.restore();
        }
    }
    // 阴影：水滴 drop-shadow(0 2px 2px rgba(60,45,25,.3))（σ = 1）；其他 box-shadow 0 1px 3px rgba(60,45,25,.4)（σ = 1.5）
    const bool pin = st.shape == QLatin1String("pin");
    QImage sh;
    if (pin) {
        sh = shadowOf(fg, QColor(60, 45, 25, int(0.3 * 255)), 0, 2 * dpr, 1.0 * dpr);
    } else {
        // box-shadow 只取边框盒的形状（不含文字）
        QImage shape = blank(g.w + 2 * P, g.h + 2 * P, dpr);
        QPainter p(&shape);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.translate(P, P);
        const double s = g.w;
        if (st.shape == QLatin1String("diamond")) {
            p.translate(s / 2, s / 2); p.rotate(45); p.scale(0.84, 0.84); p.translate(-s / 2, -s / 2);
            boxShape(p, QRectF(0, 0, s, s), 4, 0, Qt::black, Qt::black);
        } else {
            boxShape(p, QRectF(0, 0, s, s), st.shape == QLatin1String("square") ? 5 : s / 2, 0, Qt::black, Qt::black);
        }
        p.end();
        sh = shadowOf(shape, QColor(60, 45, 25, int(0.4 * 255)), 0, 1 * dpr, 1.5 * dpr);
    }
    return compose(sh, fg);
}

QImage MarkerImages::paintCluster(int size, const QString &bgName, const QString &fgName, int n, double dpr)
{
    dpr = std::clamp(dpr > 0 ? dpr : 1.0, 0.5, 4.0);
    const double P = kPad, s = size;
    const QColor bg = cssColor(bgName), fgc = cssColor(fgName);
    QImage fg = blank(s + 2 * P, s + 2 * P, dpr);
    QImage shape = blank(s + 2 * P, s + 2 * P, dpr);
    QImage textImg = blank(s + 2 * P, s + 2 * P, dpr);
    {
        QPainter p(&fg);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.translate(P, P);
        boxShape(p, QRectF(0, 0, s, s), s / 2, 2.5, kPaper, bg);   // border 2.5px #fffdf8（box-sizing: border-box）
    }
    {
        QPainter p(&shape);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.translate(P, P);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawEllipse(QRectF(0, 0, s, s));
    }
    {
        QPainter p(&textImg);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(dpr, dpr);
        p.translate(P, P);
        drawCentered(p, textFont(), QString::number(n), QPointF(s / 2, s / 2), fgc);
    }
    // box-shadow: 0 0 0 1px rgba(60,45,25,.15)（外面一圈 1px）, 0 2px 6px rgba(60,45,25,.35)（σ = 3）
    QImage out = shadowOf(shape, QColor(60, 45, 25, int(0.35 * 255)), 0, 2 * dpr, 3 * dpr);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        p.save();
        p.scale(dpr, dpr);
        p.translate(P, P);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 45, 25, int(0.15 * 255)));
        QPainterPath ring;
        ring.addEllipse(QRectF(-1, -1, s + 2, s + 2));
        ring.addEllipse(QRectF(0, 0, s, s));
        p.drawPath(ring);
        p.restore();
        p.drawImage(0, 0, fg);
        // text-shadow: 0 0 2px rgba(0,0,0,.35)（σ = 1）
        p.drawImage(0, 0, shadowOf(textImg, QColor(0, 0, 0, int(0.35 * 255)), 0, 0, 1 * dpr));
        p.drawImage(0, 0, textImg);
    }
    return out;
}

QImage MarkerImages::paintGlow(double w, double h, bool pin, double dpr)
{
    dpr = std::clamp(dpr > 0 ? dpr : 1.0, 0.5, 4.0);
    // .mk-focus::after：inset -16px（水滴 -12 -16 -4）；radial-gradient(circle, …) 默认半径 = 到最远的角；border-radius 50%（椭圆）
    const double bw = w + 32, bh = pin ? h + 16 : h + 32;
    QImage img = blank(bw, bh, dpr);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(dpr, dpr);
    const QPointF c(bw / 2, bh / 2);
    const double r = std::hypot(bw / 2, bh / 2);
    QRadialGradient grad(c, r);
    grad.setColorAt(0.0, QColor(255, 255, 255, int(0.95 * 255)));
    grad.setColorAt(0.26, QColor(255, 255, 255, int(0.95 * 255)));
    grad.setColorAt(0.40, QColor(255, 255, 255, int(0.6 * 255)));
    grad.setColorAt(0.56, QColor(20, 40, 60, int(0.28 * 255)));
    grad.setColorAt(0.72, QColor(20, 40, 60, 0));
    grad.setColorAt(1.0, QColor(20, 40, 60, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawEllipse(QRectF(0, 0, bw, bh));
    return img;
}

} // namespace tp::map
