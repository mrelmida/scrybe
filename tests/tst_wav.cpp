#include "util/Wav.h"

#include <QTemporaryFile>
#include <QtTest>

#include <cmath>
#include <vector>

using scrybe::decodeWav;
using scrybe::encodeWav16;
using scrybe::loadWavFile;

class TestWav : public QObject {
    Q_OBJECT
private slots:
    void roundtrip() {
        std::vector<float> pcm(1600);
        for (size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = 0.5f * std::sin(2.0 * M_PI * 440.0 * i / 16000.0);

        const QByteArray wav = encodeWav16(pcm, 16000);
        int rate = 0;
        const QVector<float> back = decodeWav(wav, &rate);

        QCOMPARE(rate, 16000);
        QCOMPARE(size_t(back.size()), pcm.size());
        for (int i = 0; i < back.size(); ++i)
            QVERIFY(qAbs(back[i] - pcm[i]) < 1.5f / 32767.0f);   // 16-bit quantization
    }

    void headerCarriesSampleRate() {
        const QByteArray wav = encodeWav16({0.0f, 0.1f}, 48000);
        int rate = 0;
        decodeWav(wav, &rate);
        QCOMPARE(rate, 48000);
    }

    void clipsOutOfRangeSamples() {
        const QByteArray wav = encodeWav16({2.0f, -2.0f}, 16000);
        const QVector<float> back = decodeWav(wav, nullptr);
        QCOMPARE(back.size(), 2);
        QVERIFY(back[0] > 0.99f);
        QVERIFY(back[1] < -0.99f);
    }

    void rejectsGarbage() {
        QVERIFY(decodeWav(QByteArray(), nullptr).isEmpty());
        QVERIFY(decodeWav(QByteArray("not a wav file at all, definitely not 44 bytes!!"),
                          nullptr).isEmpty());
        // Valid RIFF magic but no data chunk.
        QByteArray truncated = encodeWav16({0.1f}, 16000).left(20);
        QVERIFY(decodeWav(truncated, nullptr).isEmpty());
    }

    void loadsFromFile() {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write(encodeWav16({0.25f, -0.25f, 0.0f}, 16000));
        f.close();

        int rate = 0;
        const QVector<float> pcm = loadWavFile(f.fileName(), &rate);
        QCOMPARE(rate, 16000);
        QCOMPARE(pcm.size(), 3);
        QVERIFY(loadWavFile(QStringLiteral("/nonexistent/file.wav"), nullptr).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestWav)
#include "tst_wav.moc"
