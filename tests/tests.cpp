#include "singleinstance.h"
#include "capture.h"
#include "pin.h"
#include "texteditor.h"
#include "hotkey.h"
#include "history.h"
#include "onboarding.h"
#include "translation.h"
#include "translationcard.h"
#include "doubletap.h"
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QApplication>
#include <QLabel>
#include <QAction>
#include <QClipboard>
#include <QPainter>
#include <QDialog>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalSpy>
#include <QScreen>
#include <QToolButton>
#include <QPushButton>
#include <QUrlQuery>
#include <QtTest>
#include <future>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

class NativeTests : public QObject {
    Q_OBJECT
private slots:
    void offlineDictionaryAndOnlineTranslationRouting()
    {
        QVERIFY(offlineDefinition(" Bank. ").contains(QString::fromUtf8("銀行")));
        QVERIFY(!offlineDefinition("bank").contains('['));
        QVERIFY(!offlineDefinition("bank").contains("bank", Qt::CaseInsensitive));
        QVERIFY(!offlineDefinition("bank").contains('\n'));
        QVERIFY(offlineDefinition("bank").contains(QString::fromUtf8("，")));
        QVERIFY(!offlineDefinition(QString::fromUtf8("電腦")).contains('['));
        QVERIFY(offlineDefinition("HELLO").contains(QString::fromUtf8("哈囉")));
        QVERIFY(offlineDefinition(QString::fromUtf8("電腦")).contains("computer"));
        QVERIFY(offlineDefinition(QString::fromUtf8("电脑")).contains("computer"));
        QVERIFY(!offlineDefinition("thank you").isEmpty());
        QVERIFY(offlineDefinition("fastpinmadeupword").isEmpty());
        QVERIFY(offlineDefinition("A whole sentence that has no dictionary entry.").isEmpty());
        QVERIFY(offlineDefinition("").isEmpty());
        QVERIFY(isDictionaryWord("fastpinmadeupword"));
        QVERIFY(isDictionaryWord(QString::fromUtf8("詞庫")));
        QVERIFY(!isDictionaryWord("Where is the bank?"));
        QVERIFY(!isDictionaryWord(QString::fromUtf8("這是一段中文。")));
        const QString sentence = "Hello & goodbye? A+B #test\nSecond line.";
        const auto url = onlineTranslationUrl(sentence);
        QCOMPARE(url.scheme(), QString("https")); QCOMPARE(url.host(), QString("api.mymemory.translated.net"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue("q", QUrl::FullyDecoded), sentence);
        QCOMPARE(query.queryItemValue("langpair", QUrl::FullyDecoded), QString("en|zh-TW"));
        QCOMPARE(QUrlQuery(onlineTranslationUrl(QString::fromUtf8("這是一段中文。"))).queryItemValue("langpair", QUrl::FullyDecoded), QString("zh-TW|en"));
        const auto reply = parseTranslationReply(R"({"responseStatus":200,"responseData":{"translatedText":"A &amp; B"}})");
        QCOMPARE(reply.text, QString("A & B")); QVERIFY(reply.error.isEmpty());
        QVERIFY(!parseTranslationReply("not json").error.isEmpty());
        QVERIFY(!parseTranslationReply(R"({"responseStatus":200,"responseData":{"translatedText":""}})").error.isEmpty());
        QVERIFY(parseTranslationReply(R"({"responseStatus":"429","quotaFinished":true})").error.contains("quota"));
        QVERIFY(parseTranslationReply({}, true).error.contains("connection"));
        QVERIFY(parseTranslationReply(R"({"responseStatus":"429","quotaFinished":true})", true).error.contains("quota"));
        QVERIFY(!parseTranslationReply(R"({"responseStatus":500})").error.contains("connection"));
    }

    void translationCardStaysNearPointerWithoutFocusAndCloses()
    {
        QWidget original; original.show(); original.activateWindow(); original.setFocus();
        auto *card = new TranslationCard("bank"); QPointer<TranslationCard> pointer(card);
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        card->showAt(area.bottomRight() - QPoint(2, 2));
        QVERIFY(card->isVisible()); QVERIFY(card->width() <= 360); QVERIFY(card->height() <= 250);
        QVERIFY(area.contains(card->geometry()));
        QVERIFY(card->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(card->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY(card->testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(card->findChild<QPlainTextEdit *>("translationDefinition")->toPlainText().contains(QString::fromUtf8("銀行")));
        QVERIFY(card->findChild<QToolButton *>("pronounce"));
        card->findChild<QToolButton *>("closeTranslation")->click();
        QTRY_VERIFY(pointer.isNull()); QVERIFY(original.isVisible());
        auto *oversized = new TranslationCard(QString(501, 'x'));
        QVERIFY(oversized->findChild<QPlainTextEdit *>("translationDefinition")->toPlainText().contains("shorter"));
        oversized->close();
    }

    void annotationHistoryKeepsEditsAndBranchesInOrder()
    {
        AnnotationLayer layer;
        Annotation first; first.tool = Tool::Text; first.start = {20, 40}; first.text = "First";
        layer.add(first); layer.replaceText(0, "Edited");
        Annotation second = first; second.start = {20, 100}; second.text = "Second"; layer.add(second);
        layer.undo(); layer.undo(); QCOMPARE(layer.at(0).text, QString("First"));
        layer.redo(); layer.redo(); QCOMPARE(layer.at(0).text, QString("Edited")); QCOMPARE(layer.at(1).text, QString("Second"));
        layer.undo(); layer.replaceText(0, "New branch");
        QVERIFY(!layer.canRedo()); QCOMPARE(layer.textAt({20, 95}), qsizetype(-1));
        layer.undo(); QCOMPARE(layer.at(0).text, QString("Edited"));
        layer.undo(); QCOMPARE(layer.at(0).text, QString("First"));
        layer.undo(); QVERIFY(!layer.canUndo()); QCOMPARE(layer.textAt({20, 35}), qsizetype(-1));
        layer.redo(); layer.redo(); layer.redo(); QCOMPARE(layer.at(0).text, QString("New branch"));
    }

    void translationActionsLeaveOcrShortcutIntact()
    {
        QImage image(600, 350, QImage::Format_RGB32); image.fill(Qt::white);
        PinWindow pin(image);
        QSignalSpy pinTranslation(&pin, &PinWindow::translateRequested), pinOcr(&pin, &PinWindow::ocrRequested);
        QTest::keyClick(&pin, Qt::Key_O, Qt::ShiftModifier);
        QCOMPARE(pinTranslation.count(), 1); QCOMPARE(pinOcr.count(), 0);
        QTest::keyClick(&pin, Qt::Key_O);
        QCOMPARE(pinOcr.count(), 1);
        pin.findChild<QToolButton *>("translate")->click(); QCOMPARE(pinTranslation.count(), 2);
        CaptureOverlay capture({image, {0, 0, 600, 350}, 1}, {});
        capture.show();
        QTest::mousePress(&capture, Qt::LeftButton, {}, {20, 20});
        QTest::mouseMove(&capture, {400, 220}); QTest::mouseRelease(&capture, Qt::LeftButton, {}, {400, 220});
        QSignalSpy captureTranslation(&capture, &CaptureOverlay::translateRequested), captureOcr(&capture, &CaptureOverlay::ocrRequested);
        QTest::keyClick(&capture, Qt::Key_O, Qt::ShiftModifier);
        QCOMPARE(captureTranslation.count(), 1); QCOMPARE(captureOcr.count(), 0);
        QTest::keyClick(&capture, Qt::Key_O); QCOMPARE(captureOcr.count(), 1);
        capture.findChild<QToolButton *>("translate")->click(); QCOMPARE(captureTranslation.count(), 2);
    }

    void singleInstanceForwardsAndReleasesOwnership()
    {
        QTemporaryDir dir;
        {
            SingleInstance first(dir.path()); QVERIFY2(first.isPrimary(), qPrintable(first.error()));
            QSignalSpy requests(&first, &SingleInstance::requested);
            SingleInstance second(dir.path()); QVERIFY(!second.isPrimary()); QVERIFY(second.error().isEmpty());
            QVERIFY(second.forward({"--settings", "/tmp/image with spaces.png"}));
            QTRY_COMPARE(requests.count(), 1);
            QCOMPARE(requests.first().first().toStringList(), (QStringList{"--settings", "/tmp/image with spaces.png"}));
        }
        SingleInstance restarted(dir.path()); QVERIFY(restarted.isPrimary());
    }

    void onboardingCompletionRequiresFinishingAndCanBeReopened()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        Onboarding first(settings, Hotkeys::defaults());
        first.readPermissions = [] { return Onboarding::Permissions{true, true, true}; };
        QVERIFY(first.start()); first.reject();
        QVERIFY(!settings.value("onboarding/completed").toBool());
        Onboarding next(settings, Hotkeys::defaults());
        next.readPermissions = first.readPermissions;
        QVERIFY(next.start()); // Seeing or dismissing setup is not completing it.
        QSignalSpy preferences(&next, &Onboarding::preferencesRequested);
        QSignalSpy capture(&next, &Onboarding::captureRequested);
        auto *pages = next.findChild<QStackedWidget *>(); QVERIFY(pages);
        auto *forward = next.findChild<QPushButton *>("primary"); QVERIFY(forward);
        QCOMPARE(pages->currentIndex(), 1);
        forward->click(); QCOMPARE(pages->currentIndex(), 2);
        for (auto *button : next.findChildren<QPushButton *>()) if (button->text() == "Customize shortcuts…") button->click();
        QCOMPARE(preferences.count(), 1); QVERIFY(next.isVisible());
        QVERIFY(!settings.value("onboarding/completed").toBool());
        forward->click(); QCOMPARE(capture.count(), 1); QVERIFY(!next.isVisible());
        QVERIFY(settings.value("onboarding/completed").toBool());
        Onboarding done(settings, Hotkeys::defaults()); done.readPermissions = first.readPermissions;
        QVERIFY(!done.start()); QVERIFY(done.start(true));
    }

    void onboardingDetectsPermissionsAndResumesUntilAllAreAllowed()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        Onboarding::Permissions access{false, false, false};
        Onboarding setup(settings, Hotkeys::defaults());
        setup.readPermissions = [&] { return access; };
        QSignalSpy ready(&setup, &Onboarding::permissionsReady);
        QVERIFY(setup.start());
        auto *pages = setup.findChild<QStackedWidget *>();
        auto *forward = setup.findChild<QPushButton *>("primary");
        forward->click(); QCOMPARE(pages->currentIndex(), 1);
        QVERIFY(!forward->isEnabled()); QVERIFY(setup.permissionTimer.isActive());
        access.screen = access.accessibility = true;
        setup.refreshPermissions(); QVERIFY(!forward->isEnabled());
        QVERIFY(!setup.complete()); QVERIFY(!settings.value("onboarding/completed").toBool());
        setup.reject(); QVERIFY(!setup.permissionTimer.isActive());
        QVERIFY(setup.start()); QCOMPARE(pages->currentIndex(), 1);
        QTest::qWait(30); // Drain activation events before simulating a grant in System Settings.
        access.input = true;
        QTRY_VERIFY_WITH_TIMEOUT(forward->isEnabled(), 2000); // Actual timer detects the grant without another click.
        QCOMPARE(ready.count(), 1); QVERIFY(!setup.permissionTimer.isActive());
        forward->click(); QCOMPARE(pages->currentIndex(), 2);
        access.screen = false; // Revocation must also block the final completion action.
        forward->click(); QCOMPARE(pages->currentIndex(), 1);
        QVERIFY(!settings.value("onboarding/completed").toBool()); QVERIFY(!forward->isEnabled());
        access.screen = true; setup.refreshPermissions(); forward->click(); forward->click();
        QVERIFY(settings.value("onboarding/completed").toBool());
        access.input = false;
        Onboarding returning(settings, Hotkeys::defaults()); returning.readPermissions = [&] { return access; };
        QVERIFY(returning.start()); QCOMPARE(returning.findChild<QStackedWidget *>()->currentIndex(), 1);
        QVERIFY(!settings.value("onboarding/completed").toBool());
    }

    void mouseShortcutsPersistAlongsideKeyboardBindings()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        auto bindings = Hotkeys::defaults();
        bindings[0] = Shortcut(Qt::BackButton | Qt::ForwardButton, Qt::ControlModifier | Qt::AltModifier);
        bindings[1] = Shortcut(Qt::BackButton);
        QVERIFY(Hotkeys::validate(bindings).isEmpty());
        Hotkeys::save(settings, bindings);
        QCOMPARE(Hotkeys::load(settings), bindings);
        QCOMPARE(Shortcut::fromString(bindings[0].toString(QKeySequence::PortableText)), bindings[0]);
        bindings[2] = bindings[1]; QVERIFY(!Hotkeys::validate(bindings).isEmpty());
        bindings[2] = Shortcut(Qt::LeftButton); QVERIFY(!Hotkeys::validate(bindings).isEmpty());
        bindings[2] = Shortcut(Qt::ForwardButton, Qt::KeypadModifier); QVERIFY(!Hotkeys::validate(bindings).isEmpty());
        for (const QString &invalid : {"Mouse6", "Mouse4+K", "Mouse4+Mouse4", "Ctrl++Mouse5"}) {
            settings.setValue("shortcuts/0", invalid);
            QCOMPARE(Hotkeys::load(settings), Hotkeys::defaults());
        }
    }

    void mouseGesturesPreferCompleteChordAndReplayUnfinishedPrefix()
    {
        const Hotkeys::Bindings bindings{Shortcut(Qt::BackButton), Shortcut(Qt::BackButton | Qt::ForwardButton),
                                        Shortcut(Qt::ForwardButton, Qt::AltModifier)};
        MouseGesture gesture;
        QVERIFY(gesture.event(Qt::BackButton, true, {}, bindings).consume);
        QCOMPARE(gesture.event(Qt::BackButton, false, {}, bindings).action, 0);
        for (auto first : {Qt::BackButton, Qt::ForwardButton}) {
            const auto second = first == Qt::BackButton ? Qt::ForwardButton : Qt::BackButton;
            QCOMPARE(gesture.event(first, true, {}, bindings).action, -1);
            QCOMPARE(gesture.event(second, true, {}, bindings).action, -1);
            QCOMPARE(gesture.event(first, false, {}, bindings).action, -1);
            QCOMPARE(gesture.event(second, false, {}, bindings).action, 1);
            QCOMPARE(gesture.event(second, false, {}, bindings).action, -1);
        }
        QVERIFY(gesture.event(Qt::ForwardButton, true, {}, bindings).consume);
        const auto incomplete = gesture.event(Qt::ForwardButton, false, {}, bindings);
        QVERIFY(incomplete.consume); QCOMPARE(incomplete.action, -1); QCOMPARE(incomplete.replay, Qt::MouseButtons(Qt::ForwardButton));
        QVERIFY(!gesture.event(Qt::BackButton, true, Qt::ShiftModifier, bindings).consume);
        QVERIFY(!gesture.event(Qt::BackButton, false, {}, bindings).consume);
        QVERIFY(gesture.event(Qt::ForwardButton, true, Qt::AltModifier, bindings).consume);
        QCOMPARE(gesture.event(Qt::ForwardButton, false, {}, bindings).action, 2); // Modifiers are captured on press.
        QVERIFY(!gesture.event(Qt::LeftButton, true, {}, bindings).consume);
        gesture.event(Qt::BackButton, true, {}, bindings); gesture = {};
        QVERIFY(!gesture.event(Qt::BackButton, false, {}, bindings).consume);
    }

    void recorderCapturesSideButtonsOverOtherWidgetsAndKeepsKeyboardSupport()
    {
        QWidget window; ShortcutEdit edit(&window); QPushButton other("Other", &window);
        edit.setGeometry(0, 0, 230, 40); other.setGeometry(0, 50, 100, 40);
        window.resize(260, 110); window.show(); window.activateWindow(); edit.setFocus();
        QTRY_VERIFY(edit.hasFocus());
        const Shortcut keyboard(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K)); edit.setBinding(keyboard);
        for (auto first : {Qt::BackButton, Qt::ForwardButton}) {
            const auto second = first == Qt::BackButton ? Qt::ForwardButton : Qt::BackButton;
            QTest::mousePress(window.windowHandle(), first, Qt::AltModifier, {40, 70});
            QTest::mousePress(window.windowHandle(), second, Qt::AltModifier, {40, 70});
            QTest::mouseRelease(window.windowHandle(), first, Qt::AltModifier, {40, 70});
            QVERIFY(edit.hasFocus());
            QTest::mouseRelease(window.windowHandle(), second, Qt::AltModifier, {40, 70});
            QCOMPARE(edit.binding(), Shortcut(Qt::BackButton | Qt::ForwardButton, Qt::AltModifier));
        }
        QTest::keyClick(&edit, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(edit.binding(), keyboard);
        const auto mods = Qt::ControlModifier | Qt::ShiftModifier;
#ifdef Q_OS_MACOS
        const quint32 digitCode = 0x13; // kVK_ANSI_2
#else
        const quint32 digitCode = '2'; // VK_2
#endif
        QKeyEvent shiftedDigit(QEvent::KeyPress, Qt::Key_At, mods, 0, digitCode, 0, "@");
        QApplication::sendEvent(&edit, &shiftedDigit);
        QCOMPARE(edit.binding(), Shortcut(QKeySequence(QKeyCombination(mods, Qt::Key_2))));
        edit.setBinding(keyboard);
        QEvent override(QEvent::ShortcutOverride); QApplication::sendEvent(&edit, &override); QVERIFY(override.isAccepted());
        QTest::mousePress(window.windowHandle(), Qt::BackButton, {}, {40, 20});
        other.setFocus(); QTest::mouseRelease(window.windowHandle(), Qt::BackButton, {}, {40, 70});
        QCOMPARE(edit.binding(), keyboard); // Leaving a field cancels an unfinished mouse gesture.
        edit.setFocus(); QTest::keyClick(&edit, Qt::Key_Backspace); QVERIFY(edit.binding().isEmpty());
        QTest::mouseClick(window.windowHandle(), Qt::ForwardButton, Qt::ShiftModifier, {40, 70});
        QCOMPARE(edit.binding(), Shortcut(Qt::ForwardButton, Qt::ShiftModifier));
    }

    void recorderAcceptsKeyboardCombinationsAndFunctionKeys_data()
    {
        QTest::addColumn<int>("key"); QTest::addColumn<int>("modifiers");
        QTest::newRow("Ctrl+A") << int(Qt::Key_A) << int(Qt::ControlModifier);
        QTest::newRow("Meta+A") << int(Qt::Key_A) << int(Qt::MetaModifier);
        QTest::newRow("Shift+A") << int(Qt::Key_A) << int(Qt::ShiftModifier);
        QTest::newRow("Ctrl+Shift+A") << int(Qt::Key_A) << int(Qt::ControlModifier | Qt::ShiftModifier);
        for (int key = Qt::Key_F1; key <= Qt::Key_F10; ++key)
            QTest::newRow(qPrintable(QKeySequence(key).toString())) << key << 0;
    }

    void recorderAcceptsKeyboardCombinationsAndFunctionKeys()
    {
        QFETCH(int, key); QFETCH(int, modifiers);
        QWidget window; ShortcutEdit edit(&window);
        const Shortcut expected{QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key)))};
        QAction action(&window); action.setShortcut(expected.key); window.addAction(&action);
        QSignalSpy triggered(&action, &QAction::triggered);
        window.show(); window.activateWindow(); edit.setFocus(); QTRY_VERIFY(edit.hasFocus());
        QTest::keyClick(window.windowHandle(), Qt::Key(key), Qt::KeyboardModifiers(modifiers));
        QCOMPARE(edit.binding(), expected);
        QCOMPARE(edit.text(), expected.toString());
        QCOMPARE(triggered.count(), 0); // Recording must override existing application shortcuts.
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const Hotkeys::Bindings bindings{edit.binding(), {}, {}};
        QVERIFY2(Hotkeys::validate(bindings).isEmpty(), qPrintable(Hotkeys::validate(bindings)));
        Hotkeys::save(settings, bindings);
        QSettings reloaded(dir.filePath("settings.ini"), QSettings::IniFormat);
        QCOMPARE(Hotkeys::load(reloaded), bindings);
    }

    void shortcutsValidateAndPersist()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        auto bindings = Hotkeys::defaults();
        QVERIFY(Hotkeys::validate(bindings).isEmpty());
        bindings[0] = QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K); bindings[2] = {};
        Hotkeys::save(settings, bindings);
        QSettings reloaded(dir.filePath("settings.ini"), QSettings::IniFormat);
        QCOMPARE(Hotkeys::load(reloaded), bindings);
        bindings[1] = bindings[0]; QVERIFY(!Hotkeys::validate(bindings).isEmpty());
        bindings[1] = QKeySequence(Qt::Key_X); QVERIFY(!Hotkeys::validate(bindings).isEmpty());
        bindings[1] = QKeySequence("Ctrl+K, Ctrl+C"); QVERIFY(!Hotkeys::validate(bindings).isEmpty());
        reloaded.setValue("shortcuts/0", "Ctrl+K, Ctrl+C"); QCOMPARE(Hotkeys::load(reloaded), Hotkeys::defaults());
    }

    void dictionaryShortcutAvoidsCommonSystemKeys()
    {
        const auto defaults = Hotkeys::defaults();
        const auto translate = int(HotkeyAction::Translate);
        const auto control = defaults[0].key[0].keyboardModifiers() & ~Qt::ShiftModifier;
        const Shortcut legacy(QKeySequence(QKeyCombination(control, Qt::Key_D)));
        QCOMPARE(defaults[translate], Shortcut(QKeySequence(QKeyCombination(control | Qt::ShiftModifier, Qt::Key_1))));
        QVERIFY(Hotkeys::validate(defaults).isEmpty());
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        // preview.7 saved Control+D as the Dictionary default without a settings version.
        settings.setValue("shortcuts/4", legacy.toString(QKeySequence::PortableText));
        QCOMPARE(Hotkeys::load(settings)[translate], defaults[translate]);
        // Once saved, a deliberate Control+D choice is kept.
        auto chosen = defaults; chosen[translate] = legacy;
        Hotkeys::save(settings, chosen); QCOMPARE(Hotkeys::load(settings), chosen);
        // The migrated default must not take a shortcut another action already uses.
        settings.clear();
        settings.setValue("shortcuts/0", defaults[translate].toString(QKeySequence::PortableText));
        settings.setValue("shortcuts/4", legacy.toString(QKeySequence::PortableText));
        auto loaded = Hotkeys::load(settings);
        QCOMPARE(loaded[0], defaults[translate]); QVERIFY(loaded[translate].isEmpty());
        // A paste shortcut customized to Control+D no longer has to move.
        settings.clear();
        settings.setValue("shortcuts/1", legacy.toString(QKeySequence::PortableText));
        loaded = Hotkeys::load(settings);
        QCOMPARE(loaded[1], legacy); QCOMPARE(loaded[translate], defaults[translate]);
    }

    void doubleControlTapTriggersOnlyForTwoQuickCleanTaps()
    {
        using In = DoubleTapDetector::Input;
        auto taps = [](DoubleTapDetector &d, std::initializer_list<std::pair<In, qint64>> events) {
            int triggered = 0;
            for (const auto &[input, at] : events) triggered += d.feed(input, at);
            return triggered;
        };
        DoubleTapDetector quick;
        QCOMPARE(taps(quick, {{In::ControlDown, 0}, {In::ControlUp, 80}, {In::ControlDown, 200}, {In::ControlUp, 260}}), 1);
        // A third tap starts over instead of firing again.
        QCOMPARE(taps(quick, {{In::ControlDown, 330}, {In::ControlUp, 380}}), 0);
        DoubleTapDetector slowGap;
        QCOMPARE(taps(slowGap, {{In::ControlDown, 0}, {In::ControlUp, 80}, {In::ControlDown, 700}, {In::ControlUp, 760}}), 0);
        DoubleTapDetector held;
        QCOMPARE(taps(held, {{In::ControlDown, 0}, {In::ControlUp, 600}, {In::ControlDown, 700}, {In::ControlUp, 760}}), 0);
        // Control used as a modifier (Ctrl+C, Ctrl-click) never counts as a tap.
        DoubleTapDetector chord;
        QCOMPARE(taps(chord, {{In::ControlDown, 0}, {In::Other, 40}, {In::ControlUp, 80}, {In::ControlDown, 200}, {In::ControlUp, 260}}), 0);
        DoubleTapDetector interrupted;
        QCOMPARE(taps(interrupted, {{In::ControlDown, 0}, {In::ControlUp, 80}, {In::Other, 120}, {In::ControlDown, 200}, {In::ControlUp, 260}}), 0);
        // Windows repeats key-down while Control is held; repeats are ignored.
        DoubleTapDetector repeats;
        QCOMPARE(taps(repeats, {{In::ControlDown, 0}, {In::ControlDown, 30}, {In::ControlUp, 80}, {In::ControlDown, 200}, {In::ControlUp, 260}}), 1);
        DoubleTapDetector strayUp;
        QCOMPARE(taps(strayUp, {{In::ControlUp, 0}, {In::ControlDown, 100}, {In::ControlUp, 150}}), 0);
    }

    void quickOcrBindingPreservesExistingPreferences()
    {
        QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        auto existing = Hotkeys::defaults();
        existing[0] = existing[3];
        for (int i = 0; i < 3; ++i)
            settings.setValue(QString("shortcuts/%1").arg(i), existing[i].toString(QKeySequence::PortableText));
        auto loaded = Hotkeys::load(settings);
        for (int i = 0; i < 3; ++i) QCOMPARE(loaded[i], existing[i]);
        QVERIFY(loaded[3].isEmpty());
        loaded[3] = Shortcut(Qt::ForwardButton, Qt::ShiftModifier);
        Hotkeys::save(settings, loaded);
        QCOMPARE(Hotkeys::load(settings), loaded);
        MouseGesture gesture;
        QVERIFY(gesture.event(Qt::ForwardButton, true, Qt::ShiftModifier, loaded).consume);
        QCOMPARE(gesture.event(Qt::ForwardButton, false, {}, loaded).action, 3);
        loaded[3] = loaded[0]; QVERIFY(!Hotkeys::validate(loaded).isEmpty());
    }

    void conflictingShortcutKeepsPreviousRegistration()
    {
#ifdef Q_OS_WIN
        DWORD session = 0;
        if (ProcessIdToSessionId(GetCurrentProcessId(), &session) && session == 0)
            QSKIP("SSH Session 0 has no interactive window station (RegisterHotKey error 1459). Verify on the interactive desktop.");
#endif
        Hotkeys first, second, probe;
        Hotkeys::Bindings a{QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F18), {}, {}};
        Hotkeys::Bindings b{QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F19), {}, {}};
        QVERIFY2(first.apply(a).isEmpty(), "The F18 test shortcut must be available on this test host.");
        QVERIFY(second.apply(b).isEmpty());
        QVERIFY(!second.apply(a).isEmpty()); QCOMPARE(second.bindings(), b);
        QVERIFY(!probe.apply(b).isEmpty()); // The old native registration is still owned by second.
        QVERIFY(first.apply({}).isEmpty()); QVERIFY(second.apply(a).isEmpty());
        QVERIFY(probe.apply(b).isEmpty()); // A successful change released the old native key.
        second.suspend(); QVERIFY(first.apply(a).isEmpty()); // The recorder can receive a currently bound chord.
        QVERIFY(!second.apply(second.bindings()).isEmpty());
        QVERIFY(first.apply({}).isEmpty()); QVERIFY(second.apply(second.bindings()).isEmpty());
    }

