#pragma once

#include "annotation.h"
#include <QIcon>
#include <QWidget>

class QButtonGroup;
class QToolButton;
enum class EditorAction { Undo, Redo, Copy, Save, Pin, Ocr, Close };

QIcon toolIcon(const QString &name, QColor color = QColor("#eef2f8"));

class AnnotationToolbar : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationToolbar(bool capture, QWidget *parent, Qt::WindowFlags flags = {});
    void setTool(Tool tool);
    void setHistory(bool undo, bool redo);
protected:
    void paintEvent(QPaintEvent *) override;
signals:
    void toolChanged(Tool tool);
    void colorChanged(QColor color);
    void widthChanged(qreal width);
    void actionTriggered(EditorAction action);
private:
    QButtonGroup *tools;
    QToolButton *colorButton, *undoButton, *redoButton;
    QColor color = QColor("#ff5a65");
};
