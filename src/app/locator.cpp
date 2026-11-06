#include "locator.h"

#include "datastore.h"
#include "mapcontroller.h"
#include "planedit.h"
#include "settings.h"
#include "translator.h"

#include <QDate>
#include <QGeoPositionInfoSource>
#include <QLocale>
#include <cmath>

namespace tp::app {

namespace {
// ISO 3166-1 两字母 → 三字母（只列省界数据里有的国家；码表是公开的标准代码）。Kosovo 用 Natural Earth 的 KOS
const char kIso[] = "ADAND AEARE AFAFG AGATG AIAIA ALALB AMARM AOAGO AQATA ARARG ASASM ATAUT AUAUS AWABW AZAZE BABIH BBBRB BDBGD BEBEL BFBFA BGBGR BHBHR BIBDI BJBEN BLBLM BMBMU BNBRN BOBOL BRBRA BSBHS BTBTN BWBWA BYBLR BZBLZ CACAN CDCOD CFCAF CGCOG CHCHE CICIV CKCOK CLCHL CMCMR CNCHN COCOL CRCRI CUCUB CVCPV CWCUW CYCYP CZCZE DEDEU DJDJI DKDNK DMDMA DODOM DZDZA ECECU EEEST EGEGY ERERI ESESP ETETH FIFIN FJFJI FKFLK FMFSM FOFRO FRFRA GAGAB GBGBR GDGRD GEGEO GGGGY GHGHA GIGIB GLGRL GMGMB GNGIN GQGNQ GRGRC GSSGS GTGTM GUGUM GWGNB GYGUY HKHKG HMHMD HNHND HRHRV HTHTI HUHUN IDIDN IEIRL ILISR IMIMN ININD IOIOT IQIRQ IRIRN ISISL ITITA JEJEY JMJAM JOJOR JPJPN KEKEN KGKGZ KHKHM KIKIR KMCOM KNKNA KPPRK KRKOR KWKWT KYCYM KZKAZ LALAO LBLBN LCLCA LILIE LKLKA LRLBR LSLSO LTLTU LULUX LVLVA LYLBY MAMAR MCMCO MDMDA MEMNE MFMAF MGMDG MHMHL MKMKD MLMLI MMMMR MNMNG MOMAC MPMNP MRMRT MSMSR MTMLT MUMUS MVMDV MWMWI MXMEX MYMYS MZMOZ NANAM NCNCL NENER NFNFK NGNGA NINIC NLNLD NONOR NPNPL NRNRU NUNIU NZNZL OMOMN PAPAN PEPER PFPYF PGPNG PHPHL PKPAK PLPOL PMSPM PNPCN PRPRI PTPRT PWPLW PYPRY QAQAT ROROU RSSRB RURUS RWRWA SASAU SBSLB SCSYC SDSDN SESWE SGSGP SHSHN SISVN SKSVK SLSLE SMSMR SNSEN SOSOM SRSUR STSTP SVSLV SXSXM SYSYR SZSWZ TCTCA TDTCD TFATF TGTGO THTHA TJTJK TLTLS TMTKM TNTUN TOTON TRTUR TTTTO TVTUV TWTWN TZTZA UAUKR UGUGA UMUMI USUSA UYURY UZUZB VAVAT VCVCT VEVEN VGVGB VIVIR VNVNM VUVUT WFWLF WSWSM YEYEM ZAZAF ZMZMB ZWZWE XKKOS";
} // namespace

QString Locator::iso3FromIso2(const QString &iso2)
{
    const QByteArray t(kIso);
    const QByteArray k = iso2.toUpper().toLatin1();
    if (k.size() != 2) return {};
    for (const QByteArray &p : t.split(' '))
        if (p.size() == 5 && p.left(2) == k) return QString::fromLatin1(p.mid(2));
    return {};
}

Locator::Locator(Settings *settings, data::DataStore *store, map::MapController *mapc, QObject *parent)
    : QObject(parent), m_settings(settings), m_store(store), m_mapc(mapc)
{
    m_guard.setSingleShot(true);
    m_guard.setInterval(kTimeoutMs + 1000);
    connect(&m_guard, &QTimer::timeout, this, [this] {
        if (!m_busy) return;
        if (m_src) m_src->stopUpdates();
        qInfo("我的位置：系统定位 %d 秒没有回音", kTimeoutMs / 1000);
        fallback(tp::tr("系统定位超时"));
    });
}

QStringList Locator::sources() { return QGeoPositionInfoSource::availableSources(); }

QString Locator::homeCountry() const
{
    const QString s = m_settings ? m_settings->value(QStringLiteral("homeCountry")).toString().trimmed().toUpper() : QString();
    if (s.size() == 3) return s;
    // 改进 c：按 Windows 的区域设置猜
    const QString iso3 = iso3FromIso2(QLocale::territoryToCode(QLocale::system().territory()));
    return iso3.isEmpty() ? QStringLiteral("AUS") : iso3;
}

void Locator::locate()
{
    if (m_busy) return;
    if (!m_srcTried) {
        m_srcTried = true;
        m_src = QGeoPositionInfoSource::createDefaultSource(this);
        qInfo("我的位置：系统定位来源 %s（可用：%s）", m_src ? qUtf8Printable(m_src->sourceName()) : "（没有）",
              qUtf8Printable(sources().join(QStringLiteral(", "))));
        if (m_src) {
            m_src->setPreferredPositioningMethods(QGeoPositionInfoSource::AllPositioningMethods);   // Electron enableHighAccuracy: false
            connect(m_src, &QGeoPositionInfoSource::positionUpdated, this, [this](const QGeoPositionInfo &info) {
                if (!m_busy) return;
                m_guard.stop();
                // Electron maximumAge: 600000：10 分钟内的旧位置也用（Qt 的 requestUpdate 给的就是一次新位置，这里不额外判断）
                const double acc = info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy) ? info.attribute(QGeoPositionInfo::HorizontalAccuracy) : 0;
                gotFix(info.coordinate().latitude(), info.coordinate().longitude(), std::isfinite(acc) ? acc : 0);
            });
            connect(m_src, &QGeoPositionInfoSource::errorOccurred, this, [this](QGeoPositionInfoSource::Error e) {
                if (!m_busy) return;
                m_guard.stop();
                qInfo("我的位置：系统定位出错（%d）", int(e));
                fallback(e == QGeoPositionInfoSource::AccessError ? tp::tr("没有定位权限")
                         : e == QGeoPositionInfoSource::UpdateTimeoutError ? tp::tr("系统定位超时") : tp::tr("拿不到系统定位"));
            });
        }
    }
    if (!m_src) { fallback(tp::tr("拿不到系统定位")); return; }
    setBusy(true);
    m_guard.start();
    m_src->requestUpdate(kTimeoutMs);
}

