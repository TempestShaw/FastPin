#include "toolbar.h"
#include <QButtonGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

QIcon toolIcon(const QString &name, QColor color)
{
    QPixmap image(48, 48); image.setDevicePixelRatio(2); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "pen") {
        p.drawPolygon(QPolygonF{{5, 15}, {15, 5}, {19, 9}, {9, 19}, {4, 20}});
        p.drawLine(13, 7, 17, 11);
    } else if (name == "arrow") {
        p.drawLine(5, 19, 19, 5); p.drawPolyline(QPolygonF{{10, 5}, {19, 5}, {19, 14}});
    } else if (name == "rectangle") p.drawRoundedRect(QRectF(4, 5, 16, 14), 2, 2);
    else if (name == "text") { p.drawLine(5, 5, 19, 5); p.drawLine(12, 5, 12, 19); p.drawLine(8, 19, 16, 19); }
    else if (name == "move") {
        p.drawLine(4, 12, 20, 12); p.drawLine(12, 4, 12, 20);
        p.drawPolyline(QPolygonF{{7, 9}, {4, 12}, {7, 15}}); p.drawPolyline(QPolygonF{{17, 9}, {20, 12}, {17, 15}});
        p.drawPolyline(QPolygonF{{9, 7}, {12, 4}, {15, 7}}); p.drawPolyline(QPolygonF{{9, 17}, {12, 20}, {15, 17}});
    } else if (name == "undo" || name == "redo") {
        if (name == "redo") { p.translate(24, 0); p.scale(-1, 1); }
        p.drawPolyline(QPolygonF{{9, 5}, {4, 10}, {9, 15}});
        QPainterPath path; path.moveTo(4, 10); path.lineTo(14, 10); path.cubicTo(22, 10, 21, 20, 13, 20); p.drawPath(path);
    } else if (name == "copy") {
        p.drawRoundedRect(QRectF(8, 8, 12, 13), 2, 2); p.drawPolyline(QPolygonF{{15, 4}, {4, 4}, {4, 16}});
    } else if (name == "save") {
        p.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2); p.drawRect(QRectF(8, 4, 8, 6)); p.drawRect(QRectF(8, 15, 8, 5));
    } else if (name == "pin") {
        p.drawPolygon(QPolygonF{{8, 4}, {16, 4}, {15, 11}, {19, 15}, {5, 15}, {9, 11}}); p.drawLine(12, 15, 12, 21);
    } else if (name == "ocr") {
        for (const auto &r : {QRectF(3, 3, 5, 5), QRectF(16, 3, 5, 5), QRectF(3, 16, 5, 5), QRectF(16, 16, 5, 5)}) {
            p.drawLine(r.topLeft(), r.topRight()); p.drawLine(r.topLeft(), r.bottomLeft());
        }
        p.drawLine(8, 8, 16, 8); p.drawLine(12, 8, 12, 16);
    } else if (name == "color") { p.setBrush(color); p.drawEllipse(QRectF(5, 5, 14, 14)); }
    else if (name == "width") { p.drawLine(5, 6, 19, 6); p.setPen(QPen(color, 3)); p.drawLine(5, 12, 19, 12); p.setPen(QPen(color, 5)); p.drawLine(5, 19, 19, 19); }
    else if (name == "capture") {
        p.drawPolyline(QPolygonF{{3, 9}, {3, 3}, {9, 3}}); p.drawPolyline(QPolygonF{{15, 3}, {21, 3}, {21, 9}});
        p.drawPolyline(QPolygonF{{21, 15}, {21, 21}, {15, 21}}); p.drawPolyline(QPolygonF{{9, 21}, {3, 21}, {3, 15}});
        p.drawRect(QRectF(8, 8, 8, 8));
    } else if (name == "close") { p.drawLine(6, 6, 18, 18); p.drawLine(18, 6, 6, 18); }
    p.end(); return QIcon(image);
}

