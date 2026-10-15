#include "capture.h"
#include "texteditor.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <cmath>
#include <memory>

QPoint ScreenFrame::pixelAt(QPointF local) const
{
    return {qBound(0, int(std::floor(local.x() * ratio)), image.width() - 1),
            qBound(0, int(std::floor(local.y() * ratio)), image.height() - 1)};
}

QRect ScreenFrame::pixelRect(QRectF local) const
{
    local = local.normalized().intersected(QRectF(QPointF(), geometry.size()));
    const int left = qRound(local.left() * ratio);
    const int top = qRound(local.top() * ratio);
    const int right = qRound(local.right() * ratio);
    const int bottom = qRound(local.bottom() * ratio);
    return QRect(left, top, right - left, bottom - top).intersected(image.rect());
}

QColor ScreenFrame::colorAt(QPointF local) const
{
    return image.isNull() ? QColor() : image.pixelColor(pixelAt(local));
}

QImage ScreenFrame::crop(QRectF local) const
{
    QImage result = image.copy(pixelRect(local));
    result.setDevicePixelRatio(ratio);
    return result;
}

Qt::Edges Selection::edgesAt(QPointF point) const
{
    if (rect.isEmpty() || !rect.adjusted(-6, -6, 6, 6).contains(point))
        return {};
    Qt::Edges result;
    const qreal horizontal = qMin(6.0, rect.width() / 3);
    const qreal vertical = qMin(6.0, rect.height() / 3);
    if (std::abs(point.x() - rect.left()) <= horizontal) result |= Qt::LeftEdge;
    else if (std::abs(point.x() - rect.right()) <= horizontal) result |= Qt::RightEdge;
    if (std::abs(point.y() - rect.top()) <= vertical) result |= Qt::TopEdge;
    else if (std::abs(point.y() - rect.bottom()) <= vertical) result |= Qt::BottomEdge;
    return result;
}

void Selection::begin(QPointF point)
{
    anchor = point;
    initial = rect;
    edges = edgesAt(point);
    moving = !edges && rect.contains(point);
    creating = !edges && !moving;
    dragging = true;
    if (creating) rect = QRectF(point, point);
}

void Selection::drag(QPointF point, QRectF bounds)
{
    if (!dragging) return;
    point.setX(qBound(bounds.left(), point.x(), bounds.right()));
    point.setY(qBound(bounds.top(), point.y(), bounds.bottom()));
    if (creating) {
        rect = QRectF(anchor, point).normalized();
    } else if (moving) {
        const QPointF delta = point - anchor;
        const qreal x = qBound(bounds.left(), initial.x() + delta.x(), bounds.right() - initial.width());
        const qreal y = qBound(bounds.top(), initial.y() + delta.y(), bounds.bottom() - initial.height());
        rect.moveTopLeft({x, y});
    } else {
        QRectF changed = initial;
        if (edges & Qt::LeftEdge) changed.setLeft(point.x());
        if (edges & Qt::RightEdge) changed.setRight(point.x());
        if (edges & Qt::TopEdge) changed.setTop(point.y());
        if (edges & Qt::BottomEdge) changed.setBottom(point.y());
        rect = changed.normalized().intersected(bounds);
    }
}

CaptureOverlay::CaptureOverlay(ScreenFrame value, QList<DesktopWindow> targets, DesktopCoordinates desktop)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), frame(std::move(value)),
      windows(std::move(targets)), coordinates(desktop)
{
    setWindowTitle("FastPin — Select region");
    setAccessibleName("Screenshot selection");
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(frame.geometry);
    toolbar = new AnnotationToolbar(true, this);
    textEditor = new InlineTextEditor(this);
    connect(textEditor, &InlineTextEditor::submitted, this, [this](QString text, QPointF baseline) {
        Annotation a; a.tool = Tool::Text; a.start = baseline * frame.ratio;
        a.text = text; a.color = annotationColor; a.fontSize = 20 * frame.ratio;
        annotations.add(std::move(a)); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update(selection.rect.toAlignedRect());
    });
    toolbar->hide();
    connect(toolbar, &AnnotationToolbar::toolChanged, this, &CaptureOverlay::setTool);
    connect(toolbar, &AnnotationToolbar::colorChanged, this, [this](QColor value) { textEditor->finish(); annotationColor = value; });
    connect(toolbar, &AnnotationToolbar::widthChanged, this, [this](qreal value) { strokeWidth = value; });
    connect(toolbar, &AnnotationToolbar::actionTriggered, this, [this](EditorAction action) {
        textEditor->finish(action != EditorAction::Close);
        if (action == EditorAction::Undo) annotations.undo();
        else if (action == EditorAction::Redo) annotations.redo();
        else if (action == EditorAction::Copy) accept(true);
        else if (action == EditorAction::Pin) accept(false);
        else if (action == EditorAction::Save) save();
        else if (action == EditorAction::Ocr) emit ocrRequested(selectedImage());
        else if (action == EditorAction::Close) emit canceled();
        toolbar->setHistory(annotations.canUndo(), annotations.canRedo());
        update(selection.rect.toAlignedRect());
    });
    pointer = mapFromGlobal(QCursor::pos());
    setCursor(Qt::CrossCursor);
    detectionDelay.setSingleShot(true);
    detectionDelay.setInterval(25);
    connect(&detectionDelay, &QTimer::timeout, this, &CaptureOverlay::queryElement);
    setElementMode(false);
}

