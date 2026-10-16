#include "capture.h"
#include "hotkey.h"
#include "history.h"
#include "pin.h"
#include "onboarding.h"
#include "translation.h"
#include "translationcard.h"
#include "doubletap.h"
#if defined(Q_OS_MACOS) && !defined(FASTPIN_CONTROLLER_TEST)
#define FASTPIN_NATIVE_SETTINGS
#include "settings_mac.h"
#include <QMenuBar>
#endif

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileOpenEvent>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QFrame>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

class Controller : public QWidget {
public:
    ~Controller() override { setDoubleControl(false); }

    Controller()
        : hotkeys(this), tray(this), history(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/last-image.png", this)
    {
        setWindowTitle("FastPin");
        setFixedWidth(620);
        setStyleSheet("QWidget#panel { background: #E9EDEB; } QLabel { color: #151B1C; }"
                      "QLabel#muted { color: #596660; font-size: 12px; }"
                      "QFrame#card { background: white; border: 1px solid #D5DDD8; border-radius: 12px; }"
                      "QPushButton { color: #151B1C; background: white; border: 1px solid #D5DDD8; border-radius: 8px; padding: 9px 14px; }"
                      "QPushButton:hover { background: #DDF6E9; border-color: #19D98A; }"
                      "QPushButton:disabled { color: #77817E; background: #E9EDEB; }"
                      "QPushButton#captureButton, QPushButton#applyShortcuts { color: white; background: #128252; border: 0; }"
                      "QPushButton#captureButton:hover, QPushButton#applyShortcuts:hover { background: #0C693F; }"
                      "QTabWidget::pane { border: 0; } QTabBar::tab { color: #596660; padding: 9px 18px; border-bottom: 2px solid transparent; }"
                      "QTabBar::tab:selected { color: #128252; border-bottom-color: #128252; }"
                      "QLineEdit#shortcutEdit { background: #E9EDEB; color: #151B1C; border: 1px solid #D5DDD8; border-radius: 7px; padding: 7px; }"
                      "QLineEdit#shortcutEdit:focus { border-color: #128252; } QCheckBox { color: #151B1C; spacing: 8px; }");
        setObjectName("panel");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 26, 28, 18); layout->setSpacing(16);
        auto *header = new QHBoxLayout;
        auto *logo = new QLabel(this); logo->setPixmap(QIcon(":/brand/app-icon.png").pixmap(48, 48)); header->addWidget(logo);
        auto *title = new QLabel("<span style='font-size:24px; font-weight:600'>FastPin</span><br>"
                                "<span style='color:#596660; font-size:12px'>Capture ideas. Keep them in view.</span>", this);
        header->addWidget(title); header->addStretch();
        auto *gettingStarted = new QPushButton("Getting started", this); header->addWidget(gettingStarted);
        connect(gettingStarted, &QPushButton::clicked, this, [this] { showGettingStarted(); }); layout->addLayout(header);
        pages = new QTabWidget(this); pages->tabBar()->setElideMode(Qt::ElideNone);
#ifdef FASTPIN_NATIVE_SETTINGS
        // Preferences live in the native Settings window; the panel only keeps the workspace page.
        auto *settingsButton = new QPushButton("Settings…", this); header->addWidget(settingsButton);
        connect(settingsButton, &QPushButton::clicked, this, [this] { showPreferences(); });
        pages->tabBar()->hide();
#endif
        pages->setAccessibleName("Control panel pages"); layout->addWidget(pages);
        auto *home = new QWidget(this); auto *homeLayout = new QVBoxLayout(home);
        homeLayout->setContentsMargins(0, 18, 0, 0); homeLayout->setSpacing(12); pages->addTab(home, "Workspace");
        auto actionCard = [this](const QString &name, const QString &title, const QString &detail, bool primary) {
            auto *button = new QPushButton(this); button->setMinimumHeight(76); button->setAccessibleName(title);
            button->setObjectName(name + "Button");
            auto *row = new QHBoxLayout(button); row->setContentsMargins(18, 10, 18, 10); row->setSpacing(14);
            auto *icon = new QLabel(button); icon->setPixmap(toolIcon(name, primary ? Qt::white : QColor("#128252")).pixmap(28, 28));
            auto *text = new QLabel("<span style='font-size:15px; font-weight:600'>" + title + "</span><br>"
                                   "<span style='font-size:12px'>" + detail + "</span>", button);
            text->setStyleSheet(primary ? "color: white;" : "color: #151B1C;");
            row->addWidget(icon); row->addWidget(text); row->addStretch();
            auto *keys = new QLabel(button); keys->setStyleSheet(primary ? "color: #FFFFFF; font-size:13px;" : "color:#596660; font-size:13px;");
            row->addWidget(keys);
            for (auto *label : {icon, text, keys}) label->setAttribute(Qt::WA_TransparentForMouseEvents);
            return std::pair{button, keys};
        };
        struct CardSpec { HotkeyAction action; const char *icon, *detail; };
        const CardSpec cards[]{{HotkeyAction::Capture, "capture", "Select a window, element or any region"},
                               {HotkeyAction::Paste, "pin", "Keep a clipboard image above your workspace"},
                               {HotkeyAction::CopyText, "ocr", "Select a region to recognize and copy its text"},
                               {HotkeyAction::Translate, "translate", "Select text and release to see its translation"}};
        for (const auto &spec : cards) {
            const auto card = actionCard(spec.icon, hotkeyActionName(int(spec.action)), spec.detail, spec.action == HotkeyAction::Capture);
            actionKeys[int(spec.action)] = card.second; homeLayout->addWidget(card.first);
            connect(card.first, &QPushButton::clicked, this, [this, which = spec.action] { trigger(which); });
        }
        auto *recovery = new QFrame(this); recovery->setObjectName("card");
        auto *recoveryLayout = new QHBoxLayout(recovery); recoveryLayout->setContentsMargins(18, 16, 18, 16); recoveryLayout->setSpacing(16);
        preview = new QLabel(this); preview->setFixedSize(112, 70); preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet("background:#E9EDEB; color:#77817E; border-radius:7px;"); recoveryLayout->addWidget(preview);
        auto *recoveryText = new QVBoxLayout;
        recoveryText->addWidget(new QLabel("<b>Pick up where you left off</b>", this));
        historyDetail = new QLabel(this); historyDetail->setObjectName("muted"); historyDetail->setWordWrap(true); recoveryText->addWidget(historyDetail);
        auto *replayKeys = new QLabel(this); replayKeys->setObjectName("muted"); recoveryText->addWidget(replayKeys);
        actionKeys[int(HotkeyAction::Replay)] = replayKeys;
        recoveryLayout->addLayout(recoveryText, 1);
        replayButton = new QPushButton("Restore pin", this); replayButton->setObjectName("replayButton");
        connect(replayButton, &QPushButton::clicked, this, [this] { replay(); }); recoveryLayout->addWidget(replayButton);
        homeLayout->addWidget(recovery);
        auto *help = new QLabel("<b>Tab</b> Window / Element · <b>P</b> Pen · <b>A</b> Arrow · <b>R</b> Rectangle · <b>T</b> Text<br>"
                               "<b>O</b> Local OCR · <b>C</b> Pick color · <b>Space</b> Pin tools on hover · <b>Scroll</b> Zoom", this);
        help->setObjectName("muted"); help->setStyleSheet("color:#596660; font-size:12px; padding-top:6px;"); homeLayout->addWidget(help);
        homeLayout->addStretch();

        history.enabled = settings.value("recovery/enabled", true).toBool();
#ifndef FASTPIN_NATIVE_SETTINGS
        buildPreferencesPage();
#endif
        auto *footer = new QHBoxLayout;
        status = new QLabel("Ready · Close the panel to keep using the global shortcuts.", this);
        status->setWordWrap(true); status->setObjectName("muted"); footer->addWidget(status, 1);
        auto *hideButton = new QPushButton("Hide panel", this); connect(hideButton, &QPushButton::clicked, this, &QWidget::hide); footer->addWidget(hideButton); layout->addLayout(footer);

        auto *menu = new QMenu(this);
        for (int i = 0; i < hotkeyActionCount; ++i)
            actions[i] = menu->addAction(hotkeyActionName(i), this, [this, i] { trigger(HotkeyAction(i)); });
        menu->addSeparator();
        menu->addAction("Restore mouse input for all pins", this, [this] { restorePins(); });
        menu->addAction("Show controls…", this, [this] { pages->setCurrentIndex(0); show(); raise(); activateWindow(); });
        menu->addAction("Settings…", this, [this] { showPreferences(); });
#ifdef FASTPIN_NATIVE_SETTINGS
        menu->addAction("About FastPin…", this, [this] { nativeSettings->show(MacSettings::About); });
        menu->addAction("Check for Updates…", this, [this] { nativeSettings->checkForUpdates(); });
#endif
        menu->addAction("Getting started…", this, [this] { showGettingStarted(); });
        menu->addSeparator();
        menu->addAction("Quit", this, [this] { quitApplication(); });
        tray.setContextMenu(menu);
        auto trayIcon = QIcon(":/brand/app-icon.png");
#ifdef Q_OS_MACOS
        trayIcon = QIcon(":/brand/tray-icon.png"); trayIcon.setIsMask(true);
#endif
        tray.setIcon(trayIcon); tray.setToolTip("FastPin"); tray.show();
#ifndef Q_OS_MACOS
        connect(&tray, &QSystemTrayIcon::activated, this, [this](auto reason) { if (reason == QSystemTrayIcon::Trigger) show(); });
#endif
        connect(&hotkeys, &Hotkeys::capture, this, [this] { trigger(HotkeyAction::Capture); });
        connect(&hotkeys, &Hotkeys::paste, this, [this] { trigger(HotkeyAction::Paste); });
        connect(&hotkeys, &Hotkeys::replay, this, [this] { trigger(HotkeyAction::Replay); });
        connect(&hotkeys, &Hotkeys::quickOcr, this, [this] { trigger(HotkeyAction::CopyText); });
        connect(&hotkeys, &Hotkeys::quickTranslate, this, [this] { trigger(HotkeyAction::Translate); });
        connect(&history, &CaptureHistory::changed, this, [this](QString error) {
            if (isVisible()) refreshHistory();
            action(HotkeyAction::Replay)->setEnabled(history.available() || history.busy());
            if (!error.isEmpty()) { replayPending = false; status->setText("Recovery: " + error); }
            else if (replayPending && !history.busy()) { replayPending = false; replay(); }
        });
        connect(pages, &QTabWidget::currentChanged, this, [this] { refreshHistory(); });
#ifdef FASTPIN_NATIVE_SETTINGS
        setupNativeSettings();
#endif
        const auto bindings = Hotkeys::load(settings);
        const QString error = hotkeys.apply(bindings);
        for (int i = 0; i < hotkeyActionCount; ++i) if (edits[i]) edits[i]->setBinding(bindings[i]);
        if (!error.isEmpty()) { setSettingsFeedback(error); status->setText("A shortcut is unavailable. Change it in Settings."); }
        applyDoubleControl(settings.value(doubleControlSetting, true).toBool());
        refreshShortcuts(); refreshHistory();
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { refreshShortcutRecording(); });
            // Input Monitoring may have been granted while FastPin was in the background.
            if (!stopDoubleControl && settings.value(doubleControlSetting, true).toBool()) setDoubleControl(true);
        });
        qApp->installEventFilter(this);
    }

    void openImage(const QString &path)
    {
        QImage image(path);
        if (image.isNull()) status->setText("Could not read this image.");
        else pin(std::move(image), QCursor::pos());
    }

    void showGettingStarted(bool force = true)
    {
        if (welcome) { welcome->raise(); welcome->activateWindow(); return; }
        welcome = new Onboarding(settings, hotkeys.bindings(), this);
        welcome->setAttribute(Qt::WA_DeleteOnClose);
        connect(welcome, &Onboarding::captureRequested, this, [this] { capture(); });
        connect(welcome, &Onboarding::preferencesRequested, this, [this] {
            showPreferences();
        });
        if (!welcome->start(force)) { delete welcome; }
    }

    void showPreferences()
    {
#ifdef FASTPIN_NATIVE_SETTINGS
        nativeSettings->show();
#else
        pages->setCurrentIndex(1); show(); raise(); activateWindow();
#endif
    }

