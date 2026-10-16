#include "settings_mac.h"
#include "hotkey.h"
#include "history.h"
#include "doubletap.h"

#include <QCoreApplication>
#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

static NSString *text(const QString &value) { return value.toNSString(); }

static Qt::KeyboardModifiers modifiers(NSEventModifierFlags flags)
{
    Qt::KeyboardModifiers result;
    if (flags & NSEventModifierFlagControl) result |= Qt::MetaModifier;
    if (flags & NSEventModifierFlagCommand) result |= Qt::ControlModifier;
    if (flags & NSEventModifierFlagOption) result |= Qt::AltModifier;
    if (flags & NSEventModifierFlagShift) result |= Qt::ShiftModifier;
    return result;
}

@interface FastPinShortcutButton : NSButton {
@public
    Shortcut binding;
    std::function<void(bool)> recordingChanged;
    bool recording;
    Qt::MouseButtons held, seen;
    Qt::KeyboardModifiers mouseModifiers;
    id mouseMonitor;
}
- (void)finishRecording;
- (void)refreshTitle;
@end

@implementation FastPinShortcutButton
- (BOOL)acceptsFirstResponder { return YES; }
- (void)refreshTitle
{
    self.title = recording ? @"Press shortcut…" : (binding.isEmpty() ? @"None" : text(binding.toString()));
    self.accessibilityValue = self.title;
}
- (void)record:(id)sender
{
    if (recording) { [self.window makeFirstResponder:nil]; return; }
    [self.window makeFirstResponder:self];
    recording = true;
    recordingChanged(true);
    [self refreshTitle];
    __weak FastPinShortcutButton *weakSelf = self;
    mouseMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp
        handler:^NSEvent *(NSEvent *event) {
            FastPinShortcutButton *button = weakSelf;
            if (!button || !button->recording || (event.buttonNumber != 3 && event.buttonNumber != 4)) return event;
            const auto side = event.buttonNumber == 3 ? Qt::BackButton : Qt::ForwardButton;
            if (event.type == NSEventTypeOtherMouseDown) {
                if (!button->held) { button->seen = {}; button->mouseModifiers = modifiers(event.modifierFlags); }
                button->held |= side; button->seen |= side;
                button.title = text(Shortcut(button->seen, button->mouseModifiers).toString());
            } else if (button->held & side) {
                button->held &= ~Qt::MouseButtons(side);
                if (!button->held) {
                    button->binding = Shortcut(button->seen, button->mouseModifiers);
                    [button.window makeFirstResponder:nil];
                }
            }
            return nil;
        }];
}
- (void)finishRecording
{
    if (!recording) return;
    recording = false; held = {}; seen = {};
    [NSEvent removeMonitor:mouseMonitor]; mouseMonitor = nil;
    recordingChanged(false);
    [self refreshTitle];
}
- (BOOL)resignFirstResponder { [self finishRecording]; return [super resignFirstResponder]; }
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    if (!recording) return [super performKeyEquivalent:event];
    [self keyDown:event]; return YES;
}
- (void)keyDown:(NSEvent *)event
{
    if (!recording) { [super keyDown:event]; return; }
    if (event.isARepeat) return;
    if (event.keyCode == 53 || event.keyCode == 36 || event.keyCode == 76) {
        [self.window makeFirstResponder:nil]; return;
    }
    if (event.keyCode == 48) { [self.window selectNextKeyView:self]; return; }
    if ((event.keyCode == 51 || event.keyCode == 117) && !modifiers(event.modifierFlags)) binding = {};
    else binding = Shortcut::fromNativeKey(event.keyCode, modifiers(event.modifierFlags));
    [self.window makeFirstResponder:nil];
}
@end

static NSTextField *label(NSString *value, BOOL secondary = NO)
{
    NSTextField *field = [NSTextField wrappingLabelWithString:value];
    if (secondary) { field.textColor = NSColor.secondaryLabelColor; field.font = [NSFont systemFontOfSize:12]; }
    return field;
}

