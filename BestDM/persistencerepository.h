#pragma once

#include "domain.h"
#include <QString>
#include <QList>

class PersistenceRepository final {
public:
    PersistenceRepository();
    ~PersistenceRepository();
    PersistenceRepository(const PersistenceRepository &) = delete;
    PersistenceRepository &operator=(const PersistenceRepository &) = delete;
    bool save(const DownloadRecord &record, QString *error = nullptr) const;
    DownloadRecord restoreLatest(QString *error = nullptr) const;
    QList<DownloadRecord> restoreAll(QString *error = nullptr) const;
    bool remove(const QString &id, QString *error = nullptr) const;
    QString initializationError() const { return m_initializationError; }
private:
    QString m_connectionName;
    bool m_ready = false;
    QString m_initializationError;
};
