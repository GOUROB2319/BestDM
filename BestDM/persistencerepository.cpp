#include "persistencerepository.h"
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

PersistenceRepository::PersistenceRepository() : m_connectionName(QStringLiteral("bestdm-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    auto db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    QDir data(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)); data.mkpath(".");
    db.setDatabaseName(data.filePath("bestdm.sqlite")); db.open();
    QSqlQuery query(db);
    query.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)");
    query.exec("INSERT INTO schema_version(version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version)");
    query.exec("CREATE TABLE IF NOT EXISTS downloads (id TEXT PRIMARY KEY, url TEXT NOT NULL, filename TEXT NOT NULL, destination TEXT NOT NULL, state TEXT NOT NULL, error TEXT, etag TEXT, last_modified TEXT, received INTEGER NOT NULL, total INTEGER NOT NULL, retry_count INTEGER NOT NULL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)");
}
bool PersistenceRepository::save(const DownloadRecord &r, QString *error) const {
    auto db = QSqlDatabase::database(m_connectionName); if (!db.isOpen()) { if (error) *error = db.lastError().text(); return false; }
    QSqlQuery q(db); q.prepare("INSERT INTO downloads VALUES (:id,:url,:filename,:destination,:state,:error,:etag,:lastModified,:received,:total,:retry,:created,:updated) ON CONFLICT(id) DO UPDATE SET url=excluded.url,filename=excluded.filename,destination=excluded.destination,state=excluded.state,error=excluded.error,etag=excluded.etag,last_modified=excluded.last_modified,received=excluded.received,total=excluded.total,retry_count=excluded.retry_count,updated_at=excluded.updated_at");
    q.bindValue(":id",r.id); q.bindValue(":url",r.url); q.bindValue(":filename",r.fileName); q.bindValue(":destination",r.destination); q.bindValue(":state",r.state); q.bindValue(":error",r.error); q.bindValue(":etag",r.etag); q.bindValue(":lastModified",r.lastModified); q.bindValue(":received",r.received); q.bindValue(":total",r.total); q.bindValue(":retry",r.retryCount); q.bindValue(":created",r.createdAt); q.bindValue(":updated",r.updatedAt);
    if (!q.exec()) { if (error) *error=q.lastError().text(); return false; } return true;
}
DownloadRecord PersistenceRepository::restoreLatest(QString *error) const {
    DownloadRecord r; auto db=QSqlDatabase::database(m_connectionName); QSqlQuery q(db);
    if (!q.exec("SELECT id,url,filename,destination,state,error,etag,last_modified,received,total,retry_count,created_at,updated_at FROM downloads WHERE state NOT IN ('Completed','Canceled') ORDER BY updated_at DESC LIMIT 1")) { if(error)*error=q.lastError().text(); return r; }
    if (q.next()) { r.id=q.value(0).toString(); r.url=q.value(1).toString(); r.fileName=q.value(2).toString(); r.destination=q.value(3).toString(); r.state=q.value(4).toString(); r.error=q.value(5).toString(); r.etag=q.value(6).toString(); r.lastModified=q.value(7).toString(); r.received=q.value(8).toLongLong(); r.total=q.value(9).toLongLong(); r.retryCount=q.value(10).toInt(); r.createdAt=q.value(11).toLongLong(); r.updatedAt=q.value(12).toLongLong(); } return r;
}
bool PersistenceRepository::remove(const QString &id, QString *error) const { QSqlQuery q(QSqlDatabase::database(m_connectionName)); q.prepare("DELETE FROM downloads WHERE id=:id"); q.bindValue(":id",id); if (!q.exec()) { if(error)*error=q.lastError().text(); return false; } return true; }
