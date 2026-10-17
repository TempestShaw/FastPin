#include "capture.h"
#include "hotkey.h"
#include "platform.h"
#include <QCursor>
#include <QScreen>
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTextStream>
#include <algorithm>

static QElapsedTimer showClock; // Started just before overlays are shown.

class MeasuredOverlay : public CaptureOverlay {
public:
    using CaptureOverlay::CaptureOverlay;
    QList<double> times;
    double firstPaintAt = -1; // Milliseconds after show() began.
protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (firstPaintAt < 0) firstPaintAt = showClock.nsecsElapsed() / 1e6;
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


// Real capture path on this machine's displays, as Controller::capture() runs it after its
// settle delay: window list, grab, convert, build overlays, show, first paint.
static int captureLatency(QApplication &app)
{
    QMap<QString, QList<double>> steps;
    for (int run = 0; run < 6; ++run) {
        QElapsedTimer total, step; total.start(); step.start();
        const auto windows = desktopWindows();
        steps["window_list_ms"].append(step.nsecsElapsed() / 1e6);
        QList<ScreenFrame> frames;
        double grab = 0, convert = 0;
        for (auto *screen : QGuiApplication::screens()) {
            step.restart();
            const QPixmap pixmap = screen->grabWindow(0);
            grab += step.nsecsElapsed() / 1e6; step.restart();
            ScreenFrame frame{pixmap.toImage(), screen->geometry(), pixmap.devicePixelRatio()};
            frame.image.setDevicePixelRatio(1);
            convert += step.nsecsElapsed() / 1e6;
            frames.append(std::move(frame));
        }
        steps["grab_ms"].append(grab); steps["to_image_ms"].append(convert);
        QList<QRect> geometries;
        for (const auto &frame : frames) geometries.append(frame.geometry);
        step.restart();
        QList<MeasuredOverlay *> overlays;
        for (const qsizetype i : cursorScreenFirst(geometries, QCursor::pos())) {
            overlays.append(new MeasuredOverlay(std::move(frames[i]), windows, {}));
            overlays.last()->setMode(CaptureOverlay::Mode::Translate);
        }
        steps["overlay_build_ms"].append(step.nsecsElapsed() / 1e6);
        const double beforeShow = total.nsecsElapsed() / 1e6;
        showClock.start();
        for (auto *overlay : overlays) overlay->show();
        while (std::any_of(overlays.begin(), overlays.end(), [](auto *o) { return o->firstPaintAt < 0; }) && showClock.elapsed() < 2000)
            app.processEvents(QEventLoop::AllEvents, 5);
        steps["show_to_cursor_screen_paint_ms"].append(overlays.first()->firstPaintAt);
        double last = 0; for (auto *overlay : overlays) last = qMax(last, overlay->firstPaintAt);
        steps["show_to_all_screens_paint_ms"].append(last);
        steps["cursor_screen_visible_ms"].append(beforeShow + overlays.first()->firstPaintAt);
        qDeleteAll(overlays); app.processEvents();
    }
    QJsonObject report{{"scenario", "real displays; excludes the settle delay before the grab"},
        {"screens", int(QGuiApplication::screens().size())}, {"screen_capture_allowed", screenCaptureAllowed(false)}};
    for (auto it = steps.cbegin(); it != steps.cend(); ++it) {
        auto sorted = it.value().mid(1); std::sort(sorted.begin(), sorted.end()); // Run 0 warms up.
        report[it.key()] = sorted[sorted.size() / 2];
    }
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
    return 0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains("--capture-latency")) return captureLatency(app);
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
