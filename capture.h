#pragma once

#include "platform.h"
#include "toolbar.h"
#include <QImage>
#include <QTimer>
#include <QWidget>
class InlineTextEditor;

struct ScreenFrame {
    QImage image; // Physical pixels, DPR 1; never mutated while selecting.
    QRect geometry;
    qreal ratio = 1;
    QPoint pixelAt(QPointF local) const;
    QRect pixelRect(QRectF local) const;
    QColor colorAt(QPointF local) const;
    QImage crop(QRectF local) const;
};

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
    explicit CaptureOverlay(ScreenFrame frame, QList<DesktopWindow> windows = {}, DesktopCoordinates coordinates = {});
    void setElementMode(bool enabled);
    bool isElementMode() const { return elementMode; }
signals:
    void accepted(QImage image, QPoint position, bool clipboardOnly);
    void ocrRequested(QImage image);
    void detectionModeChanged(bool elementMode);
    void canceled();
protected:
    bool event(QEvent *) override;
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
    QImage selectedImage() const;
    void save();
    ScreenFrame frame;
    Selection selection;
    QPointF pointer;
    bool hex = true;
    AnnotationLayer annotations;
    AnnotationToolbar *toolbar;
    InlineTextEditor *textEditor;
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
