#include "util/Version.h"

#include <QtTest>

using scrybe::isNewerVersion;
using scrybe::parseVersion;

class TestVersion : public QObject {
    Q_OBJECT
private slots:
    void parse() {
        QCOMPARE(parseVersion("1.4.10"), (QVector<int>{1, 4, 10}));
        QCOMPARE(parseVersion("v2.0"), (QVector<int>{2, 0}));
        QCOMPARE(parseVersion("V3"), (QVector<int>{3}));
        QCOMPARE(parseVersion(" 1.2.3 \n"), (QVector<int>{1, 2, 3}));
        QCOMPARE(parseVersion("2.0-rc1"), (QVector<int>{2, 0}));      // suffix ignored
        QCOMPARE(parseVersion("1.x.3"), (QVector<int>{1, 0, 3}));     // junk segment → 0
        QCOMPARE(parseVersion(""), (QVector<int>{0}));
    }

    void newer_data() {
        QTest::addColumn<QString>("latest");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("expected");
        QTest::newRow("patch bump")      << "0.2.1" << "0.2.0" << true;
        QTest::newRow("equal")           << "0.2.0" << "0.2.0" << false;
        QTest::newRow("older")           << "0.1.9" << "0.2.0" << false;
        QTest::newRow("major bump")      << "1.0.0" << "0.9.9" << true;
        QTest::newRow("shorter equal")   << "1.2"   << "1.2.0" << false;
        QTest::newRow("shorter newer")   << "1.3"   << "1.2.9" << true;
        QTest::newRow("v prefix")        << "v0.3.0" << "0.2.0" << true;
        QTest::newRow("double digits")   << "0.10.0" << "0.9.0" << true; // not lexicographic
    }
    void newer() {
        QFETCH(QString, latest);
        QFETCH(QString, current);
        QFETCH(bool, expected);
        QCOMPARE(isNewerVersion(latest, current), expected);
    }
};

QTEST_APPLESS_MAIN(TestVersion)
#include "tst_version.moc"
