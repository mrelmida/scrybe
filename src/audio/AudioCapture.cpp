#include "AudioCapture.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QtGlobal>

#include <cmath>
#include <cstdint>

namespace {
constexpr qreal kGain = 9.0;    // maps typical speech RMS toward full-scale
constexpr qreal kDecay = 0.80;  // peak-hold decay so bars fall smoothly
} // namespace

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Float);
}

AudioCapture::~AudioCapture() { stop(); }

void AudioCapture::start() {
    if (m_source)
        stop();

    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) {
        emit error(QStringLiteral("No audio input device found."));
        return;
    }

    QAudioFormat fmt = m_format;
    if (!dev.isFormatSupported(fmt)) {
        fmt = dev.preferredFormat();   // fall back to whatever the device offers
    }
    m_activeFormat = fmt;

    m_source = new QAudioSource(dev, fmt, this);
    m_pcm.clear();
    m_displayLevel = 0.0;

    m_io = m_source->start();   // pull mode: read from the returned QIODevice
    if (!m_io) {
        emit error(QStringLiteral("Failed to start microphone capture."));
        m_source->deleteLater();
        m_source = nullptr;
        return;
    }
    connect(m_io, &QIODevice::readyRead, this, &AudioCapture::onReadyRead);
}

void AudioCapture::stop() {
    if (!m_source)
        return;
    if (m_io)
        disconnect(m_io, nullptr, this, nullptr);
    m_source->stop();
    m_source->deleteLater();
    m_source = nullptr;
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
    double sumSq = 0.0;
    qint64 frames = 0;

    // Convert to mono float, accumulate PCM, and measure RMS. Handle the two
    // formats we actually see from PipeWire (Float / Int16); others → level only.
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

    const qreal rms = std::sqrt(sumSq / double(frames));
    const qreal target = qBound(0.0, rms * kGain, 1.0);
    m_displayLevel = qMax(target, m_displayLevel * kDecay);
    emit levelChanged(m_displayLevel);
}
