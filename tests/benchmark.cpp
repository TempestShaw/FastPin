#include "capture.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTextStream>
#include <algorithm>

class MeasuredOverlay : public CaptureOverlay {
public:
    using CaptureOverlay::CaptureOverlay;
    QList<double> times;
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QElapsedTimer timer; timer.start();
        CaptureOverlay::paintEvent(event);
        times.append(timer.nsecsElapsed() / 1e6);
    }
};

static void mouse(QWidget &widget, QEvent::Type type, QPointF point)
{
    const bool press = type == QEvent::MouseButtonPress;
    QMouseEvent event(type, point, widget.mapToGlobal(point), press ? Qt::LeftButton : Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
    QApplication::processEvents();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ScreenFrame frame{QImage(3840, 2160, QImage::Format_RGB32), QRect(0, 0, 1920, 1080), 2};
    for (int y = 0; y < frame.image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(frame.image.scanLine(y));
        for (int x = 0; x < frame.image.width(); ++x) row[x] = qRgb(x % 256, y % 256, (x + y) % 256);
    }
    MeasuredOverlay overlay(frame); overlay.show(); app.processEvents();
    mouse(overlay, QEvent::MouseButtonPress, {150, 180});
    for (int i = 0; i < 320; ++i)
        mouse(overlay, QEvent::MouseMove, {400.0 + i * 3, 500.0 + i % 120});
    const auto samples = overlay.times.mid(20); // Warm-up excludes initial full-window paints.
    if (samples.isEmpty()) return 1;
    auto sorted = samples; std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&](double q) { return sorted[qMin(sorted.size() - 1, qsizetype(q * sorted.size()))]; };
    QJsonObject report{{"scenario", "synthetic 3840x2160 screenshot; selection drag; offscreen raster paint only"},
        {"samples", int(sorted.size())}, {"paint_ms_p50", percentile(0.50)}, {"paint_ms_p95", percentile(0.95)},
        {"paint_ms_p99", percentile(0.99)}, {"includes_os_capture_or_display_present", false}};
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
}
