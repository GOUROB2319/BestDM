#pragma once
#include "downloadtask.h"
#include "persistencerepository.h"
#include <QObject>
#include <QList>

class DownloadQueue final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int maximumConcurrent READ maximumConcurrent WRITE setMaximumConcurrent NOTIFY queueChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY queueChanged)
public:
    explicit DownloadQueue(PersistenceRepository &repository, QObject *parent=nullptr);
    void enqueue(DownloadRecord record);
    int maximumConcurrent() const { return m_maximumConcurrent; }
    int activeCount() const { return m_active.size(); }
    void setMaximumConcurrent(int limit);
signals:
    void queueChanged();
    void taskChanged(const DownloadRecord &record);
private:
    void schedule();
    void taskFinished(DownloadTask *task);
    PersistenceRepository &m_repository;
    QList<DownloadTask*> m_waiting, m_active;
    int m_maximumConcurrent = 3;
};
