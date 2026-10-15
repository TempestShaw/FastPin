#include "translationcard.h"
#include "translation.h"
#include "toolbar.h"
#include "platform.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

static constexpr int cardWidth = 360;
static constexpr int onlineByteLimit = 500; // MyMemory's anonymous per-request limit.
static const char *const onlineCredit = "MyMemory · Online";

TranslationCard::TranslationCard(QString text)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setObjectName("translationCard");
    setWindowTitle("FastPin — Translation");
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setFixedWidth(cardWidth);
    applyStyle();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 12);
    layout->setSpacing(10);
    layout->addLayout(buildHeader());
    layout->addWidget(buildDefinition());
    layout->addLayout(buildFooter());
    if (!text.isEmpty()) setSourceText(std::move(text));
    else {
        setResult("Recognizing text…", "On-device OCR");
        setActionsEnabled(false);
    }
}

void TranslationCard::applyStyle()
{
    QFont body = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    body.setPointSizeF(15);
    setFont(body);
    QPalette colors = palette();
    colors.setColor(QPalette::Window, QColor("#ffffff"));
    colors.setColor(QPalette::WindowText, QColor("#20242b"));
    colors.setColor(QPalette::Text, QColor("#414956"));
    colors.setColor(QPalette::Base, Qt::transparent);
    setPalette(colors);
    setStyleSheet("QToolButton { border: none; border-radius: 7px; padding: 0; color: #586270; background: transparent; }"
                  "QToolButton:hover { background: rgba(70, 80, 95, 18); } QToolButton:pressed { background: rgba(70, 80, 95, 30); }");
}

QToolButton *TranslationCard::headerButton(const QString &name, const QString &icon, const QString &label)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setIcon(toolIcon(icon, QColor("#778291")));
    button->setIconSize({17, 17});
    button->setToolTip(label);
    button->setAccessibleName(label);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    button->setFixedSize(26, 26);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLayout *TranslationCard::buildHeader()
{
    auto *header = new QHBoxLayout;
    header->setSpacing(4);
    title = new QLabel("Translation", this);
    title->setTextFormat(Qt::PlainText);
    QFont font = title->font();
    font.setPointSizeF(17);
    font.setWeight(QFont::DemiBold);
    title->setFont(font);
    header->addWidget(title, 1);
    speaker = headerButton("pronounce", "speaker", "Pronounce source text");
    connect(speaker, &QToolButton::clicked, this, [this] {
        if (stopSpeech) stopSpeech();
        stopSpeech = pronounceText(source);
        if (!stopSpeech) credit->setText("System speech is unavailable.");
    });
    copy = headerButton("copyDefinition", "copy", "Copy translation");
    connect(copy, &QToolButton::clicked, this, [this] { QApplication::clipboard()->setText(definition->toPlainText()); });
    auto *close = headerButton("closeTranslation", "close", "Close translation");
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    for (auto *button : {speaker, copy, close}) header->addWidget(button);
    return header;
}

QWidget *TranslationCard::buildDefinition()
{
    definition = new QPlainTextEdit(this);
    definition->setObjectName("translationDefinition");
    definition->setFont(font());
    definition->setReadOnly(true);
    definition->setFocusPolicy(Qt::NoFocus);
    definition->setFrameStyle(QFrame::NoFrame);
    definition->document()->setDocumentMargin(0);
    definition->setStyleSheet("QPlainTextEdit { background: transparent; }");
    return definition;
}

QLayout *TranslationCard::buildFooter()
{
    auto *footer = new QHBoxLayout;
    credit = new QLabel(this);
    credit->setTextFormat(Qt::PlainText);
    credit->setStyleSheet("color: #9aa2ad;");
    QFont small = credit->font();
    small.setPointSizeF(10);
    credit->setFont(small);
    footer->addWidget(credit, 1);
    online = new QToolButton(this);
    online->setObjectName("translateOnline");
    online->setText("Online");
    online->setToolTip("Send this text to MyMemory for online translation.");
    online->setAccessibleName("Translate online");
    online->setFont(small);
    online->setAutoRaise(true);
    online->setFocusPolicy(Qt::NoFocus);
    online->setMinimumSize(46, 24);
    connect(online, &QToolButton::clicked, this, &TranslationCard::translateOnline);
    footer->addWidget(online);
    return footer;
}

