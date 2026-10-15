#include "hotkey.h"
#include <QApplication>
#include <QKeyEvent>
#include <utility>

static int nextHotkeyBase = 0x5a00;
static QList<int> availableHotkeyBases;

#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
struct Hotkeys::Platform {
    EventHandlerRef handler = nullptr;
    std::array<EventHotKeyRef, 6> keys{};
    int base;
    CFMachPortRef mouseTap = nullptr;
    CFRunLoopSourceRef mouseSource = nullptr;
};
static int nativeKey(Qt::Key key)
{
    static const int letters[] = {kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F,
        kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
        kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U, kVK_ANSI_V,
        kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z};
    static const int digits[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
        kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
    static const int function[] = {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8,
        kVK_F9, kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return letters[key - Qt::Key_A];
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return digits[key - Qt::Key_0];
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) return function[key - Qt::Key_F1];
    if (key == Qt::Key_Space) return kVK_Space;
    return -1;
}
#elif defined(Q_OS_WIN)
#include <windows.h>
struct Hotkeys::Platform {
    int base;
    HHOOK mouseHook = nullptr;
    inline static Hotkeys *mouseOwner = nullptr;
    static LRESULT CALLBACK mouseCallback(int code, WPARAM message, LPARAM data);
};
static int nativeKey(Qt::Key key)
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) return key;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) return VK_F1 + key - Qt::Key_F1;
    return key == Qt::Key_Space ? VK_SPACE : -1;
}
#else
struct Hotkeys::Platform { int base; };
static int nativeKey(Qt::Key) { return -1; }
#endif

Shortcut Shortcut::fromKeyEvent(const QKeyEvent *event)
{
    auto key = Qt::Key(event->key());
    // Qt reports shifted digits as punctuation (e.g. Shift+2 is Key_At on macOS).
    if ((event->modifiers() & Qt::ShiftModifier) && event->nativeVirtualKey())
        for (int digit = Qt::Key_0; digit <= Qt::Key_9; ++digit)
            if (nativeKey(Qt::Key(digit)) == int(event->nativeVirtualKey())) { key = Qt::Key(digit); break; }
    return QKeySequence(QKeyCombination(event->modifiers(), key));
}

Shortcut Shortcut::fromNativeKey(quint32 virtualKey, Qt::KeyboardModifiers modifiers)
{
    for (const auto range : {std::pair{Qt::Key_A, Qt::Key_Z}, std::pair{Qt::Key_0, Qt::Key_9},
                             std::pair{Qt::Key_F1, Qt::Key_F20}, std::pair{Qt::Key_Space, Qt::Key_Space}})
        for (int key = range.first; key <= range.second; ++key)
            if (nativeKey(Qt::Key(key)) == int(virtualKey)) return QKeySequence(QKeyCombination(modifiers, Qt::Key(key)));
    return QKeySequence(Qt::Key_unknown);
}

Hotkeys::Bindings Hotkeys::defaults()
{
#ifdef Q_OS_MACOS
    const auto control = Qt::META; // Qt Meta is physical Control; Qt Control is Command.
#else
    const auto control = Qt::CTRL;
#endif
    return {QKeySequence(control | Qt::SHIFT | Qt::Key_2), QKeySequence(control | Qt::SHIFT | Qt::Key_3),
            QKeySequence(control | Qt::SHIFT | Qt::Key_4), QKeySequence(control | Qt::SHIFT | Qt::Key_5),
            QKeySequence(control | Qt::Key_D)};
}

Hotkeys::Bindings Hotkeys::load(QSettings &settings)
{
    auto result = defaults();
    for (int i = 0; i < 5; ++i)
        result[i] = Shortcut::fromString(settings.value(QString("shortcuts/%1").arg(i), result[i].toString(QKeySequence::PortableText)).toString());
    // Move the old Ctrl+D paste shortcut to make room for Dictionary.
    if (!settings.contains("shortcuts/4") && result[1] == result[4]) {
        result[1] = defaults()[1];
        for (int i : {0, 2, 3}) if (result[i] == result[1]) { result[1] = {}; break; }
    }
    // A new default must not overwrite another customized action.
    for (int added : {3, 4}) if (!settings.contains(QString("shortcuts/%1").arg(added)))
        for (int i = 0; i < 5; ++i) if (i != added && result[i] == result[added]) { result[added] = {}; break; }
    return validate(result).isEmpty() ? result : defaults();
}

void Hotkeys::save(QSettings &settings, const Bindings &bindings)
{
    for (int i = 0; i < 5; ++i) settings.setValue(QString("shortcuts/%1").arg(i), bindings[i].toString(QKeySequence::PortableText));
    settings.sync();
}

