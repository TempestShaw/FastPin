#include "platform.h"

#include <QScreen>
#include <QWidget>
#include <unistd.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Vision/Vision.h>

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
