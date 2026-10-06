#pragma once

#include "domain.h"
#include <QString>

class PersistenceRepository final {
public:
    PersistenceRepository();
    bool save(const DownloadRecord &record, QString *error = nullptr) const;
    DownloadRecord restoreLatest(QString *error = nullptr) const;
    bool remove(const QString &id, QString *error = nullptr) const;
private:
    QString m_connectionName;
};
