#include "singleinstance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>

SingleInstance::SingleInstance(const QString &directory)
    : lock(directory + "/instance.lock")
{
    QDir().mkpath(directory);
    name = "fastpin-" + QString::fromLatin1(QCryptographicHash::hash(directory.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    lock.setStaleLockTime(0); // A live process keeps its lock, regardless of age.
    if (!lock.tryLock()) {
        if (lock.error() != QLockFile::LockFailedError) failure = "Cannot lock the FastPin instance directory.";
        return;
    }
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(name)) { failure = server.errorString(); return; }
    primary = true;
    connect(&server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server.nextPendingConnection()) {
            socket->setParent(this);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (socket->bytesAvailable() > 65536) { socket->abort(); return; }
                if (!socket->canReadLine()) return;
                const auto doc = QJsonDocument::fromJson(socket->readLine(65537));
                if (!doc.isArray()) { socket->abort(); return; }
                QStringList args;
                for (const auto &item : doc.array()) {
                    if (!item.isString()) { socket->abort(); return; }
                    args << item.toString();
                }
                emit requested(args);
                socket->disconnectFromServer();
            });
        }
    });
}

bool SingleInstance::forward(const QStringList &arguments)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(2000)) return false;
    const auto data = QJsonDocument(QJsonArray::fromStringList(arguments)).toJson(QJsonDocument::Compact) + '\n';
    if (data.size() > 65536) return false;
    return socket.write(data) == data.size() && socket.waitForBytesWritten(2000);
}
