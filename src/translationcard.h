#pragma once

#include <QWidget>
#include <functional>

class QLabel;
class QLayout;
class QNetworkAccessManager;
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
    void applyStyle();
    QToolButton *headerButton(const QString &name, const QString &icon, const QString &label);
    QLayout *buildHeader();
    QWidget *buildDefinition();
    QLayout *buildFooter();
    void setActionsEnabled(bool enabled);
    void setResult(QString text, QString credit);
    void translateOnline();
    QString source;
    QPlainTextEdit *definition;
    QLabel *title, *credit;
    QToolButton *speaker, *copy, *online;
    QNetworkAccessManager *network = nullptr; // Created on first online request, reused afterwards.
    std::function<void()> stopSpeech;
#ifdef Q_OS_MACOS
    std::function<void()> stopFocusWatch;
    std::function<void()> removeGlass;
#endif
    bool moving = false;
    QPoint dragOffset;
};
