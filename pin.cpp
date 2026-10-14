#include "pin.h"
#include "texteditor.h"
#include "platform.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <cmath>

PinWindow::PinWindow(QImage value)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), image(std::move(value))
{
    ratio = image.devicePixelRatio();
    image.setDevicePixelRatio(1);
    setWindowTitle("FastPin — Pinned image");
    setAccessibleName("Pinned image. Hover and press Space for annotation tools. Wheel to zoom. Double-click to close.");
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFocusPolicy(Qt::StrongFocus);
    toolbar = new AnnotationToolbar(false, this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    toolbar->setAttribute(Qt::WA_ShowWithoutActivating);
    textEditor = new InlineTextEditor(this);
    textEditor->installEventFilter(this);
    connect(&toolShortcut, &Hotkeys::pinTools, this, [this] { if (canToggleTools()) toggleTools(); });
    connect(textEditor, &InlineTextEditor::submitted, this, [this](QString text, QPointF baseline) {
        Annotation a; a.tool = Tool::Text; a.start = imagePoint(baseline);
        a.text = text; a.color = color; a.fontSize = 20 * ratio; addAnnotation(std::move(a));
    });
    connect(toolbar, &AnnotationToolbar::toolChanged, this, &PinWindow::setTool);
    connect(toolbar, &AnnotationToolbar::colorChanged, this, [this](QColor value) { textEditor->finish(); color = value; });
    connect(toolbar, &AnnotationToolbar::widthChanged, this, [this](qreal value) { strokeWidth = value; });
    connect(toolbar, &AnnotationToolbar::actionTriggered, this, [this](EditorAction action) {
        textEditor->finish(action != EditorAction::Close);
        if (action == EditorAction::Undo) undo();
        else if (action == EditorAction::Redo) redo();
        else if (action == EditorAction::Copy) copy();
        else if (action == EditorAction::Save) save();
        else if (action == EditorAction::Ocr) emit ocrRequested(exportedImage());
        else if (action == EditorAction::Close) { toolbar->hide(); setTool(Tool::Move); }
    });
    setZoom(1);
    setCursor(Qt::SizeAllCursor);
}

PinWindow::~PinWindow()
{
#ifdef Q_OS_MACOS
    if (stopHover) stopHover();
#endif
}

QPointF PinWindow::imagePoint(QPointF point) const
{
    QPointF result = point * (ratio / zoom);
    return {qBound(0.0, result.x(), qreal(image.width())), qBound(0.0, result.y(), qreal(image.height()))};
}

void PinWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.scale(zoom / ratio, zoom / ratio);
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1);
    p.drawImage(QPointF(), image);
    p.setRenderHint(QPainter::Antialiasing);
    annotations.paint(p);
}

QImage PinWindow::exportedImage() const
{
    QImage result = image.copy(); // Only export allocates a composite image.
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    annotations.paint(p, false);
    p.end();
    result.setDevicePixelRatio(ratio);
    return result;
}

void PinWindow::setZoom(qreal value)
{
    textEditor->finish();
    zoom = qBound(0.1, value, 8.0);
    resize(qMax(1, qRound(image.width() * zoom / ratio)), qMax(1, qRound(image.height() * zoom / ratio)));
    if (toolbar->isVisible()) positionTools();
    update();
}

void PinWindow::setClickThrough(bool enabled)
{
    textEditor->finish();
    const QPoint position = pos();
    clickThrough = enabled;
    if (enabled) toolbar->hide();
    setWindowFlag(Qt::WindowTransparentForInput, enabled);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, enabled);
    move(position);
    show();
    updateToolsShortcut();
}

void PinWindow::addAnnotation(Annotation annotation)
{
    annotations.add(std::move(annotation));
    toolbar->setHistory(annotations.canUndo(), annotations.canRedo());
    update();
}

void PinWindow::undo() { annotations.undo(); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update(); }
void PinWindow::redo() { annotations.redo(); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update(); }
void PinWindow::copy() { textEditor->finish(); QApplication::clipboard()->setImage(exportedImage()); }

void PinWindow::save()
{
    textEditor->finish();
    const auto path = QFileDialog::getSaveFileName(this, "Save pinned image", "screenshot.png", "PNG image (*.png)");
    if (!path.isEmpty() && !exportedImage().save(path, "PNG"))
        QMessageBox::warning(this, "Save failed", "Could not write the image to this location.");
}

void PinWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    if (tool == Tool::Move) {
        moving = true; dragStart = e->globalPosition(); windowStart = pos();
    } else if (tool == Tool::Text) {
        textEditor->begin(e->position(), rect(), color, 20 * zoom);
    } else {
        annotations.begin(tool, imagePoint(e->position()), color, strokeWidth * ratio);
        update();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (moving) move(windowStart + (e->globalPosition() - dragStart).toPoint());
    else if (annotations.isDrawing()) {
        const QRectF dirty = annotations.drag(imagePoint(e->position()));
        const qreal factor = zoom / ratio;
        update(QRectF(dirty.topLeft() * factor, dirty.size() * factor).toAlignedRect().adjusted(-2, -2, 2, 2));
    }
}

void PinWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    mouseMoveEvent(e);
    moving = false;
    if (annotations.isDrawing()) {
        annotations.end(); toolbar->setHistory(annotations.canUndo(), annotations.canRedo()); update();
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (tool == Tool::Move && e->button() == Qt::LeftButton) close();
}

