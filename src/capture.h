#pragma once

#include "platform.h"
#include "toolbar.h"
#include <QImage>
#include <QTimer>
#include <QWidget>
class InlineTextEditor;
class QLabel;

struct ScreenFrame {
    QImage image; // Physical pixels, DPR 1; never mutated while selecting.
    QRect geometry;
    qreal ratio = 1;
    QPoint pixelAt(QPointF local) const;
    QRect pixelRect(QRectF local) const;
    QColor colorAt(QPointF local) const;
    QImage crop(QRectF local) const;
};

// Milliseconds to wait after hiding FastPin before grabbing the screen. A menu click or a
// window FastPin just hid needs one compositor pass to disappear; a shortcut with nothing
// to hide can grab at once.
int captureSettleDelayMs(bool fromShortcut, bool hidWindows);

// Screen indices with the one under the pointer first, so the screen being looked at
// gets its overlay before the others.
QList<qsizetype> cursorScreenFirst(const QList<QRect> &screens, QPoint pointer);

class Selection {
public:
    QRectF rect;
    Qt::Edges edgesAt(QPointF point) const;
    void begin(QPointF point);
    void drag(QPointF point, QRectF bounds);
    void end() { dragging = false; }
    bool isDragging() const { return dragging; }
private:
    QPointF anchor;
    QRectF initial;
    Qt::Edges edges;
    bool moving = false;
    bool creating = false;
    bool dragging = false;
};

class CaptureOverlay : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Screenshot, CopyText, Translate };
    explicit CaptureOverlay(ScreenFrame frame, QList<DesktopWindow> windows = {}, DesktopCoordinates coordinates = {});
    void setElementMode(bool enabled);
    void setMode(Mode mode);
    bool isElementMode() const { return elementMode; }
signals:
    void accepted(QImage image, QPoint position, bool clipboardOnly);
    void ocrRequested(QImage image);
    void translateRequested(QImage image);
    void colorCopied(QString color);
    void detectionModeChanged(bool elementMode);
    void canceled();
protected:
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    friend class NativeTests;
    QRect magnifierRect(QPointF point) const;
    QRegion decorationRegion() const;
    void movePointer(QPointF point);
    void accept(bool clipboardOnly);
    void updateDetection();
    void queryElement();
    void applyElementResult(DesktopWindow window, QPointF point, quint64 generation, ElementResult result);
    QRectF detectedLocalRect(QRectF native) const;
    void setTool(Tool tool);
    void positionTools();
    void updateShortcutHint();
    QImage selectedImage() const;
    void save();
    ScreenFrame frame;
    Selection selection;
    QPointF pointer;
    bool hex = true;
    Mode mode = Mode::Screenshot;
    AnnotationLayer annotations;
    AnnotationToolbar *toolbar;
    InlineTextEditor *textEditor;
    qsizetype editingText = -1;
    QLabel *shortcutHint = nullptr;
    bool shortcutHintStale = false; // Rich-text layout waits until the overlay is shown.
    Tool tool = Tool::Move;
    QColor annotationColor = QColor("#ff5a65");
    qreal strokeWidth = 3;
    QList<DesktopWindow> windows;
    DesktopCoordinates coordinates;
    QTimer detectionDelay;
    DesktopWindow target;
    QPointF pressPoint, queriedPoint;
    QRectF pressedPreview, elementRect;
    quintptr queriedWindow = 0;
    quint64 detectionGeneration = 0;
    QString detectionHint;
    bool elementMode = false, elementAllowed = false, locked = false, detectClick = false, queryBusy = false;
};