void TranslationCard::setActionsEnabled(bool enabled)
{
    for (auto *button : {speaker, copy, online}) button->setEnabled(enabled);
}

TranslationCard::~TranslationCard()
{
    if (stopSpeech) stopSpeech();
#ifdef Q_OS_MACOS
    if (stopFocusWatch) stopFocusWatch();
    if (removeGlass) removeGlass();
#endif
}

void TranslationCard::setSourceText(QString text)
{
    source = std::move(text);
    title->setText(title->fontMetrics().elidedText(source.simplified(), Qt::ElideRight, 215));
    title->setToolTip(source);
    setActionsEnabled(true);
    const QString found = offlineDefinition(source);
    if (!found.isEmpty()) setResult(found, "CC-CEDICT · CC BY-SA 4.0 · Offline");
    else if (isDictionaryWord(source)) setResult("No offline entry. Use Online to look up this word.", "CC-CEDICT · Offline");
    else translateOnline();
}

void TranslationCard::setOcrError(QString error)
{
    setResult(std::move(error), "On-device OCR");
    setActionsEnabled(false);
}

void TranslationCard::setResult(QString text, QString attribution)
{
    definition->setPlainText(text); credit->setText(attribution);
    definition->setFixedHeight(qBound(40, definition->fontMetrics().boundingRect(QRect(0, 0, width() - 40, 10000), Qt::TextWordWrap, text).height() + 4, 150));
    adjustSize();
    if (isVisible()) {
        const QRect area = screen()->availableGeometry().adjusted(8, 8, -8, -8);
        move(qBound(area.left(), x(), area.right() - width() + 1), qBound(area.top(), y(), area.bottom() - height() + 1));
    }
}

void TranslationCard::translateOnline()
{
    if (source.toUtf8().size() > onlineByteLimit) {
        setResult(QString("Select a shorter sentence (up to %1 UTF-8 bytes).").arg(onlineByteLimit), "MyMemory · Online limit");
        return;
    }
    online->setEnabled(false); copy->setEnabled(false);
    setResult("Translating…", onlineCredit);
    if (!network) network = new QNetworkAccessManager(this);
    QNetworkRequest request(onlineTranslationUrl(source));
    request.setTransferTimeout(10000);
    auto *reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        // Network-layer failures (offline, DNS, proxy, timeout) sort below Qt's content errors.
        const auto error = reply->error();
        const bool transportFailed = error != QNetworkReply::NoError && error < QNetworkReply::ContentAccessDenied;
        const auto result = parseTranslationReply(reply->readAll(), transportFailed);
        if (!result.error.isEmpty()) setResult(result.error, onlineCredit);
        else { setResult(result.text, onlineCredit); copy->setEnabled(true); }
        online->setEnabled(true);
    });
}

void TranslationCard::showAt(QPoint anchor)
{
    adjustSize();
    auto *screen = QGuiApplication::screenAt(anchor); if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry().adjusted(8, 8, -8, -8);
    QPoint position = anchor + QPoint(16, 16);
    if (position.x() + width() > area.right()) position.setX(anchor.x() - width() - 16);
    if (position.y() + height() > area.bottom()) position.setY(anchor.y() - height() - 16);
    position.setX(qBound(area.left(), position.x(), area.right() - width() + 1));
    position.setY(qBound(area.top(), position.y(), area.bottom() - height() + 1));
    move(position);
#ifdef Q_OS_MACOS
    if (!stopFocusWatch) stopFocusWatch = makePanelNonactivating(this);
    if (!removeGlass) removeGlass = installTranslationGlass(this);
#endif
    show();
}

void TranslationCard::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    bool glass = false;
#ifdef Q_OS_MACOS
    glass = bool(removeGlass);
#endif
    p.setBrush(QColor(255, 253, 245, glass ? 160 : 250)); p.setPen(QPen(QColor("#38A87E"), 1.25));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
}
void TranslationCard::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) { moving = true; dragOffset = e->globalPosition().toPoint() - pos(); }
}
void TranslationCard::mouseMoveEvent(QMouseEvent *e) { if (moving) move(e->globalPosition().toPoint() - dragOffset); }
void TranslationCard::mouseReleaseEvent(QMouseEvent *) { moving = false; }