QString Hotkeys::validate(const Bindings &bindings)
{
    for (int i = 0; i < 5; ++i) {
        const auto &binding = bindings[i];
        const auto &key = binding.key;
        if (binding.isEmpty()) continue; // Empty disables this global shortcut.
        if (binding.buttons) {
            if (!key.isEmpty() || (binding.buttons & ~(Qt::BackButton | Qt::ForwardButton)) ||
                (binding.modifiers & ~(Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier | Qt::ShiftModifier)))
                return "Use Mouse4, Mouse5, or both side buttons, optionally with modifier keys.";
        } else {
            if (key.count() != 1 || nativeKey(key[0].key()) < 0 ||
                (key[0].keyboardModifiers() & (Qt::KeypadModifier | Qt::GroupSwitchModifier)))
                return "Use one shortcut with a letter, number, Space, or F1–F20.";
            if (key[0].key() < Qt::Key_F1 && !(key[0].keyboardModifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier | Qt::ShiftModifier)))
                return "Add Shift, Control, Command, Alt or Option to a letter, number or Space.";
        }
        for (int j = 0; j < i; ++j) if (binding == bindings[j]) return "Each action needs a different shortcut.";
    }
    return {};
}

Hotkeys::Hotkeys(QObject *parent) : QObject(parent), platform(std::make_unique<Platform>())
{
    // Pins create temporary shortcut owners; recycle IDs when they close.
    if (availableHotkeyBases.isEmpty()) { platform->base = nextHotkeyBase; nextHotkeyBase += 6; }
    else platform->base = availableHotkeyBases.takeLast();
#ifdef Q_OS_MACOS
    const EventTypeSpec types[]{{kEventClassKeyboard, kEventHotKeyPressed}, {kEventClassKeyboard, kEventHotKeyReleased}};
    InstallEventHandler(GetApplicationEventTarget(), [](EventHandlerCallRef, EventRef event, void *context) -> OSStatus {
        EventHotKeyID id{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr)
            return eventNotHandledErr;
        auto *hotkeys = static_cast<Hotkeys *>(context);
        const int index = int(id.id) - hotkeys->platform->base;
        if (id.signature != 0x53504b4e || index < 0 || index >= 6) return eventNotHandledErr;
        if (GetEventKind(event) == kEventHotKeyReleased) {
            if (index != 5) return eventNotHandledErr;
            hotkeys->pinToolsPressed = false; return noErr;
        }
        if (index == 5) {
            if (hotkeys->pinToolsPressed) return noErr;
            hotkeys->pinToolsPressed = true;
        }
        hotkeys->dispatch(index);
        return noErr;
    }, 2, types, this, &platform->handler);
#elif defined(Q_OS_WIN)
    qApp->installNativeEventFilter(this);
#endif
}

bool Hotkeys::registerKey(int index, const QKeySequence &key)
{
    if (key.isEmpty()) return true;
    const auto mods = key[0].keyboardModifiers();
#ifdef Q_OS_MACOS
    if (!platform->handler) return false;
    UInt32 nativeMods = 0;
    if (mods & Qt::MetaModifier) nativeMods |= controlKey;
    if (mods & Qt::ControlModifier) nativeMods |= cmdKey;
    if (mods & Qt::ShiftModifier) nativeMods |= shiftKey;
    if (mods & Qt::AltModifier) nativeMods |= optionKey;
    registered[index] = RegisterEventHotKey(nativeKey(key[0].key()), nativeMods, {0x53504b4e, UInt32(platform->base + index)},
        GetApplicationEventTarget(), 0, &platform->keys[index]) == noErr;
#elif defined(Q_OS_WIN)
    UINT nativeMods = MOD_NOREPEAT;
    if (mods & Qt::ControlModifier) nativeMods |= MOD_CONTROL;
    if (mods & Qt::MetaModifier) nativeMods |= MOD_WIN;
    if (mods & Qt::ShiftModifier) nativeMods |= MOD_SHIFT;
    if (mods & Qt::AltModifier) nativeMods |= MOD_ALT;
    registered[index] = RegisterHotKey(nullptr, platform->base + index, nativeMods, nativeKey(key[0].key()));
#endif
    return registered[index];
}

void Hotkeys::dispatch(int index)
{
    if (index == 0) emit capture();
    else if (index == 1) emit paste();
    else if (index == 2) emit replay();
    else if (index == 3) emit quickOcr();
    else if (index == 4) emit quickTranslate();
    else if (index == 5) emit pinTools();
}

void Hotkeys::enablePinTools(bool enabled)
{
    if (enabled == registered[5]) return;
    if (enabled) registerKey(5, QKeySequence(Qt::Key_Space));
    else unregisterKey(5);
}

