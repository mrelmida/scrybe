#include "core/Controller.h"
#include "core/ModelDownloader.h"
#include "audio/AudioCapture.h"
#include <QTimer>
#include "stt/SttEngine.h"
#include "stt/SttBackend.h"
#include "llm/LlmBeautifier.h"
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

std::unique_ptr<ISttBackend> makeSttBackend(const QString &) { return {}; }

class FakeDownloader : public ModelDownloader {
public:
    using ModelDownloader::ModelDownloader;
    int requests = 0;
    quint64 requestedGeneration = 0;
    void ensure(const QString &, const QString &, quint64 generation) override {
        ++requests;
        requestedGeneration = generation;
    }
    void cancel() override {}
};

class AudioCaptureTest {
public:
    static void seed(AudioCapture *capture) {
        capture->m_pcm = QVector<float>(16000, 0.5f);
        capture->m_activeFormat.setSampleRate(16000);
    }
};

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
    void init() { QSettings().clear(); }
    void microphoneStartupFailureRestoresIdle() {
        auto *capture = new AudioCapture(nullptr, [](const QString &) { return QAudioDevice(); });
        Controller controller(nullptr, capture);
        QSignalSpy show(&controller, &Controller::requestShow);
        QSignalSpy notify(&controller, &Controller::notify);
        QSignalSpy loads(controller.m_stt, &SttEngine::requestLoad);
        controller.startListening();
        QCOMPARE(controller.state(), Controller::Idle);
        QCOMPARE(show.size(), 0);
        QCOMPARE(notify.size(), 1);
        QCOMPARE(loads.size(), 0);
        QVERIFY(!controller.m_partialTimer->isActive());
        QVERIFY(!controller.m_autoSendTimer->isActive());
        QVERIFY(!controller.m_sttBusy);
    }
    void captureFailureClearsTimersPendingAudioAndPreview() {
        Controller controller;
        controller.m_session = 5;
        controller.m_pendingFinal = {0.5f};
        controller.m_pendingSession = 5;
        controller.m_sttBusy = true;
        controller.m_sttRequest = 4;
        controller.setState(Controller::Listening);
        controller.m_partialTimer->start();
        controller.m_autoSendTimer->start();
        emit controller.m_audio->error("disconnected");
        QCOMPARE(controller.state(), Controller::Idle);
        QVERIFY(controller.m_pendingFinal.isEmpty());
        QVERIFY(!controller.m_sttBusy);
        QVERIFY(!controller.m_partialTimer->isActive());
        QVERIFY(!controller.m_autoSendTimer->isActive());
        controller.m_micPreviewActive = true;
        emit controller.m_audio->error("preview disconnected");
        QVERIFY(!controller.micPreviewActive());
    }
    void finalWaitsForCurrentModelAndCancelCanReuseLoad() {
        QSettings().setValue("stt/vad", false);
        Controller controller;
        controller.m_modelGeneration = 4;
        controller.m_modelLoading = true;
        controller.m_session = 7;
        controller.m_stt->setSession(7);
        controller.m_stt->setModelGeneration(4);
        controller.setState(Controller::Listening);
        AudioCaptureTest::seed(controller.m_audio);
        QSignalSpy requests(controller.m_stt, &SttEngine::requestTranscribe);
        controller.requestPartial();
        QCOMPARE(requests.size(), 0);
        controller.stopListening();
        QCOMPARE(controller.state(), Controller::Transcribing);
        QCOMPARE(requests.size(), 0);
        QVERIFY(!controller.m_pendingFinal.isEmpty());
        emit controller.m_stt->ready("stale", 3);
        emit controller.m_stt->error("stale load failed", 3);
        QCOMPARE(controller.state(), Controller::Transcribing);
        QCOMPARE(requests.size(), 0);
        controller.cancel();
        QVERIFY(controller.m_pendingFinal.isEmpty());
        QVERIFY(controller.m_modelLoading); // reusable across recording sessions
        emit controller.m_stt->ready("current", 4);
        QVERIFY(controller.modelReady());
        QCOMPARE(requests.size(), 0); // cancelled audio never transcribes
        controller.setState(Controller::Listening);
        AudioCaptureTest::seed(controller.m_audio);
        controller.stopListening();
        QCOMPARE(requests.size(), 1);
        QVERIFY(requests[0][3].toBool());
        QCOMPARE(requests[0][4].toULongLong(), controller.m_session);
    }
    void downloadCompletionCannotReloadOldSelection() {
        QSettings().setValue("stt/backend", "openvino");
        auto *downloader = new FakeDownloader;
        Controller controller(nullptr, nullptr, downloader);
        controller.ensureModelLoaded();
        QCOMPARE(downloader->requests, 1);
        const auto old = downloader->requestedGeneration;
        controller.setModel("medium");
        controller.ensureModelLoaded();
        QCOMPARE(downloader->requests, 2);
        const auto current = downloader->requestedGeneration;
        QVERIFY(current != old);
        QSignalSpy loads(controller.m_stt, &SttEngine::requestLoad);
        emit downloader->finished(true, {}, old);
        emit downloader->finished(false, "old failure", old);
        QCOMPARE(loads.size(), 0);
        QVERIFY(controller.m_modelLoading);
        emit downloader->finished(true, {}, current);
        QCOMPARE(loads.size(), 1);
        QCOMPARE(loads[0][3].toULongLong(), current);
        QVERIFY(loads[0][1].toString().contains("medium"));
    }
    void downloadFailureFinishesPendingRecording() {
        auto *downloader = new FakeDownloader;
        Controller controller(nullptr, nullptr, downloader);
        controller.m_modelLoading = true;
        controller.m_pendingFinal = {0.5f};
        controller.m_pendingSession = controller.m_session;
        controller.setState(Controller::Transcribing);
        emit downloader->finished(false, "download failed", controller.m_modelGeneration);
        QCOMPARE(controller.state(), Controller::Idle);
        QVERIFY(controller.m_pendingFinal.isEmpty());
        QVERIFY(!controller.m_modelLoading);
        QVERIFY(!controller.m_sttBusy);
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
