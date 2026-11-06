#include "platform.h"
#include "doubletap.h"
#include "translation.h"

#include <QCoreApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <unistd.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Vision/Vision.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <IOKit/hidsystem/IOLLEvent.h>

std::function<void()> pronounceText(const QString &text)
{
    auto *speech = [[AVSpeechSynthesizer alloc] init];
    auto *utterance = [AVSpeechUtterance speechUtteranceWithString:text.toNSString()];
    utterance.voice = [AVSpeechSynthesisVoice voiceWithLanguage:containsHan(text) ? @"zh-TW" : @"en-US"];
    [speech speakUtterance:utterance];
    return [speech] { [speech stopSpeakingAtBoundary:AVSpeechBoundaryImmediate]; };
}

@interface FastPinHover : NSObject {
@public
    std::function<void(bool)> changed;
}
@end
@implementation FastPinHover
- (void)mouseEntered:(NSEvent *)event { Q_UNUSED(event); if (changed) changed(true); }
- (void)mouseExited:(NSEvent *)event { Q_UNUSED(event); if (changed) changed(false); }
@end

std::function<void()> watchPinHover(QWidget *window, std::function<void(bool)> changed)
{
    if (QGuiApplication::platformName() != "cocoa") return {};
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    FastPinHover *observer = [[FastPinHover alloc] init]; observer->changed = std::move(changed);
    NSTrackingArea *area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
        owner:observer userInfo:nil];
    [view addTrackingArea:area];
    return [view, area, observer] { observer->changed = {}; [view removeTrackingArea:area]; };
}

std::function<void()> captureFocusRestorer()
{
    if (QGuiApplication::platformName() != "cocoa") return {};
    NSRunningApplication *previous = NSWorkspace.sharedWorkspace.frontmostApplication;
    if (!previous || previous.processIdentifier == getpid()) return {};
    return [previous] {
        if (!NSApp.isActive || previous.terminated) return;
        [NSApp yieldActivationToApplication:previous];
        [previous activateFromApplication:NSRunningApplication.currentApplication options:0];
    };
}

std::function<void()> makePanelNonactivating(QWidget *window)
{
    if (QGuiApplication::platformName() != "cocoa") return {};
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    NSPanel *panel = (NSPanel *)view.window;
    panel.styleMask |= NSWindowStyleMaskNonactivatingPanel;
    panel.becomesKeyOnlyIfNeeded = YES;
    panel.hidesOnDeactivate = NO;
    // Qt creates the panel before we can set its style. Keep the foreground app
    // on mouse-up too, since AppKit may retain the original activation behavior.
    NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
    __block NSRunningApplication *previous = workspace.frontmostApplication;
    id activation = [workspace.notificationCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
        object:nil queue:nil usingBlock:^(NSNotification *notification) {
            NSRunningApplication *app = notification.userInfo[NSWorkspaceApplicationKey];
            if (app.processIdentifier != getpid()) previous = app;
        }];
    id clicks = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseUp handler:^NSEvent *(NSEvent *event) {
        if (event.window == panel) {
            // AppKit finishes activating after mouse-up; wait for that transition.
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                if (NSRunningApplication.currentApplication.active && previous && !previous.terminated && previous.processIdentifier != getpid()) {
                    [NSApp yieldActivationToApplication:previous];
                    [previous activateFromApplication:NSRunningApplication.currentApplication options:0];
                }
            });
        }
        return event;
    }];
    return [workspace, activation, clicks] {
        [workspace.notificationCenter removeObserver:activation]; [NSEvent removeMonitor:clicks];
    };
}

@interface FastPinTranslationGlass : NSVisualEffectView {
@public
    QPointer<QWidget> card;
    std::function<void(bool)> backdropChanged;
    bool sampling, resample, darkBackdrop;
}
- (void)refreshBackdrop;
@end

@implementation FastPinTranslationGlass
- (void)refreshBackdrop
{
    if (!card || !backdropChanged || !card->isVisible() || !CGPreflightScreenCaptureAccess()) return;
    if (sampling) { resample = true; return; }
    const QRect screen = card->screen()->geometry();
    const QRect region = card->geometry().intersected(screen).translated(-screen.topLeft());
    if (region.isEmpty()) return;
    sampling = true; resample = false;
    const CGDirectDisplayID displayId = [self.window.screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    const CGWindowID windowId = self.window.windowNumber;
    const CGRect rect = CGRectMake(region.x(), region.y(), region.width(), region.height());
    __weak FastPinTranslationGlass *weak = self;
    void (^finished)(CGImageRef) = ^(CGImageRef image) {
        double brightness = -1;
        if (image) {
            unsigned char pixel[4] = {};
            CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(pixel, 1, 1, 8, 4, color, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            if (context) {
                CGContextDrawImage(context, CGRectMake(0, 0, 1, 1), image);
                brightness = (0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]) / 255;
                CGContextRelease(context);
            }
            CGColorSpaceRelease(color);
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            FastPinTranslationGlass *glass = weak;
            if (!glass || !glass->backdropChanged) return;
            glass->sampling = false;
            // Hysteresis keeps text from flickering over mixed light/dark content.
            const bool dark = brightness >= 0 && brightness < (glass->darkBackdrop ? 0.58 : 0.42);
            if (brightness >= 0 && dark != glass->darkBackdrop) {
                glass->darkBackdrop = dark;
                glass.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
                glass->backdropChanged(dark);
            }
            if (glass->resample) [glass refreshBackdrop];
        });
    };
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:YES
        completionHandler:^(SCShareableContent *content, NSError *error) {
            if (error) { finished(nil); return; }
            SCDisplay *display = nil;
            for (SCDisplay *candidate in content.displays) if (candidate.displayID == displayId) display = candidate;
            SCWindow *excluded = nil;
            for (SCWindow *candidate in content.windows) if (candidate.windowID == windowId) excluded = candidate;
            if (!display || !excluded) { finished(nil); return; }
            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[excluded]];
            SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
            config.sourceRect = rect; config.width = 32; config.height = 16; config.showsCursor = NO;
            [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef image, NSError *) { finished(image); }];
        }];
}
@end

