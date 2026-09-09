#pragma once

#include <array>
#include <cstdint>

namespace scrybe {

// Lightweight energy-based voice activity detector with an adaptive noise
// floor. Feed it the same mono float PCM the capture accumulates; it answers
// "has any speech happened yet?" and "how long has it been silent since the
// last speech?" — used to skip transcribing pure silence (Whisper hallucinates
// fillers on it) and to auto-finalize hands-free dictation.
//
// A fixed RMS threshold is unreliable here (ambient ≈ 0.04 vs speech ≈ 0.1 on
// a typical laptop mic), so speech is instead anything sustained above
// kEnterRatio × the tracked noise floor. The floor seeds from the quietest early
// frame, falls quickly, and rises slowly. Recent frames are reconsidered when
// the floor falls, so speech starting with the hotkey can be recovered on its
// first pause. Energy alone cannot distinguish constant speech from constant
// noise, or speech from a sufficiently large change in background noise.
class Vad {
public:
    explicit Vad(int sampleRate = 16000);

    void reset(int sampleRate);
    void process(const float *samples, int64_t count);

    bool inSpeech() const { return m_hangover > 0; }      // includes hold-over
    bool hasSpeech() const { return m_hasSpeech; }        // any speech so far
    double noiseFloor() const { return m_floor; }

    // Milliseconds of silence since the last active speech frame. While
    // speaking this is ~0; before any speech it is the total elapsed time.
    double silenceMs() const;

private:
    void pushFrame(double rms);
    void reconsiderRecentFrames(double threshold);

    int m_rate = 16000;
    int m_frameSamples = 480;   // 30 ms at the current rate
    double m_sumSq = 0.0;       // partial frame accumulator across process() calls
    int m_filled = 0;
    double m_floor = 0.0;       // adaptive ambient RMS
    int m_seedFrames = 0;       // frames left in the floor-seeding phase
    int m_speechRun = 0;        // consecutive frames above threshold
    int m_hangover = 0;         // frames of speech-hold remaining
    bool m_hasSpeech = false;
    int64_t m_framesTotal = 0;
    int64_t m_lastSpeechFrame = -1;
    // Two seconds of RMS values, not audio. Bounded even for long recordings.
    std::array<double, 67> m_recent{};
    int m_recentCount = 0;
};

} // namespace scrybe
