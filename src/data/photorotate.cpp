#include "photorotate.h"

#include "atomicfile.h"
#include "exif.h"
#include "photolib.h"
#include "thumbcache.h"
#include "trdata.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QRegularExpression>
#include <QTransform>

#include <array>

namespace tp::data {

namespace {

inline quint32 be32(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint32(u[0]) << 24 | quint32(u[1]) << 16 | quint32(u[2]) << 8 | u[3]; }
inline quint32 le32(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint32(u[3]) << 24 | quint32(u[2]) << 16 | quint32(u[1]) << 8 | u[0]; }
void putBe32(QByteArray &b, quint32 v) { b.append(char(v >> 24)); b.append(char(v >> 16)); b.append(char(v >> 8)); b.append(char(v)); }
void putLe32(QByteArray &b, quint32 v) { b.append(char(v)); b.append(char(v >> 8)); b.append(char(v >> 16)); b.append(char(v >> 24)); }
void setLe32(QByteArray &b, int at, quint32 v) { b[at] = char(v); b[at + 1] = char(v >> 8); b[at + 2] = char(v >> 16); b[at + 3] = char(v >> 24); }

quint32 crc32(const QByteArray &data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t {};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data) c = table[(c ^ uchar(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// 方向对应的变换（和 thumbcache.cpp applyOrientation 一致；y 向下的图像坐标，作用在点上）
using M = std::array<int, 4>;   // [a b; c d]
M mul(const M &x, const M &y) { return { x[0] * y[0] + x[1] * y[2], x[0] * y[1] + x[1] * y[3], x[2] * y[0] + x[3] * y[2], x[2] * y[1] + x[3] * y[3] }; }
M orientMatrix(int o)
{
    const M I { 1, 0, 0, 1 }, H { -1, 0, 0, 1 }, V { 1, 0, 0, -1 }, R { 0, -1, 1, 0 };
    switch (o) {
    case 2: return H;
    case 3: return mul(R, R);
    case 4: return V;
    case 5: return mul(H, R);
    case 6: return R;
    case 7: return mul(V, R);
    case 8: return mul(R, mul(R, R));
    default: return I;
    }
}

// JPEG 里的段：[位置, 整段长度]（不含 SOS 以后）
struct Seg { int pos; int len; uchar marker; };
QList<Seg> jpegSegments(const QByteArray &j)
{
    QList<Seg> out;
    if (j.size() < 4 || uchar(j[0]) != 0xFF || uchar(j[1]) != 0xD8) return out;
    int pos = 2;
    while (pos + 4 <= j.size()) {
        if (uchar(j[pos]) != 0xFF) break;
        const uchar m = uchar(j[pos + 1]);
        if (m == 0xFF) { ++pos; continue; }
        if (m == 0xDA || m == 0xD9) { out.append({ pos, 0, m }); break; }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { pos += 2; continue; }
        const int len = (uchar(j[pos + 2]) << 8 | uchar(j[pos + 3])) + 2;
        if (len < 4 || pos + len > j.size()) break;
        out.append({ pos, len, m });
        pos += len;
    }
    return out;
}

bool isExifApp1(const QByteArray &j, const Seg &s) { return s.marker == 0xE1 && s.len >= 10 && j.mid(s.pos + 4, 6) == QByteArrayLiteral("Exif\0\0"); }

// XMP 里的 tiff:Orientation 改成 o（只改 1 个数字，长度不变）。返回改了几处
int patchXmpOrientation(QByteArray &j, int o)
{
    int n = 0;
    for (const Seg &s : jpegSegments(j)) {
        if (s.marker != 0xE1 || !j.mid(s.pos + 4, 29).startsWith("http://ns.adobe.com/xap/1.0/")) continue;
        static const QRegularExpression re(QStringLiteral("tiff:Orientation(?:=\"|>)([1-8])"));
        const QString seg = QString::fromLatin1(j.mid(s.pos, s.len));
        auto it = re.globalMatch(seg);
        while (it.hasNext()) {
            const auto m = it.next();
            j[s.pos + int(m.capturedStart(1))] = char('0' + o);
            ++n;
        }
    }
    return n;
}

QString originalPath(const QString &dataDir, const QString &rel) { return dataDir + QStringLiteral("/photos/.originals/") + rel.mid(7); }

QByteArray encode(const QImage &img, const char *fmt, int quality)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, fmt);
    if (quality >= 0) w.setQuality(quality);
    if (!w.write(img)) return {};
    return out;
}

bool webpIsLossless(const QByteArray &w)
{
    int pos = 12;
    bool vp8l = false, vp8 = false;
    while (pos + 8 <= w.size()) {
        const QByteArray cc = w.mid(pos, 4);
        const quint32 len = le32(w.constData() + pos + 4);
        if (cc == "VP8L") vp8l = true;
        if (cc == "VP8 ") vp8 = true;
        pos += 8 + int(len) + int(len & 1);
        if (len > quint32(w.size())) break;
    }
    return vp8l && !vp8;
}

} // namespace

int rotateOrientation(int orientation, int turnsCw)
{
    if (orientation < 1 || orientation > 8) orientation = 1;
    M m = orientMatrix(orientation);
    const M R = orientMatrix(6);
    for (int i = 0; i < ((turnsCw % 4) + 4) % 4; ++i) m = mul(R, m);
    for (int o = 1; o <= 8; ++o)
        if (orientMatrix(o) == m) return o;
    return 1;
}

QByteArray buildMinimalExifApp1(int orientation)
{
    QByteArray tiff("II*\0", 4);
    putLe32(tiff, 8);
    tiff.append(char(1)); tiff.append(char(0));                       // 1 项
    tiff.append(char(0x12)); tiff.append(char(0x01));                 // 0x0112 Orientation
    tiff.append(char(3)); tiff.append(char(0));                       // SHORT
    putLe32(tiff, 1);                                                 // 1 个
    tiff.append(char(orientation)); tiff.append(char(0)); tiff.append(char(0)); tiff.append(char(0));
    putLe32(tiff, 0);                                                 // 没有下一个 IFD
    QByteArray app1("\xFF\xE1", 2);
    const int len = 2 + 6 + int(tiff.size());
    app1.append(char(len >> 8)); app1.append(char(len & 0xFF));
    app1.append(QByteArrayLiteral("Exif\0\0"));
    app1.append(tiff);
    return app1;
}

QByteArray jpegWithApp1(const QByteArray &jpeg, const QByteArray &app1)
{
    const QList<Seg> segs = jpegSegments(jpeg);
    if (segs.isEmpty()) return {};
    QByteArray out = jpeg.left(2);   // SOI
    int insertAfter = 2;
    // JFIF 规定 APP0 放最前面：有 APP0 时插在它后面
    if (segs.first().marker == 0xE0) { out.append(jpeg.mid(segs.first().pos, segs.first().len)); insertAfter = segs.first().pos + segs.first().len; }
    out.append(app1);
    int pos = insertAfter;
    for (const Seg &s : segs) {
        if (s.pos < insertAfter) continue;
        if (s.len == 0) break;   // SOS：后面整个照搬
        if (isExifApp1(jpeg, s)) { out.append(jpeg.mid(pos, s.pos - pos)); pos = s.pos + s.len; continue; }
    }
    out.append(jpeg.mid(pos));
    return out;
}

QByteArray tiffForRewrite(const QByteArray &tiff, qint64 orientationOff, qint64 nextIfdOff, bool le)
{
    QByteArray t = tiff;
    if (orientationOff >= 0 && orientationOff + 2 <= t.size()) {
        t[int(orientationOff)] = char(le ? 1 : 0);
        t[int(orientationOff) + 1] = char(le ? 0 : 1);
    }
    if (nextIfdOff >= 0 && nextIfdOff + 4 <= t.size())
        for (int i = 0; i < 4; ++i) t[int(nextIfdOff) + i] = 0;
    return t;
}

QByteArray pngWithExif(const QByteArray &png, const QByteArray &tiff)
{
    if (!png.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1a\n"))) return {};
    int pos = 8;
    QByteArray out = png.left(8);
    bool inserted = false;
    while (pos + 12 <= png.size()) {
        const quint32 len = be32(png.constData() + pos);
        const QByteArray type = png.mid(pos + 4, 4);
        if (len > quint32(png.size() - pos - 12)) return {};
        const int total = 12 + int(len);
        if (type == "eXIf") { pos += total; continue; }   // 去掉旧的
        if (type == "IDAT" && !inserted) {
            QByteArray chunk;
            putBe32(chunk, quint32(tiff.size()));
            QByteArray body = QByteArrayLiteral("eXIf") + tiff;
            chunk.append(body);
            putBe32(chunk, crc32(body));
            out.append(chunk);
            inserted = true;
        }
        out.append(png.mid(pos, total));
        pos += total;
    }
    return inserted ? out : QByteArray();
}

QByteArray webpWithExif(const QByteArray &webp, const QByteArray &tiff, int width, int height)
{
    if (webp.size() < 20 || !webp.startsWith("RIFF") || webp.mid(8, 4) != "WEBP") return {};
    // 列出块（去掉旧的 EXIF）
    QList<QByteArray> chunks;
    int pos = 12;
    while (pos + 8 <= webp.size()) {
        const quint32 len = le32(webp.constData() + pos + 4);
        if (len > quint32(webp.size() - pos - 8)) return {};
        const int total = 8 + int(len) + int(len & 1);
        const QByteArray c = webp.mid(pos, std::min(total, int(webp.size()) - pos));
        if (c.left(4) != "EXIF") chunks << c;
        pos += total;
    }
    if (chunks.isEmpty()) return {};
    if (chunks.first().left(4) == "VP8X") {
        chunks.first()[8] = char(uchar(chunks.first()[8]) | 0x08);
    } else {
        quint8 flags = 0x08;
        const QByteArray &img = chunks.first();
        if (img.left(4) == "VP8L" && img.size() >= 13 && uchar(img[8]) == 0x2F) {
            const quint32 b = le32(img.constData() + 9);
            if (b & (1u << 28)) flags |= 0x10;   // alpha_is_used
        }
        QByteArray x("VP8X", 4);
        putLe32(x, 10);
        x.append(char(flags)); x.append(char(0)); x.append(char(0)); x.append(char(0));
        const quint32 w1 = quint32(width - 1), h1 = quint32(height - 1);
        x.append(char(w1)); x.append(char(w1 >> 8)); x.append(char(w1 >> 16));
        x.append(char(h1)); x.append(char(h1 >> 8)); x.append(char(h1 >> 16));
        chunks.prepend(x);
    }
    QByteArray e("EXIF", 4);
    putLe32(e, quint32(tiff.size()));
    e.append(tiff);
    if (tiff.size() & 1) e.append(char(0));
    chunks << e;
    QByteArray out("RIFF\0\0\0\0WEBP", 12);
    for (const QByteArray &c : chunks) out.append(c);
    setLe32(out, 4, quint32(out.size() - 8));
    return out;
}

RotateResult rotatePhoto(const QString &dataDir, const QString &rel, int turnsCw)
{
    RotateResult r;
    const QString abs = dataDir + u'/' + rel;
    if (!rel.startsWith(QLatin1String("photos/")) || !QFileInfo(abs).isFile()) { r.error = trd("照片文件找不到"); return r; }
    if (!photolib::canRotate(abs)) { r.error = trd("这种格式不能旋转"); return r; }
    turnsCw = ((turnsCw % 4) + 4) % 4;
    if (turnsCw == 0) { r.ok = true; return r; }

    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly)) { r.error = trd("照片打不开：%1").arg(f.errorString()); return r; }
    const QByteArray orig = f.readAll();
    f.close();
    const exif::Info info = exif::readBytes(orig);

