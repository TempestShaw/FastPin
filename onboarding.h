#pragma once

#include "hotkey.h"
#include <QDialog>

class QLabel;
class QPushButton;
class QStackedWidget;

class Onboarding : public QDialog {
    Q_OBJECT
public:
    Onboarding(QSettings &settings, const Hotkeys::Bindings &bindings, QWidget *parent = nullptr);
    bool start(bool force = false);
signals:
    void captureRequested();
    void preferencesRequested();
protected:
    bool event(QEvent *event) override;
private:
    void setStep(int step);
    void refreshPermissions();
    void complete();
    QSettings &settings;
    QStackedWidget *pages;
    QLabel *progress, *screenStatus = nullptr, *elementStatus = nullptr;
    QPushButton *back, *next;
    QPushButton *screenGrant = nullptr, *elementGrant = nullptr;
};
