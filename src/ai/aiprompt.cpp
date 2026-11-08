#include "aiprompt.h"

#include "translator.h"

#include <QTimeZone>

namespace tp::ai {

namespace {
const char kRules[] =
    "你是「旅行计划」桌面 app 里的助手，帮用户安排和修改他自己的旅行计划。\n\n"
    "说话方式：\n"
    "- 用中文回答（用户用英文提问就用英文）。地名、站点名保留原文。\n"
    "- 第一句就给答案或结论。不要开场白和客套（\"好的\"\"当然可以\"\"很高兴帮你\"\"这是个好问题\"），结尾不要总结，也不要问\"还需要什么吗\"。\n"
    "- 短。要列举时用\"- \"开头的短列表，每条一行。不要用标题（#）、表格、分隔线、表情符号；粗体最多一两处。\n"
    "- 不复述用户的问题，不把计划里已有的内容再念一遍。\n"
    "- 具体：说哪一天、哪一站、多远、多久。不写空话（\"建议合理安排时间\"\"注意安全\"\"祝旅途愉快\"这类不要写）。\n"
    "- 不确定就直说不确定。不要编造营业时间、价格、班次、订单号。\n"
    "- 文字里不写网站名、网址，也不说你搜了什么、怎么搜的；直接给结论。\n"
    "- 意思能合理推断就直接做，用半句话说明假设。缺少会让结果完全不同的信息时，只问一个最关键的问题。\n\n"
    "做计划、改计划：先粗后细\n"
    "- 第一次回答要快、要完整但可以粗略：一两句结论 + 每天一行的安排（只写去哪几个点，不写几点几分），同时调用 propose_changes 把方案写进计划（站点、日程含时间、交通方式；粗略攻略写进计划的 guide 字段）。\n"
    "- 联网搜索开着时，第一次回答前先做 1 到 3 次\"广度\"搜索（经典路线、必去点、当季封路或季节性关闭），确保不漏重点。第一次回答不要去查票价、班次、具体开放时间。\n"
    "- 细节（在哪个网站买票、开放日期、具体时间、班次、价格、要不要预约、住宿还没订）放进 propose_changes 的 gaps：每条一句话，需要联网查的 search 设为 true。gaps 不要在文字里再列一遍。\n"
    "- 用户说\"补充\"时，只查他选的那几项，可以多搜几次；把查到的写进站点备注、日程、待办或 guide（订票网址写完整），再出一份 propose_changes。查不到或不确定的照样放进 gaps。\n"
    "- 联网搜索关着或这家服务商不能联网时：只用已有知识，并用半句话说明\"没联网查，可能有遗漏\"；涉及会变的信息（票价、班次、开放时间、签证政策）提醒去官方渠道核实。\n"
    "- 查找要搜 3 次以上时（比如用户点了\"补充\"）：先调用 research_status 列出要查的几项和预计秒数，再开始查；每一轮最多搜 3 次，每轮结束调用 research_status 汇报进度；全部查完时 finished = true，再给结果和 propose_changes。app 会显示计时，到预计时间会停下来问用户要不要继续。\n"
    "- 回答要能很快出来：跨几个计划时，每个计划单独调用一次 propose_changes（写完一个再写下一个）；第一次回答里每个计划的 guide 不超过 300 字，只写要点，细节等\"补充\"时再写。\n\n"
    "读取和修改：\n"
    "- 计划简表里只有概要。要改或评估某个计划、而下面没有它的完整内容时，先调用 get_plans 读取（需要的几个一次读完），不要请用户去点开。\n"
    "- 所有修改都通过 propose_changes，用户逐条确认后才写入，所以放心提出完整方案；文字里不要再逐条复述改动。能改任何东西：新建 / 删除计划；站点增、删、改、挪；日程增、删、改、挪；交通住宿、待办、行李的增、删、改。\n"
    "- 用 id 指明改哪一项：站点 stop_id；日程 stop_id + n（n 是 get_plans 给的序号）；booking_id、todo_id、packing_id。每一项写 plan_id。新建计划时给一个 new_id（例如 \"new1\"），同一次里往这个新计划加站点时 plan_id 写这个 new_id，站点按顺序加。新站点的日程直接写在 add_stop 的 schedule 里（新站点还没有 stop_id，不能用 add_schedule）。\n"
    "- 删除要有理由：只删用户要求的，或为了满足要求必须删的（比如回程提前，后面几天的站点），summary 里说清楚。\n"
    "- 新站点给出尽量准确的经纬度（app 会再用地名搜索核对）。\n"
    "- legMode 写在起点站点上，表示去下一站的方式：大巴、包车、打车、自驾 = drive；徒步、城市步行 = walk；火车、地铁 = train；船 = ferry；飞机 = flight（不写也是飞机）。拿不准的按最可能的标，用半句话说出来。\n"
    "- 日期写 YYYY-MM-DD，时间写 HH:MM（当地时间）。\n\n"
    "app 的数据：计划（plan）有 status（planning 计划中 / completed 已完成 / wish 想去）、startDate、endDate、days 预计天数（没定日期时）、notes 备注、guide 攻略（Markdown 文字）、"
    "站点 stops（id、index、name、lat、lng、arriveDate、departDate、schedule 日程 [{n, date, time, end, text}]、notes、legMode、elev 海拔米、tz 时区、photoCount）、"
    "bookings 交通住宿、todos 待办、packing 行李。";
const char kWd[] = "日一二三四五六";
} // namespace

QString systemHead(const QString &summary)
{
    return QString::fromUtf8(kRules) + QStringLiteral("\n\n所有计划的简表（JSON）：\n") + summary;
}

QString systemTail(const QDateTime &now, const QString &page, const QString &planName, const QString &planId, const QString &live,
                   bool web, bool canWeb, const QString &planJson)
{
    const QString tz = QString::fromUtf8(now.timeZone().id());
    const QString wd = QString::fromUtf8(kWd).mid(now.date().dayOfWeek() % 7, 1);
    QStringList l;
    l << QStringLiteral("今天：%1（周%2），现在 %3%4。").arg(now.toString(QStringLiteral("yyyy-MM-dd")), wd, now.toString(QStringLiteral("HH:mm")), tz.isEmpty() ? QString() : QStringLiteral("，用户电脑时区 ") + tz);
    l << QStringLiteral("用户正在看：%1%2。").arg(page, planId.isEmpty() ? QStringLiteral("，没有选中某个计划") : QStringLiteral("，当前计划是「%1」（id %2）").arg(planName, planId));
    if (!live.isEmpty()) l << live;
    l << (!canWeb ? QStringLiteral("联网搜索：这家服务商不能联网，只能用已有知识。") : web ? QStringLiteral("联网搜索：开着。") : QStringLiteral("联网搜索：关着（用户可以在设置 → AI 里打开）。"));
    QString t = QStringLiteral("现在的情况：\n") + l.join(u'\n');
    t += planJson.isEmpty() ? QStringLiteral("\n\n用户没有选中计划。需要哪个计划的完整内容就用 get_plans 读。")
                            : QStringLiteral("\n\n当前计划的完整内容（JSON）：\n") + planJson;
    return t;
}

WizardMsg wizardMessage(const QVariantMap &f, const QString &cur)
{
    auto v = [&f](const char *k) { return f.value(QLatin1String(k)).toString().trimmed(); };
    QStringList zh, ui;
    auto add = [&](const QString &kZh, const QString &kUi, const QString &val) { if (val.isEmpty()) return; zh << QStringLiteral("- %1：%2").arg(kZh, val); ui << QStringLiteral("- %1：%2").arg(kUi, val); };
    add(QStringLiteral("出发地"), tp::tr("出发地"), v("from"));
    add(QStringLiteral("目的地"), tp::tr("目的地"), v("to"));
    if (!v("start").isEmpty() || !v("end").isEmpty()) {
        const QString d = v("start") + QStringLiteral(" — ") + v("end");
        add(QStringLiteral("日期"), tp::tr("日期"), d);
    } else if (v("days").toInt() > 0) {
        zh << QStringLiteral("- 天数：%1 天（日期没定）").arg(v("days").toInt());
        ui << QStringLiteral("- %1").arg(tp::tr("%1 天").arg(v("days").toInt()));
    }
    if (v("people").toInt() > 0) { zh << QStringLiteral("- 人数：%1 人").arg(v("people").toInt()); ui << QStringLiteral("- %1").arg(tp::tr("%1 人").arg(v("people").toInt())); }
    if (v("budget").toDouble() > 0) {
        zh << QStringLiteral("- 预算：约 %1 %2（所有人合计）").arg(cur, v("budget"));
        ui << QStringLiteral("- %1：%2 %3").arg(tp::tr("预算"), cur, v("budget"));
    }
    const QString pace = v("pace");
    if (!pace.isEmpty()) {
        const QString pz = pace == QLatin1String("slow") ? QStringLiteral("松（每天少几个点，留出空闲）") : pace == QLatin1String("fast") ? QStringLiteral("紧（每天多看几个点）") : QStringLiteral("适中");
        const QString pu = pace == QLatin1String("slow") ? tp::tr("松") : pace == QLatin1String("fast") ? tp::tr("紧") : tp::tr("适中");
        zh << QStringLiteral("- 节奏：") + pz;
        ui << QStringLiteral("- %1：%2").arg(tp::tr("节奏"), pu);
    }
    const QStringList ints = f.value(QStringLiteral("interests")).toStringList();
    if (!ints.isEmpty()) add(QStringLiteral("兴趣"), tp::tr("兴趣"), ints.join(QStringLiteral("、")));
    add(QStringLiteral("必去"), tp::tr("必去"), v("must"));
    add(QStringLiteral("不去"), tp::tr("不去"), v("avoid"));

    WizardMsg m;
    m.shown = tp::tr("用 AI 规划一趟新旅行") + (ui.isEmpty() ? QString() : QStringLiteral("\n") + ui.join(u'\n'));
    m.text = QStringLiteral("我想规划一趟新的旅行。下面是我在「让 AI 帮我规划」里填的，没写的项是我跳过了：\n")
           + (zh.isEmpty() ? QStringLiteral("- （全部跳过了）") : zh.join(u'\n'))
           + QStringLiteral("\n\n请这样做：\n"
                            "1. 只追问缺了就做不出方案的（比如没写目的地）：一次只问一个最关键的问题，问完就停下等我回答，这一轮不调用 propose_changes。其他没写的合理假设，用半句话说明。\n"
                            "2. 信息够了就给粗略方案：一两句结论 + 每天一行（只写去哪几个点，不写几点几分），同时调用 propose_changes 新建这个计划："
                            "create_plan（new_id 写 new1，status 写 planning，type 按路线 / 单一目的地选；有日期写 startDate、endDate；粗略攻略写进 guide，不超过 300 字）；"
                            "没定日期时加一项 set_days；然后按顺序 add_stop（准确的经纬度、legMode、有日期时写 arriveDate / departDate，每天做什么写进这一站的 schedule（每项一件事：date、可选 time / end_time、text），notes 只留一两句提示）；"
                            "要订的交通、住宿、门票写成 add_todo，不要编造订单号、价格和班次；还没查的细节放进 gaps。\n"
                            "3. 人数、预算、节奏、兴趣用来定方案，不用写进计划。预算明显不够时用一句话说出来。");
    return m;
}

QString orderTask(const QString &planId)
{
    return QStringLiteral("现在的任务：用户会粘贴订单确认邮件或 PDF 里复制的文字。把里面的交通和住宿整理成 add_booking，只调用 propose_changes（plan_id 写 %1），不要做别的。"
                          "kind 按交通方式或住宿选；时间写成当地时间 YYYY-MM-DDTHH:MM；订单号放 ref，金额放 cost（只填数字）。看不出来的字段留空，不要猜。").arg(planId);
}

QString orderMessage(const QString &pasted) { return QStringLiteral("下面是订单确认的文字，请整理成交通住宿：\n\n") + pasted; }

} // namespace tp::ai
