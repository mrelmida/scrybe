#include "llm/LlmBeautifier.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

class FakeReply : public QNetworkReply {
public:
    explicit FakeReply(QObject *parent) : QNetworkReply(parent) {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void abort() override {
        aborted = true;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        emit finished(); // specifically exercise synchronous abort completion
    }
    void complete(const QByteArray &body, bool fail = false) {
        data = body;
        if (fail)
            setError(ConnectionRefusedError, QStringLiteral("refused"));
        emit finished();
    }
    bool aborted = false;
protected:
    qint64 readData(char *out, qint64 max) override {
        const auto count = qMin(max, qint64(data.size()));
        if (!count) return -1;
        std::memcpy(out, data.constData(), size_t(count));
        data.remove(0, count);
        return count;
    }
private:
    QByteArray data;
};

class FakeManager : public QNetworkAccessManager {
public:
    QList<FakeReply *> replies;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &, QIODevice *) override {
        auto *reply = new FakeReply(this);
        replies.append(reply);
        return reply;
    }
};

class LlmSessionTest : public QObject {
    Q_OBJECT
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QVERIFY(config.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("scrybe-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("session-regressions"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, config.path());
    }
    void cancelledRequestCannotFinishNewRecording() {
        FakeManager manager;
        LlmBeautifier llm(nullptr, &manager);
        QSignalSpy done(&llm, &LlmBeautifier::done);
        QSignalSpy failed(&llm, &LlmBeautifier::failed);
        llm.beautify("old", "format", 1);
        const auto old = manager.replies.last();
        llm.cancel();
        QVERIFY(old->aborted);
        QCOMPARE(failed.size(), 0);
        llm.beautify("new", "format", 2);
        // Late backend signals are ignored even if they occur after abort.
        old->complete(R"({"response":"stale"})");
        QCOMPARE(done.size(), 0);
        manager.replies.last()->complete(R"({"response":"current"})");
        QCOMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toString(), QStringLiteral("current"));
        QCOMPARE(done[0][1].toULongLong(), quint64(2));
        QCOMPARE(failed.size(), 0);
    }
    void replacingRequestAbortsOnlyDictation() {
        FakeManager manager;
        LlmBeautifier llm(nullptr, &manager);
        QSignalSpy preview(&llm, &LlmBeautifier::previewDone);
        QSignalSpy failed(&llm, &LlmBeautifier::failed);
        llm.preview("sample", "format", 0.1);
        const auto settingsReply = manager.replies.last();
        llm.beautify("old", "format", 3);
        const auto old = manager.replies.last();
        llm.beautify("new", "format", 4);
        QVERIFY(old->aborted);
        QVERIFY(!settingsReply->aborted);
        manager.replies.last()->complete({}, true);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed[0][1].toULongLong(), quint64(4));
        settingsReply->complete(R"({"response":"preview"})");
        QCOMPARE(preview.size(), 1);
    }
};
QTEST_GUILESS_MAIN(LlmSessionTest)
#include "tst_llm_session.moc"