    QByteArray out;
    if (info.container == exif::Container::Jpeg) {
        if (info.orientationPos >= 0 && info.orientationPos + 2 <= orig.size()) {
            // 只改方向标记
            const int o = rotateOrientation(info.orientation, turnsCw);
            out = orig;
            const int p = int(info.orientationPos);
            out[p] = char(info.littleEndian ? o : 0);
            out[p + 1] = char(info.littleEndian ? 0 : o);
            patchXmpOrientation(out, o);
            r.lossless = true;
        } else if (!info.hasExif) {
            // 没有 EXIF：补一段只含方向的
            const int o = rotateOrientation(1, turnsCw);
            out = jpegWithApp1(orig, buildMinimalExifApp1(o));
            patchXmpOrientation(out, o);
            r.lossless = true;
        } else {
            // 有 EXIF 但没有方向标签：重新压缩，原 EXIF 写回（去掉嵌入小图）
            QString err;
            QImage img = loadOriented(abs, 0, false, &err);
            if (img.isNull()) { r.error = err; return r; }
            img = img.transformed(QTransform().rotate(90.0 * turnsCw));
            const QByteArray enc = encode(img, "jpeg", 95);
            if (enc.isEmpty()) { r.error = trd("照片写不出来"); return r; }
            const QByteArray tiff = tiffForRewrite(orig.mid(int(info.tiffPos), int(info.tiffLen)), -1,
                                                   info.nextIfdPos >= 0 ? info.nextIfdPos - info.tiffPos : -1, info.littleEndian);
            QByteArray app1("\xFF\xE1", 2);
            const int len = 2 + 6 + int(tiff.size());
            if (len > 0xFFFF) { out = enc; }
            else {
                app1.append(char(len >> 8)); app1.append(char(len & 0xFF));
                app1.append(QByteArrayLiteral("Exif\0\0"));
                app1.append(tiff);
                out = jpegWithApp1(enc, app1);
            }
        }
    } else {
        QString err;
        QImage img = loadOriented(abs, 0, false, &err);
        if (img.isNull()) { r.error = err; return r; }
        img = img.transformed(QTransform().rotate(90.0 * turnsCw));
        QByteArray tiff;
        if (info.hasExif && info.tiffPos >= 0 && info.tiffLen > 0)
            tiff = tiffForRewrite(orig.mid(int(info.tiffPos), int(info.tiffLen)),
                                  info.orientationPos >= 0 ? info.orientationPos - info.tiffPos : -1,
                                  info.nextIfdPos >= 0 ? info.nextIfdPos - info.tiffPos : -1, info.littleEndian);
        if (info.container == exif::Container::Png) {
            out = encode(img, "png", -1);
            r.lossless = true;
            if (!out.isEmpty() && !tiff.isEmpty()) out = pngWithExif(out, tiff);
        } else {
            const bool lossless = webpIsLossless(orig);
            out = encode(img, "webp", lossless ? 100 : 95);
            r.lossless = lossless;
            if (!out.isEmpty() && !tiff.isEmpty()) out = webpWithExif(out, tiff, img.width(), img.height());
        }
        if (out.isEmpty()) { r.error = trd("照片写不出来（这台电脑可能缺 %1 的图片插件）").arg(exif::containerName(info.container)); return r; }
    }
    if (out.isEmpty()) { r.error = trd("照片写不出来"); return r; }

    // 第一次旋转：备份原图
    const QString bak = originalPath(dataDir, rel);
    if (!QFileInfo::exists(bak)) {
        if (!QDir().mkpath(QFileInfo(bak).absolutePath())) { r.error = trd("建不了文件夹 %1").arg(QFileInfo(bak).absolutePath()); return r; }
        const platform::WriteResult w = platform::writeFile(bak, orig);
        if (!w.ok) { r.error = trd("原图备份失败：%1").arg(w.error); return r; }
        r.backedUp = true;
    }
    const QString tmp = abs + QStringLiteral(".tmp-") + QString::number(QDateTime::currentMSecsSinceEpoch());
    const platform::WriteResult w = platform::writeViaTemp(abs, tmp, out);
    if (!w.ok) { r.error = w.error; return r; }
    QFile back(abs);
    if (!back.open(QIODevice::ReadOnly) || back.readAll() != out) { r.error = trd("写完回读不一致，原图在 .originals 里"); return r; }
    const exif::Info after = exif::readBytes(out);
    r.width = after.displayWidth();
    r.height = after.displayHeight();
    r.ok = true;
    return r;
}

} // namespace tp::data
