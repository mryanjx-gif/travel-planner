#include "itinerary.h"

#include "planpage.h"
#include "plans.h"
#include "roads.h"
#include "translator.h"

#include <QFile>
#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace tp::exporter {

using json::Value;
namespace plans = tp::map::plans;
namespace roads = tp::map::roads;
namespace geo = tp::map::geo;

namespace {

// ---- A4 ----
constexpr double kMm = kContentW / (210.0 - 2 * 12.7);   // 每毫米多少排版像素（边距 0.5in = 12.7mm）
constexpr double kMargin = 12.7 * kMm;
constexpr double kFootH = 30;
constexpr double kPngTop = 28, kPngBottom = 36;

const QColor kText(0x2b, 0x26, 0x20), kMuted(0x6d, 0x64, 0x58), kLine(0xee, 0xee, 0xee), kFoot(0x9a, 0x90, 0x86);
const QColor kRed(0xc0, 0x39, 0x2b);

QString str(const Value &o, const char *k) { return o[QString::fromLatin1(k)].toString(); }
double num(const Value &o, const char *k) { return o[QString::fromLatin1(k)].jsToNumber(); }

QFont fontOf(const Options &o, int px, bool bold = false)
{
    QFont f;
    if (!o.font.isEmpty()) f.setFamily(o.font);
    f.setPixelSize(px);
    f.setBold(bold);
    f.setHintingPreference(QFont::PreferNoHinting);   // 量的时候（屏幕）和画的时候（PDF）字宽一样
    return f;
}

// 一段文字：排好行，记住高度；画的时候用同一份排版（换行位置不会因为设备不同而变）
struct Para {
    std::shared_ptr<QTextLayout> lay;
    double h = 0;
    void draw(QPainter &p, double x, double y, const QColor &c) const
    {
        if (!lay) return;
        p.setPen(c);
        lay->draw(&p, QPointF(x, y));
    }
};
Para para(const QString &text0, const QFont &f, double w, Qt::Alignment align = Qt::AlignLeft)
{
    if (text0.isEmpty()) return {};
    QString text = text0;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\n'), QChar::LineSeparator);
    auto lay = std::make_shared<QTextLayout>(text, f);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setAlignment(align);
    lay->setTextOption(opt);
    lay->setCacheEnabled(true);
    lay->beginLayout();
    double y = 0;
    for (;;) {
        QTextLine line = lay->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(w);
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    lay->endLayout();
    return { lay, y };
}

QString range(const QString &a, const QString &b) { return !a.isEmpty() && !b.isEmpty() && a != b ? a + QStringLiteral(" → ") + b : a.isEmpty() ? b : a; }

QString kindName(const QString &k)
{
    if (k == QLatin1String("flight")) return tp::tr("机票");
    if (k == QLatin1String("train")) return tp::tr("火车");
    if (k == QLatin1String("bus")) return tp::tr("大巴");
    if (k == QLatin1String("ferry")) return tp::tr("船票");
    if (k == QLatin1String("car")) return tp::tr("租车");
    if (k == QLatin1String("hotel")) return tp::tr("住宿");
    if (k == QLatin1String("ticket")) return tp::tr("门票");
    return tp::tr("其他");
}
QString catName(const QString &c)
{
    if (c == QLatin1String("transport")) return tp::tr("交通");
    if (c == QLatin1String("stay")) return tp::tr("住宿");
    if (c == QLatin1String("food")) return tp::tr("餐饮");
    if (c == QLatin1String("ticket")) return tp::tr("门票");
    if (c == QLatin1String("shop")) return tp::tr("购物");
    return tp::tr("其他");
}
QString localLabel(const QString &l)
{
    if (l == QLatin1String("报警")) return tp::tr("报警");
    if (l == QLatin1String("急救")) return tp::tr("急救");
    if (l == QLatin1String("消防")) return tp::tr("消防");
    if (l == QLatin1String("使领馆 / 领事保护")) return tp::tr("使领馆 / 领事保护");
    if (l == QLatin1String("其他")) return tp::tr("其他");
    return l;   // 旧数据导入的一段文字
}
QString money(double v, const QString &cur) { return cur + QLatin1Char(' ') + QLocale(QLocale::English).toString(v, 'f', 2); }

// ---- 封面的路线简图 ----
struct World {
    QVector<QPolygonF> rings;   // 省界多边形（经纬度），偶奇填充
    QVector<QRectF> ringBox;
    QVector<QPolygonF> lines;   // 国界线
    QVector<QRectF> lineBox;
};
void addLines(const QJsonArray &coords, QVector<QPolygonF> &out, QVector<QRectF> &box)
{
    QPolygonF pl;
    pl.reserve(coords.size());
    for (const QJsonValue &c : coords) {
        const QJsonArray xy = c.toArray();
        pl << QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
    }
    if (pl.size() < 2) return;
    box << pl.boundingRect();
    out << pl;
}
const World &world(const QString &dir)
{
    static QHash<QString, std::shared_ptr<World>> cache;   // 进程里读一次（5 MB 的文字，约零点几秒）
    auto &w = cache[dir];
    if (w) return *w;
    w = std::make_shared<World>();
    if (dir.isEmpty()) return *w;
    const auto read = [&dir](const char *name) {
        QFile f(dir + QLatin1Char('/') + QLatin1String(name));
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("features")).toArray() : QJsonArray();
    };
    for (const QJsonValue &fv : read("adm1.geojson")) {
        const QJsonObject g = fv.toObject().value(QStringLiteral("geometry")).toObject();
        const QString type = g.value(QStringLiteral("type")).toString();
        const QJsonArray c = g.value(QStringLiteral("coordinates")).toArray();
        if (type == QLatin1String("Polygon")) for (const QJsonValue &r : c) addLines(r.toArray(), w->rings, w->ringBox);
        else if (type == QLatin1String("MultiPolygon")) for (const QJsonValue &poly : c) for (const QJsonValue &r : poly.toArray()) addLines(r.toArray(), w->rings, w->ringBox);
    }
    for (const QJsonValue &fv : read("adm0.geojson")) {
        const QJsonObject g = fv.toObject().value(QStringLiteral("geometry")).toObject();
        const QString type = g.value(QStringLiteral("type")).toString();
        const QJsonArray c = g.value(QStringLiteral("coordinates")).toArray();
        if (type == QLatin1String("LineString")) addLines(c, w->lines, w->lineBox);
        else if (type == QLatin1String("MultiLineString")) for (const QJsonValue &l : c) addLines(l.toArray(), w->lines, w->lineBox);
    }
    return *w;
}

double mercY(double lat)
{
    const double l = std::clamp(lat, -82.0, 82.0) * M_PI / 180;
    return std::log(std::tan(M_PI / 4 + l / 2));
}
double latOf(double y) { return (2 * std::atan(std::exp(y)) - M_PI / 2) * 180 / M_PI; }

struct Sketch {
    QVector<QPointF> stops;              // 经纬度（x = lng，y = lat）
    QVector<QVector<QPointF>> legs;      // 每段的点
    QVector<bool> flight;                // 这一段是飞机（画虚线）
    QColor color;
    bool route = false;
};
Sketch sketchOf(const Value &plan)
{
    Sketch s;
    s.color = QColor(plans::planColorOf(plan));
    s.route = plans::typeOf(plan) == QLatin1String("route");
    const json::Array &arr = plan[QStringLiteral("stops")].asArray();
    const QVector<int> idx = plans::validStops(plan);
    for (int i = 0; i < idx.size(); ++i) {
        const Value &st = arr[size_t(idx[i])];
        s.stops << QPointF(num(st, "lng"), num(st, "lat"));
    }
    if (!s.route) { if (s.stops.size() > 1) s.stops.resize(1); return s; }
    for (int i = 0; i + 1 < idx.size(); ++i) {
        const Value &a = arr[size_t(idx[i])], &leg = a[QStringLiteral("leg")];
        const QString mode = roads::legModeOf(str(leg, "mode"));
        QVector<QPointF> pts{ s.stops[i], s.stops[i + 1] };
        if (roads::isRoadMode(mode) && !str(leg, "path").isEmpty()
            && str(leg, "key") == roads::legKeyOf(mode, s.stops[i].y(), s.stops[i].x(), s.stops[i + 1].y(), s.stops[i + 1].x())) {
            pts.clear();
            for (const geo::LatLng &p : roads::decodePolyline(str(leg, "path"))) pts << QPointF(p.lng, p.lat);
        }
        s.legs << pts;
        s.flight << (mode == QLatin1String("flight"));
    }
    return s;
}

void paintSketch(QPainter &p, const QRectF &r, const Sketch &s, const QString &adminDir, const QFont &numFont)
{
    // 视野：所有点的范围（墨卡托），四周留白，补成和框一样的长宽比；至少 3°（单一目的地）
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    const auto take = [&](const QPointF &q) { x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x()); y0 = std::min(y0, mercY(q.y())); y1 = std::max(y1, mercY(q.y())); };
    for (const QPointF &q : s.stops) take(q);
    for (const auto &l : s.legs) for (const QPointF &q : l) take(q);
    // 墨卡托 y 换成"经度度数"同一尺度：y 的弧度 × 180/π
    const double k = 180 / M_PI;
    y0 *= k; y1 *= k;
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    double w = std::max(3.0, (x1 - x0) * 1.3), h = std::max(3.0, (y1 - y0) * 1.3);
    // 点只落在内框里：四周按圆点半径（10 / 12）+ 白边和余量留出来，最边上的站不贴框
    const double rad = s.route ? 10 : 12, pad = rad + 8;
    const QRectF in = r.adjusted(pad, pad, -pad, -pad);
    const double aspect = in.width() / in.height();
    if (w / h < aspect) w = h * aspect; else h = w / aspect;
    // ponytail: 跨 180° 经线的路线按经度直接取范围（会画得很宽）；真有这种计划再按 unwrap 处理
    w = std::min(w, 360.0);
    h = w / aspect;
    const auto proj = [&](const QPointF &q) {
        return QPointF(in.left() + (q.x() - (cx - w / 2)) / w * in.width(), in.top() + ((cy + h / 2) - mercY(q.y()) * k) / h * in.height());
    };
    // 底图要铺满整个框（含边距），裁剪范围按外框放大
    const double vw = w * r.width() / in.width(), vh = h * r.height() / in.height();
    const QRectF view(cx - vw / 2, latOf((cy - vh / 2) / k), vw, latOf((cy + vh / 2) / k) - latOf((cy - vh / 2) / k));

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(r, 12, 12);
    p.setClipPath(clip);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(r, QColor(0xe8, 0xe2, 0xd6));   // 和"已解锁"底图一样：海 #e8e2d6、陆地白、省界 #c9c2b8、国界 #7d746a
    const World &wd = world(adminDir);
    QPainterPath land;
    land.setFillRule(Qt::OddEvenFill);
    for (int i = 0; i < wd.rings.size(); ++i) {
        if (!wd.ringBox[i].intersects(view)) continue;
        QPolygonF pg;
        pg.reserve(wd.rings[i].size());
        for (const QPointF &q : wd.rings[i]) pg << proj(q);
        land.addPolygon(pg);
        land.closeSubpath();
    }
    p.fillPath(land, Qt::white);
    p.strokePath(land, QPen(QColor(0xc9, 0xc2, 0xb8), 0.6));
    QPen border(QColor(0x7d, 0x74, 0x6a), 1.0);
    p.setPen(border);
    for (int i = 0; i < wd.lines.size(); ++i) {
        if (!wd.lineBox[i].intersects(view)) continue;
        QPolygonF pl;
        for (const QPointF &q : wd.lines[i]) pl << proj(q);
        p.drawPolyline(pl);
    }
    // 路线
    for (int i = 0; i < s.legs.size(); ++i) {
        QPen pen(s.color, 2.4, s.flight[i] ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        QPolygonF pl;
        for (const QPointF &q : s.legs[i]) pl << proj(q);
        p.drawPolyline(pl);
    }
    // 站点：带序号的圆点（和下面的列表对得上）
    // 太密时只标首尾，中间的按顺序挑和已标的不重叠的（其余画成小点），编号不互相盖住
    const int n = int(s.stops.size());
    QVector<QPointF> pc(n);
    for (int i = 0; i < n; ++i) pc[i] = proj(s.stops[i]);
    QVector<bool> big(n, false);
    QVector<int> picked;
    const auto tryPick = [&](int i) {
        for (int j : picked) if (QLineF(pc[i], pc[j]).length() < 2 * rad + 2) return;
        big[i] = true;
        picked << i;
    };
    if (n) { big[0] = true; picked << 0; }
    if (n > 1) { big[n - 1] = true; picked << n - 1; }   // 终点总标
    for (int i = 1; i < n - 1; ++i) tryPick(i);
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(s.color);
    for (int i = 0; i < n; ++i) if (!big[i]) p.drawEllipse(pc[i], 4, 4);
    p.setFont(numFont);
    for (int i = n - 1; i >= 0; --i) {
        if (!big[i]) continue;
        const QPointF c = pc[i];
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(s.color);
        p.drawEllipse(c, rad, rad);
        p.setPen(Qt::white);
        p.drawText(QRectF(c.x() - rad, c.y() - rad, 2 * rad, 2 * rad), Qt::AlignCenter, s.route ? QString::number(i + 1) : QStringLiteral("●"));
    }
    p.restore();
}

// ---- 块的拼装 ----
struct Builder {
    const Options &o;
    QVector<Doc::Block> blocks;
    int group = 0;

