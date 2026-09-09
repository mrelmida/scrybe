#include "AudioCapture.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QSettings>
#include <QtGlobal>

#include <cmath>
#include <cstdint>

namespace {
constexpr qreal kDecay = 0.80;  // peak-hold decay so bars fall smoothly
constexpr int kMaxSeconds = 600; // safety cap: 10 min ≈ 37 MB of float PCM
} // namespace

AudioCapture::AudioCapture(QObject *parent, DeviceLookup lookup)
    : QObject(parent), m_deviceLookup(std::move(lookup)) {
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Float);
}

AudioCapture::~AudioCapture() { stop(); }

void AudioCapture::resetCapture() {
    m_pcm.clear();
    m_activeFormat = QAudioFormat();
    m_vad.reset(16000);
    m_displayLevel = 0.0;
    m_limitNotified = false;
    emit levelChanged(0.0);
}

void AudioCapture::captureFailed(const QString &message) {
    stop();
    resetCapture();
    emit error(message);
}

bool AudioCapture::supportedFormat(const QAudioFormat &format) {
    return format.sampleRate() > 0 && format.channelCount() > 0 &&
           (format.sampleFormat() == QAudioFormat::Float ||
            format.sampleFormat() == QAudioFormat::Int16);
}

bool AudioCapture::start() {
    stop();
    // Clear old speech before discovery: a missing device must never leave a
    // previous recording available to the final transcription path.
    resetCapture();

    // audio/device holds the base64 QAudioDevice::id() of the user's pick, or
    // is empty for "system default" — re-read here so a settings change takes
    // effect on the next start() without restarting the app.
    QSettings s;
    const QString wantId = s.value(QStringLiteral("audio/device")).toString();
    QAudioDevice dev;
    if (m_deviceLookup) {
        dev = m_deviceLookup(wantId);
    } else if (!wantId.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioInputs()) {
            if (QString::fromLatin1(d.id().toBase64()) == wantId) {
                dev = d;
                break;
            }
        }
    }
    if (dev.isNull() && !m_deviceLookup && wantId.isEmpty())
        dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) {
        emit error(tr("The selected microphone is unavailable. Connect it or choose another microphone in Settings."));
        return false;
    }
    m_gain = s.value(QStringLiteral("audio/gain"), 9.0).toDouble();

    QAudioFormat fmt = m_format;
    if (!dev.isFormatSupported(fmt)) {
        fmt = dev.preferredFormat();   // fall back to whatever the device offers
    }
    if (!supportedFormat(fmt)) {
        emit error(tr("This microphone does not provide a supported Float or Int16 audio format. Choose another microphone."));
        return false;
    }
    m_activeFormat = fmt;

    m_source = new QAudioSource(dev, fmt, this);
    m_vad.reset(fmt.sampleRate());
    m_displayLevel = 0.0;
    m_limitNotified = false;

    auto *source = m_source;
    m_io = source->start();   // pull mode: read from the returned QIODevice
    if (!m_io || source->error() != QAudio::NoError || source->state() == QAudio::StoppedState) {
        captureFailed(tr("Could not start microphone capture. Check microphone permissions and whether the device is connected."));
        return false;
    }
    connect(source, &QAudioSource::stateChanged, this, [this, source](QAudio::State state) {
        if (source != m_source) return;
        if (source->error() != QAudio::NoError || state == QAudio::StoppedState) {
            captureFailed(tr("Microphone capture stopped unexpectedly. Reconnect the microphone and try again."));
        }
    });
    connect(m_io, &QIODevice::readyRead, this, &AudioCapture::onReadyRead);
    return true;
}

void AudioCapture::setGain(qreal gain) { m_gain = gain; }

void AudioCapture::stop() {
    if (!m_source)
        return;
    if (m_io)
        disconnect(m_io, nullptr, this, nullptr);
    auto *source = m_source;
    m_source = nullptr;
    disconnect(source, nullptr, this, nullptr);
    source->stop();
    source->deleteLater();
    m_io = nullptr;
    m_displayLevel = 0.0;
    emit levelChanged(0.0);
}

void AudioCapture::onReadyRead() {
    if (!m_io)
        return;
    const QByteArray data = m_io->readAll();
    if (data.isEmpty())
        return;

    const int channels = qMax(1, m_activeFormat.channelCount());
    const qsizetype before = m_pcm.size();
    double sumSq = 0.0;
    qint64 frames = 0;

    // Convert to mono float, accumulate PCM, and measure RMS. Handle the two
    // formats validated at startup (Float / Int16).
    switch (m_activeFormat.sampleFormat()) {
    case QAudioFormat::Float: {
        const auto *s = reinterpret_cast<const float *>(data.constData());
        const qint64 n = data.size() / int(sizeof(float));
        for (qint64 i = 0; i + channels <= n; i += channels) {
            float mono = 0.f;
            for (int c = 0; c < channels; ++c) mono += s[i + c];
            mono /= channels;
            m_pcm.push_back(mono);
            sumSq += double(mono) * mono;
            ++frames;
        }
        break;
    }
    case QAudioFormat::Int16: {
        const auto *s = reinterpret_cast<const int16_t *>(data.constData());
        const qint64 n = data.size() / int(sizeof(int16_t));
        for (qint64 i = 0; i + channels <= n; i += channels) {
            float mono = 0.f;
            for (int c = 0; c < channels; ++c) mono += s[i + c] / 32768.f;
            mono /= channels;
            m_pcm.push_back(mono);
            sumSq += double(mono) * mono;
            ++frames;
        }
        break;
    }
    default:
        // Unsupported format for PCM extraction; skip level update this chunk.
        return;
    }

    if (frames == 0)
        return;

    m_vad.process(m_pcm.constData() + before, m_pcm.size() - before);

    // Don't grow without bound if a recording is left running; the buffer is
    // truncated at the cap and the owner is told once so it can finalize.
    const qint64 maxSamples = qint64(kMaxSeconds) * qMax(1, m_activeFormat.sampleRate());
    if (m_pcm.size() > maxSamples) {
        m_pcm.resize(maxSamples);
        if (!m_limitNotified) {
            m_limitNotified = true;
            emit limitReached();
            if (!m_source) return; // the owner may stop capture synchronously
        }
    }

    const qreal rms = std::sqrt(sumSq / double(frames));
    const qreal target = qBound(0.0, rms * m_gain, 1.0);
    m_displayLevel = qMax(target, m_displayLevel * kDecay);
    emit levelChanged(m_displayLevel);
}
