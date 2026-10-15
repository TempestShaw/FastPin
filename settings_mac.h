#pragma once

#include <QString>
#include <functional>
#include <memory>

class Hotkeys;
class QSettings;
class CaptureHistory;

class MacSettings {
public:
    enum Page { Settings, Shortcuts, About };
    MacSettings(Hotkeys &hotkeys, QSettings &settings, CaptureHistory &history,
                std::function<void()> shortcutsChanged, std::function<void()> historyChanged);
    ~MacSettings();
    void show(Page page = Settings);
    void hide();
    void checkForUpdates();
    void quit();
    void setFeedback(const QString &message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
