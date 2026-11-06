#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QImage>
#include <QUrl>
#include <rhi/qrhi.h>

// 用法：maptest <style.json> [--screenshot <输出路径>]
int main(int argc, char *argv[]) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);  // maplibre 在 Windows 只支持 OpenGL/Vulkan
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString shot;
    const int i = args.indexOf(QStringLiteral("--screenshot"));
    if (i >= 0 && i + 1 < args.size()) shot = args.at(i + 1);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("styleUrl", QUrl::fromLocalFile(args.value(1)).toString());
    engine.loadFromModule("MapTestApp", "Main");
    if (engine.rootObjects().isEmpty()) return 2;
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(win, &QQuickWindow::sceneGraphInitialized, win, [win] {
        if (QRhi *r = win->rhi()) qInfo("RHI backend: %s / %s", r->backendName(), r->driverInfo().deviceName.constData());
    }, Qt::DirectConnection);
    if (!shot.isEmpty()) {
        QTimer::singleShot(12000, &app, [&] {
            QObject *map = win->findChild<QObject *>("map");
            qInfo("styleLoaded=%d", map ? map->property("styleLoaded").toBool() : -1);
            const QImage img = win->grabWindow();
            qInfo("grab %dx%d saved=%d", img.width(), img.height(), img.save(shot));
            app.exit(img.isNull() ? 3 : 0);
        });
    }
    return app.exec();
}
