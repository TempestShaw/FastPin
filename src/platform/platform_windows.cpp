#include "platform.h"
#include "doubletap.h"

#include <QElapsedTimer>
#include <QCoreApplication>
#include <QScreen>
#include <QStringList>
#include <windows.h>
#include <appmodel.h>
#include <dwmapi.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <sapi.h>

using Microsoft::WRL::ComPtr;

namespace {
struct DoubleControlWatch {
    inline static DoubleControlWatch *active = nullptr;
    DoubleTapDetector detector;
    std::function<void()> triggered;
    HHOOK keyboard = nullptr, mouse = nullptr;
    void feed(DoubleTapDetector::Input input, DWORD time)
    {
        if (detector.feed(input, time)) QMetaObject::invokeMethod(qApp, triggered, Qt::QueuedConnection);
    }
};

LRESULT CALLBACK doubleControlKeyboard(int code, WPARAM message, LPARAM data)
{
    const auto *key = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
    if (code == HC_ACTION && DoubleControlWatch::active && !(key->flags & LLKHF_INJECTED)) {
        using Input = DoubleTapDetector::Input;
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool control = key->vkCode == VK_LCONTROL || key->vkCode == VK_RCONTROL;
        const bool chord = (GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_MENU) |
                            GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000;
        if (control && !chord) DoubleControlWatch::active->feed(down ? Input::ControlDown : Input::ControlUp, key->time);
        else if (down) DoubleControlWatch::active->feed(Input::Other, key->time);
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK doubleControlMouse(int code, WPARAM message, LPARAM data)
{
    const bool press = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
    if (code == HC_ACTION && press && DoubleControlWatch::active)
        DoubleControlWatch::active->feed(DoubleTapDetector::Input::Other, reinterpret_cast<MSLLHOOKSTRUCT *>(data)->time);
    return CallNextHookEx(nullptr, code, message, data);
}
}

std::function<void()> watchDoubleControl(std::function<void()> triggered)
{
    if (DoubleControlWatch::active) return {};
    auto *watch = new DoubleControlWatch{DoubleTapDetector(DoubleTapDetector::Fire::OnSecondPress), std::move(triggered)};
    watch->keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, doubleControlKeyboard, GetModuleHandleW(nullptr), 0);
    watch->mouse = SetWindowsHookExW(WH_MOUSE_LL, doubleControlMouse, GetModuleHandleW(nullptr), 0);
    auto stop = [watch] {
        if (watch->keyboard) UnhookWindowsHookEx(watch->keyboard);
        if (watch->mouse) UnhookWindowsHookEx(watch->mouse);
        if (DoubleControlWatch::active == watch) DoubleControlWatch::active = nullptr;
        delete watch;
    };
    if (!watch->keyboard) { stop(); return {}; }
    DoubleControlWatch::active = watch;
    return stop;
}

std::function<void()> pronounceText(const QString &text)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return {};
    ComPtr<ISpVoice> speech;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&speech))) ||
        FAILED(speech->Speak(text.toStdWString().c_str(), SPF_ASYNC | SPF_IS_NOT_XML, nullptr))) {
        speech.Reset(); CoUninitialize(); return {};
    }
    return [speech]() mutable { speech->Speak(L"", SPF_PURGEBEFORESPEAK, nullptr); speech.Reset(); CoUninitialize(); };
}

QList<DesktopWindow> desktopWindows()
{
    QList<DesktopWindow> result;
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) && !GetWindowRect(hwnd, &r)) return TRUE;
        if (r.right - r.left < 2 || r.bottom - r.top < 2) return TRUE;
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        reinterpret_cast<QList<DesktopWindow> *>(data)->append({QRectF(r.left, r.top, r.right - r.left, r.bottom - r.top), quintptr(hwnd), pid});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