void CaptureOverlay::setQuickOcr(bool enabled)
{
    quickOcr = enabled;
    setWindowTitle(enabled ? "FastPin — Select text" : "FastPin — Select region");
    setAccessibleName(enabled ? "OCR selection" : "Screenshot selection");
    update();
}

QRectF CaptureOverlay::detectedLocalRect(QRectF native) const
{
    return coordinates.localRect(native.intersected(target.rect)).intersected(rect());
}

void CaptureOverlay::setElementMode(bool enabled)
{
    textEditor->finish(false);
    ++detectionGeneration;
    elementMode = enabled;
    locked = false;
    selection.end();
    annotations = {};
    tool = Tool::Move;
    toolbar->setTool(tool);
    toolbar->setHistory(false, false);
    toolbar->hide();
    elementRect = {};
    queriedWindow = 0;
    detectionDelay.stop();
    detectionHint.clear();
    elementAllowed = enabled && elementAccessAllowed(false);
    if (enabled && !elementAllowed) detectionHint = "Element needs Accessibility permission; using Window";
    setAccessibleDescription(enabled ? "Detect: Element. Tab switches to Window." : "Detect: Window. Tab switches to Element.");
    updateDetection();
    update();
}

bool CaptureOverlay::event(QEvent *e)
{
    // QWidget normally consumes Tab for focus traversal before keyPressEvent.
    if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Tab) {
        if (!static_cast<QKeyEvent *>(e)->isAutoRepeat()) {
            setElementMode(!elementMode);
            emit detectionModeChanged(elementMode);
        }
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void CaptureOverlay::updateDetection()
{
    if (locked || selection.isDragging()) return;
    const QRegion before = decorationRegion() + QRegion(selection.rect.toAlignedRect());
    const auto point = coordinates.nativePoint(pointer);
    target = {};
    for (const auto &window : windows) {
        if (window.rect.contains(point)) { target = window; break; }
    }
    selection.rect = detectedLocalRect(target.rect);
    if (elementMode && elementAllowed && target.handle) {
        // Keep the visible element while a newer hit-test is pending within its bounds.
        if (queriedWindow == target.handle && elementRect.contains(point)) selection.rect = detectedLocalRect(elementRect);
        if ((queriedWindow != target.handle || queriedPoint != point) && !queryBusy && !detectionDelay.isActive()) detectionDelay.start();
    }
    // Include changed mask, but avoid repainting the interiors of unchanged rectangles.
    update((before - QRegion(selection.rect.toAlignedRect())) + decorationRegion() +
           (QRegion(selection.rect.toAlignedRect()) - before));
}

void CaptureOverlay::queryElement()
{
    if (!elementMode || locked || selection.isDragging() || queryBusy || !target.handle) return;
    const auto window = target;
    const auto point = coordinates.nativePoint(pointer);
    const auto generation = detectionGeneration;
    queryBusy = true;
    auto result = std::make_shared<ElementResult>();
    auto *worker = QThread::create([window, point, result] { *result = desktopElementAt(window, point); });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, window, point, generation, result] {
        queryBusy = false;
        applyElementResult(window, point, generation, *result);
    });
    worker->start();
}

