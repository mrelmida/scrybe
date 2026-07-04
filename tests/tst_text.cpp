#include "util/Text.h"

#include <QtTest>

using scrybe::unquote;

class TestText : public QObject {
    Q_OBJECT
private slots:
    void unquoting() {
        QCOMPARE(unquote("\"hello\""), QStringLiteral("hello"));
        QCOMPARE(unquote("'hello'"), QStringLiteral("hello"));
        QCOMPARE(unquote("  \"padded\"  "), QStringLiteral("padded"));
        QCOMPARE(unquote("plain text"), QStringLiteral("plain text"));
        QCOMPARE(unquote("\"mismatched'"), QStringLiteral("\"mismatched'"));
        QCOMPARE(unquote("it's fine"), QStringLiteral("it's fine"));   // inner quote kept
        QCOMPARE(unquote("\"\""), QString());
        QCOMPARE(unquote("\""), QStringLiteral("\""));   // single char untouched
        QCOMPARE(unquote(""), QString());
    }
};

QTEST_APPLESS_MAIN(TestText)
#include "tst_text.moc"