    void add(double h, std::function<void(QPainter &, double)> f, int g = -1, bool keepNext = false) { blocks.push_back({ h, g, keepNext, std::move(f) }); }
    void space(double h) { add(h, [](QPainter &, double) {}); }
    void h2(const QString &text, const QColor &line, double top = 22, double lineW = 1.5)
    {
        const Para t = para(text, fontOf(o, 15, true), kContentW);
        add(top + t.h + 4 + lineW + 8, [t, line, top, lineW](QPainter &p, double y) {
            t.draw(p, 0, y + top, kText);
            p.fillRect(QRectF(0, y + top + t.h + 4, kContentW, lineW), line);
        }, -1, true);
    }
    // 两列的一行（紧急信息）：左边标签（灰），右边值（粗）
    void row(const QString &label, const QString &value, int valuePx, int g)
    {
        const double lw = kContentW * 0.4;
        const Para a = para(label, fontOf(o, 14), lw - 8), b = para(value, fontOf(o, valuePx, true), kContentW - lw);
        const double h = std::max(a.h, b.h) + 10;
        add(h, [a, b, lw](QPainter &p, double y) { a.draw(p, 0, y + 5, QColor(0x55, 0x55, 0x55)); b.draw(p, lw, y + 5, QColor(0x1d, 0x1d, 0x1d)); }, g);
    }
    // 紧急信息（信息卡和行程单最后一节共用）。返回有没有内容
    bool emergency(const Value &root, const Value &plan, const QColor &accent, int valuePx, double lineW)
    {
        const Value &e = root[QStringLiteral("emergency")];
        bool any = false;
        QVector<std::pair<QString, QString>> loc;
        for (const Value &x : plan[QStringLiteral("localEmergency")].asArray())
            if (x.isObject() && !(str(x, "label").isEmpty() && str(x, "phone").isEmpty())) loc.push_back({ localLabel(str(x, "label")), str(x, "phone") });
        const auto section = [&](const QString &t, const QVector<std::pair<QString, QString>> &rows) {
            if (rows.isEmpty()) return;
            any = true;
            h2(t, accent, 18, lineW);
            const int g = group++;
            for (const auto &[a, b] : rows) row(a, b, valuePx, g);
        };
        section(tp::tr("当地紧急电话"), loc);
        QVector<std::pair<QString, QString>> ins;
        if (!str(e, "insurer").isEmpty()) ins.push_back({ tp::tr("保险公司"), str(e, "insurer") });
        if (!str(e, "policyNo").isEmpty()) ins.push_back({ tp::tr("保单号"), str(e, "policyNo") });
        if (!str(e, "insurerPhone").isEmpty()) ins.push_back({ tp::tr("24 小时救援"), str(e, "insurerPhone") });
        section(tp::tr("保险"), ins);
        QVector<std::pair<QString, QString>> cs;
        for (const Value &c : e[QStringLiteral("contacts")].asArray()) {
            if (!c.isObject() || (str(c, "name").isEmpty() && str(c, "phone").isEmpty())) continue;
            cs.push_back({ str(c, "name") + (str(c, "relation").isEmpty() ? QString() : tp::tr("（%1）").arg(str(c, "relation"))), str(c, "phone") });
        }
        section(tp::tr("紧急联系人"), cs);
        if (!str(e, "notes").isEmpty()) {
            any = true;
            h2(tp::tr("其他"), accent, 18, lineW);
            const Para n = para(str(e, "notes"), fontOf(o, 14), kContentW);
            add(n.h + 6, [n](QPainter &p, double y) { n.draw(p, 0, y + 3, kText); }, group++);
        }
        return any;
    }
};

} // namespace

