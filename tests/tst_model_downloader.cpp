#include "core/ModelDownloader.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ModelDownloaderTest : public QObject {
    Q_OBJECT
    QString script(QTemporaryDir &dir, const QByteArray &body) {
        const QString path = dir.filePath(QStringLiteral("downloader"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write("#!/bin/sh\n" + body);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
private slots:
    void failedStartCompletesOnce() {
        QTemporaryDir dir;
        ModelDownloader downloader(nullptr, dir.filePath("missing-program"), 20);
        QSignalSpy done(&downloader, &ModelDownloader::finished);
        downloader.ensure(dir.filePath("model"), "fake-repo", 1);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done[0][0].toBool());
        QVERIFY(!done[0][1].toString().isEmpty());
        QCOMPARE(done[0][2].toULongLong(), quint64(1));
        QTest::qWait(50);
        QCOMPARE(done.size(), 1);
    }
    void timeoutAndCancelCompleteOnce() {
        QTemporaryDir dir;
        ModelDownloader downloader(nullptr, script(dir, "exec sleep 10\n"), 20);
        QSignalSpy done(&downloader, &ModelDownloader::finished);
        downloader.ensure(dir.filePath("model"), "fake-repo", 2);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done[0][0].toBool());
        QVERIFY(done[0][1].toString().contains("timed out"));
        downloader.cancel();
        QTest::qWait(50);
        QCOMPARE(done.size(), 1);
    }
    void replacementReportsBothGenerationsOnce() {
        QTemporaryDir dir;
        ModelDownloader downloader(nullptr, script(dir, "exec sleep 10\n"), 30);
        QSignalSpy done(&downloader, &ModelDownloader::finished);
        downloader.ensure(dir.filePath("old"), "fake-repo", 3);
        downloader.ensure(dir.filePath("new"), "fake-repo", 4);
        QCOMPARE(done.size(), 1);
        QCOMPARE(done[0][2].toULongLong(), quint64(3));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done[1][2].toULongLong(), quint64(4));
        QTest::qWait(50);
        QCOMPARE(done.size(), 2);
    }
    void successfulDownloadWritesMarker() {
        QTemporaryDir dir;
        ModelDownloader downloader(nullptr, script(dir,
            "mkdir -p \"$4\"\nprintf model > \"$4/model.xml\"\nprintf weights > \"$4/model.bin\"\n"));
        QSignalSpy done(&downloader, &ModelDownloader::finished);
        const auto model = dir.filePath("model");
        downloader.ensure(model, "fake-repo", 5);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done[0][0].toBool());
        QVERIFY(QFile::exists(QDir(model).filePath(".complete")));
        downloader.ensure(model, "fake-repo", 6);
        QCOMPARE(done.size(), 2);
        QVERIFY(done[1][0].toBool());
    }
    void interruptedXmlBinPairMustResumeDownload() {
        QTemporaryDir dir;
        const auto model = dir.filePath("model");
        QVERIFY(QDir().mkpath(model));
        for (const auto &name : {"model.xml", "model.bin"}) {
            QFile file(QDir(model).filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("partial");
        }
        ModelDownloader downloader(nullptr, dir.filePath("missing-program"));
        QSignalSpy done(&downloader, &ModelDownloader::finished);
        downloader.ensure(model, "fake-repo", 8);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done[0][0].toBool());
        QVERIFY(!QFile::exists(QDir(model).filePath(".complete")));
    }
    void emptySuccessfulProcessIsNotACompleteModel() {
        QTemporaryDir dir;
        ModelDownloader downloader(nullptr, script(dir, "mkdir -p \"$4\"\n"));
        QSignalSpy done(&downloader, &ModelDownloader::finished);
        const auto model = dir.filePath("model");
        downloader.ensure(model, "fake-repo", 7);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done[0][0].toBool());
        QVERIFY(!QFile::exists(QDir(model).filePath(".complete")));
    }
};
QTEST_GUILESS_MAIN(ModelDownloaderTest)
#include "tst_model_downloader.moc"