static NSStackView *stack(NSArray<NSView *> *views, NSUserInterfaceLayoutOrientation orientation = NSUserInterfaceLayoutOrientationVertical)
{
    NSStackView *result = [NSStackView stackViewWithViews:views];
    result.orientation = orientation; result.alignment = orientation == NSUserInterfaceLayoutOrientationVertical ? NSLayoutAttributeLeading : NSLayoutAttributeCenterY;
    result.spacing = 14;
    return result;
}

static NSViewController *page(NSString *title, NSString *symbol, NSStackView *content)
{
    NSViewController *controller = [[NSViewController alloc] init];
    controller.title = title;
    controller.preferredContentSize = NSMakeSize(620, 430);
    controller.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 620, 430)];
    content.translatesAutoresizingMaskIntoConstraints = NO;
    [controller.view addSubview:content];
    [NSLayoutConstraint activateConstraints:@[
        [content.leadingAnchor constraintEqualToAnchor:controller.view.leadingAnchor constant:40],
        [content.trailingAnchor constraintEqualToAnchor:controller.view.trailingAnchor constant:-40],
        [content.topAnchor constraintEqualToAnchor:controller.view.topAnchor constant:30]
    ]];
    controller.representedObject = symbol;
    return controller;
}

@interface FastPinSettingsTabs : NSTabViewController {
@public
    std::function<void()> selectionChanged;
}
@end
@implementation FastPinSettingsTabs
- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)item
{
    if (selectionChanged) selectionChanged();
    [super tabView:tabView didSelectTabViewItem:item];
}
@end

@interface FastPinSettingsController : NSWindowController <NSWindowDelegate, SPUUpdaterDelegate> {
@public
    Hotkeys *hotkeys;
    QSettings *settings;
    CaptureHistory *history;
    std::function<void()> shortcutsChanged, historyChanged;
    std::function<void(bool)> doubleControlChanged;
    NSArray<FastPinShortcutButton *> *shortcutButtons;
    NSTextField *feedback, *updateStatus;
    NSButton *remember, *automaticChecks, *automaticDownloads, *updateButton;
    NSTabViewController *tabs;
    SPUStandardUpdaterController *updates;
    NSString *feedbackText, *updateText;
    void (^installUpdate)(void);
}
- (void)finishRecording;
- (void)syncUpdates;
- (void)startUpdater;
- (void)checkForUpdates:(id)sender;
- (void)quit:(id)sender;
@end