double pageWidthLayout() { return 210.0 * kMm; }
double pageHeightLayout() { return 297.0 * kMm; }
double bodyHeightLayout() { return pageHeightLayout() - 2 * kMargin - kFootH; }

QVector<QVector<int>> paginate(const QVector<BlockSize> &b, double pageH)
{
    QVector<QVector<int>> pages(1);
    double y = 0;
    const int n = int(b.size());
    const auto unitEnd = [&](int i) {   // 从 i 开始的一组（同一 group 连着的块）结束的位置（不含）
        int j = i + 1;
        if (b[i].group >= 0) while (j < n && b[j].group == b[i].group) ++j;
        return j;
    };
    int i = 0;
    while (i < n) {
        int j = unitEnd(i);
        while (j < n && b[j - 1].keepNext) j = unitEnd(j);   // 小标题、表头跟着下一组
        double h = 0;
        for (int k = i; k < j; ++k) h += b[k].h;
        const auto place = [&](int k) { pages.last() << k; y += b[k].h; };
        const auto newPage = [&] { if (!pages.last().isEmpty()) { pages << QVector<int>(); y = 0; } };
        if (h <= pageH) {
            if (y > 0 && y + h > pageH) newPage();
            for (int k = i; k < j; ++k) place(k);
        } else {   // 一组比一页还高：从新的一页开始，组内按块换页
            newPage();
            for (int k = i; k < j; ++k) {
                if (y > 0 && y + b[k].h > pageH) newPage();
                place(k);
            }
        }
        i = j;
    }
    return pages;
}

