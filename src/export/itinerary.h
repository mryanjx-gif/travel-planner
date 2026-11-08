// 行程单和紧急信息卡（0.14.0，E-01–E-03；照 Electron export.js itineraryHtml、travelkit.js cardHtml，原生版用 QPainter 重新排版）。
// 一套排版出三种东西：A4 PDF（边距 0.5in，每站不跨页，每页页脚）、PNG 长图（宽 820，高 ≤ 16000）、第一页缩略图（导出前预览）。
// 封面是路线简图：打包的 Natural Earth 省界（和"已解锁"底图同样的颜色）+ 站点和路线，不用在线瓦片。
// 固定文字按界面语言（tp::tr），用户内容不翻译。排版单位是"排版像素"：正文宽 740（和 Electron 的 .pg 一样）。
#pragma once

#include "jsonvalue.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class QPainter;

namespace tp::exporter {

struct Options {
    bool refs = false;       // 交通住宿里的订单号
    bool money = false;      // 费用列 + 消费表
    bool notes = true;       // 计划、站点、交通住宿的备注
    bool emergency = false;  // 最后加紧急信息
    QString font;            // 字体（空 = 默认）；程序里是思源宋体
    QString adminDir;        // assets/admin（adm1.geojson、adm0.geojson）；空或读不到时封面只画站点和路线
    QString currency;        // 计划没写币种时用的主币种
    QString today;           // 页脚的日期（yyyy-MM-dd）
};

constexpr double kContentW = 740;           // 正文宽
constexpr double kPngW = 820;               // 长图宽
constexpr int kPngMaxH = 16000;             // 长图最高

// 分页（纯计算，单元测试用）：blocks 按顺序；同一 group（≥ 0）的块尽量放在同一页（一站），整组比一页还高时才在组内换页；
// keepNext = 和下一个块（或下一组）放在同一页（小标题、表头）。返回每页的块下标
struct BlockSize { double h = 0; int group = -1; bool keepNext = false; };
QVector<QVector<int>> paginate(const QVector<BlockSize> &blocks, double pageH);

class Doc {
public:
    static Doc itinerary(const json::Value &root, const json::Value &plan, const Options &o);
    static Doc card(const json::Value &root, const json::Value &plan, const Options &o);

    QString title() const { return m_title; }
    int pageCount() const { return int(m_pages.size()); }
    QVector<QVector<int>> pages() const { return m_pages; }
    QVector<BlockSize> sizes() const;
    // 第 i 页（A4，含边距和页脚），宽 widthPx 像素
    QImage renderPage(int i, int widthPx) const;
    bool writePdf(const QString &path, QString *error) const;
    // 长图：宽 820；超过 16000 高时后面的块不放，最后写一句"太长了"。truncated 告诉调用方
    QImage longImage(bool *truncated = nullptr) const;

    struct Block {
        double h = 0;
        int group = -1;
        bool keepNext = false;
        std::function<void(QPainter &, double y)> paint;   // 在 x = 0…kContentW、y…y+h 里画
    };

private:
    void paintPage(QPainter &p, int i) const;   // 坐标是排版像素，含边距
    QString m_title, m_footer, m_font;
    QVector<Block> m_blocks;
    QVector<QVector<int>> m_pages;
};

// A4 换成排版像素（边距 0.5in，正文宽 = kContentW）
double pageWidthLayout();
double pageHeightLayout();
double bodyHeightLayout();   // 一页正文能放多高（去掉页脚）

} // namespace tp::exporter
