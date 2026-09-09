#include "audio/AudioCapture.h"
#include <QSignalSpy>
#include <QtTest>

class AudioCaptureTest : public QObject {
    Q_OBJECT
private slots:
    void missingDeviceClearsPreviousCaptureBeforeDiscovery() {
        bool resetBeforeLookup = false;
        AudioCapture capture(nullptr, [&](const QString &) {
            resetBeforeLookup = capture.sampleRate() == 0 && capture.pcm().isEmpty() &&
                                !capture.hasSpeech();
            return QAudioDevice();
        });
        capture.m_pcm = QVector<float>(16000, 0.5f);
        capture.m_activeFormat.setSampleRate(16000);
        capture.m_vad.process(capture.m_pcm.constData(), capture.m_pcm.size());
        QSignalSpy errors(&capture, &AudioCapture::error);
        QVERIFY(!capture.start());
        QVERIFY(resetBeforeLookup);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!capture.isActive());
        QVERIFY(capture.pcm().isEmpty());
        QCOMPARE(capture.sampleRate(), 0);
        QVERIFY(!capture.hasSpeech());
    }
    void runtimeFailureClearsSpeechAndReportsOnce() {
        AudioCapture capture;
        capture.m_pcm = {0.5f};
        capture.m_activeFormat.setSampleRate(48000);
        QSignalSpy errors(&capture, &AudioCapture::error);
        capture.captureFailed("microphone disconnected");
        QCOMPARE(errors.size(), 1);
        QVERIFY(capture.pcm().isEmpty());
        QCOMPARE(capture.sampleRate(), 0);
        QVERIFY(!capture.isActive());
    }
    void rejectsUnusableFormats() {
        QAudioFormat format;
        format.setSampleRate(16000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int32);
        QVERIFY(!AudioCapture::supportedFormat(format));
        format.setSampleFormat(QAudioFormat::Int16);
        QVERIFY(AudioCapture::supportedFormat(format));
        format.setSampleRate(0);
        QVERIFY(!AudioCapture::supportedFormat(format));
    }
};
QTEST_GUILESS_MAIN(AudioCaptureTest)
#include "tst_audio_capture.moc"