void PinWindow::wheelEvent(QWheelEvent *e)
{
    const QPointF anchor = imagePoint(e->position());
    const QPoint global = mapToGlobal(e->position().toPoint());
    setZoom(zoom * std::pow(1.1, e->angleDelta().y() / 120.0));
    move(global - (anchor * (zoom / ratio)).toPoint());
    e->accept();
}

void PinWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Copy)) copy();
    else if (e->matches(QKeySequence::Save)) save();
    else if (e->matches(QKeySequence::Undo)) undo();
    else if (e->matches(QKeySequence::Redo)) redo();
    else if (e->key() == Qt::Key_Escape) close();
    else if (e->key() == Qt::Key_M) setTool(Tool::Move);
    else if (e->key() == Qt::Key_P) setTool(Tool::Pen);
    else if (e->key() == Qt::Key_A) setTool(Tool::Arrow);
    else if (e->key() == Qt::Key_R) setTool(Tool::Rectangle);
    else if (e->key() == Qt::Key_T) setTool(Tool::Text);
    else if (e->key() == Qt::Key_O) emit ocrRequested(exportedImage());
    else if (e->key() == Qt::Key_E) toggleTools();
    else if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier && !e->isAutoRepeat() && canToggleTools()) toggleTools();
    else if (e->key() == Qt::Key_0) setZoom(1);
    else if (e->key() == Qt::Key_Left) move(pos() + QPoint(-1, 0));
    else if (e->key() == Qt::Key_Right) move(pos() + QPoint(1, 0));
    else if (e->key() == Qt::Key_Up) move(pos() + QPoint(0, -1));
    else if (e->key() == Qt::Key_Down) move(pos() + QPoint(0, 1));
    setCursor(tool == Tool::Move ? Qt::SizeAllCursor : Qt::CrossCursor);
}

void PinWindow::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    menu.addAction("Annotate\tSpace", this, &PinWindow::showTools);
    menu.addSeparator();
    menu.addAction("Undo", this, &PinWindow::undo)->setEnabled(annotations.canUndo());
    menu.addAction("Redo", this, &PinWindow::redo)->setEnabled(annotations.canRedo());
    menu.addAction("Copy image", this, &PinWindow::copy);
    menu.addAction("OCR → copy text · O", this, [this] { emit ocrRequested(exportedImage()); });
    menu.addAction("Save PNG…", this, &PinWindow::save);
    menu.addAction("Original size · 0", this, [this] { setZoom(1); });
    menu.addAction("Click through (restore from tray)", this, [this] { setClickThrough(true); });
    menu.addSeparator();
    menu.addAction("Close", this, &QWidget::close);
    menu.exec(e->globalPos());
}

void PinWindow::closeEvent(QCloseEvent *e) { textEditor->finish(); emit closed(); QWidget::closeEvent(e); }

void PinWindow::setTool(Tool value)
{
    textEditor->finish();
    tool = value; toolbar->setTool(tool);
    setCursor(tool == Tool::Move ? Qt::SizeAllCursor : Qt::CrossCursor);
    if (isVisible()) { activateWindow(); setFocus(); }
}

void PinWindow::positionTools()
{
    toolbar->adjustSize();
    const QRect area = screen()->availableGeometry();
    const QPoint bottom = mapToGlobal(QPoint(width(), height()));
    int x = qBound(area.left() + 8, bottom.x() - toolbar->width(), area.right() - toolbar->width() - 8);
    int y = bottom.y() + 10;
    if (y + toolbar->height() > area.bottom() - 8) y = mapToGlobal(QPoint()).y() - toolbar->height() - 10;
    toolbar->move(x, qMax(area.top() + 8, y));
}

void PinWindow::showTools() { if (!clickThrough) { toolbar->show(); positionTools(); } }
void PinWindow::toggleTools()
{
    if (toolbar->isVisible()) {
        textEditor->finish(); toolbar->hide();
        tool = Tool::Move; toolbar->setTool(tool); setCursor(Qt::SizeAllCursor);
    }
    else showTools();
}

bool PinWindow::canToggleTools() const
{
    return hovered && isVisible() && !clickThrough && !textEditor->isVisible() && rect().contains(mapFromGlobal(QCursor::pos()));
}

void PinWindow::updateToolsShortcut() { toolShortcut.enablePinTools(QGuiApplication::platformName() != "offscreen" && canToggleTools()); }

bool PinWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == textEditor && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) updateToolsShortcut();
    return QWidget::eventFilter(object, event);
}

void PinWindow::enterEvent(QEnterEvent *e)
{
    QWidget::enterEvent(e);
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") return; // Qt ignores inactive-app hover; the native tracking area handles it.
#endif
    hovered = true; updateToolsShortcut();
}

void PinWindow::leaveEvent(QEvent *e)
{
    QWidget::leaveEvent(e);
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa") return;
#endif
    hovered = false; updateToolsShortcut();
}

void PinWindow::moveEvent(QMoveEvent *e) { QWidget::moveEvent(e); if (toolbar->isVisible()) positionTools(); }
void PinWindow::hideEvent(QHideEvent *e) { toolbar->hide(); hovered = false; updateToolsShortcut(); QWidget::hideEvent(e); }
void PinWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
#ifdef Q_OS_MACOS
    if (stopHover) stopHover();
    stopHover = watchPinHover(this, [this](bool inside) { hovered = inside; updateToolsShortcut(); });
#endif
    hovered = rect().contains(mapFromGlobal(QCursor::pos())); updateToolsShortcut();
}
