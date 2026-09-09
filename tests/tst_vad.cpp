#include "util/Vad.h"

#include <QtTest>

#include <cmath>
#include <random>
#include <vector>

using scrybe::Vad;

namespace {

// Uniform noise at a target RMS (uniform [-a,a] has RMS a/√3). Deterministic
// seed so runs are reproducible.
std::vector<float> noise(int samples, double rms, unsigned seed = 42) {
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    const float amp = float(rms * std::sqrt(3.0));
    std::vector<float> out(samples);
    for (auto &v : out) v = d(rng) * amp;
    return out;
}

constexpr int kRate = 16000;
int secs(double s) { return int(s * kRate); }

} // namespace

class TestVad : public QObject {
    Q_OBJECT
private slots:
    void silenceIsNotSpeech() {
        Vad vad(kRate);
        const auto quiet = noise(secs(2.0), 0.002);
        vad.process(quiet.data(), qint64(quiet.size()));
        QVERIFY(!vad.hasSpeech());
        QVERIFY(!vad.inSpeech());
        QVERIFY(vad.silenceMs() >= 1900);
    }

    void typicalLaptopAmbienceIsNotSpeech() {
        // The problem case from the field: ambient RMS ≈ 0.04 hallucinated
        // "Thank you" — a fixed threshold below that would fire constantly.
        Vad vad(kRate);
        const auto ambient = noise(secs(3.0), 0.04);
        vad.process(ambient.data(), qint64(ambient.size()));
        QVERIFY(!vad.hasSpeech());
    }

    void speechOverAmbienceDetected() {
        // 1 s ambience (0.04), 0.8 s speech-level (0.12), 1.5 s ambience.
        Vad vad(kRate);
        const auto amb1 = noise(secs(1.0), 0.04, 1);
        const auto talk = noise(secs(0.8), 0.12, 2);
        const auto amb2 = noise(secs(1.5), 0.04, 3);

        vad.process(amb1.data(), qint64(amb1.size()));
        QVERIFY(!vad.hasSpeech());

        vad.process(talk.data(), qint64(talk.size()));
        QVERIFY(vad.hasSpeech());
        QVERIFY(vad.inSpeech());
        QVERIFY(vad.silenceMs() < 100);

        vad.process(amb2.data(), qint64(amb2.size()));
        QVERIFY(vad.hasSpeech());          // sticky
        QVERIFY(!vad.inSpeech());          // hangover expired
        // ≈1500 ms since the last speech frame (minus hangover slack).
        QVERIFY(vad.silenceMs() > 1200);
        QVERIFY(vad.silenceMs() < 1600);
    }

    void shortClickDoesNotTrigger() {
        // A 60 ms transient (door, keyboard) spans at most 3 frames even when
        // straddling boundaries — below the 4-frame entry gate.
        Vad vad(kRate);
        const auto amb1 = noise(secs(1.0), 0.02, 4);
        const auto click = noise(secs(0.06), 0.3, 5);
        const auto amb2 = noise(secs(1.0), 0.02, 6);
        vad.process(amb1.data(), qint64(amb1.size()));
        vad.process(click.data(), qint64(click.size()));
        vad.process(amb2.data(), qint64(amb2.size()));
        QVERIFY(!vad.hasSpeech());
    }

    void quietMicSpeechStillDetected() {
        // Very quiet setup: ambience 0.004, speech only 0.02 — well under any
        // sane fixed threshold, but 5× the floor.
        Vad vad(kRate);
        const auto amb = noise(secs(1.0), 0.004, 7);
        const auto talk = noise(secs(0.5), 0.02, 8);
        vad.process(amb.data(), qint64(amb.size()));
        vad.process(talk.data(), qint64(talk.size()));
        QVERIFY(vad.hasSpeech());
    }

    void chunkBoundariesDoNotMatter() {
        // Same signal fed in awkward chunk sizes gives the same verdict.
        Vad a(kRate), b(kRate);
        auto sig = noise(secs(1.0), 0.04, 9);
        const auto talk = noise(secs(0.5), 0.15, 10);
        sig.insert(sig.end(), talk.begin(), talk.end());

        a.process(sig.data(), qint64(sig.size()));
        for (size_t i = 0; i < sig.size();) {
            const size_t n = std::min<size_t>(97, sig.size() - i);   // odd chunks
            b.process(sig.data() + i, qint64(n));
            i += n;
        }
        QCOMPARE(a.hasSpeech(), b.hasSpeech());
        QCOMPARE(a.silenceMs(), b.silenceMs());
    }

    void resetClearsState() {
        Vad vad(kRate);
        const auto talk = noise(secs(1.0), 0.2, 11);
        vad.process(talk.data(), qint64(talk.size()));
        // (Loud from t=0 caps the seeded floor at its max, so this triggers.)
        QVERIFY(vad.hasSpeech());
        vad.reset(kRate);
        QVERIFY(!vad.hasSpeech());
        QVERIFY(!vad.inSpeech());
    }
};

QTEST_APPLESS_MAIN(TestVad)
#include "tst_vad.moc"
