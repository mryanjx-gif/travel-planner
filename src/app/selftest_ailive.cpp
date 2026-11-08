// 1.2.0 真接口实测（只有点名 ailive 才跑，要花钱）：用凭据管理器里已存的 Claude 钥匙（程序自己读，不打印、不记录），
// 快档（Haiku）、联网开着，同一组 3 句短对话，记下每个请求的输入 / 缓存读 / 输出 / 花费，用来对比改前改后。
// 累计超过约 ¥0.9 就不再发下一句（两次合计 ≤ ¥2）。不应用任何修改；对话存到临时文件，设置跑完还原。
#include "selftest_util.h"

#include "aicontroller.h"
#include "appcontroller.h"

#include <QRegularExpression>
#include <QTemporaryDir>

namespace tp::app::selftest {

void runAiLive(Report &r, AppController *app, QQuickWindow *win)
{
    Q_UNUSED(app);
    Q_UNUSED(win);
    r.section(QStringLiteral("AI 真接口实测（Claude 快档，花真钱）"));
    AiController *ai = aiCtl();
    if (!ai) { r.check(false, QStringLiteral("有 AI 控制器")); return; }
    const QString oldProv = ai->provider(), oldTier = ai->tier(), oldManual = ai->manualModel();
    const bool oldWeb = ai->web();
    QTemporaryDir tmp;
    AiController::TestHooks h;
    h.credPrefix = QStringLiteral("TravelPlannerNative/ai/");   // 真钥匙的目标名
    h.chatsFile = tmp.filePath(QStringLiteral("live-chats.json"));
    ai->setTestHooks(h);
    ai->setProvider(QStringLiteral("claude"));
    if (!ai->hasKey()) {
        r.info(QStringLiteral("凭据管理器里没有 Claude 钥匙，跳过"));
    } else {
        ai->setTier(QStringLiteral("fast"));
        ai->setManualModel(QString());
        ai->setWeb(true);
        ai->newChat();
        const QStringList turns = { QStringLiteral("帮我规划悉尼市区 3 天，住环形码头附近"), QStringLiteral("第二天改去邦迪海滩"), QStringLiteral("第三天轻松一点，下午留空") };
        static const QRegularExpression reIn(QStringLiteral("输入 ([\\d,]+)")), reOut(QStringLiteral("输出 ([\\d,]+)")), reCache(QStringLiteral("缓存 ([\\d,]+)"));
        auto num = [](const QRegularExpression &re, const QString &s) { const auto m = re.match(s); return m.hasMatch() ? m.captured(1).remove(u',').toLongLong() : 0LL; };
        double total = 0;   // 美元
        int seen = 0;
        for (int t = 0; t < turns.size(); ++t) {
            if (total * 7.1 > 0.9) { r.info(QStringLiteral("累计约 ¥%1，不再发下一句").arg(total * 7.1, 0, 'f', 2)); break; }
            if (!ai->send(turns.at(t)) && !ai->pendingTell().isEmpty()) ai->confirmTell();
            waitFor([ai] { return !ai->busy(); }, 300000);
            const QVariantList ms = ai->messages();
            qint64 tin = 0, tcr = 0, tout = 0;
            double tc = 0;
            int reqs = 0;
            for (int i = seen; i < ms.size(); ++i) {
                const QVariantMap m = ms.at(i).toMap();
                if (m.value(QStringLiteral("role")).toString() != QLatin1String("assistant")) continue;
                const QString meta = m.value(QStringLiteral("meta")).toString() + QLatin1Char(' ') + m.value(QStringLiteral("tokens")).toString();
                const qint64 in = num(reIn, meta), cr = num(reCache, meta), out = num(reOut, meta);
                const double c = m.value(QStringLiteral("cost")).toDouble();
                ++reqs; tin += in; tcr += cr; tout += out; tc += c;
                r.info(QStringLiteral("  第 %1 句 · 请求 %2：输入 %3（缓存读 %4）· 输出 %5 · $%6%7").arg(t + 1).arg(reqs).arg(in).arg(cr).arg(out).arg(c, 0, 'f', 4)
                           .arg(m.contains(QStringLiteral("error")) ? QStringLiteral(" · 出错：") + m.value(QStringLiteral("error")).toString() : QString()));
            }
            seen = int(ms.size());
            total += tc;
            r.info(QStringLiteral("第 %1 句合计：%2 个请求，输入 %3（缓存读 %4），输出 %5，$%6 ≈ ¥%7").arg(t + 1).arg(reqs).arg(tin).arg(tcr).arg(tout).arg(tc, 0, 'f', 4).arg(tc * 7.1, 0, 'f', 3));
        }
        r.info(QStringLiteral("总计 $%1 ≈ ¥%2（按 7.1 折算）").arg(total, 0, 'f', 4).arg(total * 7.1, 0, 'f', 3));
        r.check(total > 0, QStringLiteral("真接口实测跑完并计费"));
    }
    ai->newChat();
    ai->setTier(oldTier);
    ai->setManualModel(oldManual);
    ai->setWeb(oldWeb);
    ai->setProvider(oldProv);
    AiController::TestHooks back;
    back.credPrefix = QStringLiteral("TravelPlannerNative-test/ai/");
    back.chatsFile = QString();
    ai->setTestHooks(back);
}

} // namespace tp::app::selftest