protected:
    void showEvent(QShowEvent *event) override { refreshHistory(); QWidget::showEvent(event); }
    void closeEvent(QCloseEvent *event) override { hide(); event->accept(); }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            openImage(static_cast<QFileOpenEvent *>(event)->file());
            return true;
        }
        if (event->type() == QEvent::FocusIn && isVisible() && isActiveWindow()) {
            auto *widget = qobject_cast<QWidget *>(object);
            for (auto *edit : edits) if (edit && widget && (widget == edit || edit->isAncestorOf(widget))) {
                hotkeys.suspend(); shortcutsRecording = true; break;
            }
        } else if (event->type() == QEvent::FocusOut && shortcutsRecording) {
            QTimer::singleShot(0, this, [this] { refreshShortcutRecording(); });
        } else if (object == this && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide)) {
            QTimer::singleShot(0, this, [this] { refreshShortcutRecording(); });
        }
        return false;
    }

private:
    friend class ControllerTests;
#ifndef FASTPIN_NATIVE_SETTINGS
    void buildPreferencesPage()
    {
        auto *preferences = new QWidget(this); auto *preferencesLayout = new QVBoxLayout(preferences);
        preferencesLayout->setContentsMargins(0, 18, 0, 0); preferencesLayout->setSpacing(12); pages->addTab(preferences, "Preferences");
        preferencesLayout->addWidget(new QLabel("<span style='font-size:17px; font-weight:600'>Make it yours</span>", this));
        auto *hint = new QLabel("Click a field, then press a shortcut (Ctrl+A, Shift+A, F1–F20) or Mouse4 / Mouse5.\nHold both side buttons for a combo; release to finish. Clear a field to disable its shortcut.", this);
#ifdef Q_OS_MACOS
        hint->setToolTip("If the top row controls brightness or volume, hold Fn while pressing F1–F20.");
#endif
        hint->setObjectName("muted"); hint->setWordWrap(true); preferencesLayout->addWidget(hint);
        auto *shortcutCard = new QFrame(this); shortcutCard->setObjectName("card");
        auto *shortcutLayout = new QVBoxLayout(shortcutCard); shortcutLayout->setContentsMargins(18, 12, 18, 12);
        for (int i = 0; i < hotkeyActionCount; ++i) {
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(hotkeyActionName(i), this), 1);
            edits[i] = new ShortcutEdit(this);
            edits[i]->setAccessibleName(hotkeyActionName(i) + " shortcut"); edits[i]->setFixedWidth(218); row->addWidget(edits[i]); shortcutLayout->addLayout(row);
        }
        preferencesLayout->addWidget(shortcutCard);
        auto *doubleControl = new QCheckBox("Double-tap Control to translate", this);
        doubleControl->setChecked(settings.value(doubleControlSetting, true).toBool());
        preferencesLayout->addWidget(doubleControl);
        connect(doubleControl, &QCheckBox::toggled, this, [this](bool enabled) {
            settings.setValue(doubleControlSetting, enabled); settings.sync();
            applyDoubleControl(enabled);
        });
        auto *buttons = new QHBoxLayout;
        auto *defaults = new QPushButton("Reset to defaults", this); buttons->addWidget(defaults); buttons->addStretch();
        auto *apply = new QPushButton("Apply shortcuts", this); apply->setObjectName("applyShortcuts"); buttons->addWidget(apply); preferencesLayout->addLayout(buttons);
        connect(defaults, &QPushButton::clicked, this, [this] {
            const auto bindings = Hotkeys::defaults(); for (int i = 0; i < hotkeyActionCount; ++i) edits[i]->setBinding(bindings[i]);
            settingsFeedback->setText("Defaults selected. Apply to activate them.");
        });
        connect(apply, &QPushButton::clicked, this, [this] {
            Hotkeys::Bindings bindings; for (int i = 0; i < hotkeyActionCount; ++i) bindings[i] = edits[i]->binding();
            for (auto *edit : edits) edit->clearFocus();
            refreshShortcutRecording();
            const QString error = hotkeys.apply(bindings);
            if (!error.isEmpty()) { settingsFeedback->setText(error); return; }
            Hotkeys::save(settings, bindings); refreshShortcuts();
            settingsFeedback->setText(settings.status() == QSettings::NoError ? "Saved. Your shortcuts are active." :
                                     "Active for this session, but settings could not be saved to disk.");
        });
        settingsFeedback = new QLabel(this); settingsFeedback->setObjectName("muted"); settingsFeedback->setWordWrap(true); preferencesLayout->addWidget(settingsFeedback);
        remember = new QCheckBox("Remember the last image after closing or restarting", this);
        remember->setChecked(history.enabled); preferencesLayout->addWidget(remember);
        auto *privacy = new QLabel("One image is stored on this computer, including its annotations.\nTurning this off also deletes the saved image.", this);
        privacy->setObjectName("muted"); preferencesLayout->addWidget(privacy);
        auto *clear = new QPushButton("Clear saved image", this); preferencesLayout->addWidget(clear, 0, Qt::AlignLeft);
        connect(clear, &QPushButton::clicked, this, [this] {
            replayPending = false; if (history.clear()) status->setText("Saved image cleared.");
        });
        connect(remember, &QCheckBox::toggled, this, [this](bool enabled) {
            history.enabled = enabled; settings.setValue("recovery/enabled", enabled); settings.sync();
            if (!enabled) { replayPending = false; history.clear(); } refreshHistory();
        });
        preferencesLayout->addStretch();
    }
