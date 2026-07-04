#include "stt/Resample.h"

#include <QtTest>

#include <cmath>

using scrybe::resampleTo16k;

class TestResample : public QObject {
    Q_OBJECT
private slots:
    void passthroughAt16k() {
        const QVector<float> in{0.1f, -0.2f, 0.3f};
        const auto out = resampleTo16k(in, 16000);
        QCOMPARE(int(out.size()), 3);
        QCOMPARE(out[1], -0.2f);
    }

    void passthroughOnInvalidRate() {
        const QVector<float> in{0.5f, 0.5f};
        QCOMPARE(int(resampleTo16k(in, 0).size()), 2);
        QCOMPARE(int(resampleTo16k(in, -8000).size()), 2);
    }

    void emptyInput() {
        QVERIFY(resampleTo16k({}, 48000).empty());
    }

    void downsamples48kToThird() {
        QVector<float> in(48000, 0.0f);   // 1 s @ 48 kHz
        const auto out = resampleTo16k(in, 48000);
        QCOMPARE(qint64(out.size()), qint64(16000));
    }

    void preservesConstantSignal() {
        QVector<float> in(44100, 0.25f);  // 1 s @ 44.1 kHz
        const auto out = resampleTo16k(in, 44100);
        QVERIFY(std::llabs(qint64(out.size()) - 16000) <= 1);
        for (float v : out)
            QVERIFY(qAbs(v - 0.25f) < 1e-6f);
    }

    void preservesLowFrequencySine() {
        // A 100 Hz sine is far below Nyquist at both rates; linear interpolation
        // should track it closely.
        const int srcRate = 48000;
        QVector<float> in(srcRate);
        for (int i = 0; i < srcRate; ++i)
            in[i] = std::sin(2.0 * M_PI * 100.0 * i / srcRate);
        const auto out = resampleTo16k(in, srcRate);
        double maxErr = 0.0;
        for (size_t i = 0; i < out.size(); ++i) {
            const double expected = std::sin(2.0 * M_PI * 100.0 * i / 16000.0);
            maxErr = std::max(maxErr, std::abs(out[i] - expected));
        }
        QVERIFY2(maxErr < 0.01, qPrintable(QString::number(maxErr)));
    }
};

QTEST_APPLESS_MAIN(TestResample)
#include "tst_resample.moc"
