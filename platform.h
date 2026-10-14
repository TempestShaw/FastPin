#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

class QScreen;
#ifdef Q_OS_MACOS
#include <functional>
class QWidget;
std::function<void()> watchPinHover(QWidget *window, std::function<void(bool)> changed);
#endif

// OS desktop coordinates: points on macOS, physical pixels on Windows.
struct DesktopWindow {
    QRectF rect;
    quintptr handle = 0;
    quint32 pid = 0;
};
struct DesktopCoordinates {
    QPointF origin;
    qreal scale = 1;
    QPointF nativePoint(QPointF local) const { return origin + local * scale; }
    QRectF localRect(QRectF native) const { return {(native.topLeft() - origin) / scale, native.size() / scale}; }
};
struct ElementResult { QRectF rect; QString message; };
struct OcrResult { QString text; QString error; };

QList<DesktopWindow> desktopWindows(); // Front to back; call before showing capture overlays.
DesktopCoordinates desktopCoordinates(QScreen *screen);
bool elementAccessAllowed(bool prompt);
ElementResult desktopElementAt(DesktopWindow window, QPointF point); // Worker thread only.
OcrResult recognizeText(QImage image); // Worker thread only; no network or bundled model.
OcrResult recognizeTextInWorker(QImage image, const QString &executable);
int runOcrWorker();
