#include "core/Controller.h"
#include "stt/SttEngine.h"
#include "stt/SttBackend.h"
#include "llm/LlmBeautifier.h"
#include "paste/Paster.h"
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

std::unique_ptr<ISttBackend> makeSttBackend(const QString &) { return {}; }

class ControllerSessionTest : public QObject {
    Q_OBJECT
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QVERIFY(config.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("scrybe-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("session-regressions"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, config.path());
    }
    void staleSttCannotClearBusyOrUpdateText() {
        Controller controller;
        controller.m_session = 2;
        controller.m_sttRequest = 20;
        controller.m_sttBusy = true;
        controller.setState(Controller::Listening);
        emit controller.m_stt->transcript("old", {}, false, 1, 10);
        emit controller.m_stt->transcriptionFailed("old failure", true, 1, 10);
        QVERIFY(controller.m_sttBusy);
        QCOMPARE(controller.transcript(), QString());
        QCOMPARE(controller.state(), Controller::Listening);
        emit controller.m_stt->transcript("current", {}, false, 2, 20);
        QVERIFY(!controller.m_sttBusy);
        QCOMPARE(controller.transcript(), QStringLiteral("current"));
    }
    void latePreviewCannotClearFinalBusy() {
        Controller controller;
        controller.m_session = 2;
        controller.m_sttRequest = 21;
        controller.m_sttBusy = true;
        controller.setState(Controller::Transcribing);
        emit controller.m_stt->transcript("preview", {}, false, 2, 20);
        emit controller.m_stt->transcriptionFailed("preview failed", false, 2, 20);
        QVERIFY(controller.m_sttBusy);
        QCOMPARE(controller.m_sttRequest, quint64(21));
        emit controller.m_stt->transcriptionFailed("final failed", true, 2, 21);
        QVERIFY(!controller.m_sttBusy);
        QCOMPARE(controller.state(), Controller::Idle);
    }
    void cancelledLlmCannotFinishNewBeautification() {
        Controller controller;
        controller.m_session = 1;
        controller.setState(Controller::Beautifying);
        controller.cancel();
        const auto current = controller.m_session;
        controller.setState(Controller::Beautifying);
        controller.setTranscript("new raw text");
        emit controller.m_llm->done("old formatted text", 1);
        emit controller.m_llm->failed("old failure", 1);
        QCOMPARE(controller.state(), Controller::Beautifying);
        QCOMPARE(controller.transcript(), QStringLiteral("new raw text"));
        emit controller.m_llm->done("new formatted text", current);
        QCOMPARE(controller.state(), Controller::Pasting);
        QCOMPARE(controller.transcript(), QStringLiteral("new formatted text"));
        controller.cancel(); // stop the real paste timer before it can run
    }
    void pasteCompletionMustMatchSessionAndTransaction() {
        Controller controller;
        controller.m_session = 2;
        controller.m_pasteSession = 2;
        controller.m_pasteRequest = 42;
        controller.setState(Controller::Pasting);
        emit controller.m_paster->finished(41, true);
        QCOMPARE(controller.state(), Controller::Pasting);
        controller.m_pasteSession = 1;
        emit controller.m_paster->finished(42, true);
        QCOMPARE(controller.state(), Controller::Pasting);
        controller.m_pasteSession = 2;
        emit controller.m_paster->finished(42, false);
        QCOMPARE(controller.state(), Controller::Idle);
        QCOMPARE(controller.m_pasteRequest, quint64(0));
    }
    void cancelAlsoCancelsDispatchedPaste() {
        Controller controller;
        controller.m_session = 1;
        controller.m_pasteSession = 1;
        controller.setState(Controller::Pasting);
        const auto request = controller.m_paster->paste(QStringLiteral("cancelled"));
        controller.m_pasteRequest = request;
        QSignalSpy completed(controller.m_paster, &Paster::finished);
        controller.cancel(); // before event loop can launch clipboard helper
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toULongLong(), request);
        QVERIFY(!completed.first().at(1).toBool());
        QCOMPARE(controller.m_pasteRequest, quint64(0));
        controller.setState(Controller::Listening);
        emit controller.m_paster->finished(request, true);
        QCOMPARE(controller.state(), Controller::Listening);
    }
    void cancelDuringPasteDelayPreservesNextRecording() {
        Controller controller;
        controller.m_session = 1;
        controller.setTranscript("cancelled text");
        controller.finish();
        QCOMPARE(controller.state(), Controller::Pasting);
        controller.cancel();
        // Simulate the next recording without accessing a physical microphone.
        controller.setState(Controller::Listening);
        controller.setTranscript("new recording");
        QSignalSpy states(&controller, &Controller::stateChanged);
        QTest::qWait(300);
        QCOMPARE(states.size(), 0);
        QCOMPARE(controller.state(), Controller::Listening);
        QCOMPARE(controller.transcript(), QStringLiteral("new recording"));
    }
};
QTEST_GUILESS_MAIN(ControllerSessionTest)
#include "tst_controller_session.moc"