void CaptureOverlay::applyElementResult(DesktopWindow window, QPointF point, quint64 generation, ElementResult result)
{
    if (!elementMode || locked || selection.isDragging()) return;
    const auto current = coordinates.nativePoint(pointer);
    if (generation == detectionGeneration && window.handle == target.handle &&
        (point == current || result.rect.intersected(window.rect).contains(current))) {
        // A failed hit-test must not briefly erase a still-valid visible element.
        if (queriedWindow != window.handle || !result.rect.isEmpty() || !elementRect.contains(current)) elementRect = result.rect.intersected(window.rect);
        queriedWindow = window.handle;
        queriedPoint = point;
        detectionHint = result.message;
    }
    updateDetection();
    update(QRect(12, height() - 65, width() - 24, 53));
}

QRect CaptureOverlay::magnifierRect(QPointF point) const
{
    const int w = 174, h = 180;
    int x = qRound(point.x()) + 24, y = qRound(point.y()) + 24;
    if (x + w > width()) x = qRound(point.x()) - w - 24;
    if (y + h > height()) y = qRound(point.y()) - h - 24;
    return {qMax(0, x), qMax(0, y), w, h};
}

QRegion CaptureOverlay::decorationRegion() const
{
    QRegion result(magnifierRect(pointer).adjusted(-2, -2, 2, 2));
    if (!selection.rect.isEmpty()) {
        const QRect r = selection.rect.toAlignedRect();
        result += QRegion(r.adjusted(-9, -9, 9, 9)) - QRegion(r.adjusted(9, 9, -9, -9));
        result += QRect(qBound(0, r.left(), qMax(0, width() - 166)), qMax(0, r.top() - 29), 166, 24);
    }
    return result;
}

void CaptureOverlay::movePointer(QPointF point)
{
    QRegion dirty = decorationRegion();
    const QRect old = selection.rect.toAlignedRect();
    pointer = point;
    // Snap to a physical pixel boundary, including on Retina screens.
    const QPoint pixel = frame.pixelAt(point);
    if (selection.isDragging()) selection.drag(QPointF(pixel) / frame.ratio, rect());
    else updateDetection();
    dirty += QRegion(old).xored(QRegion(selection.rect.toAlignedRect()));
    dirty += decorationRegion();
    if (toolbar->isVisible()) positionTools();
    const auto edges = tool == Tool::Move ? selection.edgesAt(pointer) : Qt::Edges{};
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge) || (edges & Qt::RightEdge && edges & Qt::BottomEdge))
        setCursor(Qt::SizeFDiagCursor);
    else if ((edges & Qt::RightEdge && edges & Qt::TopEdge) || (edges & Qt::LeftEdge && edges & Qt::BottomEdge))
        setCursor(Qt::SizeBDiagCursor);
    else if (edges & (Qt::LeftEdge | Qt::RightEdge)) setCursor(Qt::SizeHorCursor);
    else if (edges & (Qt::TopEdge | Qt::BottomEdge)) setCursor(Qt::SizeVerCursor);
    else if (tool == Tool::Move && locked && selection.rect.contains(point)) setCursor(Qt::SizeAllCursor);
    else setCursor(Qt::CrossCursor);
    update(dirty);
}

void CaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this); // Qt clips to the dirty region. No screenshot copy here.
    p.drawImage(QRectF(rect()), frame.image, QRectF(frame.image.rect()));
    const QColor mask(9, 13, 22, 104);
    const QRectF selected = selection.rect;
    if (selected.isEmpty()) p.fillRect(rect(), mask);
    else {
        p.save(); p.setClipRect(selected, Qt::IntersectClip); p.scale(1 / frame.ratio, 1 / frame.ratio);
        p.setRenderHint(QPainter::Antialiasing); annotations.paint(p); p.restore();
        p.fillRect(QRectF(0, 0, width(), selected.top()), mask);
        p.fillRect(QRectF(0, selected.bottom(), width(), height() - selected.bottom()), mask);
        p.fillRect(QRectF(0, selected.top(), selected.left(), selected.height()), mask);
        p.fillRect(QRectF(selected.right(), selected.top(), width() - selected.right(), selected.height()), mask);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#287cf4"), 1));
        p.drawRect(selected);
        p.setBrush(Qt::white);
        for (auto point : {selected.topLeft(), selected.topRight(), selected.bottomLeft(), selected.bottomRight(),
                           QPointF(selected.center().x(), selected.top()), QPointF(selected.center().x(), selected.bottom()),
                           QPointF(selected.left(), selected.center().y()), QPointF(selected.right(), selected.center().y())})
            p.drawEllipse(point, 3, 3);
        const QRect pixels = frame.pixelRect(selected);
        QRect label(qBound(0, qRound(selected.left()), qMax(0, width() - 166)), qMax(0, qRound(selected.top()) - 29), 166, 24);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#242a35")); p.drawRoundedRect(label, 5, 5);
        p.setPen(Qt::white);
        p.drawText(label.adjusted(8, 0, 0, 0), Qt::AlignVCenter,
                   QString("%1 × %2 px").arg(pixels.width()).arg(pixels.height()));
    }

    if (tool == Tool::Move) {
    const QRect box = magnifierRect(pointer);
    p.fillRect(box, QColor(24, 28, 36));
    const QPoint pixel = frame.pixelAt(pointer);
    const QRect source(pixel.x() - 5, pixel.y() - 5, 11, 11);
    const QRect glass(box.x() + 21, box.y() + 8, 132, 132);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(glass, frame.image, source);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(QRect(glass.x() + 60, glass.y() + 60, 12, 12));
    const QColor color = frame.colorAt(pointer);
    const QString code = hex ? color.name().toUpper() : QString("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
    p.drawText(box.adjusted(8, 143, -8, -6), Qt::AlignCenter, code + "  [C]\n" + QString("%1, %2 · Shift: RGB/HEX").arg(pixel.x()).arg(pixel.y()));
    }
    const QRect help(12, height() - 65, qMin(width() - 24, detectionHint.isEmpty() ? 460 : 720), detectionHint.isEmpty() ? 30 : 53);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#242a35")); p.drawRoundedRect(help, 6, 6); p.setPen(Qt::white);
    p.drawText(help.adjusted(8, 0, 0, 0), Qt::AlignVCenter,
               QString("%1  [Tab] · %2 · Esc to cancel%3")
                   .arg(elementMode ? "Element" : "Window",
                        quickOcr ? "Select to copy text" : "Drag to select · Enter to pin",
                        detectionHint.isEmpty() ? QString() : "\n" + detectionHint));
}

void CaptureOverlay::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) { emit canceled(); return; }
    if (e->button() != Qt::LeftButton) return;
    movePointer(e->position());
    if (tool != Tool::Move && locked && selection.rect.contains(e->position())) {
        const QPointF point = e->position() * frame.ratio;
        if (tool == Tool::Text) {
            textEditor->begin(e->position(), selection.rect, annotationColor, 20);
        } else annotations.begin(tool, point, annotationColor, strokeWidth * frame.ratio);
        return;
    }
    if (tool != Tool::Move) return;
    const auto old = decorationRegion();
    const QRegion previous(selection.rect.toAlignedRect());
    detectClick = !locked;
    pressPoint = e->position();
    pressedPreview = selection.rect;
    if (detectClick) selection.rect = {};
    selection.begin(QPointF(frame.pixelAt(e->position())) / frame.ratio);
    update(old + decorationRegion() + previous.xored(QRegion(selection.rect.toAlignedRect())));
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent *e)
{
    if (annotations.isDrawing()) {
        const QPointF point(qBound(selection.rect.left(), e->position().x(), selection.rect.right()),
                            qBound(selection.rect.top(), e->position().y(), selection.rect.bottom()));
        const QRectF dirty = annotations.drag(point * frame.ratio);
        update(QRectF(dirty.topLeft() / frame.ratio, dirty.size() / frame.ratio).toAlignedRect().adjusted(-2, -2, 2, 2));
    } else movePointer(e->position());
}
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (annotations.isDrawing()) {
            mouseMoveEvent(e); annotations.end(); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update(selection.rect.toAlignedRect()); return;
        }
        if (tool != Tool::Move) return;
        movePointer(e->position());
        selection.end();
        const QRegion previous(selection.rect.toAlignedRect());
        if (detectClick && QLineF(pressPoint, e->position()).length() < 3) selection.rect = pressedPreview;
        locked = !selection.rect.isEmpty();
        if (quickOcr && locked) { emit ocrRequested(selectedImage()); return; }
        if (locked) { positionTools(); toolbar->show(); }
        update(previous + decorationRegion() + QRegion(selection.rect.toAlignedRect()));
    }
}
void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (tool == Tool::Move && e->button() == Qt::LeftButton && selection.rect.contains(e->position())) accept(false);
}