@implementation FastPinSettingsController
- (void)startUpdater
{
    if ([NSBundle.mainBundle objectForInfoDictionaryKey:@"SUFeedURL"]) {
        updates = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO updaterDelegate:self userDriverDelegate:nil];
        [updates.updater addObserver:self forKeyPath:@"canCheckForUpdates" options:0 context:nullptr];
        [updates startUpdater];
    } else updateText = @"Automatic updates are available in packaged FastPin builds.";
}
- (void)loadWindow
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 620, 430)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    window.title = @"FastPin Settings"; window.delegate = self; window.releasedWhenClosed = NO;
    window.toolbarStyle = NSWindowToolbarStylePreference;
    self.window = window;
    FastPinSettingsTabs *nativeTabs = [[FastPinSettingsTabs alloc] init];
    __weak FastPinSettingsController *weakSelf = self;
    nativeTabs->selectionChanged = [weakSelf] { [weakSelf finishRecording]; };
    tabs = nativeTabs;
    tabs.tabStyle = NSTabViewControllerTabStyleToolbar;

    remember = [NSButton checkboxWithTitle:@"Remember the last image after closing or restarting"
        target:self action:@selector(rememberChanged:)];
    remember.state = history->enabled ? NSControlStateValueOn : NSControlStateValueOff;
    NSButton *clear = [NSButton buttonWithTitle:@"Clear Saved Image" target:self action:@selector(clearHistory:)];
    NSStackView *general = stack(@[label(@"Image Recovery"), remember,
        label(@"One image is stored on this Mac, including its annotations. Turning this off also deletes the saved image.", YES), clear]);
    [tabs addChildViewController:page(@"Settings", @"slider.horizontal.3", general)];

    NSMutableArray *buttons = [NSMutableArray array];
    NSStackView *shortcutRows = stack(@[]);
    for (int i = 0; i < hotkeyActionCount; ++i) {
        NSString *actionName = text(hotkeyActionName(i));
        FastPinShortcutButton *button = [[FastPinShortcutButton alloc] init];
        button.bezelStyle = NSBezelStyleRounded; button.target = button; button.action = @selector(record:);
        button->binding = hotkeys->bindings()[i];
        __weak FastPinSettingsController *weakSelf = self;
        button->recordingChanged = [weakSelf](bool recording) {
            FastPinSettingsController *owner = weakSelf;
            if (!owner) return;
            if (recording) owner->hotkeys->suspend();
            else {
                const auto error = owner->hotkeys->apply(owner->hotkeys->bindings());
                if (!error.isEmpty()) { owner->feedbackText = text(error); owner->feedback.stringValue = owner->feedbackText; }
            }
        };
        [button refreshTitle]; button.accessibilityLabel = [actionName stringByAppendingString:@" shortcut"];
        [button.widthAnchor constraintEqualToConstant:190].active = YES;
        NSTextField *name = label(actionName); [name.widthAnchor constraintEqualToConstant:240].active = YES;
        [shortcutRows addArrangedSubview:stack(@[name, button], NSUserInterfaceLayoutOrientationHorizontal)];
        [buttons addObject:button];
    }
    shortcutButtons = buttons;
    NSButton *defaults = [NSButton buttonWithTitle:@"Reset to Defaults" target:self action:@selector(resetShortcuts:)];
    NSButton *apply = [NSButton buttonWithTitle:@"Apply Shortcuts" target:self action:@selector(applyShortcuts:)];
    apply.keyEquivalent = @"\r";
    feedback = label(feedbackText ?: @"", YES);
    NSButton *doubleControl = [NSButton checkboxWithTitle:@"Double-tap Control to translate" target:self action:@selector(doubleControlToggled:)];
    doubleControl.state = settings->value(doubleControlSetting, true).toBool() ? NSControlStateValueOn : NSControlStateValueOff;
    doubleControl.toolTip = @"Tap Control twice quickly to start Dictionary / translate. Needs Input Monitoring permission.";
    [tabs addChildViewController:page(@"Shortcuts", @"keyboard", stack(@[
        label(@"Global Shortcuts"), shortcutRows, doubleControl,
        label(@"Click a shortcut, then press keys or Mouse4 / Mouse5. Hold both side buttons for a combo. Delete clears a shortcut; Escape ends recording. Use Fn for function keys when needed.", YES),
        stack(@[defaults, apply], NSUserInterfaceLayoutOrientationHorizontal), feedback]))];

    NSImageView *icon = [NSImageView imageViewWithImage:NSApp.applicationIconImage];
    [icon.widthAnchor constraintEqualToConstant:100].active = YES; [icon.heightAnchor constraintEqualToConstant:100].active = YES;
    NSTextField *name = label(@"FastPin for macOS"); name.font = [NSFont boldSystemFontOfSize:20];
    NSButton *source = [NSButton buttonWithTitle:@"GitHub" target:self action:@selector(openLink:)]; source.identifier = @"https://github.com/TempestShaw/FastPin";
    NSButton *issue = [NSButton buttonWithTitle:@"Report an Issue…" target:self action:@selector(openLink:)]; issue.identifier = @"https://github.com/TempestShaw/FastPin/issues";
    NSButton *credits = [NSButton buttonWithTitle:@"Acknowledgements" target:self action:@selector(openLink:)]; credits.identifier = @"https://github.com/TempestShaw/FastPin/blob/main/NOTICE.md";
    NSStackView *identity = stack(@[icon, stack(@[name,
        label([@"Version " stringByAppendingString:text(QCoreApplication::applicationVersion())], YES),
        stack(@[source, issue], NSUserInterfaceLayoutOrientationHorizontal), credits])], NSUserInterfaceLayoutOrientationHorizontal);
    NSBox *separator = [[NSBox alloc] init]; separator.boxType = NSBoxSeparator;
    [separator.widthAnchor constraintEqualToConstant:540].active = YES;
    automaticChecks = [NSButton checkboxWithTitle:@"Automatically check for updates" target:self action:@selector(updatePreferences:)];
    automaticDownloads = [NSButton checkboxWithTitle:@"Download updates automatically" target:self action:@selector(updatePreferences:)];
    updateButton = [NSButton buttonWithTitle:@"Check for Updates…" target:self action:@selector(checkForUpdates:)];
    NSButton *notes = [NSButton buttonWithTitle:@"Release Notes" target:self action:@selector(openLink:)]; notes.identifier = @"https://github.com/TempestShaw/FastPin/releases";
    updateStatus = label(@"", YES);
    [tabs addChildViewController:page(@"About", @"info.circle", stack(@[identity,
        label(@"Capture ideas. Keep them in view.\n© 2026 FastPin contributors", YES), separator,
        automaticChecks, automaticDownloads, stack(@[updateButton, notes], NSUserInterfaceLayoutOrientationHorizontal), updateStatus]))];
    for (NSTabViewItem *item in tabs.tabViewItems)
        item.image = [NSImage imageWithSystemSymbolName:item.viewController.representedObject accessibilityDescription:item.label];
    window.contentViewController = tabs;
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(finishRecording)
        name:NSWindowDidResignKeyNotification object:window];
    [window center];
    [self syncUpdates];
}
- (void)finishRecording
{
    for (FastPinShortcutButton *button in shortcutButtons) [button finishRecording];
    if (self.isWindowLoaded) [self.window makeFirstResponder:nil];
}
- (BOOL)windowShouldClose:(NSWindow *)sender { [self finishRecording]; return YES; }
- (void)rememberChanged:(NSButton *)sender
{
    const bool enabled = sender.state == NSControlStateValueOn;
    history->enabled = enabled; settings->setValue("recovery/enabled", enabled); settings->sync();
    if (!enabled) history->clear();
    historyChanged();
}
- (void)clearHistory:(id)sender { history->clear(); historyChanged(); }
- (void)doubleControlToggled:(NSButton *)sender
{
    const bool enabled = sender.state == NSControlStateValueOn;
    settings->setValue(doubleControlSetting, enabled); settings->sync();
    if (doubleControlChanged) doubleControlChanged(enabled);
}
- (void)resetShortcuts:(id)sender
{
    [self finishRecording];
    const auto defaults = Hotkeys::defaults();
    for (int i = 0; i < hotkeyActionCount; ++i) { shortcutButtons[i]->binding = defaults[i]; [shortcutButtons[i] refreshTitle]; }
    feedback.stringValue = @"Defaults selected. Apply to activate them.";
}
- (void)applyShortcuts:(id)sender
{
    [self finishRecording];
    Hotkeys::Bindings bindings;
    for (int i = 0; i < hotkeyActionCount; ++i) bindings[i] = shortcutButtons[i]->binding;
    const auto error = hotkeys->apply(bindings);
    if (!error.isEmpty()) { feedback.stringValue = text(error); return; }
    Hotkeys::save(*settings, bindings); shortcutsChanged();
    feedback.stringValue = settings->status() == QSettings::NoError ? @"Saved. Your shortcuts are active." :
        @"Active for this session, but settings could not be saved to disk.";
}
- (void)openLink:(NSButton *)sender { [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:sender.identifier]]; }
- (void)syncUpdates
{
    automaticChecks.state = updates.updater.automaticallyChecksForUpdates ? NSControlStateValueOn : NSControlStateValueOff;
    automaticChecks.enabled = updates != nil;
    automaticDownloads.state = updates.updater.automaticallyDownloadsUpdates ? NSControlStateValueOn : NSControlStateValueOff;
    automaticDownloads.enabled = updates && updates.updater.automaticallyChecksForUpdates;
    updateButton.title = installUpdate ? @"Update and Restart" : @"Check for Updates…";
    updateButton.enabled = installUpdate || updates.updater.canCheckForUpdates;
    updateStatus.stringValue = updateText ?: @"Updates are verified before installation. Your settings and saved image are kept.";
}
- (void)updatePreferences:(NSButton *)sender
{
    updates.updater.automaticallyChecksForUpdates = automaticChecks.state == NSControlStateValueOn;
    updates.updater.automaticallyDownloadsUpdates = automaticDownloads.state == NSControlStateValueOn;
    [self syncUpdates];
}
- (void)checkForUpdates:(id)sender
{
    if (installUpdate) installUpdate();
    else if (updates.updater.canCheckForUpdates) [updates checkForUpdates:sender];
}
- (void)quit:(id)sender { [NSApp terminate:sender]; }
- (void)observeValueForKeyPath:(NSString *)path ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
    [self syncUpdates];
}
- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item
{
    updateText = [NSString stringWithFormat:@"FastPin %@ is available.", item.displayVersionString]; [self syncUpdates];
}
- (BOOL)updater:(SPUUpdater *)updater willInstallUpdateOnQuit:(SUAppcastItem *)item immediateInstallationBlock:(void (^)(void))handler
{
    installUpdate = [handler copy];
    updateText = [NSString stringWithFormat:@"FastPin %@ is ready. Restart now, or it will install when FastPin quits.", item.displayVersionString];
    [self syncUpdates]; return YES;
}
- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error
{
    installUpdate = nil; updateText = error.localizedDescription; [self syncUpdates];
}
- (void)updater:(SPUUpdater *)updater didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check error:(NSError *)error
{
    if (error.code == SUNoUpdateError) updateText = @"You’re up to date.";
    [self syncUpdates];
}
- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [updates.updater removeObserver:self forKeyPath:@"canCheckForUpdates"];
}
@end

