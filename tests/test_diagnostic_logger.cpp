#include "../BestDM/diagnosticlogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

class DiagnosticLoggerTest final : public QObject {
    Q_OBJECT
private slots:
    void redactsCredentialsAndSignedUrlQueries() {
        const QString input = QStringLiteral(
            "request https://alice:secret-pass@example.test/file?X-Amz-Signature=sig-value&token=jwt-value#fragment "
            "Authorization: Bearer bearer-value");
        const QString redacted = DiagnosticLogger::redact(input);
        QVERIFY(redacted.contains(QStringLiteral("https://example.test/file?")));
        QVERIFY(redacted.contains(QStringLiteral("Authorization: [redacted]")));
        QVERIFY(!redacted.contains(QStringLiteral("secret-pass")));
        QVERIFY(!redacted.contains(QStringLiteral("sig-value")));
        QVERIFY(!redacted.contains(QStringLiteral("jwt-value")));
        QVERIFY(!redacted.contains(QStringLiteral("fragment")));
        QVERIFY(!redacted.contains(QStringLiteral("bearer-value")));

        const QString home = QDir::homePath();
        if (!home.isEmpty() && home != QStringLiteral("/") && home != QStringLiteral("\\")) {
            const QString personalPath = DiagnosticLogger::redact(QStringLiteral("failed at %1/cache/file.bin").arg(home));
            QVERIFY(!personalPath.contains(home, Qt::CaseInsensitive));
        }
    }

    void logRotationKeepsConfiguredBoundedHistory() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DiagnosticLogger logger(directory.path(), 1024, 2);
        for (int index = 0; index < 80; ++index)
            logger.writeMessage(QtInfoMsg, QStringLiteral("test"), QString(48, QLatin1Char('x'))
                                + QString::number(index));

        const QString activePath = logger.activeLogFilePath();
        QVERIFY(QFileInfo::exists(activePath));
        QVERIFY(QFileInfo(activePath).size() <= 1024);
        QVERIFY(QFileInfo::exists(activePath + QStringLiteral(".1")));
        QVERIFY(QFileInfo(activePath + QStringLiteral(".2")));
        QVERIFY(QFileInfo(activePath + QStringLiteral(".1")).size() <= 1024);
        QVERIFY(QFileInfo(activePath + QStringLiteral(".2")).size() <= 1024);
        QVERIFY(!QFileInfo::exists(activePath + QStringLiteral(".3")));
    }

    void installedHandlerWritesSanitizedQtMessages() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DiagnosticLogger logger(directory.path());
        QVERIFY(logger.install());
        qWarning().noquote() << "request https://user:secret@example.test/file?token=private-value";
        logger.uninstall();

        QFile file(logger.activeLogFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString contents = QString::fromUtf8(file.readAll());
        QVERIFY(contents.contains(QStringLiteral("[WARNING]")));
        QVERIFY(contents.contains(QStringLiteral("https://example.test/file?")));
        QVERIFY(!contents.contains(QStringLiteral("secret")));
        QVERIFY(!contents.contains(QStringLiteral("private-value")));
    }

    void exportContainsOnlyRedactedLogFiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString logDirectory = QDir(directory.path()).filePath(QStringLiteral("logs"));
        DiagnosticLogger logger(logDirectory);
        logger.writeMessage(QtWarningMsg, QStringLiteral("test"),
                            QStringLiteral("URL https://user:password@example.test/file?token=secret-value"));
        const QString exportPath = QDir(directory.path()).filePath(QStringLiteral("diagnostics.txt"));

        QVERIFY(logger.exportToFile(QUrl::fromLocalFile(exportPath)));
        QFile file(exportPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString contents = QString::fromUtf8(file.readAll());
        QVERIFY(contents.contains(QStringLiteral("https://example.test/file?")));
        QVERIFY(!contents.contains(QStringLiteral("password")));
        QVERIFY(!contents.contains(QStringLiteral("secret-value")));
        QVERIFY(!contents.contains(QStringLiteral("bestdm.sqlite")));
        QVERIFY(logger.exportStatus().contains(QStringLiteral("logs only")));
    }

    void exportRejectsNonLocalAndNonTextDestinations() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DiagnosticLogger logger(QDir(directory.path()).filePath(QStringLiteral("logs")));
        QVERIFY(!logger.exportToFile(QUrl(QStringLiteral("https://example.test/diagnostics.txt"))));
        QVERIFY(logger.exportStatus().contains(QStringLiteral("local")));
        QVERIFY(!logger.exportToFile(QUrl::fromLocalFile(logger.activeLogFilePath())));
        QVERIFY(logger.exportStatus().contains(QStringLiteral(".txt")));
    }
};

QTEST_GUILESS_MAIN(DiagnosticLoggerTest)
#include "test_diagnostic_logger.moc"