AnnotationToolbar::AnnotationToolbar(bool capture, QWidget *parent, Qt::WindowFlags flags)
    : QWidget(parent, flags), tools(new QButtonGroup(this))
{
    setObjectName("annotationToolbar");
    setWindowTitle("FastPin — Annotation tools");
    setAccessibleName("Annotation tools");
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    if (isWindow()) setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("QToolButton { border: 0; border-radius: 6px; padding: 0; background: transparent; }"
                  "QToolButton:hover { background: #3b4555; } QToolButton:checked { background: #287cf4; }"
                  "QToolButton::menu-indicator { image: none; }");
    auto *layout = new QHBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8); layout->setSpacing(3);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    auto button = [this, layout](const QString &name, const QString &label) {
        auto *b = new QToolButton(this); b->setObjectName(name); b->setIcon(toolIcon(name)); b->setIconSize({22, 22});
        b->setFixedSize(32, 32); b->setToolTip(label); b->setAccessibleName(label); b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b); return b;
    };
    auto separator = [this, layout] {
        auto *line = new QWidget(this); line->setFixedSize(1, 22); line->setStyleSheet("background: #495363;"); layout->addSpacing(3); layout->addWidget(line); layout->addSpacing(3);
    };
    for (const auto &entry : {std::pair{"move", "Move (M)"}, {"pen", "Pen (P)"}, {"arrow", "Arrow (A)"}, {"rectangle", "Rectangle (R)"}, {"text", "Text (T)"}}) {
        auto *b = button(entry.first, entry.second); b->setCheckable(true); tools->addButton(b, tools->buttons().size());
    }
    connect(tools, &QButtonGroup::idClicked, this, [this](int id) { emit toolChanged(static_cast<Tool>(id)); });
    setTool(Tool::Move);
    separator();
    colorButton = button("color", "Annotation color"); colorButton->setIcon(toolIcon("color", color));
    connect(colorButton, &QToolButton::clicked, this, [this] {
        QMenu menu(this);
        for (const auto &entry : {std::pair{"Coral", "#ff5a65"}, {"Blue", "#287cf4"}, {"Yellow", "#ffcc45"}, {"Green", "#38d9a9"}, {"White", "#ffffff"}, {"Black", "#151a23"}}) {
            auto *a = menu.addAction(toolIcon("color", QColor(entry.second)), entry.first);
            a->setIconVisibleInMenu(true);
            connect(a, &QAction::triggered, this, [this, value = QColor(entry.second)] { color = value; });
        }
        menu.addSeparator();
        menu.addAction("More colors…", this, [this] { const auto chosen = QColorDialog::getColor(color, this); if (chosen.isValid()) color = chosen; });
        menu.exec(colorButton->mapToGlobal(QPoint(0, colorButton->height())));
        colorButton->setIcon(toolIcon("color", color)); emit colorChanged(color);
    });
    auto *width = button("width", "Stroke width");
    auto *widthMenu = new QMenu(width);
    for (int value : {2, 3, 6}) widthMenu->addAction(QString("%1 px").arg(value), this, [this, value] { emit widthChanged(value); });
    width->setMenu(widthMenu); width->setPopupMode(QToolButton::InstantPopup);
    separator();
    auto action = [this, &button](const QString &name, const QString &label, EditorAction value) {
        auto *b = button(name, label); connect(b, &QToolButton::clicked, this, [this, value] { emit actionTriggered(value); }); return b;
    };
    undoButton = action("undo", "Undo", EditorAction::Undo); redoButton = action("redo", "Redo", EditorAction::Redo);
    separator();
    action("copy", "Copy image", EditorAction::Copy); action("save", "Save PNG", EditorAction::Save);
    if (capture) action("pin", "Pin to desktop (Enter)", EditorAction::Pin);
    action("ocr", "OCR · copy text (O)", EditorAction::Ocr); action("close", capture ? "Cancel (Esc)" : "Hide tools (Space)", EditorAction::Close);
    setHistory(false, false);
}

void AnnotationToolbar::setTool(Tool tool) { tools->button(static_cast<int>(tool))->setChecked(true); }
void AnnotationToolbar::setHistory(bool undo, bool redo) { undoButton->setEnabled(undo); redoButton->setEnabled(redo); }

void AnnotationToolbar::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#495363")); p.setBrush(QColor("#242a35"));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
}
