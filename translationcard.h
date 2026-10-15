#pragma once

#include <QWidget>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QToolButton;

class TranslationCard : public QWidget {
    Q_OBJECT
public:
    explicit TranslationCard(QString text = {});
    ~TranslationCard() override;
    void showAt(QPoint anchor);
    void setSourceText(QString text);
    void setOcrError(QString error);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    void setResult(QString text, QString credit);
    void translateOnline();
    QString source;
    QPlainTextEdit *definition;
    QLabel *title, *credit;
    QToolButton *speaker, *copy, *online;
    std::function<void()> stopSpeech;
#ifdef Q_OS_MACOS
    std::function<void()> stopFocusWatch;
    std::function<void()> removeGlass;
#endif
    bool moving = false;
    QPoint dragOffset;
};