DesktopCoordinates desktopCoordinates(QScreen *screen)
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const auto name = screen->name().toStdWString();
    if (EnumDisplaySettingsW(name.c_str(), ENUM_CURRENT_SETTINGS, &mode))
        return {{qreal(mode.dmPosition.x), qreal(mode.dmPosition.y)}, screen->devicePixelRatio()};
    return {screen->geometry().topLeft(), screen->devicePixelRatio()};
}

bool elementAccessAllowed(bool) { return true; }

ElementResult desktopElementAt(DesktopWindow window, QPointF point)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return {{}, "Element service unavailable; using Window"};
    ElementResult result{{}, "Element unavailable; using Window"};
    {
        ComPtr<IUIAutomation> automation;
        if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)))) {
            ComPtr<IUIAutomation2> settings;
            if (SUCCEEDED(automation.As(&settings))) {
                settings->put_ConnectionTimeout(150);
                settings->put_TransactionTimeout(150);
            }
            ComPtr<IUIAutomationElement> element;
            ComPtr<IUIAutomationTreeWalker> walker;
            // Start in the frozen underlying window, not ElementFromPoint, which hits our overlay.
            if (SUCCEEDED(automation->ElementFromHandle(reinterpret_cast<HWND>(window.handle), &element)) &&
                SUCCEEDED(automation->get_ControlViewWalker(&walker))) {
                QElapsedTimer budget; budget.start();
                int visited = 0;
                for (int depth = 0; depth < 32 && budget.elapsed() < 120 && visited < 128; ++depth) {
                    RECT r{};
                    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&r)))
                        result.rect = QRectF(r.left, r.top, r.right - r.left, r.bottom - r.top);
                    ComPtr<IUIAutomationElement> child, match;
                    walker->GetFirstChildElement(element.Get(), &child);
                    while (child && ++visited < 128 && budget.elapsed() < 120) {
                        RECT bounds{};
                        if (SUCCEEDED(child->get_CurrentBoundingRectangle(&bounds)) &&
                            QRectF(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top).contains(point)) {
                            match = child; break;
                        }
                        ComPtr<IUIAutomationElement> next;
                        walker->GetNextSiblingElement(child.Get(), &next);
                        child = next;
                    }
                    if (!match) break;
                    element = match;
                }
                if (!result.rect.isEmpty() && result.rect.contains(point) && result.rect != window.rect) result.message.clear();
                else result.rect = {};
            }
        }
    }
    CoUninitialize();
    return result;
}

OcrResult recognizeText(QImage image)
{
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) == APPMODEL_ERROR_NO_PACKAGE)
        return {{}, "Windows OCR requires the MSIX edition. Install/register the packaged app."};
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) return {{}, "Could not initialize Windows OCR."};
    OcrResult output;
    try {
        {
            using namespace winrt::Windows;
            auto engine = Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) output.error = "Install an OCR language in Windows Settings → Time & language → Language & region.";
            else {
                const int maximum = int(Media::Ocr::OcrEngine::MaxImageDimension());
                if (image.width() > maximum || image.height() > maximum)
                    image = image.scaled(maximum, maximum, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                image = image.convertToFormat(QImage::Format_ARGB32);
                Storage::Streams::DataWriter writer;
                for (int y = 0; y < image.height(); ++y)
                    writer.WriteBytes(winrt::array_view<const uint8_t>(image.constScanLine(y), image.constScanLine(y) + image.width() * 4));
                auto bitmap = Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),
                    Graphics::Imaging::BitmapPixelFormat::Bgra8, image.width(), image.height(), Graphics::Imaging::BitmapAlphaMode::Ignore);
                auto recognized = engine.RecognizeAsync(bitmap).get();
                QStringList lines;
                for (const auto &line : recognized.Lines()) lines.append(QString::fromWCharArray(line.Text().c_str()));
                output.text = lines.join('\n');
                bitmap.Close();
            }
        }
    } catch (const winrt::hresult_error &error) {
        output.error = QString::fromWCharArray(error.message().c_str());
    }
    RoUninitialize();
    return output;
}
