// 日期、距离、时长的显示（0.8.7，"此刻"卡片和简易计划面板共用）。
// 中文照 Electron mapfirst.js md / wd（"11.03"、"周一"）、map-app.js fmtDist / fmtDur；英文用 "Nov 3"、"Mon"。
pragma Singleton
import QtQuick

QtObject {
    property bool en: false
    readonly property var wdZh: ["日", "一", "二", "三", "四", "五", "六"]
    readonly property var wdEn: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
    readonly property var monEn: ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    function ok(s) { return typeof s === "string" && /^\d{4}-\d{2}-\d{2}$/.test(s) }
    function dt(s) { const p = s.split("-"); return new Date(+p[0], +p[1] - 1, +p[2]) }
    // 11.03 / Nov 3
    function md(s) {
        if (!ok(s)) return ""
        const d = dt(s)
        return en ? monEn[d.getMonth()] + " " + d.getDate() : (d.getMonth() + 1) + "." + String(d.getDate()).padStart(2, "0")
    }
    // 周一 / Mon
    function wd(s) { if (!ok(s)) return ""; const d = dt(s); return en ? wdEn[d.getDay()] : "周" + wdZh[d.getDay()] }
    function mdw(s) { return ok(s) ? md(s) + " " + wd(s) : "" }
    // 两个日期：11.03 — 11.08（只有一个时就显示那一个）
    function range(a, b) {
        if (ok(a) && ok(b)) return a === b ? md(a) : md(a) + " — " + md(b)
        return ok(a) ? md(a) : ok(b) ? md(b) : ""
    }
    // 0.10.1 金额：AUD 8,420（Electron store.js money）；最多两位小数。负数用 − 号
    function money(n, cur) {
        const v = Math.round((+n || 0) * 100) / 100
        const s = Math.abs(v).toFixed(2).replace(/\.?0+$/, "").replace(/\B(?=(\d{3})+(?!\d))/g, ",")
        return (cur ? cur + " " : "") + (v < 0 ? "−" : "") + s
    }
    // 2026 年 12 月 5 日 · 周六 / Sat, Dec 5, 2026（Electron pages-plan.js dayLabel）
    function dayLabel(s) {
        if (!ok(s)) return s
        const d = dt(s)
        return en ? wdEn[d.getDay()] + ", " + monEn[d.getMonth()] + " " + d.getDate() + ", " + d.getFullYear()
                  : d.getFullYear() + " 年 " + (d.getMonth() + 1) + " 月 " + d.getDate() + " 日 · " + wd(s)
    }
    // 交通住宿的类型、消费的分类（Electron BOOK_KINDS、EXP_CATS、EXP_COLORS）
    function kindLabel(k) {
        return k === "flight" ? qsTr("机票") : k === "train" ? qsTr("火车") : k === "bus" ? qsTr("大巴") : k === "ferry" ? qsTr("船票")
             : k === "car" ? qsTr("租车") : k === "hotel" ? qsTr("住宿") : k === "ticket" ? qsTr("门票") : qsTr("其他")
    }
    function catLabel(k) {
        return k === "transport" ? qsTr("交通") : k === "stay" ? qsTr("住宿") : k === "food" ? qsTr("餐饮")
             : k === "ticket" ? qsTr("娱乐/门票") : k === "shop" ? qsTr("购物") : qsTr("其他")
    }
    readonly property var catColors: ({ transport: "#2d6f9e", stay: "#8a5a9e", food: "#d0742b", ticket: "#3d8a5a", shop: "#c24d6b", other: "#8a8378" })
    function nights(a, b) {
        if (!ok(a) || !ok(b)) return 0
        return Math.round((dt(b) - dt(a)) / 86400000)
    }
    // Electron fmtDist：≥ 10 km 取整、≥ 1 km 一位小数，单位 km / m（中英一样）
    function dist(m) {
        if (!(m > 0)) return ""
        return m >= 10000 ? Math.round(m / 1000) + " km" : m >= 1000 ? (m / 1000).toFixed(1) + " km" : Math.round(m) + " m"
    }
    // Electron fmtDur："45 分钟" / "2 小时 15 分"
    function dur(sec) {
        if (!(sec > 0)) return ""
        const min = Math.round(sec / 60)
        if (min < 60) return en ? min + " min" : min + " 分钟"
        const h = Math.floor(min / 60), r = min % 60
        return en ? h + " h" + (r ? " " + r + " min" : "") : h + " 小时" + (r ? " " + r + " 分" : "")
    }
}
