#include "texteditor.h"

#include <QKeyEvent>
#include <QTextDocument>

InlineTextEditor::InlineTextEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    setObjectName("inlineTextEditor");
    setAccessibleName("Text annotation");
    setPlaceholderText("Type here…");
    setToolTip("Enter to finish · Shift+Enter for a new line · Esc to cancel");
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    document()->setDocumentMargin(0);
    hide();
    connect(this, &QPlainTextEdit::textChanged, this, &InlineTextEditor::fitText);
}

void InlineTextEditor::begin(QPointF point, QRectF bounds, QColor color, qreal fontSize)
{
    finish();
    area = bounds.toAlignedRect().intersected(parentWidget()->rect());
    QFont textFont; textFont.setPixelSize(qMax(1, qRound(fontSize))); setFont(textFont);
    setStyleSheet(QString("QPlainTextEdit { color: %1; background: rgba(24,28,36,35); border: 1px dashed #287cf4; padding: 3px; selection-background-color: #287cf4; }").arg(color.name()));
    clear();
    const int minWidth = qMin(160, area.width()), minHeight = qMin(fontMetrics().height() + 10, area.height());
    move(qBound(area.left(), qRound(point.x()), area.right() - minWidth + 1),
         qBound(area.top(), qRound(point.y()), area.bottom() - minHeight + 1));
    fitText();
    show(); raise(); setFocus(Qt::MouseFocusReason);
}

void InlineTextEditor::fitText()
{
    const auto lines = toPlainText().split('\n');
    int width = 160;
    for (const auto &line : lines) width = qMax(width, qCeil(QFontMetricsF(font()).horizontalAdvance(line)) + 20);
    resize(qMin(width, area.right() - x() + 1),
           qMin(qMax(1, int(lines.size())) * fontMetrics().lineSpacing() + 10, area.bottom() - y() + 1));
}

void InlineTextEditor::finish(bool accept)
{
    if (isHidden()) return;
    const QString text = toPlainText();
    const QPointF baseline = QPointF(pos() + viewport()->pos()) + QPointF(0, QFontMetricsF(font()).ascent());
    hide();
    if (accept && !text.trimmed().isEmpty()) emit submitted(text, baseline);
}

void InlineTextEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        finish(false); parentWidget()->setFocus(); event->accept();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & Qt::ShiftModifier)) {
        finish(); parentWidget()->setFocus(); event->accept();
    } else QPlainTextEdit::keyPressEvent(event);
}

void InlineTextEditor::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    if (event->reason() != Qt::PopupFocusReason && event->reason() != Qt::ActiveWindowFocusReason) finish();
}
