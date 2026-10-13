#include "persistencerepository.h"
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <utility>

PersistenceRepository::PersistenceRepository() : m_connectionName(QStringLiteral("bestdm-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {
    auto db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    QDir data(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)); data.mkpath(".");
    db.setDatabaseName(data.filePath("bestdm.sqlite")); m_ready = db.open();
    if (!m_ready) { m_initializationError = db.lastError().text(); return; }
    if (!db.transaction()) { m_ready = false; m_initializationError = db.lastError().text(); return; }
    QSqlQuery query(db);
    m_ready = query.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)")
        && query.exec("INSERT INTO schema_version(version) SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_version)")
        && query.exec("CREATE TABLE IF NOT EXISTS downloads (id TEXT PRIMARY KEY, url TEXT NOT NULL, resolved_url TEXT NOT NULL DEFAULT '', filename TEXT NOT NULL, destination TEXT NOT NULL, base_destination TEXT NOT NULL DEFAULT '', state TEXT NOT NULL, error TEXT, etag TEXT, last_modified TEXT, received INTEGER NOT NULL, total INTEGER NOT NULL, retry_count INTEGER NOT NULL, connection_count INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, completed_at INTEGER NOT NULL DEFAULT 0, content_type TEXT NOT NULL DEFAULT '', range_supported INTEGER NOT NULL DEFAULT 0, next_retry_at INTEGER NOT NULL DEFAULT 0, active_duration_ms INTEGER NOT NULL DEFAULT 0, category TEXT NOT NULL DEFAULT 'Uncategorized', metadata_probe_pending INTEGER NOT NULL DEFAULT 0, server_filename_allowed INTEGER NOT NULL DEFAULT 0, automatic_category_folder INTEGER NOT NULL DEFAULT 0, queue_name TEXT NOT NULL DEFAULT 'Default', overwrite_existing INTEGER NOT NULL DEFAULT 0)");
    const auto ensureColumn = [&db, this](const QString &name, const QString &definition) {
        QSqlQuery check(db);
        check.prepare("SELECT 1 FROM pragma_table_info('downloads') WHERE name = :name");
        check.bindValue(":name", name);
        if (!check.exec()) { m_initializationError = check.lastError().text(); return false; }
        if (check.next()) return true;
        QSqlQuery alter(db);
        const bool ok = alter.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN %1 %2").arg(name, definition));
        if (!ok) m_initializationError = alter.lastError().text();
        return ok;
    };
    m_ready = m_ready && ensureColumn(QStringLiteral("completed_at"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("content_type"), QStringLiteral("TEXT NOT NULL DEFAULT ''"))
        && ensureColumn(QStringLiteral("range_supported"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("next_retry_at"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("active_duration_ms"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("category"), QStringLiteral("TEXT NOT NULL DEFAULT 'Uncategorized'"))
        && ensureColumn(QStringLiteral("metadata_probe_pending"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("server_filename_allowed"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("resolved_url"), QStringLiteral("TEXT NOT NULL DEFAULT ''"))
        && ensureColumn(QStringLiteral("connection_count"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"))
        && ensureColumn(QStringLiteral("base_destination"), QStringLiteral("TEXT NOT NULL DEFAULT ''"))
        && ensureColumn(QStringLiteral("automatic_category_folder"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && ensureColumn(QStringLiteral("queue_name"), QStringLiteral("TEXT NOT NULL DEFAULT 'Default'"))
        && ensureColumn(QStringLiteral("overwrite_existing"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"))
        && query.exec("UPDATE schema_version SET version=12");
    if (m_ready) {
        m_ready = db.commit();
        if (!m_ready) m_initializationError = db.lastError().text();
    } else {
        if (m_initializationError.isEmpty()) m_initializationError = query.lastError().text();
        db.rollback();
    }
}
PersistenceRepository::~PersistenceRepository() {
    {
        auto db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid()) db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}
bool PersistenceRepository::save(const DownloadRecord &r, QString *error) const {
    auto db = QSqlDatabase::database(m_connectionName); if (!m_ready || !db.isOpen()) { if (error) *error = m_initializationError.isEmpty() ? db.lastError().text() : m_initializationError; return false; }
    QSqlQuery q(db); q.prepare("INSERT INTO downloads (id,url,resolved_url,filename,destination,base_destination,state,error,etag,last_modified,received,total,retry_count,connection_count,created_at,updated_at,completed_at,content_type,range_supported,next_retry_at,active_duration_ms,category,metadata_probe_pending,server_filename_allowed,automatic_category_folder,queue_name,overwrite_existing) VALUES (:id,:url,:resolvedUrl,:filename,:destination,:baseDestination,:state,:error,:etag,:lastModified,:received,:total,:retry,:connectionCount,:created,:updated,:completed,:contentType,:rangeSupported,:nextRetryAt,:activeDurationMs,:category,:metadataProbePending,:serverFilenameAllowed,:automaticCategoryFolder,:queueName,:overwriteExisting) ON CONFLICT(id) DO UPDATE SET url=excluded.url,resolved_url=excluded.resolved_url,filename=excluded.filename,destination=excluded.destination,base_destination=excluded.base_destination,state=excluded.state,error=excluded.error,etag=excluded.etag,last_modified=excluded.last_modified,received=excluded.received,total=excluded.total,retry_count=excluded.retry_count,connection_count=excluded.connection_count,updated_at=excluded.updated_at,completed_at=excluded.completed_at,content_type=excluded.content_type,range_supported=excluded.range_supported,next_retry_at=excluded.next_retry_at,active_duration_ms=excluded.active_duration_ms,category=excluded.category,metadata_probe_pending=excluded.metadata_probe_pending,server_filename_allowed=excluded.server_filename_allowed,automatic_category_folder=excluded.automatic_category_folder,queue_name=excluded.queue_name,overwrite_existing=excluded.overwrite_existing");
    q.bindValue(":id",r.id); q.bindValue(":url",r.url); q.bindValue(":resolvedUrl",r.resolvedUrl); q.bindValue(":filename",r.fileName); q.bindValue(":destination",r.destination); q.bindValue(":baseDestination",r.baseDestination); q.bindValue(":state",downloadStateName(r.state)); q.bindValue(":error",r.error); q.bindValue(":etag",r.etag); q.bindValue(":lastModified",r.lastModified); q.bindValue(":received",r.received); q.bindValue(":total",r.total); q.bindValue(":retry",r.retryCount); q.bindValue(":connectionCount",r.connectionCount); q.bindValue(":created",r.createdAt); q.bindValue(":updated",r.updatedAt); q.bindValue(":completed",r.completedAt); q.bindValue(":contentType",r.contentType); q.bindValue(":rangeSupported",r.rangeSupported); q.bindValue(":nextRetryAt",r.nextRetryAt); q.bindValue(":activeDurationMs",r.activeDurationMs); q.bindValue(":category",r.category); q.bindValue(":metadataProbePending",r.metadataProbePending); q.bindValue(":serverFilenameAllowed",r.serverFilenameAllowed); q.bindValue(":automaticCategoryFolder",r.automaticCategoryFolder); q.bindValue(":queueName",r.queueName); q.bindValue(":overwriteExisting",r.overwriteExisting);
    if (!q.exec()) { if (error) *error=q.lastError().text(); return false; } return true;
}
DownloadRecord PersistenceRepository::restoreLatest(QString *error) const {
    const auto records = restoreAll(error);
    for (const auto &record : records) {
        if (record.state != DownloadState::Completed && record.state != DownloadState::Canceled)
            return record;
    }
    return DownloadRecord{};
}
QList<DownloadRecord> PersistenceRepository::restoreAll(QString *error) const {
    QList<DownloadRecord> records;
    if (error) error->clear();
    auto db = QSqlDatabase::database(m_connectionName);
    if (!m_ready || !db.isOpen()) { if (error) *error = m_initializationError.isEmpty() ? db.lastError().text() : m_initializationError; return records; }
    QSqlQuery q(db);
    if (!q.exec("SELECT id,url,filename,destination,state,error,etag,last_modified,received,total,retry_count,connection_count,created_at,updated_at,completed_at,content_type,range_supported,next_retry_at,active_duration_ms,category,metadata_probe_pending,server_filename_allowed,resolved_url,base_destination,automatic_category_folder,queue_name,overwrite_existing FROM downloads ORDER BY created_at DESC,rowid DESC")) { if (error) *error = q.lastError().text(); return records; }
    while (q.next()) {
        DownloadRecord r;
        r.id=q.value(0).toString(); r.url=q.value(1).toString(); r.fileName=q.value(2).toString(); r.destination=q.value(3).toString();
        r.state=downloadStateFromName(q.value(4).toString()); r.error=q.value(5).toString(); r.etag=q.value(6).toString(); r.lastModified=q.value(7).toString();
        r.received=q.value(8).toLongLong(); r.total=q.value(9).toLongLong(); r.retryCount=q.value(10).toInt();
        r.connectionCount=q.value(11).toInt(); r.createdAt=q.value(12).toLongLong(); r.updatedAt=q.value(13).toLongLong();
        r.completedAt=q.value(14).toLongLong(); r.contentType=q.value(15).toString(); r.rangeSupported=q.value(16).toBool(); r.nextRetryAt=q.value(17).toLongLong(); r.activeDurationMs=q.value(18).toLongLong(); r.category=q.value(19).toString(); r.metadataProbePending=q.value(20).toBool(); r.serverFilenameAllowed=q.value(21).toBool(); r.resolvedUrl=q.value(22).toString();
        r.baseDestination=q.value(23).toString(); r.automaticCategoryFolder=q.value(24).toBool(); r.queueName=q.value(25).toString(); r.overwriteExisting=q.value(26).toBool();
        if (r.category.isEmpty()) r.category = QStringLiteral("Uncategorized");
        if (r.queueName.trimmed().isEmpty()) r.queueName = QStringLiteral("Default");
        records.append(std::move(r));
    }
    if (q.lastError().isValid()) {
        if (error) *error = q.lastError().text();
        return {};
    }
    return records;
}
bool PersistenceRepository::remove(const QString &id, QString *error) const { auto db=QSqlDatabase::database(m_connectionName); if (!m_ready || !db.isOpen()) { if(error)*error=m_initializationError.isEmpty()?db.lastError().text():m_initializationError; return false; } QSqlQuery q(db); q.prepare("DELETE FROM downloads WHERE id=:id"); q.bindValue(":id",id); if (!q.exec()) { if(error)*error=q.lastError().text(); return false; } return true; }
