#include "platform.h"

#include <QElapsedTimer>
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

using Microsoft::WRL::ComPtr;

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
