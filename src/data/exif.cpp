#include "exif.h"

#include <QBuffer>
#include <QDate>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cstring>

namespace tp::data::exif {

namespace {

// ---------- 读文件的小工具：只按位置读，越界返回 false ----------
class Src
{
public:
    explicit Src(QIODevice *d) : m_d(d), m_size(d ? d->size() : 0) {}
    qint64 size() const { return m_size; }
    bool read(qint64 pos, qint64 n, QByteArray &out) const
    {
        if (!m_d || pos < 0 || n < 0 || pos > m_size || n > m_size - pos) return false;
        if (!m_d->seek(pos)) return false;
        out = m_d->read(n);
        return out.size() == n;
    }
    QByteArray readUpTo(qint64 pos, qint64 n) const
    {
        if (!m_d || pos < 0 || pos >= m_size) return {};
        n = std::min(n, m_size - pos);
        if (!m_d->seek(pos)) return {};
        return m_d->read(n);
    }

private:
    QIODevice *m_d;
    qint64 m_size;
};

inline quint32 be32(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint32(u[0]) << 24 | quint32(u[1]) << 16 | quint32(u[2]) << 8 | u[3]; }
inline quint16 be16(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint16(u[0] << 8 | u[1]); }
inline quint32 le32(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint32(u[3]) << 24 | quint32(u[2]) << 16 | quint32(u[1]) << 8 | u[0]; }
inline quint16 le16(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint16(u[1] << 8 | u[0]); }
inline quint32 le24(const char *p) { const auto *u = reinterpret_cast<const uchar *>(p); return quint32(u[2]) << 16 | quint32(u[1]) << 8 | u[0]; }

constexpr qint64 kMaxExif = 4 * 1024 * 1024;   // EXIF 块最多读 4 MB（正常几十 KB）
constexpr int kMaxSteps = 4096;                // 每种格式最多看这么多段 / 块，防止坏文件绕圈

// 去掉 EXIF "Exif\0\0" 前缀（PNG / WebP 的块里有的程序会带上）
QByteArray stripExifHeader(const QByteArray &b, int *skipped)
{
    *skipped = 0;
    if (b.startsWith(QByteArrayLiteral("Exif\0\0"))) { *skipped = 6; return b.mid(6); }
    return b;
}

// ---------- TIFF ----------
class Tiff
{
public:
    Tiff(const QByteArray &b, bool le) : m_b(b), m_le(le) {}
    qint64 size() const { return m_b.size(); }
    bool in(quint64 off, quint64 n) const { return off <= quint64(m_b.size()) && n <= quint64(m_b.size()) - off; }
    quint16 u16(quint64 off) const { return in(off, 2) ? (m_le ? le16(m_b.constData() + off) : be16(m_b.constData() + off)) : 0; }
    quint32 u32(quint64 off) const { return in(off, 4) ? (m_le ? le32(m_b.constData() + off) : be32(m_b.constData() + off)) : 0; }
    const QByteArray &bytes() const { return m_b; }

    struct Entry
    {
        quint16 tag = 0, type = 0;
        quint32 count = 0;
        quint64 entryOff = 0;   // 这一项在 TIFF 里的位置
        quint64 dataOff = 0;    // 值的位置（4 字节以内时就在项里）
        bool ok = false;        // 值的范围在 TIFF 里
    };

    static int typeSize(quint16 t)
    {
        switch (t) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
        }
    }

    Entry entry(quint64 off) const
    {
        Entry e;
        if (!in(off, 12)) return e;
        e.entryOff = off;
        e.tag = u16(off);
        e.type = u16(off + 2);
        e.count = u32(off + 4);
        const int ts = typeSize(e.type);
        if (ts == 0) return e;
        const quint64 bytes = quint64(e.count) * quint64(ts);
        e.dataOff = bytes <= 4 ? off + 8 : u32(off + 8);
        e.ok = bytes > 0 && in(e.dataOff, bytes);
        return e;
    }