void CaptureOverlay::accept(bool clipboardOnly)
{
    textEditor->finish();
    if (frame.pixelRect(selection.rect).isEmpty()) return;
    if (quickOcr) { emit ocrRequested(selectedImage()); return; }
    emit accepted(selectedImage(), frame.geometry.topLeft() + selection.rect.topLeft().toPoint(), clipboardOnly);
}

void CaptureOverlay::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) emit canceled();
    else if (e->matches(QKeySequence::Copy)) accept(true);
    else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) accept(false);
    else if (e->matches(QKeySequence::Save)) save();
    else if (e->matches(QKeySequence::Undo)) { annotations.undo(); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update(selection.rect.toAlignedRect()); }
    else if (e->matches(QKeySequence::Redo)) { annotations.redo(); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update(selection.rect.toAlignedRect()); }
    else if (e->key() == Qt::Key_O && !frame.pixelRect(selection.rect).isEmpty()) emit ocrRequested(selectedImage());
    else if (e->key() == Qt::Key_M) setTool(Tool::Move);
    else if (e->key() == Qt::Key_P) setTool(Tool::Pen);
    else if (e->key() == Qt::Key_A) setTool(Tool::Arrow);
    else if (e->key() == Qt::Key_R) setTool(Tool::Rectangle);
    else if (e->key() == Qt::Key_T) setTool(Tool::Text);
    else if (e->key() == Qt::Key_C) {
        const auto color = frame.colorAt(pointer);
        const QString code = hex ? color.name().toUpper() : QString("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
        QApplication::clipboard()->setText(code);
        emit colorCopied(code);
    } else if (e->key() == Qt::Key_Shift) { hex = !hex; update(magnifierRect(pointer)); }
    else if (e->key() >= Qt::Key_Left && e->key() <= Qt::Key_Down) {
        QPointF delta;
        if (e->key() == Qt::Key_Left) delta.setX(-1 / frame.ratio);
        if (e->key() == Qt::Key_Right) delta.setX(1 / frame.ratio);
        if (e->key() == Qt::Key_Up) delta.setY(-1 / frame.ratio);
        if (e->key() == Qt::Key_Down) delta.setY(1 / frame.ratio);
        QPointF next = pointer + delta;
        next.setX(qBound(0.0, next.x(), (frame.image.width() - 1) / frame.ratio));
        next.setY(qBound(0.0, next.y(), (frame.image.height() - 1) / frame.ratio));
        movePointer(next); // Keep half-point precision even when QCursor is integer-valued.
    }
}

void CaptureOverlay::setTool(Tool value)
{
    if (selection.rect.isEmpty()) return;
    textEditor->finish();
    const QRegion dirty = decorationRegion();
    locked = true; tool = value; toolbar->setTool(tool);
    positionTools(); toolbar->show(); setFocus(); movePointer(pointer); update(dirty);
}

void CaptureOverlay::positionTools()
{
    toolbar->adjustSize();
    const int x = qBound(8, qRound(selection.rect.right()) - toolbar->width(), qMax(8, width() - toolbar->width() - 8));
    int y = qRound(selection.rect.bottom()) + 10;
    if (y + toolbar->height() > height() - 70) y = qRound(selection.rect.top()) - toolbar->height() - 36;
    toolbar->move(x, qBound(8, y, qMax(8, height() - toolbar->height() - 8)));
}

QImage CaptureOverlay::selectedImage() const
{
    QImage result = frame.crop(selection.rect);
    if (result.isNull()) return result;
    result.setDevicePixelRatio(1);
    QPainter p(&result); p.setRenderHint(QPainter::Antialiasing);
    p.translate(-frame.pixelRect(selection.rect).topLeft()); annotations.paint(p, false); p.end();
    result.setDevicePixelRatio(frame.ratio);
    return result;
}

void CaptureOverlay::save()
{
    textEditor->finish();
    if (frame.pixelRect(selection.rect).isEmpty()) return;
    const auto path = QFileDialog::getSaveFileName(this, "Save screenshot", "screenshot.png", "PNG image (*.png)");
    if (path.isEmpty()) return;
    if (selectedImage().save(path, "PNG")) emit canceled();
    else QMessageBox::warning(this, "Save failed", "Could not write the image to this location.");
}
