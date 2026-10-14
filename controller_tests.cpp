// Exercise the actual panel's shortcut lifecycle without a second UI implementation.
#define main fastpinApplicationMain
#include "main.cpp"
#undef main
#include <QFocusEvent>
#include <QTemporaryDir>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
};
QTEST_MAIN(ControllerTests)
#include "controller_tests.moc"