    // 第 i 个数（整数类型、有理数、浮点都转成 double）；读不了返回 NaN
    double num(const Entry &e, quint32 i = 0) const
    {
        if (!e.ok || i >= e.count) return kNaN;
        const quint64 o = e.dataOff + quint64(i) * quint64(typeSize(e.type));
        switch (e.type) {
        case 1: case 7: return double(uchar(m_b[qsizetype(o)]));
        case 6: return double(qint8(m_b[qsizetype(o)]));
        case 3: return double(u16(o));
        case 8: return double(qint16(u16(o)));
        case 4: return double(u32(o));
        case 9: return double(qint32(u32(o)));
        case 5: { const quint32 n = u32(o), d = u32(o + 4); return d ? double(n) / double(d) : kNaN; }
        case 10: { const qint32 n = qint32(u32(o)), d = qint32(u32(o + 4)); return d ? double(n) / double(d) : kNaN; }
        case 11: { const quint32 v = u32(o); float f; std::memcpy(&f, &v, 4); return std::isfinite(f) ? double(f) : kNaN; }
        case 12: {
            quint64 v = m_le ? (quint64(u32(o + 4)) << 32 | u32(o)) : (quint64(u32(o)) << 32 | u32(o + 4));
            double d; std::memcpy(&d, &v, 8); return std::isfinite(d) ? d : kNaN;
        }
        default: return kNaN;
        }
    }

    QString str(const Entry &e) const
    {
        if (!e.ok || (e.type != 2 && e.type != 7 && e.type != 1)) return {};
        QByteArray raw = m_b.mid(qsizetype(e.dataOff), qsizetype(std::min<quint32>(e.count, 1024)));
        const int nul = raw.indexOf('\0');
        if (nul >= 0) raw.truncate(nul);
        QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        QString s = utf8.decode(raw);
        if (utf8.hasError()) s = QString::fromLatin1(raw);
        return s.trimmed();
    }

private:
    QByteArray m_b;
    bool m_le;
};

// IFD 里的项：tag → Entry；nextPtr = 末尾"下一个 IFD"指针的位置（没有时 0）
struct Ifd
{
    QHash<quint16, Tiff::Entry> tags;
    quint64 nextPtrOff = 0;
    bool ok = false;
};

Ifd readIfd(const Tiff &t, quint64 off, QSet<quint64> &seen)
{
    Ifd r;
    if (off < 8 || seen.contains(off) || !t.in(off, 2)) return r;
    seen.insert(off);
    const quint16 n = t.u16(off);
    if (n == 0 || n > 1000) return r;
    for (quint16 i = 0; i < n; ++i) {
        const quint64 eo = off + 2 + quint64(i) * 12;
        if (!t.in(eo, 12)) break;   // 截断的 IFD：用读到的部分
        const Tiff::Entry e = t.entry(eo);
        if (!r.tags.contains(e.tag)) r.tags.insert(e.tag, e);
    }
    const quint64 np = off + 2 + quint64(n) * 12;
    if (t.in(np, 4)) r.nextPtrOff = np;
    r.ok = true;
    return r;
}

bool validDateTime(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{4}):(\\d{2}):(\\d{2})[ T](\\d{2}):(\\d{2})"));
    const QRegularExpressionMatch m = re.match(s);
    return m.hasMatch() && m.captured(1) != QLatin1String("0000");
}

// ---------- 各种文件：找到 EXIF 块、读尺寸 ----------

void readJpeg(const Src &src, Info &info)
{
    qint64 pos = 2;
    for (int step = 0; step < kMaxSteps && pos + 4 <= src.size(); ++step) {
        QByteArray h;
        if (!src.read(pos, 2, h)) return;
        if (uchar(h[0]) != 0xFF) return;   // 不是段标记：文件坏了
        const uchar marker = uchar(h[1]);
        if (marker == 0xFF) { ++pos; continue; }   // 填充字节
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { pos += 2; continue; }   // 没有长度的标记
        if (marker == 0xD9 || marker == 0xDA) return;   // EOI / SOS：后面是图像数据
        QByteArray l;
        if (!src.read(pos + 2, 2, l)) return;
        const int len = be16(l.constData());
        if (len < 2) return;
        if (marker == 0xE1 && !info.hasExif && len >= 8) {
            QByteArray seg;
            if (src.read(pos + 4, len - 2, seg) && seg.startsWith(QByteArrayLiteral("Exif\0\0"))) {
                info.exifSegmentPos = pos;
                info.exifSegmentLen = len + 2;
                parseTiff(seg.mid(6), pos + 10, info);
            }
        } else if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC && info.width == 0) {
            QByteArray sof;   // SOFn：精度(1) 高(2) 宽(2)
            if (src.read(pos + 4, 5, sof)) {
                info.height = be16(sof.constData() + 1);
                info.width = be16(sof.constData() + 3);
            }
        }
        pos += 2 + len;
    }
}

