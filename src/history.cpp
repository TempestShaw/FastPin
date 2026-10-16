#include "history.h"
#include <QDir>
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <memory>

static QString writeCapture(const QString &path, const SavedCapture &capture)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return "Could not create the local recovery folder.";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QImageWriter writer(&file, "PNG"); writer.setCompression(1);
    writer.setText("FastPin.PositionX", QString::number(capture.position.x()));
    writer.setText("FastPin.PositionY", QString::number(capture.position.y()));
    writer.setText("FastPin.Ratio", QString::number(capture.image.devicePixelRatio()));
    writer.setText("FastPin.Zoom", QString::number(capture.zoom));
    writer.setText("FastPin.Saved", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QByteArray preview;
    QBuffer buffer(&preview); buffer.open(QIODevice::WriteOnly);
    // A fresh image avoids carrying a previous saved image's thumbnail inside the new one.
    QImage thumbnail(capture.image.size().scaled(160, 86, Qt::KeepAspectRatio), QImage::Format_ARGB32_Premultiplied);
    thumbnail.fill(Qt::transparent);
    QPainter painter(&thumbnail); painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(thumbnail.rect(), capture.image); painter.end(); thumbnail.save(&buffer, "PNG");
    writer.setText("FastPin.Thumbnail", QString::fromLatin1(preview.toBase64()));
    if (!writer.write(capture.image)) return writer.errorString();
    return file.commit() ? QString() : file.errorString();
}

CaptureHistory::CaptureHistory(QString path, QObject *parent) : QObject(parent), path(std::move(path)) {}

CaptureHistory::~CaptureHistory()
{
    if (worker) {
        worker->wait(); delete worker;
        if (writingGeneration != generation) QFile::remove(path);
    }
    if (enabled && !pending.image.isNull()) writeCapture(path, pending);
}

void CaptureHistory::remember(QImage image, QPoint position, qreal zoom)
{
    if (!enabled || image.isNull()) return;
    pending = {std::move(image), position, zoom};
    if (!worker) writePending();
}

void CaptureHistory::writePending()
{
    const int startedGeneration = generation;
    writingGeneration = generation;
    auto error = std::make_shared<QString>();
    worker = QThread::create([path = path, capture = std::move(pending), error] { *error = writeCapture(path, capture); });
    pending = {};
    connect(worker, &QThread::finished, this, [this, error, startedGeneration] {
        worker->deleteLater(); worker = nullptr;
        if (startedGeneration != generation) QFile::remove(path);
        if (enabled && !pending.image.isNull()) writePending();
        else emit changed(startedGeneration == generation ? *error : QString());
    });
    worker->start();
}

bool CaptureHistory::clear()
{
    ++generation; pending = {};
    if (worker) worker->wait(); // A completed write cannot recreate a file after Clear returns.
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        emit changed("Could not delete the saved image. Check the recovery folder permissions.");
        return false;
    }
    emit changed({});
    return true;
}

bool CaptureHistory::available() const { return enabled && QFileInfo::exists(path); }

SavedCapture CaptureHistory::restore() const
{
    if (!available() || busy()) return {};
    QImageReader reader(path); reader.setFormat("PNG");
    SavedCapture result;
    result.image = reader.read();
    const qreal ratio = result.image.text("FastPin.Ratio").toDouble();
    result.image.setDevicePixelRatio(ratio > 0 && ratio <= 8 ? ratio : 1);
    result.position = {result.image.text("FastPin.PositionX").toInt(), result.image.text("FastPin.PositionY").toInt()};
    result.zoom = qBound(0.1, result.image.text("FastPin.Zoom").toDouble(), 8.0);
    for (const auto &key : result.image.textKeys())
        if (key.startsWith("FastPin.")) result.image.setText(key, {});
    return result;
}

QImage CaptureHistory::thumbnail() const
{
    if (!available() || busy()) return {};
    QImageReader reader(path); reader.setFormat("PNG");
    return QImage::fromData(QByteArray::fromBase64(reader.text("FastPin.Thumbnail").toLatin1()), "PNG");
}

QString CaptureHistory::description() const
{
    if (!enabled) return "Recovery is off. Your screenshots are not saved here.";
    if (busy()) return "Saving the latest image locally…";
    if (!available()) return "Your next screenshot or pin will appear here.";
    QImageReader reader(path); reader.setFormat("PNG");
    const QSize size = reader.size();
    const auto time = QDateTime::fromString(reader.text("FastPin.Saved"), Qt::ISODate).toLocalTime();
    return QString("%1 × %2 px · %3").arg(size.width()).arg(size.height()).arg(time.toString("MMM d, HH:mm"));
}
