#include "settings_mac.h"
#include "hotkey.h"
#include "history.h"
#include "translationcard.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <memory>
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>

static NSButton *buttonWithLabel(NSView *view, NSString *title)
{
    if ([view isKindOfClass:NSButton.class]) {
        NSButton *button = (NSButton *)view;
        if ([button.title isEqualToString:title] || [button.accessibilityLabel isEqualToString:title]) return button;
    }
    for (NSView *child in view.subviews) if (auto *button = buttonWithLabel(child, title)) return button;
    return nil;
}

static NSEvent *key(unsigned short code, NSEventModifierFlags flags = 0)
{
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:flags
        timestamp:0 windowNumber:0 context:nil characters:@"s" charactersIgnoringModifiers:@"s" isARepeat:NO keyCode:code];
}

class MacSettingsTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    Hotkeys::Bindings bindings;
    bool registered()
    {
        Hotkeys probe;
        return !probe.apply(bindings).isEmpty();
    }
private slots:
    void systemVoiceProducesSpeechAudio()
    {
        auto *speech = [[AVSpeechSynthesizer alloc] init];
        auto *utterance = [AVSpeechUtterance speechUtteranceWithString:@"bank"];
        utterance.voice = [AVSpeechSynthesisVoice voiceWithLanguage:@"en-US"];
        QVERIFY(utterance.voice);
        auto audible = std::make_shared<std::atomic_bool>(false);
        [speech writeUtterance:utterance toBufferCallback:^(AVAudioBuffer *buffer) {
            if (![buffer isKindOfClass:AVAudioPCMBuffer.class]) return;
            auto *pcm = (AVAudioPCMBuffer *)buffer;
            for (AVAudioFrameCount i = 0; i < pcm.frameLength; ++i)
                if ((pcm.floatChannelData && std::abs(pcm.floatChannelData[0][i]) > 0.0001f) ||
                    (pcm.int16ChannelData && std::abs(int(pcm.int16ChannelData[0][i])) > 3)) {
                    audible->store(true); break;
                }
        }];
        QTRY_VERIFY_WITH_TIMEOUT(audible->load(), 10000);
        [speech stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
    }

    void translationCardUsesNonactivatingNativePanel()
    {
        TranslationCard card("bank"); card.showAt({40, 60});
        NSView *view = (__bridge NSView *)reinterpret_cast<void *>(card.winId());
        QVERIFY([view.window isKindOfClass:NSPanel.class]);
        QVERIFY(view.window.styleMask & NSWindowStyleMaskNonactivatingPanel);
        QVERIFY(!view.window.canBecomeKeyWindow);
        NSVisualEffectView *glass = nil;
        for (NSView *sibling in view.superview.subviews)
            if ([sibling isKindOfClass:NSVisualEffectView.class]) glass = (NSVisualEffectView *)sibling;
        QVERIFY(glass);
        QCOMPARE(glass.alphaValue, CGFloat(1)); // Lower opacity disables AppKit's backdrop blur.
        // Qt's tint must leave the native backdrop visible through the card.
        QVERIFY(card.grab().toImage().pixelColor(6, card.height() / 2).alpha() < 255);
    }

    void initTestCase()
    {
        QCoreApplication::setOrganizationName("FastPinMacSettingsTests");
        QCoreApplication::setApplicationName("FastPinMacSettingsTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        for (int i = 0; i < hotkeyActionCount; ++i) bindings[i] = QKeySequence(QKeyCombination(Qt::MetaModifier | Qt::AltModifier | Qt::ShiftModifier, Qt::Key(i < 4 ? Qt::Key_F17 + i : Qt::Key_F16)));
    }

    void nativePagesRecordingAndPersistence()
    {
        Hotkeys hotkeys;
        QVERIFY2(hotkeys.apply(bindings).isEmpty(), "Test shortcuts must be available.");
        QSettings settings;
        Hotkeys::save(settings, bindings);
        settings.setValue("unrelated", "preserved");
        CaptureHistory history(directory.filePath("last-image.png"));
        int changed = 0;
        MacSettings preferences(hotkeys, settings, history, [&] { ++changed; }, [] {});
        preferences.show(MacSettings::Shortcuts);
        NSWindow *window = nil;
        for (NSWindow *candidate in NSApp.windows) if ([candidate.title isEqualToString:@"FastPin Settings"]) window = candidate;
        QVERIFY(window);
        auto *tabs = (NSTabViewController *)window.contentViewController;
        QCOMPARE(tabs.tabViewItems.count, NSUInteger(3));
        QCOMPARE(tabs.tabStyle, NSTabViewControllerTabStyleToolbar);
        QVERIFY(window.toolbar);
        auto *record = buttonWithLabel(window.contentView, @"Capture screenshot shortcut");
        auto *apply = buttonWithLabel(window.contentView, @"Apply Shortcuts");
        QVERIFY(record && apply);
        QVERIFY(buttonWithLabel(window.contentView, @"Dictionary / translate shortcut"));
        QVERIFY(registered());
        [record performClick:nil];
        QVERIFY(!registered());
        // A rejected binding must leave the previously registered shortcuts intact.
        [record keyDown:key(1)];
        QVERIFY(registered());
        [apply performClick:nil];
        QCOMPARE(hotkeys.bindings(), bindings);
        QCOMPARE(changed, 0);
        // Physical Control maps to Qt Meta, including function keys and key code 0 (A).
        QCOMPARE(Shortcut::fromNativeKey(0, Qt::MetaModifier), Shortcut(QKeySequence(QKeyCombination(Qt::MetaModifier, Qt::Key_A))));
        [record performClick:nil];
        [record keyDown:key(64, NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagShift)]; // F17
        [apply performClick:nil];
        QCOMPARE(changed, 1);
        QCOMPARE(Hotkeys::load(settings), bindings);
        QCOMPARE(settings.value("unrelated").toString(), QString("preserved"));
        [record performClick:nil];
        QVERIFY(!registered());
        [tabs.tabView selectTabViewItemAtIndex:MacSettings::About];
        QVERIFY(registered());
        auto *check = buttonWithLabel(window.contentView, @"Check for Updates…");
        QVERIFY(check);
        QVERIFY(!check.enabled); // A test executable is not a signed, installable app bundle.
        preferences.show(MacSettings::Shortcuts);
        [record performClick:nil];
        preferences.hide();
        QVERIFY(registered());
        preferences.show(MacSettings::Shortcuts);
        [record performClick:nil];
        QVERIFY(!registered());
        [NSNotificationCenter.defaultCenter postNotificationName:NSWindowDidResignKeyNotification object:window];
        QVERIFY(registered());
        preferences.show(MacSettings::Shortcuts);
        [record performClick:nil];
        [window performClose:nil];
        QVERIFY(registered());
    }

    void recoveryUsesExistingSettings()
    {
        Hotkeys hotkeys; QVERIFY(hotkeys.apply(bindings).isEmpty());
        QSettings settings;
        CaptureHistory history(directory.filePath("recovery-image.png"));
        int changed = 0;
        MacSettings preferences(hotkeys, settings, history, [] {}, [&] { ++changed; });
        preferences.show();
        NSWindow *window = nil;
        for (NSWindow *candidate in NSApp.windows) if ([candidate.title isEqualToString:@"FastPin Settings"] && candidate.visible) window = candidate;
        QVERIFY(window);
        auto *remember = buttonWithLabel(window.contentView, @"Remember the last image after closing or restarting");
        QVERIFY(remember);
        [remember performClick:nil];
        QVERIFY(!history.enabled);
        QCOMPARE(settings.value("recovery/enabled").toBool(), false);
        QCOMPARE(changed, 1);
        [remember performClick:nil];
        QVERIFY(history.enabled);
        QCOMPARE(settings.value("recovery/enabled").toBool(), true);
    }
};
QTEST_MAIN(MacSettingsTests)
#include "mac_settings_tests.moc"
