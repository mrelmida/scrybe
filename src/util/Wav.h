#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <vector>

namespace scrybe {

// Minimal WAV reader (PCM16 / float32, any channel count) → mono float samples.
// Returns an empty vector on malformed input. sampleRate receives the file's
// rate when non-null.
QVector<float> loadWavFile(const QString &path, int *sampleRate);

// Same, but from an in-memory blob (testable without a file).
QVector<float> decodeWav(const QByteArray &data, int *sampleRate);

// Encode mono float PCM as a 16-bit little-endian WAV blob.
QByteArray encodeWav16(const std::vector<float> &pcm, int sampleRate = 16000);

} // namespace scrybe
