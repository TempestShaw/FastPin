#include "onboarding.h"
#include "platform.h"
#include "toolbar.h"
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
class Preview : public QWidget {
public:
    explicit Preview(QWidget *parent) : QWidget(parent) { setFixedWidth(290); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#E9EDEB"));
        p.setPen(Qt::NoPen); p.setBrush(QColor("#DDF6E9"));
        p.drawEllipse(QRectF(-80, -70, 290, 290)); p.drawEllipse(QRectF(150, 360, 250, 250));
        auto card = [&p](QRectF r) {
            p.setBrush(QColor("#D5DDD8")); p.drawRoundedRect(r.translated(0, 8), 12, 12);
            p.setBrush(Qt::white); p.drawRoundedRect(r, 12, 12);
        };
        card({26, 152, 238, 182});
        p.setPen(QColor("#77817E")); p.setFont(QFont("", 10)); p.drawText(QRectF(44, 168, 200, 22), "YOUR WORKSPACE");
        p.setPen(QPen(QColor("#E9EDEB"), 6, Qt::SolidLine, Qt::RoundCap));
        for (int y : {216, 236, 256}) p.drawLine(46, y, 224 - (y == 256 ? 52 : 0), y);
        card({64, 282, 194, 136});
        p.setPen(QPen(QColor("#128252"), 2)); p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(72, 290, 178, 120), 8, 8);
        p.setPen(QColor("#151B1C")); p.setFont(QFont("", 17, QFont::DemiBold)); p.drawText(QRectF(88, 316, 140, 48), Qt::TextWordWrap, "Keep this\nin view.");
        p.setPen(QPen(QColor("#ff6775"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(91, 376, 214, 376);
        p.setBrush(QColor("#128252")); p.setPen(Qt::NoPen); p.drawEllipse(QRectF(212, 267, 36, 36));
        p.drawPixmap(219, 274, toolIcon("pin", Qt::white).pixmap(22, 22));
        p.setPen(QColor("#596660")); p.setFont(QFont("", 11));
        p.drawText(QRectF(28, height() - 72, 234, 40), Qt::AlignCenter | Qt::TextWordWrap, "A reference. A reminder.\nRight where you need it.");
    }
};
}

Onboarding::Onboarding(QSettings &storage, const Hotkeys::Bindings &bindings, QWidget *parent)
    : QDialog(parent), settings(storage)
{
    setWindowTitle("Welcome to FastPin"); setFixedSize(780, 600);
    setObjectName("onboarding");
    setStyleSheet("QDialog#onboarding { background: #F6F8F6; } QLabel { color: #151B1C; }"
                  "QLabel#eyebrow { color: #128252; font-size: 11px; font-weight: 600; }"
                  "QLabel#heading { font-size: 28px; font-weight: 600; }"
                  "QLabel#detail { color: #596660; font-size: 13px; }"
                  "QLabel#binding { background: #DDF6E9; color: #128252; border-radius: 6px; padding: 7px; }"
                  "QPushButton { color: #151B1C; background: white; border: 1px solid #D5DDD8; border-radius: 8px; min-height: 20px; padding: 10px 16px; }"
                  "QPushButton:hover { background: #DDF6E9; }"
                  "QPushButton#primary { background: #128252; color: white; border: 0; }"
                  "QPushButton#primary:hover { background: #0C693F; }");
    auto *outer = new QHBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    outer->addWidget(new Preview(this));
    auto *body = new QVBoxLayout; body->setContentsMargins(36, 32, 36, 28); body->setSpacing(20); outer->addLayout(body, 1);
    auto *brand = new QHBoxLayout;
    auto *logo = new QLabel(this); logo->setPixmap(QIcon(":/brand/app-icon.png").pixmap(32, 32)); brand->addWidget(logo);
    brand->addWidget(new QLabel("<b>FastPin</b>", this)); brand->addStretch(); body->addLayout(brand);
    pages = new QStackedWidget(this); body->addWidget(pages, 1);
    auto page = [this](const QString &number, const QString &title, const QString &detail) {
        auto *widget = new QWidget(this); auto *layout = new QVBoxLayout(widget); layout->setContentsMargins(0, 12, 0, 0); layout->setSpacing(10);
        auto label = [widget, layout](const QString &text, const char *name) {
            auto *item = new QLabel(text, widget); item->setObjectName(name); item->setWordWrap(true); layout->addWidget(item); return item;
        };
        label(number, "eyebrow"); label(title, "heading"); label(detail, "detail"); pages->addWidget(widget); return layout;
    };
    auto *welcome = page("01 / A SMALL INTRODUCTION", "Capture ideas.\nKeep them in view.",
                         "Capture a region, add a quick note, then pin it above your workspace. No account needed.");
    welcome->addWidget(new QLabel("<b>Capture</b> a window, element or region<br><br>"
                                 "<b>Mark up</b> with a pen, arrow, rectangle or text<br><br>"
                                 "<b>Keep</b> a pin, pick a color or extract text", this)); welcome->addStretch();
    auto *permissions = page("02 / YOUR SCREEN, YOUR CHOICE", "You're in control.",
                             "Screenshots and OCR are processed on this computer. The latest image can be saved locally; you can turn that off in Preferences.");
#ifdef Q_OS_MACOS
    auto permissionRow = [this, permissions](const QString &title, QLabel *&status, QPushButton *&grant, auto request) {
        auto *row = new QHBoxLayout; auto *text = new QVBoxLayout; text->setSpacing(6);
        auto *name = new QLabel("<b>" + title + "</b>", this); name->setWordWrap(true); text->addWidget(name);
        status = new QLabel(this); status->setObjectName("detail"); status->setWordWrap(true); text->addWidget(status);
        row->addLayout(text, 1); grant = new QPushButton("Allow…", this); row->addWidget(grant); permissions->addLayout(row);
        connect(grant, &QPushButton::clicked, this, [this, request] { request(); refreshPermissions(); });
    };
    permissionRow("Screen Recording", screenStatus, screenGrant, [] { screenCaptureAllowed(true); });
    screenGrant->setAccessibleName("Allow screen capture");
    permissionRow("Accessibility · optional", elementStatus, elementGrant, [] { elementAccessAllowed(true); });
    elementGrant->setAccessibleName("Allow element detection");
#else
    permissions->addWidget(new QLabel("<b>Ready for screen capture</b><br><br>"
                                     "Window and Element detection use Windows APIs.<br>"
                                     "OCR needs the installed MSIX app and a Windows OCR language pack.", this));
#endif
    permissions->addStretch();
    auto *shortcuts = page("03 / MAKE IT YOURS", "One shortcut away.", "These are your current shortcuts. Change them any time in Preferences, including mouse side buttons.");
    const QStringList names{"Capture screenshot", "Paste as pin", "Restore last image", "Copy text from screen"};
    for (int i = 0; i < 4; ++i) {
        auto *row = new QHBoxLayout; row->addWidget(new QLabel(names[i], this), 1);
        auto *key = new QLabel(bindings[i].isEmpty() ? "Disabled" : bindings[i].toString(QKeySequence::NativeText), this);
        key->setObjectName("binding"); row->addWidget(key); shortcuts->addLayout(row);
    }
    auto *customize = new QPushButton("Customize shortcuts…", this); shortcuts->addWidget(customize, 0, Qt::AlignLeft);
    connect(customize, &QPushButton::clicked, this, [this] { complete(); emit preferencesRequested(); });
    auto *tip = new QLabel("While capturing, press Tab to switch Window / Element.\nAfter closing a pin, Restore last image brings it back.", this);
    tip->setObjectName("detail"); tip->setWordWrap(true); shortcuts->addWidget(tip); shortcuts->addStretch();
    auto *navigation = new QHBoxLayout;
    back = new QPushButton("Back", this); navigation->addWidget(back);
    connect(back, &QPushButton::clicked, this, [this] { setStep(pages->currentIndex() - 1); });
    progress = new QLabel(this); progress->setObjectName("detail"); navigation->addWidget(progress, 1, Qt::AlignCenter);
    next = new QPushButton(this); next->setObjectName("primary"); navigation->addWidget(next);
    connect(next, &QPushButton::clicked, this, [this] {
        if (pages->currentIndex() < 2) setStep(pages->currentIndex() + 1);
        else { complete(); emit captureRequested(); }
    }); body->addLayout(navigation);
    auto *later = new QPushButton("I'll explore on my own", this); later->setFlat(true); body->addWidget(later, 0, Qt::AlignRight);
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    setStep(0);
}

bool Onboarding::start(bool force)
{
    if (!force && settings.value("onboarding/seen", false).toBool()) return false;
    settings.setValue("onboarding/seen", true); settings.sync();
    show(); return true;
}

void Onboarding::setStep(int step)
{
    pages->setCurrentIndex(step); back->setVisible(step > 0);
    progress->setText(QString("%1 / 3").arg(step + 1));
    next->setText(step == 2 ? "Try a capture" : "Continue");
    if (step == 1) refreshPermissions();
}

void Onboarding::refreshPermissions()
{
    if (screenStatus) {
        const bool allowed = screenCaptureAllowed(false);
        screenStatus->setText(allowed ? "Your screen is ready to capture." : "For captures. Allow in System Settings; reopen the app if macOS asks.");
        screenGrant->setEnabled(!allowed); screenGrant->setText(allowed ? "Allowed" : "Allow…");
    }
    if (elementStatus) {
        const bool allowed = elementAccessAllowed(false);
        elementStatus->setText(allowed ? "Element detection is ready." : "For UI elements and mouse shortcuts. Window selection works without it.");
        elementGrant->setEnabled(!allowed); elementGrant->setText(allowed ? "Allowed" : "Allow…");
    }
}

bool Onboarding::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate && screenStatus) refreshPermissions();
    return QDialog::event(event);
}

void Onboarding::complete()
{
    settings.setValue("onboarding/completed", true); settings.sync(); accept();
}
