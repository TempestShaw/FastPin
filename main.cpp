#include "capture.h"
#include "hotkey.h"
#include "history.h"
#include "pin.h"
#include "onboarding.h"

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
        auto captureCard = actionCard("capture", "Capture screenshot", "Select a window, element or any region", true);
        captureKeys = captureCard.second; homeLayout->addWidget(captureCard.first);
        connect(captureCard.first, &QPushButton::clicked, this, [this] { capture(); });
        auto pasteCard = actionCard("pin", "Paste as pin", "Keep a clipboard image above your workspace", false);
        pasteKeys = pasteCard.second; homeLayout->addWidget(pasteCard.first);
        connect(pasteCard.first, &QPushButton::clicked, this, [this] { paste(); });
        auto ocrCard = actionCard("ocr", "Copy text from screen", "Select a region to recognize and copy its text", false);
        ocrKeys = ocrCard.second; homeLayout->addWidget(ocrCard.first);
        connect(ocrCard.first, &QPushButton::clicked, this, [this] { capture(true); });
        auto *recovery = new QFrame(this); recovery->setObjectName("card");
        auto *recoveryLayout = new QHBoxLayout(recovery); recoveryLayout->setContentsMargins(18, 16, 18, 16); recoveryLayout->setSpacing(16);
        preview = new QLabel(this); preview->setFixedSize(112, 70); preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet("background:#E9EDEB; color:#77817E; border-radius:7px;"); recoveryLayout->addWidget(preview);
        auto *recoveryText = new QVBoxLayout;
        recoveryText->addWidget(new QLabel("<b>Pick up where you left off</b>", this));
        historyDetail = new QLabel(this); historyDetail->setObjectName("muted"); historyDetail->setWordWrap(true); recoveryText->addWidget(historyDetail);
        replayKeys = new QLabel(this); replayKeys->setObjectName("muted"); recoveryText->addWidget(replayKeys);
        recoveryLayout->addLayout(recoveryText, 1);
        replayButton = new QPushButton("Restore pin", this); replayButton->setObjectName("replayButton");
        connect(replayButton, &QPushButton::clicked, this, [this] { replay(); }); recoveryLayout->addWidget(replayButton);
        homeLayout->addWidget(recovery);
        auto *help = new QLabel("<b>Tab</b> Window / Element · <b>P</b> Pen · <b>A</b> Arrow · <b>R</b> Rectangle · <b>T</b> Text<br>"
                               "<b>O</b> Local OCR · <b>C</b> Pick color · <b>Space</b> Pin tools on hover · <b>Scroll</b> Zoom", this);
        help->setObjectName("muted"); help->setStyleSheet("color:#596660; font-size:12px; padding-top:6px;"); homeLayout->addWidget(help);
        homeLayout->addStretch();

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
        const QStringList names{"Capture screenshot", "Paste as pin", "Restore last image", "Copy text from screen"};
        for (int i = 0; i < 4; ++i) {
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(names[i], this), 1);
            edits[i] = new ShortcutEdit(this);
            edits[i]->setAccessibleName(names[i] + " shortcut"); edits[i]->setFixedWidth(218); row->addWidget(edits[i]); shortcutLayout->addLayout(row);
        }
        preferencesLayout->addWidget(shortcutCard);
        auto *buttons = new QHBoxLayout;
        auto *defaults = new QPushButton("Reset to defaults", this); buttons->addWidget(defaults); buttons->addStretch();
        auto *apply = new QPushButton("Apply shortcuts", this); apply->setObjectName("applyShortcuts"); buttons->addWidget(apply); preferencesLayout->addLayout(buttons);
        connect(defaults, &QPushButton::clicked, this, [this] {
            const auto bindings = Hotkeys::defaults(); for (int i = 0; i < 4; ++i) edits[i]->setBinding(bindings[i]);
            settingsFeedback->setText("Defaults selected. Apply to activate them.");
        });
        connect(apply, &QPushButton::clicked, this, [this] {
            Hotkeys::Bindings bindings; for (int i = 0; i < 4; ++i) bindings[i] = edits[i]->binding();
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
        history.enabled = settings.value("recovery/enabled", true).toBool(); remember->setChecked(history.enabled); preferencesLayout->addWidget(remember);
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
        auto *footer = new QHBoxLayout;
        status = new QLabel("Ready · Close the panel to keep using the global shortcuts.", this);
        status->setWordWrap(true); status->setObjectName("muted"); footer->addWidget(status, 1);
        auto *hideButton = new QPushButton("Hide panel", this); connect(hideButton, &QPushButton::clicked, this, &QWidget::hide); footer->addWidget(hideButton); layout->addLayout(footer);

        auto *menu = new QMenu(this);
        snipAction = menu->addAction("Capture screenshot", this, [this] { capture(); });
        pasteAction = menu->addAction("Paste as pin", this, [this] { paste(); });
        replayAction = menu->addAction("Restore last image", this, [this] { replay(); });
        ocrAction = menu->addAction("Copy text from screen", this, [this] { capture(true); });
        menu->addSeparator();
        menu->addAction("Restore mouse input for all pins", this, [this] { restorePins(); });
        menu->addAction("Show controls…", this, [this] { pages->setCurrentIndex(0); show(); raise(); activateWindow(); });
        menu->addAction("Preferences…", this, [this] { pages->setCurrentIndex(1); show(); raise(); activateWindow(); });
        menu->addAction("Getting started…", this, [this] { showGettingStarted(); });
        menu->addSeparator(); menu->addAction("Quit", qApp, &QApplication::quit);
        tray.setContextMenu(menu);
        auto trayIcon = QIcon(":/brand/app-icon.png");
#ifdef Q_OS_MACOS
        trayIcon = QIcon(":/brand/tray-icon.png"); trayIcon.setIsMask(true);
#endif
        tray.setIcon(trayIcon); tray.setToolTip("FastPin"); tray.show();
        connect(&tray, &QSystemTrayIcon::activated, this, [this](auto reason) { if (reason == QSystemTrayIcon::Trigger) show(); });
        connect(&hotkeys, &Hotkeys::capture, this, [this] { capture(); });
        connect(&hotkeys, &Hotkeys::paste, this, [this] { paste(); });
        connect(&hotkeys, &Hotkeys::replay, this, [this] { replay(); });
        connect(&hotkeys, &Hotkeys::quickOcr, this, [this] { capture(true); });
        connect(&history, &CaptureHistory::changed, this, [this](QString error) {
            if (isVisible()) refreshHistory();
            replayAction->setEnabled(history.available() || history.busy());
            if (!error.isEmpty()) { replayPending = false; status->setText("Recovery: " + error); }
            else if (replayPending && !history.busy()) { replayPending = false; replay(); }
        });
        connect(pages, &QTabWidget::currentChanged, this, [this] { refreshHistory(); });
        const auto bindings = Hotkeys::load(settings);
        const QString error = hotkeys.apply(bindings);
        for (int i = 0; i < 4; ++i) edits[i]->setBinding(bindings[i]);
        if (!error.isEmpty()) { settingsFeedback->setText(error); status->setText("A shortcut is unavailable. Change it in Preferences."); }
        refreshShortcuts(); refreshHistory();
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { refreshShortcutRecording(); });
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
            pages->setCurrentIndex(1); show(); raise(); activateWindow();
        });
        if (!welcome->start(force)) { delete welcome; }
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
            for (auto *edit : edits) if (widget && (widget == edit || edit->isAncestorOf(widget))) {
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
    void refreshShortcutRecording()
    {
        const auto *focus = QApplication::focusWidget();
        bool recording = false;
        if (isVisible() && isActiveWindow() && QGuiApplication::applicationState() == Qt::ApplicationActive)
            for (auto *edit : edits) if (focus && (focus == edit || edit->isAncestorOf(focus))) recording = true;
        if (recording == shortcutsRecording) return;
        shortcutsRecording = recording;
        if (recording) hotkeys.suspend();
        else if (const auto error = hotkeys.apply(hotkeys.bindings()); !error.isEmpty()) settingsFeedback->setText(error);
    }

    void refreshShortcuts()
    {
        const auto &bindings = hotkeys.bindings();
        const std::array<QAction *, 4> actions{snipAction, pasteAction, replayAction, ocrAction};
        const QStringList names{"Capture screenshot", "Paste as pin", "Restore last image", "Copy text from screen"};
        for (int i = 0; i < 4; ++i) {
            actions[i]->setShortcut(bindings[i].key);
            actions[i]->setText(names[i] + (bindings[i].buttons ? " · " + bindings[i].toString() : QString()));
        }
        captureKeys->setText(bindings[0].toString(QKeySequence::NativeText));
        pasteKeys->setText(bindings[1].toString(QKeySequence::NativeText));
        replayKeys->setText(bindings[2].toString(QKeySequence::NativeText));
        ocrKeys->setText(bindings[3].toString(QKeySequence::NativeText));
    }

    void refreshHistory()
    {
        historyDetail->setText(history.description());
        const auto image = history.thumbnail();
        if (image.isNull()) { preview->setPixmap({}); preview->setText("No saved image"); }
        else { preview->setText({}); preview->setPixmap(QPixmap::fromImage(image).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); }
        replayButton->setEnabled(history.available() || (history.enabled && history.busy()));
        replayAction->setEnabled(replayButton->isEnabled());
    }

    void replay()
    {
        if (captureBusy) return;
        if (history.busy()) { replayPending = true; status->setText("Preparing your last image…"); return; }
        auto saved = history.restore();
        if (saved.image.isNull()) { status->setText("No saved image to restore. Capture or paste an image first."); show(); return; }
        pin(std::move(saved.image), saved.position, saved.zoom);
        status->setText("Last image restored, including annotations and zoom.");
    }

    void pin(QImage image, QPoint position, qreal zoom = 1)
    {
        history.remember(image, position, zoom);
        replayAction->setEnabled(history.enabled);
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
        window->show();
        status->setText(QString("%1 pinned image(s) · Hover and press Space for annotation tools.").arg(pins.size()));
    }

    void paste()
    {
        auto image = QApplication::clipboard()->image();
        if (image.isNull()) { status->setText("The clipboard does not contain an image."); show(); return; }
        pin(std::move(image), QCursor::pos());
    }

    void restorePins()
    {
        for (auto *window : pins) window->setClickThrough(false);
        status->setText("Mouse input restored for all pins.");
    }

    void ocr(QImage image, bool quick = false)
    {
        if (ocrBusy) { status->setText("OCR is already running."); show(); return; }
        ocrBusy = true;
        status->setText("Recognizing text locally…");
        auto result = std::make_shared<OcrResult>();
        const auto executable = QCoreApplication::applicationFilePath();
        auto *worker = QThread::create([image = std::move(image), executable, result] { *result = recognizeTextInWorker(image, executable); });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &QThread::finished, this, [this, result, quick] {
            ocrBusy = false;
            if (!result->error.isEmpty()) status->setText("OCR: " + result->error);
            else if (result->text.trimmed().isEmpty()) status->setText("OCR found no text. Clipboard kept.");
            else {
                QApplication::clipboard()->setText(result->text);
                status->setText(QString("OCR copied %1 characters to the clipboard.").arg(result->text.size()));
            }
            if (!quick || !result->error.isEmpty() || result->text.trimmed().isEmpty()) show();
            else tray.showMessage("Text copied", "Your selected text is ready to paste.", QSystemTrayIcon::Information, 2000);
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
    }

    void capture(bool quick = false)
    {
        if (captureBusy) return;
        if (quick && ocrBusy) { status->setText("OCR is already running."); show(); return; }
        if (!screenCaptureAllowed()) {
            status->setText("Allow FastPin in System Settings → Privacy & Security → Screen Recording, then try again. macOS may require restarting the app.");
            show();
            return;
        }
        captureBusy = true;
        if (welcome) welcome->reject();
        hide();
        // One activation delay lets the panel/tray menu leave the compositor.
        // There is no recurring timer, mouse polling or screen recording stream.
        QTimer::singleShot(80, this, [this, quick] {
            QList<ScreenFrame> frames;
            QList<DesktopCoordinates> coordinates;
            const auto windows = desktopWindows();
            for (auto *screen : QGuiApplication::screens()) {
                const QPixmap pixmap = screen->grabWindow(0);
                if (pixmap.isNull()) {
                    finishCapture();
                    status->setText("Screen capture failed. Check screen recording permission.");
                    show();
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
                overlay->setQuickOcr(quick);
                overlays.append(overlay);
                connect(overlay, &CaptureOverlay::canceled, this, [this] { finishCapture(); });
                connect(overlay, &CaptureOverlay::accepted, this, [this](QImage image, QPoint pos, bool copyOnly) {
                    finishCapture();
                    if (copyOnly) { history.remember(image, pos); replayAction->setEnabled(history.enabled); QApplication::clipboard()->setImage(image); }
                    else pin(std::move(image), pos);
                });
                connect(overlay, &CaptureOverlay::ocrRequested, this, [this, quick](QImage image) {
                    finishCapture(); ocr(std::move(image), quick);
                });
                connect(overlay, &CaptureOverlay::colorCopied, this, [this](QString color) {
                    finishCapture();
                    status->setText("Color copied: " + color);
                    tray.showMessage("Color copied", color, QSystemTrayIcon::Information, 2000);
                });
                connect(overlay, &CaptureOverlay::detectionModeChanged, this, [this, overlay](bool elementMode) {
                    if (elementMode && !elementAccessAllowed(false)) {
                        finishCapture();
                        status->setText("Allow Accessibility for Element detection, then capture again. Window detection does not need this permission.");
                        show();
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
    QTabWidget *pages;
    QPointer<Onboarding> welcome;
    std::array<ShortcutEdit *, 4> edits{};
    QLabel *status, *settingsFeedback, *preview, *historyDetail, *captureKeys, *pasteKeys, *replayKeys, *ocrKeys;
    QPushButton *replayButton;
    QCheckBox *remember;
    QAction *snipAction, *pasteAction, *replayAction, *ocrAction;
    bool replayPending = false;
    bool shortcutsRecording = false;
    QList<PinWindow *> pins;
    QList<QPointer<CaptureOverlay>> overlays;
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
    if (!app.arguments().contains("--background")) {
        controller.show();
        if (!openingImage) controller.showGettingStarted(false);
    }
    return app.exec();
}