std::function<void()> installTranslationGlass(QWidget *window, std::function<void(bool)> backdropChanged)
{
    if (QGuiApplication::platformName() != "cocoa") return {};
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    FastPinTranslationGlass *glass = [[FastPinTranslationGlass alloc] initWithFrame:view.frame];
    glass->card = window; glass->backdropChanged = std::move(backdropChanged);
    glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    glass.material = NSVisualEffectMaterialHUDWindow;
    glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    glass.state = NSVisualEffectStateActive;
    glass.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    glass.wantsLayer = YES; glass.layer.cornerRadius = 16; glass.layer.masksToBounds = YES;
    [view.superview addSubview:glass positioned:NSWindowBelow relativeTo:view];
    view.window.backgroundColor = NSColor.clearColor; view.window.hasShadow = YES;
    __weak FastPinTranslationGlass *weak = glass;
    NSMutableArray *observers = [NSMutableArray array];
    for (NSNotificationName name in @[NSWindowDidMoveNotification, NSWindowDidResizeNotification, NSWindowDidChangeScreenNotification])
        [observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:name object:view.window queue:NSOperationQueue.mainQueue
            usingBlock:^(NSNotification *) { [weak refreshBackdrop]; }]];
    id activation = [NSWorkspace.sharedWorkspace.notificationCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
        object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { [weak refreshBackdrop]; }];
    QTimer::singleShot(0, window, [weak] { [weak refreshBackdrop]; });
    return [glass, observers, activation] {
        glass->backdropChanged = {};
        for (id observer in observers) [NSNotificationCenter.defaultCenter removeObserver:observer];
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:activation];
        [glass removeFromSuperview];
    };
}

QList<DesktopWindow> desktopWindows()
{
    QList<DesktopWindow> result;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return result;
    for (NSDictionary *info in (__bridge NSArray *)list) {
        if ([info[(__bridge NSString *)kCGWindowLayer] intValue] != 0 ||
            [info[(__bridge NSString *)kCGWindowAlpha] doubleValue] <= 0) continue;
        CGRect bounds;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds], &bounds)) continue;
        if (bounds.size.width < 2 || bounds.size.height < 2) continue;
        result.append({QRectF(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height),
                       [info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue],
                       [info[(__bridge NSString *)kCGWindowOwnerPID] unsignedIntValue]});
    }
    CFRelease(list);
    return result;
}

DesktopCoordinates desktopCoordinates(QScreen *screen) { return {screen->geometry().topLeft(), 1}; }

bool elementAccessAllowed(bool prompt)
{
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @(prompt)};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

bool doubleControlAccessAllowed(bool prompt)
{
    return CGPreflightListenEventAccess() || (prompt && CGRequestListenEventAccess());
}

DoubleTapDetector::Input doubleControlInput(CGEventType type, CGEventRef event)
{
    using Input = DoubleTapDetector::Input;
    if (type != kCGEventFlagsChanged) return Input::Other;
    const auto code = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    const auto flags = CGEventGetFlags(event);
    const bool chord = flags & (kCGEventFlagMaskShift | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand);
    if ((code != 59 && code != 62) || chord) return Input::Other;
    return (flags & kCGEventFlagMaskControl) ? Input::ControlDown : Input::ControlUp;
}

DoubleTapDetector::Input doubleLeftCommandInput(CGEventType type, CGEventRef event)
{
    using Input = DoubleTapDetector::Input;
    if (type != kCGEventFlagsChanged || CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) != 55) return Input::Other;
    const auto flags = CGEventGetFlags(event);
    if (flags & (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift | NX_DEVICERCMDKEYMASK)) return Input::Other;
    return (flags & kCGEventFlagMaskCommand) ? Input::ControlDown : Input::ControlUp;
}

