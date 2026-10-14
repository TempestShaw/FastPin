#include "platform.h"

#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

OcrResult recognizeTextInWorker(QImage image, const QString &executable)
{
    // Encoding and process waits happen on the caller's worker thread.
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) return {{}, "Could not encode the OCR image."};
    QProcess worker;
    worker.start(executable, {"--ocr-worker"});
    if (!worker.waitForStarted(5000)) return {{}, "Could not start OCR: " + worker.errorString()};
    worker.write(png);
    worker.closeWriteChannel();
    if (!worker.waitForFinished(30000)) {
        worker.kill(); worker.waitForFinished(1000);
        return {{}, "OCR timed out. Try a smaller region."};
    }
    const auto result = QJsonDocument::fromJson(worker.readAllStandardOutput());
    if (worker.exitStatus() != QProcess::NormalExit || worker.exitCode() != 0 || !result.isObject())
        return {{}, "The OCR worker failed."};
    return {result.object()["text"].toString(), result.object()["error"].toString()};
}

int runOcrWorker()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
    const auto image = QImage::fromData(input.readAll(), "PNG");
    const auto result = image.isNull() ? OcrResult{{}, "Could not read the OCR image."} : recognizeText(image);
    if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    output.write(QJsonDocument(QJsonObject{{"text", result.text}, {"error", result.error}}).toJson(QJsonDocument::Compact));
    output.flush();
    return 0; // The OS reclaims the OCR engine's model/cache pages on exit.
}