void Locator::simulateFix(double lat, double lng, double acc)
{
    setBusy(true);
    gotFix(lat, lng, acc);
}

void Locator::simulateError(const QString &why)
{
    setBusy(true);
    fallback(why == QLatin1String("access") ? tp::tr("没有定位权限") : why == QLatin1String("timeout") ? tp::tr("系统定位超时") : tp::tr("拿不到系统定位"));
}

void Locator::clearFix()
{
    if (!m_hasFix) return;
    m_hasFix = false;
    emit fixChanged();
}

void Locator::gotFix(double lat, double lng, double acc)
{
    setBusy(false);
    m_lat = lat;
    m_lng = lng;
    m_acc = acc;
    m_hasFix = true;
    emit fixChanged();
    // Electron showMe：flyTo(zoom = max(当前, 10), 1.4 秒)
    m_mapc->flyTo(lat, lng, std::max(m_mapc->zoom(), 10.0), 1400);
    // 改进 b：精度一句
    if (acc > 0) {
        const QString a = acc >= 1000 ? tp::tr("%1 公里").arg(acc >= 10000 ? QString::number(std::round(acc / 1000)) : QString::number(acc / 1000, 'f', 1))
                                      : tp::tr("%1 米").arg(std::round(acc));
        emit toast(tp::tr("精度约 %1 · 来自 Windows 定位").arg(a), false, 4000);
    }
    qInfo("我的位置：拿到了（精度 %.0f 米）", acc);
    emit result(QStringLiteral("fix"));
}

void Locator::fallback(const QString &why)
{
    setBusy(false);
    clearFix();
    // 1. 正在进行的旅行：今天所在的站点
    const QString today = m_today ? m_today() : QDate::currentDate().toString(Qt::ISODate);
    const QVariantMap s = map::planedit::todayStop(m_store->data(), today);
    if (!s.isEmpty()) {
        m_mapc->flyTo(s.value(QStringLiteral("lat")).toDouble(), s.value(QStringLiteral("lng")).toDouble(), std::max(m_mapc->zoom(), 9.0), 1400);
        emit toast(tp::tr("%1，先显示今天所在的站点：%2").arg(why, s.value(QStringLiteral("name")).toString()), false, 6000);
        emit result(QStringLiteral("today"));
        return;
    }
    // 2. 所在国家
    const QString code = homeCountry();
    if (m_mapc->fitCountry(code)) {
        emit toast(tp::tr("%1，先显示所在国家").arg(why), false, 6000);
        emit result(QStringLiteral("country"));
        return;
    }
    emit toast(tp::tr("%1。可以在 Windows 设置 → 隐私和安全性 → 位置 里打开定位").arg(why), true, 8000);
    emit result(QStringLiteral("none"));
}

} // namespace tp::app
