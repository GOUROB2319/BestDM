#include "../BestDM/categoryrules.h"

#include <QtTest>

class CategoryRulesTest final : public QObject {
    Q_OBJECT
private slots:
    void assignsByExtensionAndHost() {
        CategoryRules rules;
        QString error;
        QVERIFY2(rules.setText("ext: .ZIP = Archives\nhost: example.com = Vendor", &error), qPrintable(error));
        QCOMPARE(rules.categoryFor(QUrl(QStringLiteral("https://cdn.other.test/file.ZIP")), QStringLiteral("file.ZIP")),
                 QStringLiteral("Archives"));
        QCOMPARE(rules.categoryFor(QUrl(QStringLiteral("https://downloads.example.com/file.bin")), QStringLiteral("file.bin")),
                 QStringLiteral("Vendor"));
        QCOMPARE(rules.categoryFor(QUrl(QStringLiteral("https://notexample.com/file.bin")), QStringLiteral("file.bin")),
                 QStringLiteral("Uncategorized"));
    }

    void firstMatchingRuleWinsAndTextIsNormalized() {
        CategoryRules rules;
        QVERIFY(rules.setText(" HOST: example.com = First\next: zip = Second"));
        QCOMPARE(rules.categoryFor(QUrl(QStringLiteral("https://cdn.example.com/archive.zip")), QStringLiteral("archive.zip")),
                 QStringLiteral("First"));
        QCOMPARE(rules.text(), QStringLiteral("host: example.com = First\next: zip = Second"));
    }

    void invalidUpdateDoesNotReplaceWorkingRules() {
        CategoryRules rules;
        QVERIFY(rules.setText("ext: pdf = Documents"));
        QString error;
        QVERIFY(!rules.setText("ext: ../pdf = Unsafe", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(rules.text(), QStringLiteral("ext: pdf = Documents"));
        QCOMPARE(rules.categoryFor(QUrl(QStringLiteral("https://files.test/a.pdf")), QStringLiteral("a.pdf")),
                 QStringLiteral("Documents"));
    }

    void ruleCountIsBounded() {
        CategoryRules rules;
        QStringList lines;
        for (int i = 0; i < 101; ++i) lines.append(QStringLiteral("ext: x%1 = C%1").arg(i));
        QString error;
        QVERIFY(!rules.setText(lines.join(QLatin1Char('\n')), &error));
        QVERIFY(error.contains(QStringLiteral("100")));
    }

    void categoryFolderNamesCannotEscapeOrUseReservedNames() {
        QCOMPARE(CategoryRules::safeFolderName(QStringLiteral("Documents")), QStringLiteral("Documents"));
        QCOMPARE(CategoryRules::safeFolderName(QStringLiteral("../outside")), QStringLiteral(".._outside"));
        QCOMPARE(CategoryRules::safeFolderName(QStringLiteral("folder/name")), QStringLiteral("folder_name"));
        QCOMPARE(CategoryRules::safeFolderName(QStringLiteral("CON")), QStringLiteral("_CON"));
        QCOMPARE(CategoryRules::safeFolderName(QStringLiteral("...   ")), QStringLiteral("Uncategorized"));
    }
};

QTEST_GUILESS_MAIN(CategoryRulesTest)
#include "test_category_rules.moc"
