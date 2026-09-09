#include "Vad.h"

#include <algorithm>
#include <cmath>

namespace scrybe {

namespace {
constexpr double kFrameMs = 30.0;
constexpr int kSeedFrames = 5;         // floor-seeding window (~150 ms)
constexpr double kEnterRatio = 1.8;    // speech = sustained RMS above ratio × floor
constexpr double kMinThreshold = 0.006;  // never trigger below this RMS
constexpr double kFloorMin = 0.0015;   // floor can't collapse to digital silence
constexpr double kFloorMax = 0.05;     // a cough during seeding can't blind us
// A transient shorter than 90 ms can straddle at most 3 frames (partial-full-
// partial), so 4 consecutive loud frames are required to call it speech.
constexpr int kEnterFrames = 4;
constexpr int kHangoverFrames = 20;    // ~600 ms hold so word tails aren't cut
} // namespace

Vad::Vad(int sampleRate) { reset(sampleRate); }

void Vad::reset(int sampleRate) {
    m_rate = sampleRate > 0 ? sampleRate : 16000;
    m_frameSamples = std::max(1, int(m_rate * kFrameMs / 1000.0));
    m_sumSq = 0.0;
    m_filled = 0;
    m_floor = kFloorMax;
    m_seedFrames = kSeedFrames;
    m_speechRun = 0;
    m_hangover = 0;
    m_hasSpeech = false;
    m_framesTotal = 0;
    m_lastSpeechFrame = -1;
}

void Vad::process(const float *samples, int64_t count) {
    for (int64_t i = 0; i < count; ++i) {
        m_sumSq += double(samples[i]) * samples[i];
        if (++m_filled == m_frameSamples) {
            pushFrame(std::sqrt(m_sumSq / m_frameSamples));
            m_sumSq = 0.0;
            m_filled = 0;
        }
    }
}

void Vad::pushFrame(double rms) {
    if (m_seedFrames > 0) {
        // Seed the floor from the quietest early frame; the hotkey-to-speech
        // reaction gap means these are ambience.
        m_floor = std::clamp(std::min(m_floor, rms), kFloorMin, kFloorMax);
        --m_seedFrames;
    } else if (rms < m_floor) {
        m_floor = std::max(kFloorMin, rms + 0.75 * (m_floor - rms));  // fall fast
    } else if (m_speechRun == 0 && m_hangover == 0) {
        m_floor = std::min(kFloorMax, m_floor * 1.01);                // rise slowly
    }

    const double threshold = std::max(m_floor * kEnterRatio, kMinThreshold);
    if (rms > threshold) {
        if (++m_speechRun >= kEnterFrames) {
            m_hasSpeech = true;
            m_hangover = kHangoverFrames;
            m_lastSpeechFrame = m_framesTotal;
        }
    } else {
        m_speechRun = 0;
        if (m_hangover > 0)
            --m_hangover;
    }
    ++m_framesTotal;
}

double Vad::silenceMs() const {
    return double(m_framesTotal - 1 - m_lastSpeechFrame) * kFrameMs;
}

} // namespace scrybe