#endif

#ifdef FASTPIN_NATIVE_SETTINGS
    void setupNativeSettings()
    {
        nativeSettings = std::make_unique<MacSettings>(hotkeys, settings, history,
            [this] { refreshShortcuts(); }, [this] { replayPending = false; refreshHistory(); },
            [this](bool enabled) { applyDoubleControl(enabled); });
        applicationMenuBar = std::make_unique<QMenuBar>();
        auto *applicationMenu = applicationMenuBar->addMenu("FastPin");
        applicationMenu->addAction("About FastPin", this, [this] { nativeSettings->show(MacSettings::About); })->setMenuRole(QAction::AboutRole);
        applicationMenu->addAction("Settings…", this, [this] { showPreferences(); })->setMenuRole(QAction::PreferencesRole);
        applicationMenu->addAction("Check for Updates…", this, [this] { nativeSettings->checkForUpdates(); })->setMenuRole(QAction::ApplicationSpecificRole);
        applicationMenu->addAction("Quit FastPin", this, [this] { quitApplication(); })->setMenuRole(QAction::QuitRole);
    }
#endif

    void quitApplication()
    {
#ifdef FASTPIN_NATIVE_SETTINGS
        nativeSettings->quit(); // Lets Sparkle install a pending update on quit.
#else
        QApplication::quit();
#endif
    }

    void trigger(HotkeyAction requested)
    {
        switch (requested) {
        case HotkeyAction::Capture: capture(); break;
        case HotkeyAction::Paste: paste(); break;
        case HotkeyAction::Replay: replay(); break;
        case HotkeyAction::CopyText: capture(CaptureOverlay::Mode::CopyText); break;
        case HotkeyAction::Translate: capture(CaptureOverlay::Mode::Translate); break;
        }
    }

    QAction *action(HotkeyAction which) const { return actions[int(which)]; }

    void reportStatus(const QString &message, QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information)
    {
        status->setText(message);
        if (!isVisible()) tray.showMessage("FastPin", message, icon, 2000);
    }

    void setSettingsFeedback(const QString &message)
    {
#ifdef FASTPIN_NATIVE_SETTINGS
        nativeSettings->setFeedback(message);
#else
        settingsFeedback->setText(message);
#endif
    }

    void refreshShortcutRecording()
    {
        const auto *focus = QApplication::focusWidget();
        bool recording = false;
        if (isVisible() && isActiveWindow() && QGuiApplication::applicationState() == Qt::ApplicationActive)
            for (auto *edit : edits) if (edit && focus && (focus == edit || edit->isAncestorOf(focus))) recording = true;
        if (recording == shortcutsRecording) return;
        shortcutsRecording = recording;
        if (recording) hotkeys.suspend();
        else if (const auto error = hotkeys.apply(hotkeys.bindings()); !error.isEmpty()) setSettingsFeedback(error);
    }

    void refreshShortcuts()
    {
        const auto &bindings = hotkeys.bindings();
        for (int i = 0; i < hotkeyActionCount; ++i) {
            actions[i]->setShortcut(bindings[i].key);
            actions[i]->setText(hotkeyActionName(i) + (bindings[i].buttons ? " · " + bindings[i].toString() : QString()));
            actionKeys[i]->setText(bindings[i].toString(QKeySequence::NativeText));
        }
        if (stopDoubleControl) {
            auto *translateKeys = actionKeys[int(HotkeyAction::Translate)];
            const QString tap = QKeySequence(Qt::META).toString(QKeySequence::NativeText).remove('+');
            translateKeys->setText(tap + tap + (translateKeys->text().isEmpty() ? QString() : " · " + translateKeys->text()));
        }
    }

    void setDoubleControl(bool enabled)
    {
        if (stopDoubleControl) { stopDoubleControl(); stopDoubleControl = {}; }
        if (!enabled) return;
        QPointer<Controller> self(this);
        stopDoubleControl = watchDoubleControl([self] {
            if (self && !self->hotkeys.isSuspended()) self->trigger(HotkeyAction::Translate);
        });
    }

    void applyDoubleControl(bool enabled)
    {
        setDoubleControl(enabled);
        if (enabled && !stopDoubleControl) requestDoubleControlAccess();
        if (actions[0]) refreshShortcuts();
    }

    void requestDoubleControlAccess()
    {
#ifdef Q_OS_MACOS
        // The passive event tap needs Input Monitoring permission. Ask once.
        if (QGuiApplication::platformName() != "cocoa") return;
        status->setText("Double-tap Control needs Input Monitoring. Allow FastPin in System Settings → Privacy & Security → Input Monitoring, then reopen FastPin.");
        if (settings.value("translate/doubleControlInputPrompted", false).toBool()) return;
        settings.setValue("translate/doubleControlInputPrompted", true);
        QTimer::singleShot(1500, this, [this] { if (!welcome) doubleControlAccessAllowed(true); });
#else
        status->setText("Double-tap Control is unavailable. Use the Dictionary shortcut instead.");
#endif
    }

    void refreshHistory()
    {
        historyDetail->setText(history.description());
        const auto image = history.thumbnail();
        if (image.isNull()) { preview->setPixmap({}); preview->setText("No saved image"); }
        else { preview->setText({}); preview->setPixmap(QPixmap::fromImage(image).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); }
        replayButton->setEnabled(history.available() || (history.enabled && history.busy()));
        action(HotkeyAction::Replay)->setEnabled(replayButton->isEnabled());
    }

    void replay()
    {
        if (captureBusy) return;
        if (history.busy()) { replayPending = true; status->setText("Preparing your last image…"); return; }
        auto saved = history.restore();
        if (saved.image.isNull()) { reportStatus("No saved image to restore. Capture or paste an image first."); return; }
        pin(std::move(saved.image), saved.position, saved.zoom);
        status->setText("Last image restored, including annotations and zoom.");
    }

    void pin(QImage image, QPoint position, qreal zoom = 1)
    {
        history.remember(image, position, zoom);
        action(HotkeyAction::Replay)->setEnabled(history.enabled);
        auto *window = new PinWindow(std::move(image));
        window->setZoom(zoom);
        const auto *screen = QGuiApplication::screenAt(position);
        if (!screen) screen = QGuiApplication::primaryScreen();
        const QRect area = screen->availableGeometry();
        position.setX(qMax(area.left(), qMin(position.x(), area.right() - window->width() + 1)));
        position.setY(qMax(area.top(), qMin(position.y(), area.bottom() - window->height() + 1)));
        window->move(position);
        pins.append(window);
        connect(window, &PinWindow::closed, this, [this, window] {
            if (history.enabled) history.remember(window->exportedImage(), window->pos(), window->zoomFactor());
            pins.removeAll(window);
            status->setText(QString("%1 pinned image(s).").arg(pins.size()));
        });
        connect(window, &PinWindow::ocrRequested, this, [this](QImage image) { ocr(std::move(image)); });
        connect(window, &PinWindow::translateRequested, this, [this](QImage image) { ocr(std::move(image), true); });
        window->show();
        status->setText(QString("%1 pinned image(s) · Hover and press Space for annotation tools.").arg(pins.size()));
    }

    void paste()
    {
        auto image = QApplication::clipboard()->image();
        if (image.isNull()) { reportStatus("The clipboard does not contain an image."); return; }
        pin(std::move(image), QCursor::pos());
    }

    void restorePins()
    {
        for (auto *window : pins) window->setClickThrough(false);
        status->setText("Mouse input restored for all pins.");
    }

    void ocr(QImage image, bool translate = false)
    {
        if (ocrBusy) { reportStatus("OCR is already running."); return; }
        ocrBusy = true;
        status->setText("Recognizing text locally…");
        auto result = std::make_shared<OcrResult>();
        const auto executable = QCoreApplication::applicationFilePath();
        const QPoint anchor = QCursor::pos();
        QPointer<TranslationCard> card;
        if (translate) {
            card = new TranslationCard;
            connect(this, &QObject::destroyed, card, &QObject::deleteLater);
            card->showAt(anchor);
        }
        auto *worker = QThread::create([image = std::move(image), executable, result] { *result = recognizeTextInWorker(image, executable); });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &QThread::finished, this, [this, result, translate, card] {
            ocrBusy = false;
            if (!result->error.isEmpty()) {
                if (card) card->setOcrError(result->error);
                else if (!translate) reportStatus("OCR: " + result->error, QSystemTrayIcon::Warning);
            } else if (result->text.trimmed().isEmpty()) {
                if (card) card->setOcrError("No text found. Try selecting a tighter region.");
                else if (!translate) reportStatus("OCR found no text. Clipboard kept.");
            } else if (card) {
                card->setSourceText(result->text); // Translation leaves the user's clipboard untouched.
            } else if (!translate) {
                QApplication::clipboard()->setText(result->text);
                reportStatus(QString("OCR copied %1 characters to the clipboard.").arg(result->text.size()));
            }
        });
        worker->start();
    }

    void finishCapture()
    {
        for (const auto &overlay : overlays) {
            if (overlay) { overlay->hide(); overlay->deleteLater(); }
        }
        overlays.clear();
        captureBusy = false;
#ifdef Q_OS_MACOS
        if (restoreCaptureFocus) { restoreCaptureFocus(); restoreCaptureFocus = {}; }
#endif
    }

    void capture(CaptureOverlay::Mode mode = CaptureOverlay::Mode::Screenshot)
    {
        if (captureBusy) return;
        if (mode != CaptureOverlay::Mode::Screenshot && ocrBusy) { reportStatus("OCR is already running."); return; }
        if (!screenCaptureAllowed()) {
            reportStatus("Allow FastPin in System Settings → Privacy & Security → Screen Recording, then try again. macOS may require restarting the app.", QSystemTrayIcon::Warning);
            return;
        }
        captureBusy = true;
#ifdef Q_OS_MACOS
        restoreCaptureFocus = captureFocusRestorer();
#endif
        if (welcome) welcome->reject();
#ifdef FASTPIN_NATIVE_SETTINGS
        nativeSettings->hide();
#endif
        hide();
        // One activation delay lets the panel/tray menu leave the compositor.
        // There is no recurring timer, mouse polling or screen recording stream.
        QTimer::singleShot(80, this, [this, mode] {
            QList<ScreenFrame> frames;
            QList<DesktopCoordinates> coordinates;
            const auto windows = desktopWindows();
            for (auto *screen : QGuiApplication::screens()) {
                const QPixmap pixmap = screen->grabWindow(0);
                if (pixmap.isNull()) {
                    finishCapture();
                    reportStatus("Screen capture failed. Check screen recording permission.", QSystemTrayIcon::Warning);
                    return;
                }
                ScreenFrame frame{pixmap.toImage(), screen->geometry(), pixmap.devicePixelRatio()};
                frame.image.setDevicePixelRatio(1);
                frames.append(std::move(frame));
                coordinates.append(desktopCoordinates(screen));
            }
            // Capture every display before showing overlays, preserving each display's own DPR.
            for (qsizetype i = 0; i < frames.size(); ++i) {
                auto *overlay = new CaptureOverlay(std::move(frames[i]), windows, coordinates[i]);
                overlay->setMode(mode);
                overlays.append(overlay);
                connect(overlay, &CaptureOverlay::canceled, this, [this] { finishCapture(); });
                connect(overlay, &CaptureOverlay::accepted, this, [this](QImage image, QPoint pos, bool copyOnly) {
                    finishCapture();
                    if (copyOnly) { history.remember(image, pos); action(HotkeyAction::Replay)->setEnabled(history.enabled); QApplication::clipboard()->setImage(image); }
                    else pin(std::move(image), pos);
                });
                connect(overlay, &CaptureOverlay::ocrRequested, this, [this](QImage image) {
                    finishCapture(); ocr(std::move(image));
                });
                connect(overlay, &CaptureOverlay::translateRequested, this, [this](QImage image) {
                    finishCapture(); ocr(std::move(image), true);
                });
                connect(overlay, &CaptureOverlay::colorCopied, this, [this](QString color) {
                    finishCapture();
                    status->setText("Color copied: " + color);
                    tray.showMessage("Color copied", color, QSystemTrayIcon::Information, 2000);
                });
                connect(overlay, &CaptureOverlay::detectionModeChanged, this, [this, overlay](bool elementMode) {
                    if (elementMode && !elementAccessAllowed(false)) {
                        finishCapture();
                        reportStatus("Allow Accessibility for Element detection, then capture again. Window detection does not need this permission.", QSystemTrayIcon::Warning);
                        elementAccessAllowed(true);
                        return;
                    }
                    for (const auto &other : overlays) if (other && other != overlay) other->setElementMode(elementMode);
                });
                overlay->show();
                if (overlay->geometry().contains(QCursor::pos())) {
                    overlay->raise(); overlay->activateWindow(); overlay->setFocus();
                }
            }
        });
    }

    Hotkeys hotkeys;
    QSystemTrayIcon tray;
    QSettings settings;
    CaptureHistory history;
