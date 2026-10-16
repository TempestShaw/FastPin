#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QStringList>

// A per-user lock owns the server; stale sockets are removed only by its owner.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString &directory);
    bool isPrimary() const { return primary; }
    QString error() const { return failure; }
    bool forward(const QStringList &arguments);
signals:
    void requested(const QStringList &arguments);
private:
    QLockFile lock;
    QLocalServer server;
    QString name, failure;
    bool primary = false;
};