void readPng(const Src &src, Info &info)
{
    qint64 pos = 8;
    for (int step = 0; step < kMaxSteps && pos + 8 <= src.size(); ++step) {
        QByteArray h;
        if (!src.read(pos, 8, h)) return;
        const quint32 len = be32(h.constData());
        const QByteArray type = h.mid(4, 4);
        if (len > 0x7FFFFFFF) return;
        if (type == "IHDR" && len >= 8) {
            QByteArray d;
            if (src.read(pos + 8, 8, d)) { info.width = int(std::min<quint32>(be32(d.constData()), 1u << 30)); info.height = int(std::min<quint32>(be32(d.constData() + 4), 1u << 30)); }
        } else if (type == "eXIf" && !info.hasExif && len >= 8 && len <= kMaxExif) {
            QByteArray d;
            if (src.read(pos + 8, len, d)) {
                int skip = 0;
                const QByteArray t = stripExifHeader(d, &skip);
                parseTiff(t, pos + 8 + skip, info);
            }
        } else if (type == "IEND") {
            return;
        }
        pos += 12 + qint64(len);
    }
}

void readWebp(const Src &src, Info &info)
{
    qint64 pos = 12;
    const qint64 end = src.size();
    for (int step = 0; step < kMaxSteps && pos + 8 <= end; ++step) {
        QByteArray h;
        if (!src.read(pos, 8, h)) return;
        const QByteArray fourcc = h.left(4);
        const quint32 len = le32(h.constData() + 4);
        if (len > 0x7FFFFFFF) return;
        if (fourcc == "VP8X" && len >= 10) {
            QByteArray d;
            if (src.read(pos + 8, 10, d)) { info.width = int(le24(d.constData() + 4)) + 1; info.height = int(le24(d.constData() + 7)) + 1; }
        } else if (fourcc == "VP8 " && len >= 10 && info.width == 0) {
            QByteArray d;   // 帧头：3 字节 + 起始码 9D 01 2A + 宽(14 位) 高(14 位)
            if (src.read(pos + 8, 10, d) && uchar(d[3]) == 0x9D && uchar(d[4]) == 0x01 && uchar(d[5]) == 0x2A) {
                info.width = le16(d.constData() + 6) & 0x3FFF;
                info.height = le16(d.constData() + 8) & 0x3FFF;
            }
        } else if (fourcc == "VP8L" && len >= 5 && info.width == 0) {
            QByteArray d;   // 签名 0x2F + 14 位宽-1 + 14 位高-1
            if (src.read(pos + 8, 5, d) && uchar(d[0]) == 0x2F) {
                const quint32 b = le32(d.constData() + 1);
                info.width = int(b & 0x3FFF) + 1;
                info.height = int((b >> 14) & 0x3FFF) + 1;
            }
        } else if (fourcc == "EXIF" && !info.hasExif && len >= 8 && len <= kMaxExif) {
            QByteArray d;
            if (src.read(pos + 8, len, d)) {
                int skip = 0;
                const QByteArray t = stripExifHeader(d, &skip);
                parseTiff(t, pos + 8 + skip, info);
            }
        }
        pos += 8 + qint64(len) + (len & 1);
    }
}

// ---------- HEIF（ISOBMFF） ----------
struct Box
{
    QByteArray type;
    qint64 start = 0;     // 盒子起点
    qint64 body = 0;      // 内容起点（头后面）
    qint64 end = 0;       // 盒子终点
};

// 在 [from, to) 里按顺序列出盒子
QList<Box> listBoxes(const Src &src, qint64 from, qint64 to, int limit = 512)
{
    QList<Box> out;
    qint64 pos = from;
    while (pos + 8 <= to && out.size() < limit) {
        QByteArray h;
        if (!src.read(pos, 8, h)) break;
        quint64 size = be32(h.constData());
        Box b;
        b.type = h.mid(4, 4);
        b.start = pos;
        b.body = pos + 8;
        if (size == 1) {
            QByteArray l;
            if (!src.read(pos + 8, 8, l)) break;
            size = quint64(be32(l.constData())) << 32 | be32(l.constData() + 4);
            b.body = pos + 16;
        } else if (size == 0) {
            size = quint64(to - pos);
        }
        if (size < quint64(b.body - pos) || size > quint64(to - pos)) break;
        b.end = pos + qint64(size);
        out << b;
        pos = b.end;
    }
    return out;
}