static constexpr qint64 replayTag = 0x53504b4d;

#ifdef Q_OS_WIN
LRESULT CALLBACK Hotkeys::Platform::mouseCallback(int code, WPARAM message, LPARAM data)
{
    if (code < 0 || !mouseOwner || (message != WM_XBUTTONDOWN && message != WM_XBUTTONUP))
        return CallNextHookEx(nullptr, code, message, data);
    const auto *mouse = reinterpret_cast<MSLLHOOKSTRUCT *>(data);
    if (mouse->dwExtraInfo == ULONG_PTR(replayTag)) return CallNextHookEx(nullptr, code, message, data);
    const auto button = HIWORD(mouse->mouseData) == XBUTTON1 ? Qt::BackButton : Qt::ForwardButton;
    Qt::KeyboardModifiers mods;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= Qt::ControlModifier;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= Qt::ShiftModifier;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= Qt::AltModifier;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= Qt::MetaModifier;
    auto *owner = mouseOwner;
    const auto result = owner->gesture.event(button, message == WM_XBUTTONDOWN, mods, owner->current);
    if (result.action >= 0 || result.replay) {
        const auto generation = owner->generation;
        QMetaObject::invokeMethod(owner, [owner, result, generation] {
            if (generation != owner->generation) return;
            if (result.action >= 0) owner->dispatch(result.action);
            for (auto side : {Qt::BackButton, Qt::ForwardButton}) if (result.replay & side) {
                INPUT clicks[2]{};
                for (int i = 0; i < 2; ++i) {
                    clicks[i].type = INPUT_MOUSE;
                    clicks[i].mi.dwFlags = i ? MOUSEEVENTF_XUP : MOUSEEVENTF_XDOWN;
                    clicks[i].mi.mouseData = side == Qt::BackButton ? XBUTTON1 : XBUTTON2;
                    clicks[i].mi.dwExtraInfo = ULONG_PTR(replayTag);
                }
                SendInput(2, clicks, sizeof(INPUT));
            }
        }, Qt::QueuedConnection);
    }
    return result.consume ? 1 : CallNextHookEx(nullptr, code, message, data);
}
#endif

bool Hotkeys::registerMouse(const Bindings &bindings)
{
    bool needed = false;
    for (const auto &binding : bindings) needed |= bool(binding.buttons);
    if (!needed) return true;
#ifdef Q_OS_MACOS
    if (!AXIsProcessTrusted()) {
        const void *keys[] = {kAXTrustedCheckOptionPrompt}; const void *values[] = {kCFBooleanTrue};
        auto options = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        AXIsProcessTrustedWithOptions(options); CFRelease(options);
        return false;
    }
    platform->mouseTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
        CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp),
        [](CGEventTapProxy, CGEventType type, CGEventRef event, void *context) -> CGEventRef {
            auto *owner = static_cast<Hotkeys *>(context);
            if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
                owner->gesture = {}; CGEventTapEnable(owner->platform->mouseTap, true); return event;
            }
            if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == replayTag) return event;
            const auto number = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
            if (number != 3 && number != 4) return event;
            const auto flags = CGEventGetFlags(event);
            Qt::KeyboardModifiers mods;
            if (flags & kCGEventFlagMaskControl) mods |= Qt::MetaModifier;
            if (flags & kCGEventFlagMaskCommand) mods |= Qt::ControlModifier;
            if (flags & kCGEventFlagMaskAlternate) mods |= Qt::AltModifier;
            if (flags & kCGEventFlagMaskShift) mods |= Qt::ShiftModifier;
            const auto result = owner->gesture.event(number == 3 ? Qt::BackButton : Qt::ForwardButton,
                                                     type == kCGEventOtherMouseDown, mods, owner->current);
            if (result.action >= 0 || result.replay) {
                const auto generation = owner->generation;
                const auto position = CGEventGetLocation(event);
                QMetaObject::invokeMethod(owner, [owner, result, generation, position] {
                    if (generation != owner->generation) return;
                    if (result.action >= 0) owner->dispatch(result.action);
                    CGEventFlags flags = 0;
                    if (result.modifiers & Qt::MetaModifier) flags |= kCGEventFlagMaskControl;
                    if (result.modifiers & Qt::ControlModifier) flags |= kCGEventFlagMaskCommand;
                    if (result.modifiers & Qt::AltModifier) flags |= kCGEventFlagMaskAlternate;
                    if (result.modifiers & Qt::ShiftModifier) flags |= kCGEventFlagMaskShift;
                    for (auto side : {Qt::BackButton, Qt::ForwardButton}) if (result.replay & side) {
                        for (auto type : {kCGEventOtherMouseDown, kCGEventOtherMouseUp}) {
                            auto click = CGEventCreateMouseEvent(nullptr, type, position, CGMouseButton(side == Qt::BackButton ? 3 : 4));
                            CGEventSetFlags(click, flags); CGEventSetIntegerValueField(click, kCGEventSourceUserData, replayTag);
                            CGEventSetIntegerValueField(click, kCGMouseEventClickState, 1);
                            CGEventPost(kCGSessionEventTap, click); CFRelease(click);
                        }
                    }
                }, Qt::QueuedConnection);
            }
            return result.consume ? nullptr : event;
        }, this);
    if (!platform->mouseTap) return false;
    platform->mouseSource = CFMachPortCreateRunLoopSource(nullptr, platform->mouseTap, 0);
    if (!platform->mouseSource) return false;
    CFRunLoopAddSource(CFRunLoopGetMain(), platform->mouseSource, kCFRunLoopCommonModes);
    CGEventTapEnable(platform->mouseTap, true);
    return true;
