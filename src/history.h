#pragma once
#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QThread>

struct SavedCapture {
    QImage image;
    QPoint position;
    qreal zoom = 1;
};

class CaptureHistory : public QObject {
    Q_OBJECT
public:
    explicit CaptureHistory(QString path, QObject *parent = nullptr);
    ~CaptureHistory() override;
    void remember(QImage image, QPoint position, qreal zoom = 1);
    SavedCapture restore() const;
    bool clear();
    bool busy() const { return worker != nullptr; }
    bool available() const;
    QImage thumbnail() const;
    QString description() const;
    bool enabled = true;
 signals:
    void changed(QString error);
private:
    void writePending();
    QString path;
    SavedCapture pending;
    QThread *worker = nullptr;
    int generation = 0, writingGeneration = 0;
};
