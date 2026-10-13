#include "../BestDM/domain.h"
#include "../BestDM/persistencerepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest>

class DomainPersistenceTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("BestDMTests"));
        QCoreApplication::setApplicationName(QStringLiteral("BestDMTests"));
    }

    void stateNamesRoundTrip() {
        const QList<DownloadState> states{
            DownloadState::Queued, DownloadState::Probing, DownloadState::WaitingForSchedule, DownloadState::Retrying,
            DownloadState::Downloading, DownloadState::Paused, DownloadState::Verifying,
            DownloadState::Completed, DownloadState::Failed, DownloadState::Canceled
        };
        for (const auto state : states)
            QVERIFY(downloadStateFromName(downloadStateName(state)) == state);
        QVERIFY(downloadStateFromName(QStringLiteral("unknown legacy state")) == DownloadState::Failed);
    }

    void stateTransitionsAreValidated() {
        DownloadRecord record;
        QVERIFY(transitionDownloadState(record, DownloadState::Downloading));
        QVERIFY(transitionDownloadState(record, DownloadState::Paused));
        QVERIFY(transitionDownloadState(record, DownloadState::Queued));
        QVERIFY(transitionDownloadState(record, DownloadState::Downloading));
        QVERIFY(transitionDownloadState(record, DownloadState::Completed));
        QVERIFY(!transitionDownloadState(record, DownloadState::Queued));
        QVERIFY(transitionDownloadState(record, DownloadState::Failed)); // Reconciliation may detect a missing completed file.
        QVERIFY(!transitionDownloadState(record, DownloadState::Downloading));
        QVERIFY(transitionDownloadState(record, DownloadState::Queued));
        QVERIFY(transitionDownloadState(record, DownloadState::Canceled));
        QVERIFY(transitionDownloadState(record, DownloadState::Queued)); // A retained canceled partial may be resumed.

        DownloadRecord retrying;
        QVERIFY(transitionDownloadState(retrying, DownloadState::Failed));
        QVERIFY(transitionDownloadState(retrying, DownloadState::Retrying));
        QVERIFY(transitionDownloadState(retrying, DownloadState::Canceled));
    }

    void migratesLegacyRecordsAndRestoresAll() {
        const QDir appData(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        QVERIFY(QDir().mkpath(appData.path()));
        const QString databasePath = appData.filePath(QStringLiteral("bestdm.sqlite"));
        QVERIFY(QFile::remove(databasePath) || !QFileInfo::exists(databasePath));

        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("bestdm-legacy-seed"));
            db.setDatabaseName(databasePath);
            QVERIFY2(db.open(), qPrintable(db.lastError().text()));
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO schema_version(version) VALUES (1)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE downloads (id TEXT PRIMARY KEY, url TEXT NOT NULL, filename TEXT NOT NULL, destination TEXT NOT NULL, state TEXT NOT NULL, error TEXT, etag TEXT, last_modified TEXT, received INTEGER NOT NULL, total INTEGER NOT NULL, retry_count INTEGER NOT NULL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)")));
            query.prepare(QStringLiteral("INSERT INTO downloads VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            query.addBindValue(QStringLiteral("legacy-id"));
            query.addBindValue(QStringLiteral("https://example.test/legacy.zip"));
            query.addBindValue(QStringLiteral("legacy.zip"));
            query.addBindValue(appData.path());
            query.addBindValue(QStringLiteral("Paused"));
            query.addBindValue(QString{});
            query.addBindValue(QStringLiteral("etag-v1"));
            query.addBindValue(QString{});
            query.addBindValue(12);
            query.addBindValue(34);
            query.addBindValue(0);
            query.addBindValue(100);
            query.addBindValue(101);
            QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
            query = QSqlQuery();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("bestdm-legacy-seed"));

        PersistenceRepository repository;
        QString error;
        auto records = repository.restoreAll(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(records.size(), qsizetype(1));
        QCOMPARE(records.first().id, QStringLiteral("legacy-id"));
        QVERIFY(records.first().state == DownloadState::Paused);
        QCOMPARE(records.first().received, qint64(12));
        QCOMPARE(records.first().rangeSupported, false);
        QCOMPARE(records.first().activeDurationMs, qint64(0));
        QCOMPARE(records.first().category, QStringLiteral("Uncategorized"));
        QCOMPARE(records.first().metadataProbePending, false);
        QCOMPARE(records.first().serverFilenameAllowed, false);

        DownloadRecord second;
        second.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        second.url = QStringLiteral("https://example.test/second.bin");
        second.fileName = QStringLiteral("second.bin");
        second.destination = appData.path();
        second.state = DownloadState::Retrying;
        second.received = 99;
        second.total = 99;
        second.createdAt = 200;
        second.updatedAt = 201;
        second.retryCount = 1;
        second.nextRetryAt = 202;
        second.activeDurationMs = 4321;
        second.category = QStringLiteral("Archives");
        second.metadataProbePending = true;
        second.serverFilenameAllowed = true;
        second.contentType = QStringLiteral("application/octet-stream");
        second.rangeSupported = true;
        QVERIFY2(repository.save(second, &error), qPrintable(error));

        records = repository.restoreAll(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(records.size(), qsizetype(2));
        QCOMPARE(records.first().id, second.id);
        QVERIFY(records.first().state == DownloadState::Retrying);
        QCOMPARE(records.first().retryCount, 1);
        QCOMPARE(records.first().nextRetryAt, qint64(202));
        QCOMPARE(records.first().activeDurationMs, qint64(4321));
        QCOMPARE(records.first().contentType, QStringLiteral("application/octet-stream"));
        QCOMPARE(records.first().rangeSupported, true);
        QCOMPARE(records.first().category, QStringLiteral("Archives"));
        QCOMPARE(records.first().metadataProbePending, true);
        QCOMPARE(records.first().serverFilenameAllowed, true);
        QCOMPARE(records.first().overwriteExisting, false);
        second.overwriteExisting = true;
        QVERIFY2(repository.save(second, &error), qPrintable(error));
        records = repository.restoreAll(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(records.first().overwriteExisting, true);
        QVERIFY(repository.remove(second.id, &error));
        QVERIFY(repository.remove(QStringLiteral("legacy-id"), &error));
    }
};

QTEST_GUILESS_MAIN(DomainPersistenceTest)
#include "test_domain_persistence.moc"
