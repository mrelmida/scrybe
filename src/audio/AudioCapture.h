#pragma once

#include "util/Vad.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QVector>

class QAudioSource;
class QIODevice;

// Microphone capture via Qt Multimedia. Emits a smoothed 0..1 level for the
// visualizer and accumulates mono float PCM for the STT engine (used in M3).
class AudioCapture : public QObject {
    Q_OBJECT
public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    bool isActive() const { return m_source != nullptr; }

    // Captured mono float samples since the last start() (M3 will consume these).
    const QVector<float> &pcm() const { return m_pcm; }
    int sampleRate() const { return m_activeFormat.sampleRate(); }

    // Voice activity over the current/most recent capture (state persists
    // after stop() so the finalize path can still consult it).
    bool hasSpeech() const { return m_vad.hasSpeech(); }
    double silenceMs() const { return m_vad.silenceMs(); }

public slots:
    void start();
    void stop();
    void setGain(qreal gain);   // applies live to a running capture

signals:
    void levelChanged(qreal level);   // 0..1, smoothed for display
    void error(const QString &message);
    void limitReached();              // capture hit the safety cap (once per start)

private:
    void onReadyRead();

    QAudioFormat m_format;         // requested (16 kHz mono float)
    QAudioFormat m_activeFormat;   // what the device actually gave us
    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    QVector<float> m_pcm;
    scrybe::Vad m_vad;
    qreal m_displayLevel = 0.0;
    qreal m_gain = 9.0;            // audio/gain setting, re-read every start()
    bool m_limitNotified = false;
};
