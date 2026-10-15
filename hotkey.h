#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QKeySequence>
#include <QSettings>
#include "shortcut.h"
#include <array>
#include <memory>

class Hotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    using Bindings = HotkeyBindings;
    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys() override;
    static Bindings defaults();
    static Bindings load(QSettings &settings);
    static void save(QSettings &settings, const Bindings &bindings);
    static QString validate(const Bindings &bindings);
    QString apply(const Bindings &bindings);
    void enablePinTools(bool enabled);
    void suspend() { unregisterKeys(); suspended = true; }
    bool isSuspended() const { return suspended; } // While a shortcut is being recorded.
    const Bindings &bindings() const { return current; }
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
signals:
    void capture();
    void paste();
    void replay();
    void quickOcr();
    void quickTranslate();
    void pinTools();
private:
    void unregisterKeys();
    void unregisterKey(int index);
    bool registerKey(int index, const QKeySequence &key);
    bool registerMouse(const Bindings &bindings);
    void dispatch(int index);
    struct Platform;
    std::unique_ptr<Platform> platform;
    Bindings current;
    std::array<bool, hotkeyActionCount + 1> registered{}; // Actions plus the pin-tools Space slot.
    bool pinToolsPressed = false;
    bool suspended = false;
    MouseGesture gesture;
    quint64 generation = 0;
};

bool screenCaptureAllowed(bool prompt = true);