#ifdef Q_OS_WIN
    void unpackagedOcrExplainsRequiredIdentity()
    {
        QImage image(200, 100, QImage::Format_RGB32); image.fill(Qt::white);
        const auto result = std::async(std::launch::async, [image] {
            return recognizeTextInWorker(image, QString::fromUtf8(FASTPIN_APPLICATION));
        }).get();
        QVERIFY2(result.error.contains("MSIX"), qPrintable(result.error));
    }
#endif

    void lastImageSurvivesRestartWithAnnotationsAndZoom()
    {
        QTemporaryDir dir; const QString path = dir.filePath("recovery/last.png");
        QImage source(600, 300, QImage::Format_RGB32); source.fill(Qt::white); source.setDevicePixelRatio(2);
        PinWindow pin(source); pin.setZoom(0.75);
        Annotation annotation; annotation.tool = Tool::Rectangle; annotation.start = {30, 30}; annotation.end = {130, 100};
        annotation.color = Qt::red; annotation.width = 6; pin.addAnnotation(annotation);
        const QImage expected = pin.exportedImage();
        {
            CaptureHistory history(path); history.remember(expected, {-250, 120}, pin.zoomFactor());
            QTRY_VERIFY_WITH_TIMEOUT(!history.busy(), 3000); QVERIFY(history.available());
        }
        CaptureHistory restarted(path); const auto saved = restarted.restore();
        QCOMPARE(saved.image, expected); QCOMPARE(saved.image.devicePixelRatio(), 2.0);
        QCOMPARE(saved.position, QPoint(-250, 120)); QCOMPARE(saved.zoom, 0.75);
        QVERIFY(saved.image.text("FastPin.PositionX").isEmpty());
        QVERIFY(saved.image.text("FastPin.Thumbnail").isEmpty());
        const auto thumbnail = restarted.thumbnail(); QVERIFY(!thumbnail.isNull());
        QVERIFY(thumbnail.width() <= 160); QVERIFY(thumbnail.height() <= 86);
        const auto initialSize = QFileInfo(path).size();
        for (int i = 0; i < 8; ++i) {
            restarted.remember(restarted.restore().image, {i, 100}, 1.25);
            QTRY_VERIFY_WITH_TIMEOUT(!restarted.busy(), 3000);
            QCOMPARE(restarted.restore().position, QPoint(i, 100));
            QCOMPARE(restarted.restore().zoom, 1.25);
        }
        QVERIFY2(QFileInfo(path).size() < initialSize + 2048, "Repeated restoration must not recursively embed old thumbnail metadata.");
        restarted.clear(); QVERIFY(!QFileInfo::exists(path)); QVERIFY(restarted.restore().image.isNull());
    }

    void historyKeepsNewestAndClearDoesNotResurrectImage()
    {
        QTemporaryDir dir; const QString path = dir.filePath("last.png");
        QImage first(100, 50, QImage::Format_RGB32); first.fill(Qt::red);
        QImage second = first; second.fill(Qt::blue);
        {
            CaptureHistory history(path); history.remember(first, {10, 20}); history.remember(second, {30, 40}, 2);
            QTRY_VERIFY_WITH_TIMEOUT(!history.busy(), 3000);
            QCOMPARE(history.restore().image.pixelColor(0, 0), QColor(Qt::blue));
            QCOMPARE(history.restore().position, QPoint(30, 40));
            QCOMPARE(history.thumbnail().pixelColor(0, 0), QColor(Qt::blue));
            history.remember(first, {}); history.clear();
            QTRY_VERIFY_WITH_TIMEOUT(!history.busy(), 3000); QVERIFY(!QFileInfo::exists(path));
            history.remember(first, {}); history.clear(); // Also cover quitting before the completion callback.
        }
        QVERIFY(!QFileInfo::exists(path));
        CaptureHistory history(path); history.enabled = false; history.remember(first, {});
        QVERIFY(!history.busy()); QVERIFY(!history.available());
    }

    void elementPreviewStaysStableWhilePointerMoves()
    {
        ScreenFrame frame{QImage(800, 500, QImage::Format_RGB32), QRect(0, 0, 800, 500), 1};
        frame.image.fill(Qt::white);
        DesktopWindow window{QRectF(0, 0, 800, 500), 1, 0};
        CaptureOverlay overlay(frame, {window}); overlay.show();
        overlay.elementMode = overlay.elementAllowed = overlay.queryBusy = true;
        overlay.queriedWindow = window.handle;
        overlay.queriedPoint = overlay.pointer = QPointF(100, 100);
        overlay.elementRect = QRectF(80, 80, 160, 90);
        overlay.updateDetection();
        QCOMPARE(overlay.selection.rect, overlay.elementRect);
        for (int x = 101; x < 150; ++x) {
            QTest::mouseMove(&overlay, {x, 100});
            QCOMPARE(overlay.selection.rect, overlay.elementRect);
        }
        QTest::mouseMove(&overlay, {300, 100});
        QCOMPARE(overlay.selection.rect, window.rect);
    }

    void pinToolsStartHiddenAndSpaceOnlyTogglesHoveredPin()
    {
        QImage source(400, 240, QImage::Format_RGB32); source.fill(Qt::white);
        PinWindow pin(source); pin.move(40, 40); pin.show();
        auto *tools = pin.findChild<AnnotationToolbar *>();
        QVERIFY(!tools->isVisible());
        QCursor::setPos(pin.mapToGlobal(QPoint(-20, -20)));
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(!tools->isVisible());
        QCursor::setPos(pin.mapToGlobal(QPoint(100, 100)));
        QEnterEvent entered({100, 100}, {100, 100}, QCursor::pos());
        QApplication::sendEvent(&pin, &entered);
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(tools->isVisible());
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
        QApplication::sendEvent(&pin, &repeat); QVERIFY(tools->isVisible());
        QTest::keyClick(&pin, Qt::Key_Space, Qt::ShiftModifier); QVERIFY(tools->isVisible());
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(!tools->isVisible());
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(tools->isVisible());
        QTest::keyClick(&pin, Qt::Key_T);
        QTest::mouseClick(&pin, Qt::LeftButton, Qt::NoModifier, {40, 40});
        auto *editor = pin.findChild<QPlainTextEdit *>();
        QTest::keyClicks(editor, "one two");
        QCOMPARE(editor->toPlainText(), QString("one two")); QVERIFY(tools->isVisible());
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(!tools->isVisible());
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(tools->isVisible());
        pin.hide(); pin.show(); QVERIFY(!tools->isVisible());
        pin.setClickThrough(true);
        QTest::keyClick(&pin, Qt::Key_Space); QVERIFY(!tools->isVisible());
    }

    void textInputIsInlineOnPin()
    {
        QImage source(600, 240, QImage::Format_RGB32); source.fill(Qt::white);
        PinWindow pin(source); pin.show();
        QTest::keyClick(&pin, Qt::Key_T);
        // Keep this regression runnable against the old modal implementation.
        QTimer::singleShot(20, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        QTest::mouseClick(&pin, Qt::LeftButton, Qt::NoModifier, {40, 40});
        auto *editor = pin.findChild<QPlainTextEdit *>();
        QVERIFY(editor);
        QVERIFY(editor->isVisible());
        QVERIFY(!editor->isWindow());
        editor->setPlainText("Inline text");
        editor->moveCursor(QTextCursor::End);
        QTest::keyClick(editor, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClicks(editor, "Second line");
        QCOMPARE(editor->toPlainText(), QString("Inline text\nSecond line"));
        QVERIFY(editor->isVisible());
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(!editor->isVisible());
        QVERIFY(pin.exportedImage() != source);
        pin.undo(); QCOMPARE(pin.exportedImage(), source);
        pin.redo(); QVERIFY(pin.exportedImage() != source);
        QTest::mouseClick(&pin, Qt::LeftButton, Qt::NoModifier, {80, 120});
        editor->setPlainText("Canceled");
        const auto before = pin.exportedImage();
        QTest::keyClick(editor, Qt::Key_Escape);
        QCOMPARE(pin.exportedImage(), before);
        QVERIFY(pin.isVisible());
        pin.setZoom(0.5);
        QTest::mouseClick(&pin, Qt::LeftButton, Qt::NoModifier, {20, 80});
        QCOMPARE(editor->font().pixelSize(), 10);
        editor->setPlainText("Zoomed text");
        QTest::keyClick(editor, Qt::Key_Return);
        const auto zoomed = pin.exportedImage();
        QCOMPARE(zoomed.size(), source.size());
        QVERIFY(zoomed != before);
        pin.undo(); QCOMPARE(pin.exportedImage(), before);
    }

    void inlineChineseTextFitsWithoutScrollbars()
    {
        QWidget image; image.resize(1000, 320); image.show();
        InlineTextEditor editor(&image);
        editor.begin({38, 248}, image.rect(), QColor("#ff5a65"), 20);
        editor.setPlainText(QString::fromUtf8("直接在圖片上輸入"));
        QCoreApplication::processEvents();
        QCOMPARE(editor.horizontalScrollBar()->maximum(), 0);
        QVERIFY(editor.viewport()->height() >= editor.fontMetrics().height());
    }

    void completedPinTextCanBeReedited()
    {
        QImage source(800, 400, QImage::Format_RGB32); source.fill(Qt::white);
        PinWindow pin(source); pin.show();
        QTest::keyClick(&pin, Qt::Key_T);
        QTest::mouseClick(&pin, Qt::LeftButton, {}, {40, 40});
        auto *editor = pin.findChild<InlineTextEditor *>(); QVERIFY(editor);
        QSignalSpy submitted(editor, &InlineTextEditor::submitted);
        editor->setPlainText("Original text\nSecond line");
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
        const auto baseline = submitted.first()[1].toPointF();
        const QPoint editPoint = (QFontMetricsF(editor->font()).boundingRect("O").translated(baseline).center() * 0.5).toPoint();
        const QImage original = pin.exportedImage();
        QVERIFY(original != source);
        pin.setZoom(0.5);
        QTest::mouseClick(&pin, Qt::LeftButton, {}, editPoint);
        QVERIFY(editor->isVisible());
        QCOMPARE(editor->toPlainText(), QString("Original text\nSecond line"));
        QCOMPARE(editor->font().pixelSize(), 10);
        editor->setPlainText("Edited text"); QTest::keyClick(editor, Qt::Key_Return);
        const QImage edited = pin.exportedImage(); QVERIFY(edited != original);
        pin.undo(); QCOMPARE(pin.exportedImage(), original);
        pin.redo(); QCOMPARE(pin.exportedImage(), edited);
        QTest::mouseClick(&pin, Qt::LeftButton, {}, editPoint);
        QCOMPARE(editor->toPlainText(), QString("Edited text"));
        editor->setPlainText("Canceled edit"); QTest::keyClick(editor, Qt::Key_Escape);
        QCOMPARE(pin.exportedImage(), edited);
        QTest::mouseClick(&pin, Qt::LeftButton, {}, editPoint);
        editor->clear(); QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(pin.exportedImage(), source);
        pin.undo(); QCOMPARE(pin.exportedImage(), edited);
    }

    void completedCaptureTextCanBeReedited()
    {
        ScreenFrame frame{QImage(1200, 700, QImage::Format_RGB32), QRect(0, 0, 600, 350), 2};
        frame.image.fill(Qt::white);
        CaptureOverlay overlay(frame); overlay.show();
        QTest::mousePress(&overlay, Qt::LeftButton, {}, {20, 20});
        QTest::mouseMove(&overlay, {500, 250});
        QTest::mouseRelease(&overlay, Qt::LeftButton, {}, {500, 250});
        QTest::keyClick(&overlay, Qt::Key_T);
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, {60, 60});
        auto *editor = overlay.findChild<InlineTextEditor *>(); QVERIFY(editor);
        QSignalSpy submitted(editor, &InlineTextEditor::submitted);
        editor->setPlainText(QString::fromUtf8("原始文字")); QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
        const auto baseline = submitted.first()[1].toPointF();
        const QPoint editPoint = QFontMetricsF(editor->font()).boundingRect(QString::fromUtf8("原")).translated(baseline).center().toPoint();
        const QImage original = overlay.selectedImage();
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, editPoint);
        QCOMPARE(editor->toPlainText(), QString::fromUtf8("原始文字"));
        editor->setPlainText(QString::fromUtf8("重新編輯\nSecond line")); QTest::keyClick(editor, Qt::Key_Return);
        const QImage edited = overlay.selectedImage(); QVERIFY(edited != original);
        QTest::keySequence(&overlay, QKeySequence(QKeySequence::Undo)); QCOMPARE(overlay.selectedImage(), original);
        QTest::keySequence(&overlay, QKeySequence(QKeySequence::Redo)); QCOMPARE(overlay.selectedImage(), edited);
    }

    void delayedElementResultsFollowCurrentWindowAndMode()
    {
        ScreenFrame frame{QImage(800, 500, QImage::Format_RGB32), QRect(0, 0, 800, 500), 1};
        frame.image.fill(Qt::white);
        DesktopWindow left{QRectF(0, 0, 400, 500), 1, 0}, right{QRectF(400, 0, 400, 500), 2, 0};
        CaptureOverlay overlay(frame, {left, right});
        overlay.elementMode = overlay.elementAllowed = overlay.queryBusy = true;
        overlay.pointer = {120, 100}; overlay.updateDetection();
        const QRectF parent(80, 80, 200, 100), child(100, 90, 80, 40);
        // The query began at a different pixel, but the result still covers the current pointer.
        overlay.applyElementResult(left, {110, 100}, overlay.detectionGeneration, {parent, {}});
        QCOMPARE(overlay.selection.rect, parent);
        overlay.applyElementResult(left, {120, 100}, overlay.detectionGeneration, {child, {}});
        QCOMPARE(overlay.selection.rect, child); // Retaining a parent must not hide a newly found child.
        overlay.applyElementResult(left, {120, 100}, overlay.detectionGeneration, {{}, "Unavailable"});
        QCOMPARE(overlay.selection.rect, child);
        const auto oldGeneration = overlay.detectionGeneration;
        overlay.setElementMode(false); overlay.setElementMode(true); overlay.elementAllowed = true;
        overlay.applyElementResult(left, {120, 100}, oldGeneration, {child, {}});
        QCOMPARE(overlay.selection.rect, left.rect);
        overlay.pointer = {450, 100}; overlay.updateDetection();
        overlay.applyElementResult(left, {120, 100}, overlay.detectionGeneration, {child, {}});
        QCOMPARE(overlay.selection.rect, right.rect);
        // An unavailable target in another window cannot inherit an overlapping old element.
        overlay.queriedWindow = left.handle; overlay.elementRect = QRectF(300, 80, 300, 100);
        overlay.applyElementResult(right, {450, 100}, overlay.detectionGeneration, {{}, "Unavailable"});
        QCOMPARE(overlay.selection.rect, right.rect);
    }

    void inlineCaptureTextExportsAtRetinaResolution()
    {
        ScreenFrame frame{QImage(1200, 700, QImage::Format_RGB32), QRect(0, 0, 600, 350), 2};
        frame.image.fill(Qt::white);
        CaptureOverlay overlay(frame); overlay.show();
        QSignalSpy accepted(&overlay, &CaptureOverlay::accepted), canceled(&overlay, &CaptureOverlay::canceled);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QTest::mouseMove(&overlay, {500, 250});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {500, 250});
        QTest::keyClick(&overlay, Qt::Key_T);
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {60, 60});
        auto *editor = overlay.findChild<InlineTextEditor *>();
        QVERIFY(editor->isVisible()); QVERIFY(!editor->isWindow());
        auto *hint = overlay.findChild<QLabel *>("captureShortcutHint"); QVERIFY(hint);
        QVERIFY(hint->text().contains("Finish text"));
        QVERIFY(!hint->text().contains("Pin to desktop"));
        QVERIFY(overlay.selection.rect.contains(editor->geometry()));
        editor->setPlainText(QString::fromUtf8("直接輸入\nSecond line"));
        editor->moveCursor(QTextCursor::End);
        QTest::keyClick(editor, Qt::Key_Space);
        QCOMPARE(editor->toPlainText(), QString::fromUtf8("直接輸入\nSecond line "));
        QCOMPARE(accepted.size(), 0);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(accepted.size(), 0); QCOMPARE(canceled.size(), 0);
        QVERIFY(hint->text().contains("Pin to desktop"));
        const auto annotated = overlay.selectedImage();
        QCOMPARE(annotated.size(), QSize(960, 460));
        overlay.annotations.undo(); const auto blank = overlay.selectedImage();
        QVERIFY(annotated != blank);
        overlay.annotations.redo(); QCOMPARE(overlay.selectedImage(), annotated);
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {480, 230});
        QVERIFY(overlay.selection.rect.contains(editor->geometry()));
        editor->setPlainText("Cancel me"); QTest::keyClick(editor, Qt::Key_Escape);
        QCOMPARE(canceled.size(), 0); QCOMPARE(overlay.selectedImage(), annotated);
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {60, 150});
        editor->setPlainText("Commit on copy");
        auto *toolbar = overlay.findChild<AnnotationToolbar *>();
        QTest::mouseClick(toolbar->findChild<QToolButton *>("copy"), Qt::LeftButton);
        QCOMPARE(accepted.size(), 1);
        QVERIFY(qvariant_cast<QImage>(accepted[0][0]) != annotated);
        QVERIFY(!editor->isVisible());
    }

    void retinaSamplingAndCrop()
    {
        ScreenFrame frame{QImage(400, 200, QImage::Format_ARGB32), QRect(-200, 80, 200, 100), 2};
        frame.image.fill(Qt::white);
        frame.image.setPixelColor(21, 40, QColor("#123456"));
        QCOMPARE(frame.pixelAt({10.5, 20}), QPoint(21, 40));
        QCOMPARE(frame.colorAt({10.5, 20}), QColor("#123456"));
        const auto cropped = frame.crop(QRectF(10.5, 20, 25, 10));
        QCOMPARE(cropped.size(), QSize(50, 20));
        QCOMPARE(cropped.devicePixelRatio(), 2.0);
        QCOMPARE(cropped.pixelColor(0, 0), QColor("#123456"));
        QCOMPARE(frame.pixelRect(QRectF(-10, -10, 20, 20)), QRect(0, 0, 20, 20));
    }

    void selectionMoveResizeAndReverse()
    {
        Selection selection;
        const QRectF bounds(0, 0, 400, 300);
        selection.begin({200, 180}); selection.drag({50, 50}, bounds); selection.end();
        QCOMPARE(selection.rect, QRectF(50, 50, 150, 130));
        selection.begin({100, 100}); selection.drag({390, 290}, bounds); selection.end();
        QCOMPARE(selection.rect, QRectF(250, 170, 150, 130));
        selection.begin({250, 170}); selection.drag({200, 120}, bounds); selection.end();
        QCOMPARE(selection.rect, QRectF(200, 120, 200, 180));
        selection.begin({200, 200}); selection.drag({410, 200}, bounds); selection.end();
        QCOMPARE(selection.rect.width(), 0.0);
        selection.rect = QRectF(50, 50, 5, 5);
        selection.begin(selection.rect.center()); selection.drag({100, 100}, bounds); selection.end();
        QCOMPARE(selection.rect.size(), QSizeF(5, 5));
        QCOMPARE(selection.rect.center(), QPointF(100, 100));
    }

    void annotationExportUndoRedo()
    {
        QImage source(120, 80, QImage::Format_ARGB32); source.fill(Qt::white);
        PinWindow pin(source);
        Annotation arrow; arrow.tool = Tool::Arrow; arrow.start = {10, 30}; arrow.end = {100, 30}; arrow.color = Qt::red;
        pin.addAnnotation(arrow);
        QCOMPARE(pin.exportedImage().pixelColor(50, 30), QColor(Qt::red));
        QCOMPARE(source.pixelColor(50, 30), QColor(Qt::white));
        pin.undo(); QCOMPARE(pin.exportedImage(), source);
        pin.redo(); QCOMPARE(pin.exportedImage().pixelColor(50, 30), QColor(Qt::red));
        pin.undo();
        Annotation pen; pen.tool = Tool::Pen; pen.path.moveTo(20, 20); pen.path.lineTo(20, 60); pen.color = Qt::blue;
        pin.addAnnotation(pen); pin.redo();
        QCOMPARE(pin.exportedImage().pixelColor(20, 40), QColor(Qt::blue));
        QCOMPARE(pin.exportedImage().pixelColor(50, 30), QColor(Qt::white));
        pin.setZoom(2); QCOMPARE(pin.size(), QSize(240, 160));
        QCOMPARE(pin.exportedImage().size(), source.size());
    }

    void captureExportsTheSelectedSnapshot()
    {
        ScreenFrame frame{QImage(320, 240, QImage::Format_RGB32), QRect(0, 0, 320, 240), 1};
        frame.image.fill(QColor("#182838"));
        const auto *original = frame.image.constBits();
        CaptureOverlay overlay(frame);
        overlay.show();
        QSignalSpy accepted(&overlay, &CaptureOverlay::accepted);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QTest::mouseMove(&overlay, {100, 80});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 80});
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(accepted.size(), 1);
        const auto crop = qvariant_cast<QImage>(accepted[0][0]);
        QCOMPARE(crop.size(), QSize(80, 60));
        QCOMPARE(crop.pixelColor(40, 30), QColor("#182838"));
        QCOMPARE(frame.image.constBits(), original);
        QVERIFY(!accepted[0][2].toBool());
        QTest::keyClick(&overlay, Qt::Key_Space, Qt::ShiftModifier);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
        QApplication::sendEvent(&overlay, &repeat);
        QCOMPARE(accepted.size(), 1);
        QTest::keyClick(&overlay, Qt::Key_Space);
        QCOMPARE(accepted.size(), 2);
        QVERIFY(!accepted[1][2].toBool());
        QCOMPARE(qvariant_cast<QImage>(accepted[1][0]), crop);
        QTest::keySequence(&overlay, QKeySequence(QKeySequence::Copy));
        QCOMPARE(accepted.size(), 3);
        QVERIFY(accepted[2][2].toBool());
        QCOMPARE(qvariant_cast<QImage>(accepted[2][0]), crop);
    }

    void captureHintsPassThroughAndStayOutOfExports()
    {
        ScreenFrame frame{QImage(1600, 1200, QImage::Format_RGB32), QRect(0, 0, 800, 600), 2};
        frame.image.fill(QColor("#456789"));
        CaptureOverlay overlay(frame); overlay.show();
        QCoreApplication::processEvents();
        auto *hint = overlay.findChild<QLabel *>("captureShortcutHint"); QVERIFY(hint);
        QVERIFY(hint->isVisible());
        QVERIFY(hint->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(hint->y() + hint->height(), overlay.height() - 16);
        const QPoint start = hint->geometry().center(), end(650, 220);
        QVERIFY(!overlay.childAt(start));
        // Deliver through the window so a child that intercepts input would break selection.
        QTest::mousePress(overlay.windowHandle(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(overlay.windowHandle(), end);
        QTest::mouseRelease(overlay.windowHandle(), Qt::LeftButton, Qt::NoModifier, end);
        const QRectF selected = QRectF(QPointF(start), QPointF(end)).normalized();
        QCOMPARE(overlay.selection.rect, selected);
        QVERIFY(hint->geometry().intersects(selected.toAlignedRect()));
        QVERIFY(!overlay.toolbar->geometry().intersects(hint->geometry()));
        QVERIFY(!overlay.magnifierRect(end).intersects(hint->geometry()));
        QSignalSpy pinned(&overlay, &CaptureOverlay::accepted), ocr(&overlay, &CaptureOverlay::ocrRequested);
        QTest::keyClick(&overlay, Qt::Key_Space);
        QTest::keyClick(&overlay, Qt::Key_O);
        QCOMPARE(pinned.size(), 1); QCOMPARE(ocr.size(), 1);
        const QImage expected = frame.crop(selected);
        QCOMPARE(qvariant_cast<QImage>(pinned[0][0]), expected);
        QCOMPARE(qvariant_cast<QImage>(ocr[0][0]), expected);
        overlay.resize(1000, 700);
        QCoreApplication::processEvents();
        QCOMPARE(hint->y() + hint->height(), overlay.height() - 16);
        overlay.setGeometry(overlay.screen()->geometry());
        QCoreApplication::processEvents();
        const QRect available = overlay.screen()->availableGeometry().translated(-overlay.pos());
        QVERIFY(available.contains(hint->geometry()));
        QCOMPARE(hint->geometry().bottom(), available.bottom() - 16);
    }

    void toolbarAnnotationsKeepRetinaCropAndHistory()
    {
        ScreenFrame frame{QImage(1600, 1000, QImage::Format_RGB32), QRect(0, 0, 800, 500), 2};
        frame.image.fill(Qt::white);
        CaptureOverlay overlay(frame); overlay.show();
        QSignalSpy accepted(&overlay, &CaptureOverlay::accepted), ocr(&overlay, &CaptureOverlay::ocrRequested);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {50, 50});
        QTest::mouseMove(&overlay, {400, 300});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {400, 300});
        auto *toolbar = overlay.findChild<AnnotationToolbar *>();
        QVERIFY(toolbar->isVisible());
        QVERIFY(overlay.rect().contains(toolbar->geometry()));
        QTest::mouseClick(toolbar->findChild<QToolButton *>("rectangle"), Qt::LeftButton);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 80});
        QTest::mouseMove(&overlay, {160, 160});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {160, 160});
        QTest::mouseClick(toolbar->findChild<QToolButton *>("copy"), Qt::LeftButton);
        const auto crop = qvariant_cast<QImage>(accepted[0][0]);
        QCOMPARE(crop.size(), QSize(700, 500));
        QCOMPARE(crop.devicePixelRatio(), 2.0);
        QCOMPARE(crop.pixelColor(140, 60), QColor("#ff5a65"));
        QCOMPARE(crop.pixelColor(140, 140), QColor(Qt::white));
        QCOMPARE(frame.image.pixelColor(240, 160), QColor(Qt::white));
        QTest::mouseClick(toolbar->findChild<QToolButton *>("undo"), Qt::LeftButton);
        QTest::mouseClick(toolbar->findChild<QToolButton *>("copy"), Qt::LeftButton);
        QCOMPARE(qvariant_cast<QImage>(accepted[1][0]).pixelColor(140, 60), QColor(Qt::white));
        QTest::mouseClick(toolbar->findChild<QToolButton *>("redo"), Qt::LeftButton);
        QTest::mouseClick(toolbar->findChild<QToolButton *>("ocr"), Qt::LeftButton);
        QCOMPARE(qvariant_cast<QImage>(ocr[0][0]), crop);

        PinWindow pin(crop); pin.show(); pin.setZoom(0.5);
        auto *pinTools = pin.findChild<AnnotationToolbar *>();
        QTest::keyClick(&pin, Qt::Key_E);
        QTest::mouseClick(pinTools->findChild<QToolButton *>("arrow"), Qt::LeftButton);
        QTest::mousePress(&pin, Qt::LeftButton, Qt::NoModifier, {20, 50});
        QTest::mouseMove(&pin, {100, 50});
        QTest::mouseRelease(&pin, Qt::LeftButton, Qt::NoModifier, {100, 50});
        QCOMPARE(pin.exportedImage().size(), crop.size());
        QCOMPARE(pin.exportedImage().pixelColor(160, 200), QColor("#ff5a65"));
        QTest::mouseClick(pinTools->findChild<QToolButton *>("undo"), Qt::LeftButton);
        QCOMPARE(pin.exportedImage(), crop);
        pin.setClickThrough(true); QVERIFY(!pinTools->isVisible());
    }

    void physicalPixelArrowKeys()
    {
        ScreenFrame frame{QImage(640, 480, QImage::Format_RGB32), QRect(0, 0, 320, 240), 2};
        frame.image.fill(Qt::white);
        frame.image.setPixelColor(21, 40, QColor("#123456"));
        CaptureOverlay overlay(frame); overlay.show();
        QTest::mouseMove(&overlay, {10, 20});
        QTest::keyClick(&overlay, Qt::Key_Right);
        QTest::keyClick(&overlay, Qt::Key_C);
        QCOMPARE(QApplication::clipboard()->text(), QString("#123456"));
    }

    void colorCopyCompletesCaptureInDisplayedFormat()
    {
        ScreenFrame frame{QImage(640, 480, QImage::Format_RGB32), QRect(0, 0, 320, 240), 2};
        frame.image.fill(QColor("#123456"));
        CaptureOverlay overlay(frame); overlay.show();
        QSignalSpy copied(&overlay, SIGNAL(colorCopied(QString)));
        QVERIFY(copied.isValid());
        QSignalSpy accepted(&overlay, &CaptureOverlay::accepted), ocr(&overlay, &CaptureOverlay::ocrRequested);
        QTest::keyClick(&overlay, Qt::Key_C);
        QCOMPARE(copied.size(), 1);
        QCOMPARE(copied[0][0].toString(), QString("#123456"));
        QCOMPARE(QApplication::clipboard()->text(), copied[0][0].toString());
        QTest::keyClick(&overlay, Qt::Key_Shift);
        QTest::keyClick(&overlay, Qt::Key_C);
        QCOMPARE(copied.size(), 2);
        QCOMPARE(copied[1][0].toString(), QString("18, 52, 86"));
        QCOMPARE(QApplication::clipboard()->text(), copied[1][0].toString());
        QTest::keyClick(&overlay, Qt::Key_Shift);
        QTest::keyClick(&overlay, Qt::Key_C);
        QCOMPARE(copied.size(), 3);
        QCOMPARE(copied[2][0].toString(), QString("#123456"));
        QCOMPARE(accepted.size(), 0); QCOMPARE(ocr.size(), 0);
    }

    void windowDetectionAndTabKeepManualSelection()
    {
        ScreenFrame frame{QImage(640, 480, QImage::Format_RGB32), QRect(-320, 0, 320, 240), 2};
        frame.image.fill(Qt::white);
        // A scaled secondary screen with a physical origin that must not itself be divided by DPR.
        DesktopCoordinates coordinates{{-640, 100}, 2};
        QList<DesktopWindow> windows{{QRectF(-560, 140, 200, 120), 1, 0}, {QRectF(-640, 100, 640, 480), 2, 0}};
        CaptureOverlay overlay(frame, windows, coordinates); overlay.show();
        QSignalSpy accepted(&overlay, &CaptureOverlay::accepted);
        QSignalSpy modes(&overlay, &CaptureOverlay::detectionModeChanged);
        QTest::mouseMove(&overlay, {60, 40});
        QVERIFY(!overlay.isElementMode());
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {60, 40});
        QTest::mouseMove(&overlay, {250, 180}); // Locked selection must stay on the first window.
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(qvariant_cast<QImage>(accepted[0][0]).size(), QSize(200, 120));
        QCOMPARE(accepted[0][1].toPoint(), QPoint(-280, 20));
        QTest::keyClick(&overlay, Qt::Key_Tab);
        QVERIFY(overlay.isElementMode());
        QTest::keyClick(&overlay, Qt::Key_Tab);
        QVERIFY(!overlay.isElementMode());
        QCOMPARE(modes.size(), 2);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QTest::mouseMove(&overlay, {100, 80});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 80});
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(qvariant_cast<QImage>(accepted[1][0]).size(), QSize(160, 120));
    }

    void ocrUsesSelectionAndOriginalPinResolution()
    {
        ScreenFrame frame{QImage(640, 480, QImage::Format_RGB32), QRect(0, 0, 320, 240), 2};
        frame.image.fill(Qt::white);
        CaptureOverlay overlay(frame); overlay.show();
        QSignalSpy selected(&overlay, &CaptureOverlay::ocrRequested);
        QTest::keyClick(&overlay, Qt::Key_O);
        QCOMPARE(selected.size(), 0);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {20, 20});
        QTest::mouseMove(&overlay, {100, 80});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 80});
        QTest::keyClick(&overlay, Qt::Key_O);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(), QSize(160, 120));
        PinWindow pin(frame.image); pin.setZoom(0.5); pin.show();
        Annotation text; text.tool = Tool::Text; text.start = {40, 80}; text.text = "OCR export";
        pin.addAnnotation(text);
        QSignalSpy pinned(&pin, &PinWindow::ocrRequested);
        QTest::keyClick(&pin, Qt::Key_O);
        QCOMPARE(pinned.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(pinned[0][0]), pin.exportedImage());
        QCOMPARE(qvariant_cast<QImage>(pinned[0][0]).size(), QSize(640, 480));
    }

    void quickTextCaptureCompletesOnReleaseWithoutPinOrToolbar_data()
    {
        QTest::addColumn<bool>("translate");
        QTest::newRow("copy-text") << false;
        QTest::newRow("dictionary") << true;
    }

    void quickTextCaptureCompletesOnReleaseWithoutPinOrToolbar()
    {
        QFETCH(bool, translate);
        ScreenFrame frame{QImage(640, 480, QImage::Format_RGB32), QRect(0, 0, 320, 240), 2};
        frame.image.fill(Qt::white);
        CaptureOverlay overlay(frame); overlay.setMode(translate ? CaptureOverlay::Mode::Translate : CaptureOverlay::Mode::CopyText); overlay.show();
        auto *hint = overlay.findChild<QLabel *>("captureShortcutHint"); QVERIFY(hint);
        QVERIFY(hint->text().contains(translate ? "Dictionary / translate" : "Copy text (OCR)"));
        QVERIFY(!hint->text().contains("Pin to desktop"));
        QSignalSpy selected(&overlay, translate ? &CaptureOverlay::translateRequested : &CaptureOverlay::ocrRequested), pinned(&overlay, &CaptureOverlay::accepted);
        QSignalSpy other(&overlay, translate ? &CaptureOverlay::ocrRequested : &CaptureOverlay::translateRequested);
        QSignalSpy canceled(&overlay, &CaptureOverlay::canceled);
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, {20, 20});
        QCOMPARE(selected.size(), 0);
        QTest::mousePress(&overlay, Qt::LeftButton, {}, {20, 20});
        QTest::mouseMove(&overlay, {100, 80});
        QCOMPARE(selected.size(), 0);
        QTest::mouseRelease(&overlay, Qt::LeftButton, {}, {100, 80});
        QCOMPARE(selected.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(), QSize(160, 120));
        QVERIFY(!overlay.toolbar->isVisible());
        QCOMPARE(pinned.size(), 0);
        QCOMPARE(other.size(), 0);
        QTest::keyClick(&overlay, Qt::Key_Escape);
        QCOMPARE(canceled.size(), 1);
    }

    void quickOcrAcceptsDetectedWindow()
    {
        ScreenFrame frame{QImage(320, 240, QImage::Format_RGB32), QRect(0, 0, 320, 240), 1};
        frame.image.fill(Qt::white);
        CaptureOverlay overlay(frame, {{QRectF(30, 40, 120, 90), 1, 0}});
        overlay.setMode(CaptureOverlay::Mode::CopyText); overlay.show();
        QSignalSpy selected(&overlay, &CaptureOverlay::ocrRequested);
        QTest::mouseClick(&overlay, Qt::LeftButton, {}, {80, 80});
        QCOMPARE(selected.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(), QSize(120, 90));
    }

