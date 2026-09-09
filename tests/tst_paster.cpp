#include <QtTest>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "paste/Paster.h"

class PasterTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QByteArray oldFixture;
    QString program;
    void write(const QString &name, const QByteArray &data) {
        QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    QByteArray events() const {
        QFile file(dir.filePath(QStringLiteral("events"))); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
    Paster::Programs programs() const { return {program, program, 300, 250}; }
private slots:
    void initTestCase() {
        QVERIFY(dir.isValid());
        oldFixture = qgetenv("SCRYBE_PASTE_FIXTURE");
        qputenv("SCRYBE_PASTE_FIXTURE", dir.path().toUtf8());
        QCoreApplication::setOrganizationName(QStringLiteral("ScrybePasterTest"));
        QCoreApplication::setApplicationName(QStringLiteral("PasterTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        program = dir.filePath(QStringLiteral("fake-paste"));
        QVERIFY(QFile::copy(QStringLiteral(PASTE_FIXTURE), program));
        QVERIFY(QFile::setPermissions(program, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    }
    void cleanupTestCase() { qputenv("SCRYBE_PASTE_FIXTURE", oldFixture); }
    void init() {
        write(QStringLiteral("mode"), "normal");
        write(QStringLiteral("events"), {});
        QSettings settings; settings.clear();
        settings.setValue(QStringLiteral("paste/restoreDelayMs"), 200);
    }
    void successfulSerializedTransactions() {
        Paster paster(programs()); QSignalSpy done(&paster, &Paster::finished); QSignalSpy errors(&paster, &Paster::error);
        const auto a = paster.paste(QStringLiteral("first"));
        const auto b = paster.paste(QStringLiteral("second"));
        QCOMPARE(done.size(), 0);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done.at(0).at(0).toULongLong(), a); QVERIFY(done.at(0).at(1).toBool());
        QCOMPARE(done.at(1).at(0).toULongLong(), b); QVERIFY(done.at(1).at(1).toBool());
        QCOMPARE(errors.size(), 0);
        const auto log = events();
        QVERIFY(log.indexOf("READY first") < log.indexOf("KEY"));
        QVERIFY(log.indexOf("RESTORE first") < log.indexOf("START second"));
        QCOMPARE(paster.lastText(), QStringLiteral("second"));
    }
    void cancellationPreventsPendingKeys() {
        Paster paster(programs()); QSignalSpy done(&paster, &Paster::finished);
        paster.paste(QStringLiteral("first")); paster.paste(QStringLiteral("second"));
        QTRY_VERIFY(events().contains("START"));
        paster.cancel();
        QTRY_COMPARE(done.size(), 2);
        for (const auto &result : done) QVERIFY(!result.at(1).toBool());
        QVERIFY(!events().contains("KEY")); QVERIFY(!events().contains("START second"));
    }
    void cancellationReleasesKeysBeforeNextPaste() {
        write(QStringLiteral("mode"), "key-hang");
        Paster paster(programs()); QSignalSpy done(&paster, &Paster::finished);
        paster.paste(QStringLiteral("cancelled"));
        QTRY_VERIFY(events().contains("KEY"));
        paster.cancel();
        QTRY_COMPARE(done.size(), 1); QVERIFY(!done.at(0).at(1).toBool());
        QVERIFY(events().contains("RELEASE"));
        write(QStringLiteral("mode"), "normal");
        paster.paste(QStringLiteral("next"));
        QTRY_COMPARE(done.size(), 2); QVERIFY(done.at(1).at(1).toBool());
        QVERIFY(events().indexOf("RELEASE") < events().indexOf("START next"));
    }
    void missingInjectorCompletesOnce() {
        auto config = programs(); config.keyInjector = dir.filePath(QStringLiteral("missing"));
        Paster paster(config); QSignalSpy done(&paster, &Paster::finished);
        paster.paste(QStringLiteral("speech"));
        QTRY_COMPARE(done.size(), 1); QVERIFY(!done.at(0).at(1).toBool());
        QVERIFY(events().contains("RESTORE speech"));
        QTest::qWait(100); QCOMPARE(done.size(), 1);
    }
    void failures_data() {
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("prepare error") << QByteArray("prepare-fail");
        QTest::newRow("prepare timeout") << QByteArray("prepare-hang");
        QTest::newRow("injector error") << QByteArray("key-fail");
        QTest::newRow("injector timeout") << QByteArray("key-hang");
        QTest::newRow("clipboard replaced before key") << QByteArray("lost-immediate");
    }
    void failures() {
        QFETCH(QByteArray, mode); write(QStringLiteral("mode"), mode);
        Paster paster(programs()); QSignalSpy done(&paster, &Paster::finished); QSignalSpy errors(&paster, &Paster::error);
        paster.paste(QStringLiteral("recoverable"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 2000);
        QVERIFY(!done.at(0).at(1).toBool()); QVERIFY(!errors.isEmpty());
        QCOMPARE(paster.lastText(), QStringLiteral("recoverable"));
        if (mode.startsWith("prepare") || mode == "lost-immediate") QVERIFY(!events().contains("KEY"));
        if (mode.startsWith("key-")) QVERIFY(events().contains("RELEASE"));
        QTest::qWait(100); QCOMPARE(done.size(), 1);
    }
    void lostClipboardIsNeverRestored() {
        write(QStringLiteral("mode"), "lost-after-key");
        Paster paster(programs()); QSignalSpy done(&paster, &Paster::finished);
        paster.paste(QStringLiteral("speech"));
        QTRY_COMPARE(done.size(), 1); QVERIFY(done.at(0).at(1).toBool());
        QVERIFY(events().contains("USER_COPY")); QVERIFY(!events().contains("RESTORE"));
    }
    void missingHelperCompletesOnce() {
        auto config = programs(); config.clipboardHelper = dir.filePath(QStringLiteral("missing"));
        Paster paster(config); QSignalSpy done(&paster, &Paster::finished);
        paster.paste(QStringLiteral("speech"));
        QTRY_COMPARE(done.size(), 1); QVERIFY(!done.at(0).at(1).toBool());
        QTest::qWait(100); QCOMPARE(done.size(), 1); QVERIFY(!events().contains("KEY"));
    }
    void terminalShortcutAndKeepPolicy() {
        QSettings settings; settings.setValue(QStringLiteral("paste/restoreClipboard"), false);
        settings.setValue(QStringLiteral("paste/shortcut"), QStringLiteral("ctrl+shift+v"));
        Paster paster(programs()); QSignalSpy done(&paster, &Paster::finished);
        paster.paste(QStringLiteral("terminal"));
        QTRY_COMPARE(done.size(), 1); QVERIFY(done.at(0).at(1).toBool());
        QVERIFY(events().contains("KEY 29:1 42:1 47:1 47:0 42:0 29:0"));
        QVERIFY(events().contains("KEEP terminal"));
    }
};
QTEST_GUILESS_MAIN(PasterTest)
#include "tst_paster.moc"
