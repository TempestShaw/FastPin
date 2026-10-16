#pragma once

#include "hotkey.h"
#include <QDialog>
#include <QTimer>
#include <functional>

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
    void permissionsReady();
protected:
    bool event(QEvent *event) override;
private:
    void setStep(int step);
    void refreshPermissions();
    bool complete();
    friend class NativeTests;
    struct Permissions {
        bool screen, accessibility, input;
        bool ready() const { return screen && accessibility && input; }
    };
    std::function<Permissions()> readPermissions;
    Permissions permissions{false, false, false};
    QTimer permissionTimer;
    QSettings &settings;
    QStackedWidget *pages;
    QLabel *progress, *screenStatus = nullptr, *elementStatus = nullptr, *inputStatus = nullptr;
    QPushButton *back = nullptr, *next = nullptr;
    QPushButton *screenGrant = nullptr, *elementGrant = nullptr, *inputGrant = nullptr;
};