const Box *findBox(const QList<Box> &list, const char *type)
{
    for (const Box &b : list)
        if (b.type == type) return &b;
    return nullptr;
}

quint64 readN(const QByteArray &d, qint64 &p, int bytes, bool &ok)
{
    if (bytes == 0) return 0;
    if (p < 0 || p + bytes > d.size()) { ok = false; return 0; }
    quint64 v = 0;
    for (int i = 0; i < bytes; ++i) v = v << 8 | uchar(d[qsizetype(p + i)]);
    p += bytes;
    return v;
}

void readHeif(const Src &src, Info &info)
{
    const QList<Box> top = listBoxes(src, 0, src.size());
    const Box *meta = findBox(top, "meta");
    if (!meta) return;
    // meta 是 FullBox：先 4 字节 version + flags
    const QList<Box> mk = listBoxes(src, meta->body + 4, meta->end);
    quint32 primary = 0;
    if (const Box *pitm = findBox(mk, "pitm")) {
        QByteArray d = src.readUpTo(pitm->body, std::min<qint64>(pitm->end - pitm->body, 16));
        bool ok = true; qint64 p = 0;
        const int v = int(readN(d, p, 1, ok)); p = 4;
        primary = quint32(readN(d, p, v == 0 ? 2 : 4, ok));
    }
    // iinf：找 Exif 项
    quint32 exifItem = 0;
    if (const Box *iinf = findBox(mk, "iinf")) {
        QByteArray d = src.readUpTo(iinf->body, 8);
        bool ok = true; qint64 p = 0;
        const int v = int(readN(d, p, 1, ok)); p = 4;
        p += v == 0 ? 2 : 4;
        const QList<Box> infes = listBoxes(src, iinf->body + p, iinf->end, 4096);
        for (const Box &b : infes) {
            if (b.type != "infe") continue;
            QByteArray e = src.readUpTo(b.body, std::min<qint64>(b.end - b.body, 32));
            qint64 q = 0; bool ok2 = true;
            const int ev = int(readN(e, q, 1, ok2)); q = 4;
            if (ev < 2) continue;
            const quint32 id = quint32(readN(e, q, ev == 2 ? 2 : 4, ok2));
            q += 2;   // item_protection_index
            if (ok2 && q + 4 <= e.size() && e.mid(qsizetype(q), 4) == "Exif") { exifItem = id; break; }
        }
    }
    // iloc：Exif 项的位置
    if (exifItem) {
        if (const Box *iloc = findBox(mk, "iloc")) {
            const QByteArray d = src.readUpTo(iloc->body, std::min<qint64>(iloc->end - iloc->body, 1 << 20));
            bool ok = true; qint64 p = 0;
            const int v = int(readN(d, p, 1, ok)); p = 4;
            const int b1 = int(readN(d, p, 1, ok)), b2 = int(readN(d, p, 1, ok));
            const int offSize = b1 >> 4, lenSize = b1 & 15, baseSize = b2 >> 4, idxSize = (v == 1 || v == 2) ? (b2 & 15) : 0;
            const quint32 count = quint32(readN(d, p, v < 2 ? 2 : 4, ok));
            for (quint32 i = 0; ok && i < count && i < 65536; ++i) {
                const quint32 id = quint32(readN(d, p, v < 2 ? 2 : 4, ok));
                int method = 0;
                if (v == 1 || v == 2) method = int(readN(d, p, 2, ok) & 15);
                readN(d, p, 2, ok);   // data_reference_index
                const quint64 base = readN(d, p, baseSize, ok);
                const int extents = int(readN(d, p, 2, ok));
                quint64 firstOff = 0, firstLen = 0;
                for (int k = 0; ok && k < extents; ++k) {
                    if (idxSize) readN(d, p, idxSize, ok);
                    const quint64 eo = readN(d, p, offSize, ok), el = readN(d, p, lenSize, ok);
                    if (k == 0) { firstOff = eo; firstLen = el; }
                }
                if (!ok || id != exifItem) continue;
                qint64 at = qint64(base + firstOff);
                if (method == 1) {   // 在 idat 里
                    const Box *idat = findBox(mk, "idat");
                    if (!idat) break;
                    at += idat->body;
                } else if (method != 0) {
                    break;
                }
                const qint64 len = firstLen ? qint64(firstLen) : src.size() - at;
                if (len < 8 || len > kMaxExif) break;
                QByteArray blob;
                if (!src.read(at, len, blob)) break;
                // 前 4 字节：到 TIFF 头的偏移（通常跳过 "Exif\0\0"）
                const quint32 skip = be32(blob.constData());
                if (skip > quint32(blob.size()) - 4) break;
                parseTiff(blob.mid(qsizetype(4 + skip)), at + 4 + skip, info);
                break;
            }
        }
    }
    // 尺寸：主图的 ispe，再按 irot 转
    if (const Box *iprp = findBox(mk, "iprp")) {
        const QList<Box> pk = listBoxes(src, iprp->body, iprp->end);
        const Box *ipco = findBox(pk, "ipco");
        const Box *ipma = findBox(pk, "ipma");
        if (ipco) {
            const QList<Box> props = listBoxes(src, ipco->body, ipco->end, 4096);
            QList<int> assoc;   // 主图关联的属性（从 1 开始的序号）
            if (ipma) {
                const QByteArray d = src.readUpTo(ipma->body, std::min<qint64>(ipma->end - ipma->body, 1 << 20));
                bool ok = true; qint64 p = 0;
                const int v = int(readN(d, p, 1, ok));
                const quint32 flags = quint32(readN(d, p, 3, ok));
                const quint32 n = quint32(readN(d, p, 4, ok));
                for (quint32 i = 0; ok && i < n && i < 65536; ++i) {
                    const quint32 id = quint32(readN(d, p, v < 1 ? 2 : 4, ok));
                    const int cnt = int(readN(d, p, 1, ok));
                    for (int k = 0; ok && k < cnt; ++k) {
                        const quint32 a = quint32(readN(d, p, (flags & 1) ? 2 : 1, ok));
                        const int idx = int((flags & 1) ? (a & 0x7FFF) : (a & 0x7F));
                        if (id == primary) assoc << idx;
                    }
                }
            }
            int w = 0, h = 0, rot = 0, cw = 0, ch = 0;
            for (int k = 0; k < props.size(); ++k) {
                if (!assoc.isEmpty() && !assoc.contains(k + 1)) continue;
                const Box &b = props[k];
                if (b.type == "ispe" && w == 0) {
                    const QByteArray d = src.readUpTo(b.body, 12);
                    if (d.size() == 12) { w = int(std::min<quint32>(be32(d.constData() + 4), 1u << 30)); h = int(std::min<quint32>(be32(d.constData() + 8), 1u << 30)); }
                } else if (b.type == "clap") {   // 裁切框（clean aperture）：宽、高是分数
                    const QByteArray d = src.readUpTo(b.body, 16);
                    if (d.size() == 16) {
                        const quint32 wn = be32(d.constData()), wd = be32(d.constData() + 4), hn = be32(d.constData() + 8), hd = be32(d.constData() + 12);
                        if (wd && hd && wn / wd > 0 && hn / hd > 0) { cw = int(std::min<quint32>(wn / wd, 1u << 30)); ch = int(std::min<quint32>(hn / hd, 1u << 30)); }
                    }
                } else if (b.type == "irot") {
                    const QByteArray d = src.readUpTo(b.body, 1);
                    if (d.size() == 1) rot = uchar(d[0]) & 3;
                }
            }
            if (cw > 0 && ch > 0 && cw <= w && ch <= h) { w = cw; h = ch; }
            if (rot & 1) std::swap(w, h);
            info.width = w;
            info.height = h;
        }
    }
    // HEIF 的显示方向由 irot / imir 决定（规范要求忽略 EXIF 的 Orientation），上面的尺寸已经是摆正后的
    info.sizeRotated = true;
}