QVector<BlockSize> Doc::sizes() const
{
    QVector<BlockSize> out;
    for (const Block &b : m_blocks) out.push_back({ b.h, b.group, b.keepNext });
    return out;
}

Doc Doc::itinerary(const Value &root, const Value &plan, const Options &o)
{
    Doc d;
    Builder B{ o, {}, 0 };
    const QColor color(plans::planColorOf(plan));
    const QString name = str(plan, "name");
    d.m_title = name;
    const json::Array &arr = plan[QStringLiteral("stops")].asArray();
    const QVector<int> idx = plans::validStops(plan);
    // 封面
    if (!idx.isEmpty()) {
        const Sketch sk = sketchOf(plan);
        const QFont nf = fontOf(o, 11, true);
        const QString dir = o.adminDir;
        B.add(260, [sk, nf, dir](QPainter &p, double y) { paintSketch(p, QRectF(0, y, kContentW, 260), sk, dir, nf); });
    }
    // 标题、日期
    const Para t = para(name, fontOf(o, 24, true), kContentW);
    B.add(18 + t.h + 4, [t](QPainter &p, double y) { t.draw(p, 0, y + 18, kText); }, -1, true);
    const QString sd = str(plan, "startDate"), ed = str(plan, "endDate");
    int days = 0;
    if (!sd.isEmpty()) days = plans::days(sd, ed.isEmpty() ? sd : ed) + 1;
    else days = int(num(plan, "days"));
    QStringList meta{ sd.isEmpty() ? tp::tr("还没定日期") : range(sd, ed) };
    if (days > 0) meta << (days == 1 ? tp::tr("1 天") : tp::tr("%1 天").arg(days));
    if (plans::typeOf(plan) == QLatin1String("route")) meta << (idx.size() == 1 ? tp::tr("1 站") : tp::tr("%1 站").arg(idx.size()));
    const Para m = para(meta.join(QStringLiteral(" · ")), fontOf(o, 13), kContentW);
    B.add(m.h + 14, [m](QPainter &p, double y) { m.draw(p, 0, y, kMuted); });
    if (o.notes && !str(plan, "notes").isEmpty()) {
        const Para n = para(str(plan, "notes"), fontOf(o, 13), kContentW);
        B.add(n.h, [n](QPainter &p, double y) { n.draw(p, 0, y, QColor(0x4a, 0x43, 0x3b)); });
    }
    // 行程：每站一组（不跨页）
    if (!idx.isEmpty()) B.h2(tp::tr("行程"), color);
    const QFont fName = fontOf(o, 13, true), fDate = fontOf(o, 12), fItem = fontOf(o, 13), fNo = fontOf(o, 12, true);
    for (int i = 0; i < idx.size(); ++i) {
        const Value &s = arr[size_t(idx[i])];
        const int g = B.group++;
        const double tx = 40, tw = kContentW - tx;
        const Para when = para(range(str(s, "arriveDate"), str(s, "departDate")), fDate, 200, Qt::AlignRight);
        const Para nm = para(str(s, "name"), fName, tw - (when.h > 0 ? 210 : 0));
        const double head = std::max({ nm.h, when.h, 24.0 });
        B.add(7 + head, [nm, when, color, fNo, i, tx, tw](QPainter &p, double y) {
            p.save();
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(QRectF(0, y + 7, 24, 24));
            p.setPen(Qt::white);
            p.setFont(fNo);
            p.drawText(QRectF(0, y + 7, 24, 24), Qt::AlignCenter, QString::number(i + 1));
            p.restore();
            nm.draw(p, tx, y + 7 + 2, kText);
            when.draw(p, tx + tw - 200, y + 7 + 3, kMuted);
        }, g);
        // 日程（按天、按时间排，和计划页一样）
        for (const QVariant &dv : plans::schedDays(s, {})) {
            for (const QVariant &iv : dv.toMap().value(QStringLiteral("items")).toList()) {
                const QVariantMap it = iv.toMap();
                const QString text = it.value(QStringLiteral("text")).toString();
                if (text.isEmpty()) continue;
                const QString date = it.value(QStringLiteral("date")).toString(), time = it.value(QStringLiteral("time")).toString(), end = it.value(QStringLiteral("end")).toString();
                QStringList tm;
                if (!date.isEmpty()) tm << date.mid(5);
                if (!time.isEmpty()) tm << time + (end.isEmpty() ? QString() : QStringLiteral("–") + end);
                const double tmW = tm.isEmpty() ? 0 : 104;
                const Para tp_ = para(tm.join(QLatin1Char(' ')), fDate, tmW), body = para(text, fItem, tw - 18 - tmW);
                const double h = std::max(tp_.h, body.h) + 3;
                B.add(h, [tp_, body, tx, tmW](QPainter &p, double y) {
                    p.save();
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setPen(Qt::NoPen);
                    p.setBrush(kMuted);
                    p.drawEllipse(QPointF(tx + 6, y + 11), 2, 2);
                    p.restore();
                    tp_.draw(p, tx + 18, y + 2, kMuted);
                    body.draw(p, tx + 18 + tmW, y + 1, kText);
                }, g);
            }
        }
        if (o.notes && !str(s, "notes").isEmpty()) {
            const Para n = para(str(s, "notes"), fDate, tw);
            B.add(n.h + 4, [n, tx](QPainter &p, double y) { n.draw(p, tx, y + 3, QColor(0x4a, 0x43, 0x3b)); }, g);
        }
        B.add(8, [](QPainter &p, double y) { p.fillRect(QRectF(0, y + 7, kContentW, 1), kLine); }, g);
    }
    // 交通住宿
    QVector<const Value *> bk;
    for (const Value &b : plan[QStringLiteral("bookings")].asArray()) if (b.isObject()) bk << &b;
    std::stable_sort(bk.begin(), bk.end(), [](const Value *a, const Value *b) { return str(*a, "start") < str(*b, "start"); });
    QString cur = str(plan, "currency");
    if (cur.isEmpty()) cur = o.currency.isEmpty() ? QStringLiteral("CNY") : o.currency;
    if (!bk.isEmpty()) {
        B.h2(tp::tr("交通住宿"), color);
        struct Col { QString head; double w; };
        QVector<Col> cols{ { tp::tr("类型"), 64 }, { tp::tr("名称"), 0 }, { tp::tr("路线"), 0 }, { tp::tr("时间"), 160 } };
        if (o.refs) cols.push_back({ tp::tr("订单号"), 110 });
        if (o.money) cols.push_back({ tp::tr("费用"), 100 });
        double fixed = 0;
        for (const Col &c : cols) fixed += c.w;
        for (Col &c : cols) if (c.w == 0) c.w = (kContentW - fixed) / 2;
        const QFont fh = fontOf(o, 12, true), fc = fontOf(o, 12);
        const auto rowOf = [&](const QStringList &cells, const QFont &f, const QColor &c, const QString &note, bool keepNext) {
            QVector<Para> ps;
            double h = 0;
            for (int k = 0; k < cols.size(); ++k) { ps << para(cells.value(k), f, cols[k].w - 12); h = std::max(h, ps.last().h); }
            const Para np = para(note, fontOf(o, 11), kContentW - cols[0].w - 12);
            const double total = h + 10 + (np.h > 0 ? np.h + 2 : 0);
            QVector<double> xs;
            double x = 0;
            for (const Col &cc : cols) { xs << x; x += cc.w; }
            B.add(total, [ps, xs, c, np, h, x0 = cols[0].w](QPainter &p, double y) {
                for (int k = 0; k < ps.size(); ++k) ps[k].draw(p, xs[k] + 6, y + 5, c);
                np.draw(p, x0 + 6, y + 5 + h + 2, kMuted);
                p.fillRect(QRectF(0, y + 5 + h + 5 + (np.h > 0 ? np.h + 2 : 0) - 1, kContentW, 1), kLine);
            }, -1, keepNext);
        };
        QStringList heads;
        for (const Col &c : cols) heads << c.head;
        rowOf(heads, fh, kMuted, {}, true);
        for (const Value *b : bk) {
            const QString from = str(*b, "from"), to = str(*b, "to");
            QStringList cells{ kindName(str(*b, "kind")), str(*b, "title"),
                               from.isEmpty() && to.isEmpty() ? QString() : from + QStringLiteral(" → ") + to,
                               range(str(*b, "start").replace(QLatin1Char('T'), QLatin1Char(' ')), str(*b, "end").replace(QLatin1Char('T'), QLatin1Char(' '))) };
            if (o.refs) cells << str(*b, "ref");
            if (o.money) cells << (num(*b, "cost") ? money(num(*b, "cost"), cur) : QString());
            rowOf(cells, fc, kText, o.notes ? str(*b, "notes") : QString(), false);
        }
    }
    // 消费
    if (o.money) {
        QVector<const Value *> ex;
        for (const Value &x : plan[QStringLiteral("expenses")].asArray()) if (x.isObject()) ex << &x;
        std::stable_sort(ex.begin(), ex.end(), [](const Value *a, const Value *b) { return str(*a, "date") < str(*b, "date"); });
        if (!ex.isEmpty()) {
            B.h2(tp::tr("消费"), color);
            const QFont fc = fontOf(o, 12);
            QMap<QString, double> sum;
            const auto line = [&](const QString &a, const QString &b, const QString &c, const QString &dd, bool bold, bool keepNext) {
                const QFont f = bold ? fontOf(o, 12, true) : fc;
                const Para pa = para(a, f, 84), pb = para(b, f, 70), pc = para(c, f, kContentW - 84 - 70 - 150 - 12), pd = para(dd, f, 144, Qt::AlignRight);
                const double h = std::max({ pa.h, pb.h, pc.h, pd.h }) + 10;
                B.add(h, [pa, pb, pc, pd, h, bold](QPainter &p, double y) {
                    pa.draw(p, 6, y + 5, bold ? kText : kMuted); pb.draw(p, 90, y + 5, bold ? kText : kMuted); pc.draw(p, 160, y + 5, kText); pd.draw(p, kContentW - 150, y + 5, kText);
                    p.fillRect(QRectF(0, y + h - 1, kContentW, 1), kLine);
                }, -1, keepNext);
            };
            line(tp::tr("日期"), tp::tr("分类"), tp::tr("名称"), tp::tr("金额"), true, true);
            for (const Value *x : ex) {
                const QString c = str(*x, "currency").isEmpty() ? cur : str(*x, "currency");
                sum[c] += num(*x, "amount");
                QString title = str(*x, "title");
                if (o.notes && !str(*x, "notes").isEmpty()) title += QStringLiteral(" · ") + str(*x, "notes");
                line(str(*x, "date"), catName(str(*x, "cat")), title, money(num(*x, "amount"), c), false, false);
            }
            QStringList tot;
            for (auto it = sum.cbegin(); it != sum.cend(); ++it) tot << money(it.value(), it.key());
            line({}, {}, tp::tr("合计"), tot.join(QStringLiteral("\n")), true, false);
        }
    }
    // 紧急信息
    if (o.emergency) {
        const int before = int(B.blocks.size());
        B.h2(tp::tr("紧急信息"), color);
        if (!B.emergency(root, plan, color, 14, 1)) B.blocks.resize(before);
    }
    d.m_footer = tp::tr("由「旅行计划」生成") + QStringLiteral(" · ") + o.today;
    d.m_font = o.font;
    d.m_blocks = B.blocks;
    d.m_pages = paginate(d.sizes(), bodyHeightLayout());
    return d;
}

