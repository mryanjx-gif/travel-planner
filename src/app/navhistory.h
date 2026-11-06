// 页面的前进 / 后退（0.8.8，功能清单 S-13）。照搬 Electron v6.3.0 shell.js go / back / fwd：
//   - 记"页面 + 参数"，只在这次打开 app 期间有效；
//   - go：和当前这一步一样（页面和参数都相同）就不加；否则丢掉当前之后的记录再加一步，超过 100 步丢掉最早的；
//     replace = 换掉当前这一步，不新增；
//   - back / forward：在记录里移动，不新增。
// 原生版的页面（0.8.10）：地图的四种模式 plan / wish / foot / photo，整页 calendar / journal / ledger，settings（设置面板）、
// "map" 只是没调用 resetTo 时的起点（单元测试），界面里 goPage("map") 换成当前模式。
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace tp::app {

class NavHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString page READ page NOTIFY changed)
    Q_PROPERTY(QVariantMap params READ params NOTIFY changed)
    Q_PROPERTY(bool canBack READ canBack NOTIFY changed)
    Q_PROPERTY(bool canForward READ canForward NOTIFY changed)

public:
    static constexpr int kMax = 100;
    explicit NavHistory(QObject *parent = nullptr);

    QString page() const { return m_idx >= 0 ? m_hist[m_idx].page : QStringLiteral("map"); }
    QVariantMap params() const { return m_idx >= 0 ? m_hist[m_idx].params : QVariantMap(); }
    bool canBack() const { return m_idx > 0; }
    bool canForward() const { return m_idx >= 0 && m_idx < m_hist.size() - 1; }
    int size() const { return int(m_hist.size()); }
    int index() const { return m_idx; }

    void resetTo(const QString &page) { m_hist = { { page, {} } }; m_idx = 0; emit changed(); }
    Q_INVOKABLE void go(const QString &page, const QVariantMap &params = {}, bool replace = false);
    Q_INVOKABLE bool back();
    Q_INVOKABLE bool forward();

signals:
    void changed();

private:
    struct Entry { QString page; QVariantMap params; };
    QVector<Entry> m_hist;
    int m_idx = -1;
};

} // namespace tp::app