void readGif(const Src &src, Info &info)
{
    const QByteArray d = src.readUpTo(6, 4);
    if (d.size() == 4) { info.width = le16(d.constData()); info.height = le16(d.constData() + 2); }
}

void readBmp(const Src &src, Info &info)
{
    const QByteArray d = src.readUpTo(18, 8);
    if (d.size() == 8) { info.width = int(qint32(le32(d.constData()))); info.height = std::abs(int(qint32(le32(d.constData() + 4)))); }
}

QString two(int v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); }

// 规范成 ±HH:MM；不像时区的返回空
QString normalizeOffset(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([+-])(\\d{1,2}):?(\\d{2})\\s*$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) return {};
    const int h = m.captured(2).toInt(), mi = m.captured(3).toInt();
    if (h > 14 || mi > 59) return {};
    return m.captured(1) + two(h) + u':' + two(mi);
}

double round6(double v) { return std::round(v * 1e6) / 1e6; }

} // namespace

// ---------- 公开函数 ----------

Container sniff(const QByteArray &h)
{
    if (h.size() >= 3 && uchar(h[0]) == 0xFF && uchar(h[1]) == 0xD8 && uchar(h[2]) == 0xFF) return Container::Jpeg;
    if (h.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1a\n"))) return Container::Png;
    if (h.size() >= 12 && h.startsWith("RIFF") && h.mid(8, 4) == "WEBP") return Container::Webp;
    if (h.size() >= 12 && h.mid(4, 4) == "ftyp") {
        static const QList<QByteArray> brands = { "heic", "heix", "heim", "heis", "hevc", "hevx", "mif1", "msf1", "avif", "avis", "mif2" };
        if (brands.contains(h.mid(8, 4))) return Container::Heif;
    }
    if (h.startsWith("GIF87a") || h.startsWith("GIF89a")) return Container::Gif;
    if (h.startsWith("BM") && h.size() >= 14) return Container::Bmp;
    return Container::Unknown;
}

QString containerName(Container c)
{
    switch (c) {
    case Container::Jpeg: return QStringLiteral("jpeg");
    case Container::Png: return QStringLiteral("png");
    case Container::Webp: return QStringLiteral("webp");
    case Container::Heif: return QStringLiteral("heif");
    case Container::Gif: return QStringLiteral("gif");
    case Container::Bmp: return QStringLiteral("bmp");
    default: return {};
    }
}

void parseTiff(const QByteArray &b, qint64 tiffPos, Info &info)
{
    if (b.size() < 8) return;
    bool le;
    if (b.startsWith("II")) le = true;
    else if (b.startsWith("MM")) le = false;
    else return;
    const Tiff t(b, le);
    if (t.u16(2) != 42) return;
    info.hasExif = true;
    info.littleEndian = le;
    info.tiffPos = tiffPos;
    info.tiffLen = b.size();

    QSet<quint64> seen;
    const Ifd ifd0 = readIfd(t, t.u32(4), seen);
    if (!ifd0.ok) return;
    if (ifd0.nextPtrOff) info.nextIfdPos = tiffPos + qint64(ifd0.nextPtrOff);
    auto tag = [](const Ifd &ifd, quint16 k) { return ifd.tags.value(k); };

    if (const Tiff::Entry e = tag(ifd0, 0x0112); e.ok && e.type == 3) {
        const int o = int(t.num(e));
        if (o >= 1 && o <= 8) info.orientation = o;
        info.orientationPos = tiffPos + qint64(e.dataOff);
    }
    info.make = t.str(tag(ifd0, 0x010F));
    info.model = t.str(tag(ifd0, 0x0110));
    QString dt0 = t.str(tag(ifd0, 0x0132));

    QString dt, offset;
    if (const Tiff::Entry e = tag(ifd0, 0x8769); e.ok) {
        const Ifd ex = readIfd(t, quint64(t.num(e)), seen);
        for (quint16 k : { quint16(0x9003), quint16(0x9004) }) {
            const QString s = t.str(tag(ex, k));
            if (validDateTime(s)) { dt = s; break; }
        }
        for (quint16 k : { quint16(0x9011), quint16(0x9010), quint16(0x9012) }) {
            const QString s = normalizeOffset(t.str(tag(ex, k)));
            if (!s.isEmpty()) { offset = s; break; }
        }
        info.fnumber = t.num(tag(ex, 0x829D));
        info.exposure = t.num(tag(ex, 0x829A));
        double iso = t.num(tag(ex, 0x8827));
        if (!(iso > 0)) iso = t.num(tag(ex, 0x8833));
        if (iso > 0 && iso < 10000000) info.iso = int(iso);
        info.focal = t.num(tag(ex, 0x920A));
        const double f35 = t.num(tag(ex, 0xA405));
        if (f35 > 0 && f35 < 100000) info.focal35 = int(f35);
        info.ev = t.num(tag(ex, 0x9204));
        info.lens = t.str(tag(ex, 0xA434));
        if (info.lens.isEmpty()) info.lens = t.str(tag(ex, 0xA433));
        const double w = t.num(tag(ex, 0xA002)), h = t.num(tag(ex, 0xA003));
        if (w > 0 && h > 0 && w < 1e6 && h < 1e6) { info.exifWidth = int(w); info.exifHeight = int(h); }
    }
    if (dt.isEmpty() && validDateTime(dt0)) dt = dt0;
    info.dateTime = dt;
    info.offsetTime = offset;

    if (const Tiff::Entry e = tag(ifd0, 0x8825); e.ok) {
        const Ifd g = readIfd(t, quint64(t.num(e)), seen);
        const bool voidFix = t.str(tag(g, 9)).startsWith(u'V');
        const Tiff::Entry la = tag(g, 2), lo = tag(g, 4);
        if (!voidFix && la.ok && lo.ok && la.count >= 1 && lo.count >= 1) {
            auto dms = [&t](const Tiff::Entry &x) {
                double v = t.num(x, 0);
                const double m = x.count > 1 ? t.num(x, 1) : 0, s = x.count > 2 ? t.num(x, 2) : 0;
                if (std::isfinite(m)) v += m / 60;
                if (std::isfinite(s)) v += s / 3600;
                return v;
            };
            double lat = dms(la), lng = dms(lo);
            if (t.str(tag(g, 1)).startsWith(u'S')) lat = -lat;
            if (t.str(tag(g, 3)).startsWith(u'W')) lng = -lng;
            if (std::isfinite(lat) && std::isfinite(lng) && std::abs(lat) <= 90 && std::abs(lng) <= 180 && (lat != 0 || lng != 0)) {
                info.lat = round6(lat);
                info.lng = round6(lng);
                double alt = t.num(tag(g, 6));
                if (std::isfinite(alt) && std::abs(alt) < 100000) {
                    if (int(t.num(tag(g, 5))) == 1) alt = -alt;
                    info.alt = std::round(alt * 10) / 10;
                }
            }
        }
        const double d = t.num(tag(g, 17));
        if (std::isfinite(d) && d >= 0 && d <= 360) {
            info.dir = std::round(d * 10) / 10;
            if (info.dir >= 360) info.dir = 0;
        }
        const QString gd = t.str(tag(g, 29));
        static const QRegularExpression gdre(QStringLiteral("^\\d{4}:\\d{2}:\\d{2}$"));
        const Tiff::Entry ts = tag(g, 7);
        if (gdre.match(gd).hasMatch() && ts.ok && ts.count >= 3) {
            info.gpsDate = gd;
            info.gpsH = t.num(ts, 0); info.gpsM = t.num(ts, 1); info.gpsS = t.num(ts, 2);
        }
    }
}

Info readDevice(QIODevice *dev)
{
    Info info;
    if (!dev || !dev->isOpen()) return info;
    const Src src(dev);
    info.container = sniff(src.readUpTo(0, 16));
    switch (info.container) {
    case Container::Jpeg: readJpeg(src, info); break;
    case Container::Png: readPng(src, info); break;
    case Container::Webp: readWebp(src, info); break;
    case Container::Heif: readHeif(src, info); break;
    case Container::Gif: readGif(src, info); break;
    case Container::Bmp: readBmp(src, info); break;
    default: break;
    }
    if (info.width <= 0 || info.height <= 0 || info.width > 1000000 || info.height > 1000000) info.width = info.height = 0;
    if (info.width == 0 && info.exifWidth > 0) { info.width = info.exifWidth; info.height = info.exifHeight; }   // 文件本身读不到尺寸时
    return info;
}

Info readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return readDevice(&f);
}

