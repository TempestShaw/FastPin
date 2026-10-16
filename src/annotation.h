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
    qsizetype textAt(QPointF point) const;
    const Annotation &at(qsizetype index) const { return annotations[index]; }
    void replaceText(qsizetype index, QString text);
    void undo();
    void redo();
    bool canUndo() const { return applied > 0; }
    bool canRedo() const { return applied < history.size(); }
    bool isDrawing() const { return drawing; }
    void begin(Tool tool, QPointF point, QColor color, qreal width);
    QRectF drag(QPointF point);
    void end();
    void paint(QPainter &p, bool includePending = true, qsizetype hiddenText = -1) const;
private:
    struct Change { qsizetype index; Annotation before, after; bool added; };
    QList<Annotation> annotations;
    QList<Change> history;
    qsizetype applied = 0;
    Annotation pending;
    bool drawing = false;
};
