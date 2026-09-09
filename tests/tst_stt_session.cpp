#include "stt/SttBackend.h"
#include "stt/SttEngine.h"

#include <QSemaphore>
#include <QAbstractEventDispatcher>
#include <QSignalSpy>
#include <QtTest>
#include <atomic>

// The engine's default factory is unused here; no real backends or models needed.
std::unique_ptr<ISttBackend> makeSttBackend(const QString &) { return {}; }

struct BackendState {
    QSemaphore entered;
    QSemaphore release;
    std::atomic<int> calls{0};
    bool fail = false;
    QAbstractEventDispatcher *dispatcher = nullptr;
};

class FakeBackend : public ISttBackend {
public:
    explicit FakeBackend(BackendState &state) : state(state) {}
    bool load(const QString &, const QString &, QString *device, QString *) override {
        state.dispatcher = QAbstractEventDispatcher::instance();
        *device = QStringLiteral("fake");
        return true;
    }
    void unload() override {}
    bool transcribe(const std::vector<float> &, const QString &, QString *text,
                    QString *error) override {
        const int call = ++state.calls;
        state.entered.release();
        // Bounded even on a failed assertion, so engine destruction cannot hang.
        if (!state.release.tryAcquire(1, 5000)) {
            *error = QStringLiteral("test backend timed out");
            return false;
        }
        *text = QString::number(call);
        *error = QStringLiteral("fake transcription failure");
        return !state.fail;
    }
    QString name() const override { return QStringLiteral("fake"); }
    BackendState &state;
};

class SttSessionTest : public QObject {
    Q_OBJECT
private slots:
    void cancelRestartSkipsOldQueuedWork_data() {
        QTest::addColumn<bool>("oldFails");
        QTest::newRow("old-success") << false;
        QTest::newRow("old-error") << true;
    }
    void cancelRestartSkipsOldQueuedWork() {
        QFETCH(bool, oldFails);
        BackendState state;
        state.fail = oldFails;
        SttEngine engine(nullptr, [&](const QString &) { return std::make_unique<FakeBackend>(state); });
        QSignalSpy ready(&engine, &SttEngine::ready);
        QSignalSpy results(&engine, &SttEngine::transcript);
        QSignalSpy errors(&engine, &SttEngine::transcriptionFailed);
        engine.load("fake", "fake", "fake");
        QTRY_COMPARE(ready.size(), 1);
        engine.setSession(1);
        engine.transcribe({0.5f}, 16000, "auto", false, 1);
        QVERIFY(state.entered.tryAcquire(1, 1000));
        engine.transcribe({0.5f}, 16000, "auto", false, 1);
        engine.transcribe({0.5f}, 16000, "auto", true, 1);
        engine.setSession(2);
        const auto current = engine.transcribe({0.5f}, 16000, "auto", true, 2);
        state.release.release(2);
        QTRY_COMPARE(results.size() + errors.size(), 1);
        QCOMPARE(state.calls.load(), 2); // only active old work and current request ran
        if (oldFails) {
            QCOMPARE(errors[0][2].toULongLong(), quint64(2));
            QCOMPARE(errors[0][3].toULongLong(), current);
        } else {
            QCOMPARE(results[0][0].toString(), QStringLiteral("2"));
            QCOMPARE(results[0][3].toULongLong(), quint64(2));
            QCOMPARE(results[0][4].toULongLong(), current);
        }
    }
    void alreadyQueuedResultIsInvalidated() {
        BackendState state;
        SttEngine engine(nullptr, [&](const QString &) { return std::make_unique<FakeBackend>(state); });
        QSignalSpy results(&engine, &SttEngine::transcript);
        QSignalSpy ready(&engine, &SttEngine::ready);
        engine.load("fake", "fake", "fake");
        QTRY_COMPARE(ready.size(), 1);
        engine.setSession(1);
        // Wait for a subsequent worker event without pumping GUI events: the
        // old result is now queued to the facade but has not been delivered.
        engine.transcribe({}, 16000, "auto", true, 1);
        QSemaphore barrier;
        QMetaObject::invokeMethod(state.dispatcher, [&]() { barrier.release(); }, Qt::QueuedConnection);
        QVERIFY(barrier.tryAcquire(1, 1000));
        engine.setSession(2);
        const auto request = engine.transcribe({}, 16000, "auto", true, 2);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][4].toULongLong(), request);
    }
    void previewFailureCompletesWithIdentity() {
        BackendState state;
        state.fail = true;
        state.release.release();
        SttEngine engine(nullptr, [&](const QString &) { return std::make_unique<FakeBackend>(state); });
        QSignalSpy errors(&engine, &SttEngine::transcriptionFailed);
        engine.load("fake", "fake", "fake");
        engine.setSession(9);
        const auto request = engine.transcribe({0.2f}, 16000, "auto", false, 9);
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors[0][1].toBool(), false);
        QCOMPARE(errors[0][2].toULongLong(), quint64(9));
        QCOMPARE(errors[0][3].toULongLong(), request);
        state.fail = false;
        state.release.release();
        QSignalSpy results(&engine, &SttEngine::transcript);
        engine.transcribe({0.2f}, 16000, "auto", false, 9);
        QTRY_COMPARE(results.size(), 1);
    }
};
QTEST_GUILESS_MAIN(SttSessionTest)
#include "tst_stt_session.moc"