Doc Doc::card(const Value &root, const Value &plan, const Options &o)
{
    Doc d;
    Builder B{ o, {}, 0 };
    const QString name = str(plan, "name");
    d.m_title = tp::tr("紧急信息卡") + QStringLiteral(" · ") + name;
    const Para t = para(d.m_title, fontOf(o, 22, true), kContentW);
    const Para sub = para(range(str(plan, "startDate"), str(plan, "endDate")), fontOf(o, 13), kContentW);
    B.add(t.h + 2 + sub.h + 6, [t, sub](QPainter &p, double y) { t.draw(p, 0, y, QColor(0x1d, 0x1d, 0x1d)); sub.draw(p, 0, y + t.h + 2, QColor(0x66, 0x66, 0x66)); });
    B.emergency(root, plan, kRed, 16, 2);
    d.m_footer = tp::tr("由「旅行计划」生成") + QStringLiteral(" · ") + o.today;
    d.m_font = o.font;
    d.m_blocks = B.blocks;
    d.m_pages = paginate(d.sizes(), bodyHeightLayout());
    return d;
}

void Doc::paintPage(QPainter &p, int i) const
{
    p.fillRect(QRectF(0, 0, pageWidthLayout(), pageHeightLayout()), Qt::white);
    double y = kMargin;
    p.save();
    p.translate(kMargin, 0);
    for (int k : m_pages.value(i)) {
        m_blocks[k].paint(p, y);
        y += m_blocks[k].h;
    }
    // 页脚：左边"由「旅行计划」生成 · 日期"，右边页码
    QFont f;
    if (!m_font.isEmpty()) f.setFamily(m_font);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(kFoot);
    const QRectF foot(0, pageHeightLayout() - kMargin - 16, kContentW, 16);
    p.drawText(foot, Qt::AlignLeft | Qt::AlignVCenter, m_footer);
    if (pageCount() > 1) p.drawText(foot, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1 / %2").arg(i + 1).arg(pageCount()));
    p.restore();
}

QImage Doc::renderPage(int i, int widthPx) const
{
    const double s = widthPx / pageWidthLayout();
    QImage img(widthPx, int(std::ceil(pageHeightLayout() * s)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(s, s);
    paintPage(p, i);
    return img;
}

bool Doc::writePdf(const QString &path, QString *error) const
{
    QPdfWriter w(path);
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setPageMargins(QMarginsF(0, 0, 0, 0));
    w.setResolution(300);
    w.setTitle(m_title);
    w.setCreator(QStringLiteral("TravelPlanner"));
    QPainter p;
    if (!p.begin(&w)) { if (error) *error = tp::tr("文件写不进去"); return false; }
    const double s = w.width() / pageWidthLayout();
    for (int i = 0; i < pageCount(); ++i) {
        if (i > 0) w.newPage();
        p.save();
        p.scale(s, s);
        paintPage(p, i);
        p.restore();
    }
    if (!p.end()) { if (error) *error = tp::tr("文件写不进去"); return false; }
    return true;
}

QImage Doc::longImage(bool *truncated) const
{
    const double side = (kPngW - kContentW) / 2;
    const QString more = tp::tr("后面还有内容，长图放不下了；完整的行程单请导出 PDF。");
    const double moreH = 40;
    double h = 0;
    int n = 0;
    bool cut = false;
    for (const Block &b : m_blocks) {
        if (kPngTop + h + b.h + kFootH + kPngBottom + moreH > kPngMaxH) { cut = true; break; }
        h += b.h;
        ++n;
    }
    if (truncated) *truncated = cut;
    const int H = int(std::ceil(kPngTop + h + (cut ? moreH : 0) + kFootH + kPngBottom));
    QImage img(int(kPngW), H, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.translate(side, 0);
    double y = kPngTop;
    for (int k = 0; k < n; ++k) { m_blocks[k].paint(p, y); y += m_blocks[k].h; }
    QFont f;
    if (!m_font.isEmpty()) f.setFamily(m_font);
    if (cut) {
        f.setPixelSize(13);
        p.setFont(f);
        p.setPen(kMuted);
        p.drawText(QRectF(0, y + 10, kContentW, moreH - 10), Qt::AlignLeft | Qt::TextWordWrap, more);
        y += moreH;
    }
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(kFoot);
    p.drawText(QRectF(0, y + 12, kContentW, 18), Qt::AlignLeft | Qt::AlignVCenter, m_footer);
    return img;
}

} // namespace tp::exporter
