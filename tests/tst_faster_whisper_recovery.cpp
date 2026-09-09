#include "stt/FasterWhisperBackend.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class FasterWhisperRecoveryTest : public QObject {
    Q_OBJECT
private slots:
    void brokenStreamCannotSupplyNextTranscript_data() {
        QTest::addColumn<float>("mode");
        QTest::addColumn<bool>("failRestart");
        QTest::newRow("delayed-response") << -1.0f << false;
        QTest::newRow("trickling-output") << -2.0f << false;
        QTest::newRow("malformed-json") << -3.0f << false;
        QTest::newRow("transient-restart-failure") << -1.0f << true;
    }
    void brokenStreamCannotSupplyNextTranscript() {
        QFETCH(float, mode);
        QFETCH(bool, failRestart);
        QTemporaryDir dir;
        if (failRestart) {
            QFile flag(dir.filePath("fail-restart"));
            QVERIFY(flag.open(QIODevice::WriteOnly));
        }
        const auto path = dir.filePath("sidecar.py");
        QFile script(path);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"PY(import sys, json, struct, time, pathlib
folder = pathlib.Path(__file__).parent
if (folder / 'fail-restart').exists():
    counter = folder / 'launch-count'
    count = int(counter.read_text()) + 1 if counter.exists() else 1
    counter.write_text(str(count))
    if count == 2:
        print(json.dumps({'error':'temporary startup failure'}), flush=True)
        sys.exit(1)
print(json.dumps({'status':'ready','device':'fake'}), flush=True)
while True:
    line = sys.stdin.buffer.readline()
    if not line:
        break
    request = json.loads(line)
    raw = sys.stdin.buffer.read(request['n_bytes'])
    mode = struct.unpack('f', raw[:4])[0]
    if mode == -1:
        time.sleep(0.4)
        print(json.dumps({'text':'stale response'}), flush=True)
    elif mode == -2:
        for char in '{"text":"stale response"}':
            sys.stdout.write(char)
            sys.stdout.flush()
            time.sleep(0.03)
        print('', flush=True)
    elif mode == -3:
        print('not valid JSON', flush=True)
    else:
        print(json.dumps({'text':'fresh response'}), flush=True)
)PY");
        script.close();
        FasterWhisperBackend backend(path, 120, QStringLiteral(SCRYBE_TEST_PYTHON));
        QString device, error, text;
        QVERIFY2(backend.load("fake", "cpu", &device, &error), qPrintable(error));
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(!backend.transcribe({mode}, "auto", &text, &error));
        QVERIFY(!error.isEmpty());
        // Trickle bytes arrive every 30ms for ~700ms; the timeout stays total.
        QVERIFY2(elapsed.elapsed() < 600, "partial output reset the request deadline");
        if (failRestart) {
            QVERIFY(!backend.transcribe({1.0f}, "auto", &text, &error));
            QVERIFY(error.contains("temporary startup failure"));
        }
        QVERIFY2(backend.transcribe({1.0f}, "auto", &text, &error), qPrintable(error));
        QCOMPARE(text, QStringLiteral("fresh response"));
    }
};
QTEST_GUILESS_MAIN(FasterWhisperRecoveryTest)
#include "tst_faster_whisper_recovery.moc"
