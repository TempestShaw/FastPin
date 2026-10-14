#include "annotation.h"
#include <QPainter>
#include <cmath>

QRectF Annotation::bounds() const
{
    if (tool == Tool::Pen) return path.boundingRect().adjusted(-width, -width, width, width);
    if (tool == Tool::Arrow) return QRectF(start, end).normalized().adjusted(-width * 6, -width * 6, width * 6, width * 6);
    if (tool == Tool::Rectangle) return QRectF(start, end).normalized().adjusted(-width, -width, width, width);
    QFont font; font.setPixelSize(qRound(fontSize));
    const QFontMetricsF metrics(font);
    QRectF result;
    const auto lines = text.split('\n');
    for (qsizetype i = 0; i < lines.size(); ++i)
        result = result.united(metrics.boundingRect(lines[i]).translated(start + QPointF(0, i * metrics.lineSpacing())));
    return result.adjusted(-2, -2, 2, 2);
}

void Annotation::paint(QPainter &p) const
{
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    if (tool == Tool::Pen) {
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    } else if (tool == Tool::Arrow) {
        p.drawLine(start, end);
        const qreal angle = std::atan2(end.y() - start.y(), end.x() - start.x());
        const qreal length = qMin(width * 5, QLineF(start, end).length() / 2);
        const QPointF a = end - QPointF(std::cos(angle - 0.5), std::sin(angle - 0.5)) * length;
        const QPointF b = end - QPointF(std::cos(angle + 0.5), std::sin(angle + 0.5)) * length;
        p.drawPolygon(QPolygonF{end, a, b});
    } else if (tool == Tool::Rectangle) {
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(start, end).normalized());
    } else if (tool == Tool::Text) {
        QFont font = p.font(); font.setPixelSize(qRound(fontSize)); p.setFont(font);
        const auto lines = text.split('\n');
        const QFontMetricsF metrics(font);
        for (qsizetype i = 0; i < lines.size(); ++i) p.drawText(start + QPointF(0, i * metrics.lineSpacing()), lines[i]);
    }
}

void AnnotationLayer::add(Annotation annotation)
{
    annotations.erase(annotations.begin() + applied, annotations.end());
    annotations.append(std::move(annotation));
    applied = annotations.size();
}

void AnnotationLayer::undo() { if (canUndo()) --applied; }
void AnnotationLayer::redo() { if (canRedo()) ++applied; }

void AnnotationLayer::begin(Tool tool, QPointF point, QColor color, qreal width)
{
    pending = {};
    pending.tool = tool; pending.color = color; pending.width = width;
    pending.start = pending.end = point;
    pending.path.moveTo(point);
    pending.path.lineTo(point + QPointF(0.01, 0));
    drawing = true;
}

QRectF AnnotationLayer::drag(QPointF point)
{
    const QRectF before = pending.bounds();
    pending.end = point;
    if (pending.tool == Tool::Pen) pending.path.lineTo(point);
    return before.united(pending.bounds());
}

void AnnotationLayer::end() { if (drawing) { drawing = false; add(std::move(pending)); } }
void AnnotationLayer::paint(QPainter &p, bool includePending) const
{
    for (qsizetype i = 0; i < applied; ++i) annotations[i].paint(p);
    if (includePending && drawing) pending.paint(p);
}