Info readBytes(const QByteArray &bytes)
{
    QByteArray copy = bytes;
    QBuffer b(&copy);
    b.open(QIODevice::ReadOnly);
    return readDevice(&b);
}

QString takenAt(const Info &info)
{
    if (!validDateTime(info.dateTime)) return {};
    const QString &s = info.dateTime;
    return s.mid(0, 4) + u'-' + s.mid(5, 2) + u'-' + s.mid(8, 2) + u'T' + s.mid(11, 2) + u':' + s.mid(14, 2);
}

QString tzFromGps(const QString &dateTime, const QString &gpsDate, double h, double m, double s)
{
    if (!validDateTime(dateTime) || gpsDate.size() != 10 || !std::isfinite(h) || !std::isfinite(m) || !std::isfinite(s)) return {};
    if (h < 0 || h >= 24 || m < 0 || m >= 60 || s < 0 || s >= 61) return {};
    const QDate ld(dateTime.mid(0, 4).toInt(), dateTime.mid(5, 2).toInt(), dateTime.mid(8, 2).toInt());
    const QDate gd(gpsDate.mid(0, 4).toInt(), gpsDate.mid(5, 2).toInt(), gpsDate.mid(8, 2).toInt());
    if (!ld.isValid() || !gd.isValid()) return {};
    const double localSec = double(ld.toJulianDay()) * 86400 + dateTime.mid(11, 2).toInt() * 3600 + dateTime.mid(14, 2).toInt() * 60
                            + (dateTime.size() >= 19 ? dateTime.mid(17, 2).toInt() : 0);
    const double utcSec = double(gd.toJulianDay()) * 86400 + h * 3600 + m * 60 + s;
    const double diffMin = std::round((localSec - utcSec) / 60.0 / 15.0) * 15.0;
    if (std::abs(diffMin) > 14 * 60) return {};
    const int total = int(diffMin);
    const int a = std::abs(total);
    return QString(total < 0 ? u'-' : u'+') + two(a / 60) + u':' + two(a % 60);
}