static std::function<void()> watchDoubleModifier(std::function<void()> triggered,
    DoubleTapDetector::Input (*input)(CGEventType, CGEventRef), DoubleTapDetector::Fire fire)
{
    if (QGuiApplication::platformName() != "cocoa" || !doubleControlAccessAllowed(false)) return {};
    struct Watch {
        DoubleTapDetector detector;
        std::function<void()> triggered;
        DoubleTapDetector::Input (*input)(CGEventType, CGEventRef);
        CFMachPortRef tap = nullptr;
    };
    auto *watch = new Watch{DoubleTapDetector(fire), std::move(triggered), input};
    const auto mask = CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventKeyDown) |
        CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventOtherMouseDown);
    watch->tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask,
        [](CGEventTapProxy, CGEventType type, CGEventRef event, void *context) -> CGEventRef {
            auto *watch = static_cast<Watch *>(context);
            if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
                watch->detector.reset();
                CGEventTapEnable(watch->tap, true);
                return event;
            }
            if (watch->detector.feed(watch->input(type, event), qint64(CGEventGetTimestamp(event) / 1000000)))
                QMetaObject::invokeMethod(qApp, watch->triggered, Qt::QueuedConnection);
            return event; // Observe only: never swallow or rewrite another app's input.
        }, watch);
    if (!watch->tap) { delete watch; return {}; }
    auto source = CFMachPortCreateRunLoopSource(nullptr, watch->tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(watch->tap, true);
    return [watch, source] {
        CFMachPortInvalidate(watch->tap);
        CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CFRelease(source); CFRelease(watch->tap); delete watch;
    };
}

std::function<void()> watchDoubleControl(std::function<void()> triggered)
{
    // Control is rarely held between taps, so react on the second press.
    return watchDoubleModifier(std::move(triggered), doubleControlInput, DoubleTapDetector::Fire::OnSecondPress);
}

std::function<void()> watchDoubleLeftCommand(std::function<void()> triggered)
{
    // Command starts most chords (Command+Tab, Command+C), so wait for a clean second release.
    return watchDoubleModifier(std::move(triggered), doubleLeftCommandInput, DoubleTapDetector::Fire::OnSecondRelease);
}

ElementResult desktopElementAt(DesktopWindow window, QPointF point)
{
    if (!elementAccessAllowed(false)) return {{}, "Element needs Accessibility permission; using Window"};
    if (window.pid == quint32(getpid())) return {{}, "Element unavailable for image pins; using Window"};
    AXUIElementRef app = AXUIElementCreateApplication(window.pid);
    AXUIElementSetMessagingTimeout(app, 0.12f);
    AXUIElementRef element = nullptr;
    const AXError found = AXUIElementCopyElementAtPosition(app, point.x(), point.y(), &element);
    CFRelease(app);
    if (found != kAXErrorSuccess || !element) return {{}, "Element unavailable; using Window"};
    AXUIElementSetMessagingTimeout(element, 0.12f);
    CFTypeRef position = nullptr, size = nullptr;
    AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &position);
    AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &size);
    CGPoint p{};
    CGSize s{};
    const bool valid = position && size && CFGetTypeID(position) == AXValueGetTypeID() && CFGetTypeID(size) == AXValueGetTypeID() &&
        AXValueGetValue((AXValueRef)position, kAXValueTypeCGPoint, &p) &&
        AXValueGetValue((AXValueRef)size, kAXValueTypeCGSize, &s);
    if (position) CFRelease(position);
    if (size) CFRelease(size);
    CFRelease(element);
    const QRectF rect(p.x, p.y, s.width, s.height);
    return valid && !rect.isEmpty() && rect.contains(point) ? ElementResult{rect, {}} :
        ElementResult{{}, "Element unavailable; using Window"};
}

OcrResult recognizeText(QImage image)
{
    @autoreleasepool {
        image = image.convertToFormat(QImage::Format_RGBA8888);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, image.constBits(), image.sizeInBytes(), nullptr);
        CGImageRef cgImage = CGImageCreate(image.width(), image.height(), 8, 32, image.bytesPerLine(), space,
            kCGImageAlphaLast | kCGBitmapByteOrder32Big, provider, nullptr, false, kCGRenderingIntentDefault);
        CGDataProviderRelease(provider);
        CGColorSpaceRelease(space);
        if (!cgImage) return {{}, "Could not prepare the image for OCR."};
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.recognitionLanguages = @[@"zh-Hant", @"zh-Hans", @"en-US"];
        request.automaticallyDetectsLanguage = YES;
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
        NSError *error = nil;
        const bool ok = [handler performRequests:@[request] error:&error];
        CGImageRelease(cgImage);
        if (!ok) return {{}, QString::fromUtf8(error.localizedDescription.UTF8String)};
        QStringList lines;
        for (VNRecognizedTextObservation *observation in request.results) {
            VNRecognizedText *text = [observation topCandidates:1].firstObject;
            if (text) lines.append(QString::fromUtf8(text.string.UTF8String));
        }
        return {lines.join('\n'), {}};
    }
}