#ifdef FASTPIN_NATIVE_SETTINGS
    std::unique_ptr<MacSettings> nativeSettings;
    std::unique_ptr<QMenuBar> applicationMenuBar;
#endif
    QTabWidget *pages;
    QPointer<Onboarding> welcome;
    std::array<ShortcutEdit *, hotkeyActionCount> edits{};
    std::array<QAction *, hotkeyActionCount> actions{};
    std::array<QLabel *, hotkeyActionCount> actionKeys{};
    QLabel *status, *settingsFeedback = nullptr, *preview, *historyDetail;
    QPushButton *replayButton;
    QCheckBox *remember;
    bool replayPending = false;
    bool shortcutsRecording = false;
    QList<PinWindow *> pins;
    QList<QPointer<CaptureOverlay>> overlays;
#ifdef Q_OS_MACOS
    std::function<void()> restoreCaptureFocus;
#endif
    std::function<void()> stopDoubleControl;
    bool captureBusy = false;
    bool ocrBusy = false;
};

int main(int argc, char **argv)
{
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--ocr-worker") {
        QCoreApplication app(argc, argv);
        return runOcrWorker();
    }
    QApplication app(argc, argv);
    app.setApplicationName("FastPin");
    app.setOrganizationName("FastPin");
    app.setApplicationVersion(QStringLiteral(FASTPIN_VERSION));
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(QIcon(":/brand/app-icon.png"));
    Controller controller;
    bool openingImage = false;
    for (const auto &argument : app.arguments().mid(1)) {
        if (!argument.startsWith('-')) { openingImage = true; controller.openImage(argument); }
    }
    if (app.arguments().contains("--settings")) controller.showPreferences();
    else if (!app.arguments().contains("--background")) {
#ifndef Q_OS_MACOS
        controller.show();
#endif
        if (!openingImage) controller.showGettingStarted(false);
    }
    return app.exec();
}
