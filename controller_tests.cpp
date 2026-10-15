// Exercise the actual panel's shortcut and background-action lifecycle.
#define main fastpinApplicationMain
#define FASTPIN_CONTROLLER_TEST
// Keep the real worker thread and completion callback; control only the OCR result.
#define recognizeTextInWorker recognizeControllerTestText
#include "main.cpp"
#undef main
#undef recognizeTextInWorker
#include <QFocusEvent>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

OcrResult recognizeControllerTestText(QImage image, const QString &)
{
    if (image.pixelColor(0, 0) == QColor(Qt::red)) return {{}, "Test OCR failure"};
    if (image.pixelColor(0, 0) == QColor(Qt::white)) return {};
    if (image.pixelColor(0, 0) == QColor(Qt::green)) return {"bank", {}};
    return {"FastPin OCR test", {}};
}

class ControllerTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory;
    Hotkeys::Bindings bindings;
    bool shortcutsRegistered()
    {
        Hotkeys probe;
        return !probe.apply(bindings).isEmpty();
    }
private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        DWORD session = 0;
        if (ProcessIdToSessionId(GetCurrentProcessId(), &session) && session == 0)
            QSKIP("Global shortcuts require an interactive Windows session.");
#endif
        QCoreApplication::setOrganizationName("FastPinControllerTests");
        QCoreApplication::setApplicationName("FastPinControllerTests");
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
        const auto control = Hotkeys::defaults()[0].key[0].keyboardModifiers() & ~Qt::ShiftModifier;
        bindings = {QKeySequence(QKeyCombination(control, Qt::Key_S)),
                    QKeySequence(QKeyCombination(control, Qt::Key_D)),
                    QKeySequence(QKeyCombination(control, Qt::Key_F))};
        QSettings settings; Hotkeys::save(settings, bindings);
    }

    void inactiveFieldFocusKeepsGlobalShortcuts()
    {
        Controller panel;
        QVERIFY(shortcutsRegistered());
        auto *edit = panel.findChild<QLineEdit *>("shortcutEdit"); QVERIFY(edit);
        QFocusEvent focus(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(edit, &focus);
        QCoreApplication::processEvents();
        QVERIFY(shortcutsRegistered());
    }

    void applyEndsRecordingAndHiddenPanelKeepsShortcuts()
    {
        Controller panel;
        panel.findChild<QTabWidget *>()->setCurrentIndex(1);
        panel.show(); panel.activateWindow();
        auto *edit = panel.findChild<QLineEdit *>("shortcutEdit"); QVERIFY(edit);
        edit->setFocus(); QTRY_VERIFY(edit->hasFocus());
        QVERIFY(!shortcutsRegistered());
        auto *apply = panel.findChild<QPushButton *>("applyShortcuts"); QVERIFY(apply);
        apply->click();
        QVERIFY(!edit->hasFocus());
        QTRY_VERIFY(shortcutsRegistered());
        edit->setFocus(); QTRY_VERIFY(edit->hasFocus());
        QVERIFY(!shortcutsRegistered());
        panel.hide();
        QTRY_VERIFY(shortcutsRegistered());
        QSettings settings; QCOMPARE(Hotkeys::load(settings), bindings);
    }

    void invalidApplyKeepsPreviousShortcuts()
    {
        Controller panel;
        panel.findChild<QTabWidget *>()->setCurrentIndex(1);
        panel.show(); panel.activateWindow();
        auto *edit = panel.findChild<QLineEdit *>("shortcutEdit"); QVERIFY(edit);
        edit->setFocus(); QTRY_VERIFY(edit->hasFocus());
        QTest::keyClick(edit, Qt::Key_S);
        panel.findChild<QPushButton *>("applyShortcuts")->click();
        QVERIFY(!edit->hasFocus());
        QTRY_VERIFY(shortcutsRegistered());
        QSettings settings; QCOMPARE(Hotkeys::load(settings), bindings);
    }

    void ocrCompletionPreservesPanelVisibility_data()
    {
        QTest::addColumn<bool>("visible");
        QTest::addColumn<QColor>("color");
        QTest::addColumn<QString>("clipboard");
        QTest::addColumn<QString>("feedback");
        for (bool visible : {false, true}) {
            const QString state = visible ? "visible" : "hidden";
            QTest::newRow(qPrintable("success-" + state)) << visible << QColor(Qt::black) << QString("FastPin OCR test") << QString("copied");
            QTest::newRow(qPrintable("empty-" + state)) << visible << QColor(Qt::white) << QString("Original clipboard") << QString("no text");
            QTest::newRow(qPrintable("error-" + state)) << visible << QColor(Qt::red) << QString("Original clipboard") << QString("Test OCR failure");
        }
    }

    void ocrCompletionPreservesPanelVisibility()
    {
        QFETCH(bool, visible); QFETCH(QColor, color);
        QFETCH(QString, clipboard); QFETCH(QString, feedback);
        Controller panel; panel.setVisible(visible);
        QApplication::clipboard()->setText("Original clipboard");
        QImage image(8, 8, QImage::Format_RGB32); image.fill(color);
        panel.ocr(image);
        QTRY_VERIFY(!panel.ocrBusy);
        QCOMPARE(QApplication::clipboard()->text(), clipboard);
        QVERIFY2(panel.status->text().contains(feedback), qPrintable(panel.status->text()));
        QCOMPARE(panel.isVisible(), visible);
    }

    void backgroundFailuresKeepPanelHidden_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("feedback");
        QTest::newRow("empty-clipboard") << QString("paste") << QString("does not contain an image");
        QTest::newRow("missing-history") << QString("replay") << QString("No saved image");
        QTest::newRow("repeated-ocr") << QString("ocr") << QString("already running");
        QTest::newRow("repeated-quick-capture") << QString("capture") << QString("already running");
    }

    void backgroundFailuresKeepPanelHidden()
    {
        QFETCH(QString, action); QFETCH(QString, feedback);
        Controller panel;
        QApplication::clipboard()->setText("Original clipboard");
        if (action == "paste") panel.paste();
        else if (action == "replay") { panel.history.enabled = false; panel.replay(); }
        else {
            QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::black);
            panel.ocr(image);
            if (action == "ocr") panel.ocr(image);
            else panel.capture(CaptureOverlay::Mode::CopyText);
        }
        const QString message = panel.status->text();
        const bool stayedHidden = !panel.isVisible();
        QTRY_VERIFY(!panel.ocrBusy);
        QVERIFY2(message.contains(feedback), qPrintable(message));
        QVERIFY(stayedHidden);
        QVERIFY(!panel.isVisible());
    }

    void pinnedImageOcrKeepsPanelHidden()
    {
        Controller panel; panel.history.enabled = false;
        QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::black);
        panel.pin(image, {});
        auto *pin = panel.pins.first();
        QTest::keyClick(pin, Qt::Key_O);
        QTRY_VERIFY(!panel.ocrBusy);
        const bool stayedHidden = !panel.isVisible();
        pin->close();
        QCOMPARE(QApplication::clipboard()->text(), QString("FastPin OCR test"));
        QVERIFY(stayedHidden);
        QVERIFY(!panel.isVisible());
    }

    void translationPinsCardAndKeepsPanelHidden()
    {
        Controller panel;
        QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::green);
        panel.ocr(image, true);
        TranslationCard *card = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == "translationCard") card = qobject_cast<TranslationCard *>(widget);
        QVERIFY(card); QVERIFY(card->isVisible()); QVERIFY(!panel.isVisible());
        QVERIFY(panel.ocrBusy);
        QVERIFY(card->findChild<QPlainTextEdit *>("translationDefinition")->toPlainText().contains("Recognizing"));
        QVERIFY(!card->findChild<QToolButton *>("pronounce")->isEnabled());
        QVERIFY(!card->findChild<QToolButton *>("copyDefinition")->isEnabled());
        QVERIFY(!card->findChild<QToolButton *>("translateOnline")->isEnabled());
        QTRY_VERIFY(!panel.ocrBusy);
        QVERIFY(card->findChild<QPlainTextEdit *>("translationDefinition")->toPlainText().contains(QString::fromUtf8("銀行")));
        QVERIFY(card->findChild<QToolButton *>("pronounce")->isEnabled());
        QVERIFY(card->findChild<QToolButton *>("copyDefinition")->isEnabled());
        card->close();
        QCOMPARE(QApplication::clipboard()->text(), QString("bank")); QVERIFY(!panel.isVisible());
    }

    void pendingTranslationShowsOcrFailure_data()
    {
        QTest::addColumn<QColor>("color"); QTest::addColumn<QString>("message");
        QTest::newRow("error") << QColor(Qt::red) << QString("Test OCR failure");
        QTest::newRow("empty") << QColor(Qt::white) << QString("No text found");
    }

    void pendingTranslationShowsOcrFailure()
    {
        QFETCH(QColor, color); QFETCH(QString, message);
        Controller panel;
        QApplication::clipboard()->setText("Original clipboard");
        QImage image(8, 8, QImage::Format_RGB32); image.fill(color);
        panel.ocr(image, true);
        TranslationCard *card = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == "translationCard" && widget->isVisible()) card = qobject_cast<TranslationCard *>(widget);
        QVERIFY(card); QVERIFY(panel.ocrBusy);
        QTRY_VERIFY(!panel.ocrBusy);
        QVERIFY(card->isVisible()); QVERIFY(!panel.isVisible());
        QVERIFY(card->findChild<QPlainTextEdit *>("translationDefinition")->toPlainText().contains(message));
        QVERIFY(!card->findChild<QToolButton *>("pronounce")->isEnabled());
        QVERIFY(!card->findChild<QToolButton *>("copyDefinition")->isEnabled());
        QCOMPARE(QApplication::clipboard()->text(), QString("Original clipboard"));
        card->close();
    }

    void closingPendingTranslationDoesNotReopenIt()
    {
        Controller panel;
        QImage image(8, 8, QImage::Format_RGB32); image.fill(Qt::green);
        panel.ocr(image, true);
        QPointer<TranslationCard> card;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == "translationCard" && widget->isVisible()) card = qobject_cast<TranslationCard *>(widget);
        QVERIFY(card); QVERIFY(panel.ocrBusy);
        card->close();
        QTRY_VERIFY(!panel.ocrBusy);
        QTRY_VERIFY(card.isNull());
        for (auto *widget : QApplication::topLevelWidgets())
            QVERIFY(widget->objectName() != "translationCard" || !widget->isVisible());
        QVERIFY(!panel.isVisible());
    }
};
QTEST_MAIN(ControllerTests)
#include "controller_tests.moc"