struct MacSettings::Impl { FastPinSettingsController *controller; };

MacSettings::MacSettings(Hotkeys &hotkeys, QSettings &settings, CaptureHistory &history,
                         std::function<void()> shortcutsChanged, std::function<void()> historyChanged,
                         std::function<void(bool)> doubleControlChanged)
    : impl(std::make_unique<Impl>())
{
    auto *controller = [[FastPinSettingsController alloc] init];
    impl->controller = controller;
    controller->hotkeys = &hotkeys; controller->settings = &settings; controller->history = &history;
    controller->shortcutsChanged = std::move(shortcutsChanged); controller->historyChanged = std::move(historyChanged);
    controller->doubleControlChanged = std::move(doubleControlChanged);
    [controller startUpdater];
}

MacSettings::~MacSettings()
{
    auto *controller = impl->controller;
    if (controller.isWindowLoaded) { [controller finishRecording]; [controller.window orderOut:nil]; }
}

void MacSettings::show(Page page)
{
    auto *controller = impl->controller;
    if (!controller.window) [controller loadWindow];
    [controller finishRecording];
    controller->tabs.selectedTabViewItemIndex = page;
    [controller syncUpdates]; [controller showWindow:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void MacSettings::hide()
{
    auto *controller = impl->controller;
    if (controller.isWindowLoaded) { [controller finishRecording]; [controller.window orderOut:nil]; }
}

void MacSettings::checkForUpdates() { show(About); [impl->controller checkForUpdates:nil]; }
void MacSettings::quit() { [impl->controller quit:nil]; }
void MacSettings::setFeedback(const QString &message)
{
    impl->controller->feedbackText = text(message);
    impl->controller->feedback.stringValue = text(message);
}