#ifdef Q_OS_MACOS
    void visionRecognizesEnglishAndChinese()
    {
        QImage image(1000, 320, QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont("Helvetica", 44));
        painter.drawText(40, 90, "FastPin OCR 12345");
        painter.setFont(QFont("PingFang TC", 44));
        painter.drawText(40, 210, QString::fromUtf8("繁體中文識別"));
        painter.end();
        image.save("ocr-fixture.png");
        const auto result = std::async(std::launch::async, [image] { return recognizeTextInWorker(image, QString::fromUtf8(FASTPIN_APPLICATION)); }).get();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY2(result.text.contains("FastPin OCR 12345"), qPrintable(result.text));
        QVERIFY2(result.text.contains(QString::fromUtf8("繁體中文識別")), qPrintable(result.text));
        image.fill(Qt::white);
        const auto blank = std::async(std::launch::async, [image] { return recognizeText(image); }).get();
        QVERIFY2(blank.error.isEmpty(), qPrintable(blank.error));
        QVERIFY(blank.text.isEmpty());
    }
#endif

    void idleWidgetsDoNotScheduleRepaints()
    {
        struct CountPin : PinWindow {
            using PinWindow::PinWindow;
            int paints = 0;
            void paintEvent(QPaintEvent *event) override { ++paints; PinWindow::paintEvent(event); }
        };
        QImage image(200, 100, QImage::Format_RGB32); image.fill(Qt::white);
        CountPin pin(image); pin.show();
        QTest::qWait(100);
        const int before = pin.paints;
        QTest::qWait(250);
        QCOMPARE(pin.paints, before);
    }
};

QTEST_MAIN(NativeTests)
#include "tests.moc"