#elif defined(Q_OS_WIN)
    if (Platform::mouseOwner) return false;
    platform->mouseHook = SetWindowsHookExW(WH_MOUSE_LL, Platform::mouseCallback, GetModuleHandleW(nullptr), 0);
    if (!platform->mouseHook) return false;
    Platform::mouseOwner = this;
    return true;
#else
    return false;
#endif
}

void Hotkeys::unregisterKeys()
{
    ++generation; gesture = {};
#ifdef Q_OS_MACOS
    if (platform->mouseSource) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), platform->mouseSource, kCFRunLoopCommonModes);
        CFRelease(platform->mouseSource); platform->mouseSource = nullptr;
    }
    if (platform->mouseTap) { CFMachPortInvalidate(platform->mouseTap); CFRelease(platform->mouseTap); platform->mouseTap = nullptr; }
#elif defined(Q_OS_WIN)
    if (platform->mouseHook) {
        UnhookWindowsHookEx(platform->mouseHook); platform->mouseHook = nullptr;
        Platform::mouseOwner = nullptr;
    }
#endif
    for (int i = 0; i < 6; ++i) unregisterKey(i);
}

void Hotkeys::unregisterKey(int index)
{
    if (!registered[index]) return;
#ifdef Q_OS_MACOS
    UnregisterEventHotKey(platform->keys[index]); platform->keys[index] = nullptr;
#elif defined(Q_OS_WIN)
    UnregisterHotKey(nullptr, platform->base + index);
#endif
    registered[index] = false;
    if (index == 5) pinToolsPressed = false;
}

QString Hotkeys::apply(const Bindings &bindings)
{
    if (const auto error = validate(bindings); !error.isEmpty()) return error;
    const auto previous = current;
    unregisterKeys();
    QString error;
    for (int i = 0; i < 5; ++i) {
        if (registerKey(i, bindings[i].key)) continue;
        error = QString("%1 is unavailable or already in use.").arg(bindings[i].toString());
        break;
    }
    if (error.isEmpty() && !registerMouse(bindings)) {
#ifdef Q_OS_MACOS
        error = "Allow FastPin in System Settings → Privacy & Security → Accessibility, then apply again.";
#else
        error = "The system could not register mouse shortcuts.";
#endif
    }
    if (!error.isEmpty()) {
        unregisterKeys();
        bool restored = true;
        for (int j = 0; j < 5; ++j) restored = registerKey(j, previous[j].key) && restored;
        restored = registerMouse(previous) && restored;
        return error + (restored ? " Previous shortcuts kept." : " Some previous shortcuts could not be restored. Use the menu bar or panel.");
    }
    current = bindings;
    return {};
}

Hotkeys::~Hotkeys()
{
    unregisterKeys();
#ifdef Q_OS_MACOS
    if (platform->handler) RemoveEventHandler(platform->handler);
#elif defined(Q_OS_WIN)
    qApp->removeNativeEventFilter(this);
#endif
    availableHotkeyBases.append(platform->base);
}

bool Hotkeys::nativeEventFilter(const QByteArray &, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    const int index = int(msg->wParam) - platform->base;
    if (msg->message == WM_HOTKEY && index >= 0 && index < 6) {
        dispatch(index);
        *result = 0;
        return true;
    }
#else
    Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}

bool screenCaptureAllowed(bool prompt)
{
#ifdef Q_OS_MACOS
    return CGPreflightScreenCaptureAccess() || (prompt && CGRequestScreenCaptureAccess());
#else
    Q_UNUSED(prompt);
    return true;
#endif
}
