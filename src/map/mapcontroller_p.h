// MapController 几个 .cpp 共用的小东西（0.8.7 拆文件时从 mapcontroller.cpp 挪出来）。只给 mapcontroller*.cpp 用。
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace tp::map::detail {

constexpr double kUnlockMaxZoom = 9;   // UNLOCK.mapMaxZoom

inline QString compact(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
inline const QString &emptyFc()
{
    static const QString s = QStringLiteral("{\"type\":\"FeatureCollection\",\"features\":[]}");
    return s;
}

} // namespace tp::map::detail