QString timeZone(const Info &info)
{
    if (!info.offsetTime.isEmpty()) return normalizeOffset(info.offsetTime);
    return tzFromGps(info.dateTime, info.gpsDate, info.gpsH, info.gpsM, info.gpsS);
}

QString cameraName(const QString &make, const QString &model)
{
    const QString mk = make.simplified(), md = model.simplified();
    if (md.isEmpty()) return mk;
    if (mk.isEmpty()) return md;
    // 型号以品牌名的第一个词开头（不区分大小写）就不重复："NIKON CORPORATION" + "NIKON Z 6"
    const QString firstWord = mk.section(u' ', 0, 0);
    if (md.startsWith(firstWord, Qt::CaseInsensitive)) return md;
    return mk + u' ' + md;
}

json::Object photoFields(const Info &info)
{
    json::Object o;
    const QString ta = takenAt(info);
    if (!ta.isEmpty()) {
        o.set(QStringLiteral("takenAt"), json::Value(ta));
        const QString tz = timeZone(info);
        if (!tz.isEmpty()) o.set(QStringLiteral("tz"), json::Value(tz));
    }
    if (info.hasGps()) {
        o.set(QStringLiteral("lat"), json::Value(info.lat));
        o.set(QStringLiteral("lng"), json::Value(info.lng));
        if (std::isfinite(info.alt)) o.set(QStringLiteral("alt"), json::Value(info.alt));
    }
    if (std::isfinite(info.dir)) o.set(QStringLiteral("dir"), json::Value(info.dir));
    if (info.width > 0 && info.height > 0) {
        o.set(QStringLiteral("width"), json::Value(info.displayWidth()));
        o.set(QStringLiteral("height"), json::Value(info.displayHeight()));
    }
    const QString cam = cameraName(info.make, info.model);
    if (!cam.isEmpty()) o.set(QStringLiteral("camera"), json::Value(cam));
    if (!info.lens.isEmpty()) o.set(QStringLiteral("lens"), json::Value(info.lens));
    json::Object ex;
    auto put = [&ex](const char *k, double v) {   // 保留 2 位小数
        if (std::isfinite(v) && v > 0 && v < 100000) ex.set(QString::fromLatin1(k), json::Value(std::round(v * 100) / 100));
    };
    put("f", info.fnumber);
    if (std::isfinite(info.exposure) && info.exposure > 0 && info.exposure < 100000) {
        // 快门：秒数，保留 6 位有效小数（1/8000 = 0.000125）
        ex.set(QStringLiteral("t"), json::Value(std::round(info.exposure * 1e6) / 1e6));
    }
    if (info.iso > 0) ex.set(QStringLiteral("iso"), json::Value(info.iso));
    put("fl", info.focal);
    if (info.focal35 > 0) ex.set(QStringLiteral("fl35"), json::Value(info.focal35));
    if (std::isfinite(info.ev) && std::abs(info.ev) < 100) ex.set(QStringLiteral("ev"), json::Value(std::round(info.ev * 100) / 100));
    if (!ex.isEmpty()) o.set(QStringLiteral("exposure"), json::Value(std::move(ex)));
    return o;
}

} // namespace tp::data::exif
