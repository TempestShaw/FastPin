#pragma once

#include <QColor>
#include <QPainterPath>
class QPainter;

enum class Tool { Move, Pen, Arrow, Rectangle, Text };

struct Annotation {
    Tool tool = Tool::Pen;
    QPainterPath path;
    QPointF start, end;
    QString text;
    QColor color = QColor("#ff5a65");
    qreal width = 3;
    qreal fontSize = 20;
    QRectF bounds() const;
    void paint(QPainter &p) const;
};

class AnnotationLayer {
public:
    void add(Annotation annotation);
    void undo();
    void redo();
    bool canUndo() const { return applied > 0; }
    bool canRedo() const { return applied < annotations.size(); }
    bool isDrawing() const { return drawing; }
    void begin(Tool tool, QPointF point, QColor color, qreal width);
    QRectF drag(QPointF point);
    void end();
    void paint(QPainter &p, bool includePending = true) const;
private:
    QList<Annotation> annotations;
    qsizetype applied = 0;
    Annotation pending;
    bool drawing = false;
};
