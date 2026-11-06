#include "tpapplication.h"
#include "errorreporter.h"

#include <QEvent>
#include <QMetaEnum>
#include <exception>
#include <typeinfo>

#ifdef _MSC_VER
#include "crashhandler.h"
#include <windows.h>
#endif

namespace tp::app {
namespace {
void reportException(QObject *receiver, QEvent *event, const QString &what, const char *type)
{
    // C++20 拿不到调用堆栈：用"异常类型 + 接收对象的类名 + 事件类型"当位置，同一种异常每次得到同一个错误码
    const QString cls = receiver ? QString::fromLatin1(receiver->metaObject()->className()) : QStringLiteral("?");
    const QString ev = event ? QString::fromLatin1(QMetaEnum::fromType<QEvent::Type>().valueToKey(event->type())) : QString();
    const QString at = QStringLiteral("%1 in %2::%3").arg(QString::fromLatin1(type), cls, ev.isEmpty() ? QStringLiteral("event") : ev);
    if (ErrorReporter *r = ErrorReporter::instance())
        r->report(QStringLiteral("M"), what, QStringLiteral("at ") + at, at);
    else
        qCritical("没接住的 C++ 异常：%s（%s）", qUtf8Printable(what), qUtf8Printable(at));
}
} // namespace

bool TpApplication::notify(QObject *receiver, QEvent *event)
{
#ifdef _MSC_VER
    // 外层 SEH：要不要接由 sehFilter 决定（C++ 异常、调试器挂着时不接）；接了就写崩溃报告并结束进程，不会走到 return false
    __try {
        return notifyCpp(receiver, event);
    } __except (tp::platform::CrashHandler::sehFilter(GetExceptionInformation())) {
        return false;
    }
#else
    return notifyCpp(receiver, event);
#endif
}

bool TpApplication::notifyCpp(QObject *receiver, QEvent *event)
{
    try {
        return QGuiApplication::notify(receiver, event);
    } catch (const std::exception &e) {
        reportException(receiver, event, QString::fromUtf8(e.what()), typeid(e).name());
    } catch (...) {
        reportException(receiver, event, QStringLiteral("未知的 C++ 异常"), "unknown");
    }
    return false;
}

} // namespace tp::app
