#pragma once

#include <QPlainTextEdit>

class InlineTextEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit InlineTextEditor(QWidget *parent);
    void begin(QPointF point, QRectF bounds, QColor color, qreal fontSize);
    void edit(QString text, QPointF baseline, QRectF bounds, QColor color, qreal fontSize);
    void finish(bool accept = true);
signals:
    void submitted(QString text, QPointF baseline);
    void finished();
protected:
    void keyPressEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
private:
    void fitText();
    QRect area;
    bool existing = false;
};
