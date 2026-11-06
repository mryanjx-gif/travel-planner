// 日出日落（0.10.0，P-06）：移植 Electron 打包的 suncalc（app/vendor/suncalc.js，BSD-2，见 技术选型.md）的 getTimes 里用到的部分，
// 外加 travelkit.js 的 sunInfo（当地日期 → 日出、日落、白天长度、极昼极夜）。
// 和 Electron 对照：tests/golden/sun.json（tools/claude-kit/sun-golden/gen.js 用 Electron 的代码算）。
#pragma once

#include <QString>

namespace tp::map::sun {

struct Times
{
    bool ok = false;        // 日期或坐标不对时为假
    QString polar;          // "day" 极昼 / "night" 极夜 / 空
    QString rise, set;      // 当地时间 HH:mm
    int dayMin = 0;         // 白天有多少分钟
    bool approx = false;    // 没有（或认不出）时区：按经度估算
};

// date = "YYYY-MM-DD"（当地日期）；tz = IANA 时区名，空 = 按经度估算
Times info(double lat, double lng, const QString &date, const QString &tz);

} // namespace tp::map::sun
