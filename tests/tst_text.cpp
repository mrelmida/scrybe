#include "util/Text.h"

#include <QtTest>

using scrybe::unquote;

class TestText : public QObject {
    Q_OBJECT
private slots:
    void presetNames() {
        QVERIFY(scrybe::validPresetName(QStringLiteral("Formal email")));
        QVERIFY(scrybe::validPresetName(QStringLiteral("  Türkçe  ")));
        for (const auto &name : {"", "  ", "format", " markdown ", "summary",
                                 "work/email", "work\\email", "bad\nname"})
            QVERIFY2(!scrybe::validPresetName(QString::fromUtf8(name)), name);
    }

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
