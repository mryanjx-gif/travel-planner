#include "singleinstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <QLocalServer>
#include <QLocalSocket>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace tp::platform {

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

QString SingleInstance::keyFor(const QString &appDir)
{
    QString d = QDir::cleanPath(QDir::fromNativeSeparators(appDir));
#ifdef Q_OS_WIN
    d = d.toLower();
#endif
    const QByteArray h = QCryptographicHash::hash(d.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("TravelPlannerNative-") + QString::fromLatin1(h);
}

bool SingleInstance::start(const QString &key, bool waitForPrevious)
{
    if (waitForPrevious) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 5000) {
            QLocalSocket s;
            s.connectToServer(key);
            if (!s.waitForConnected(200)) break;   // 上一个已经退出
            s.disconnectFromServer();
            QThread::msleep(200);
        }
    } else {   // 先试着连已有的
        QLocalSocket s;
        s.connectToServer(key);
        if (s.waitForConnected(300)) {
#ifdef Q_OS_WIN
            AllowSetForegroundWindow(ASFW_ANY);   // 允许已有的那个进程把自己的窗口调到前台
#endif
            s.write("show\n");
            s.flush();
            s.waitForBytesWritten(500);
            s.disconnectFromServer();
            return false;
        }
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(key)) {
        // Linux 上程序崩溃会留下旧的套接字文件；Windows 的命名管道随进程消失
        QLocalServer::removeServer(key);
        if (!m_server->listen(key)) {
            qWarning("单实例监听失败：%s（继续运行，不限制多开）", qUtf8Printable(m_server->errorString()));
            return true;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *c = m_server->nextPendingConnection()) {
            connect(c, &QLocalSocket::readyRead, c, [this, c] {
                if (c->readAll().contains("show")) emit activateRequested();
                c->disconnectFromServer();
            });
            connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        }
    });
    return true;
}

} // namespace tp::platform
