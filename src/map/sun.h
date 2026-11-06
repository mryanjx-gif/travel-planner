// 太阳直射点（天文年历简化公式，精度约 0.1°），照搬 Electron map-app.js subsolarPoint。地图阴影和地球共用。
#pragma once
#include <QDateTime>
#include <cmath>

struct SubSolar { double lat, lng; };

inline SubSolar subsolarPoint(const QDateTime &utc)
{
    constexpr double D2R = M_PI / 180.0, R2D = 180.0 / M_PI;
    const double d = utc.toMSecsSinceEpoch() / 86400000.0 - 10957.5;   // 距 J2000.0 的天数
    const double g = (357.529 + 0.98560028 * d) * D2R;
    const double q = 280.459 + 0.98564736 * d;
    const double L = (q + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * D2R;
    const double e = (23.439 - 0.00000036 * d) * D2R;
    const double ra = std::atan2(std::cos(e) * std::sin(L), std::cos(L)) * R2D;
    const double dec = std::asin(std::sin(e) * std::sin(L)) * R2D;
    const double gmst = std::fmod(std::fmod(18.697374558 + 24.06570982441908 * d, 24.0) + 24.0, 24.0);
    const double lng = ra - gmst * 15;
    return { dec, std::fmod(std::fmod(lng + 540.0, 360.0) + 360.0, 360.0) - 180.0 };
}
