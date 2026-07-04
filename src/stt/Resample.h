#pragma once

#include <QVector>

#include <vector>

namespace scrybe {

// Linear resample to 16 kHz mono float (Whisper's required input rate).
// A source rate of 16000 (or an invalid rate ≤ 0) passes through unchanged.
std::vector<float> resampleTo16k(const QVector<float> &pcm, int srcRate);

} // namespace scrybe
