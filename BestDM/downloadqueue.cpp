#include "downloadqueue.h"
#include <QDateTime>
#include <QTimer>
#include <algorithm>

DownloadQueue::DownloadQueue(PersistenceRepository &repository, QObject *parent) : QObject(parent), m_repository(repository) {}
void DownloadQueue::setMaximumConcurrent(int limit) { m_maximumConcurrent=std::clamp(limit,1,16); schedule(); emit queueChanged(); }
void DownloadQueue::enqueue(DownloadRecord record) {
    record.state="Queued"; record.updatedAt=QDateTime::currentSecsSinceEpoch(); if(!record.createdAt) record.createdAt=record.updatedAt;
    m_repository.save(record); auto *task=new DownloadTask(std::move(record),this); m_waiting.append(task);
    connect(task,&DownloadTask::changed,this,[this,task]{ m_repository.save(task->record()); emit taskChanged(task->record()); });
    connect(task,&DownloadTask::finished,this,&DownloadQueue::taskFinished); schedule(); emit queueChanged();
}
void DownloadQueue::schedule() { while(m_active.size()<m_maximumConcurrent && !m_waiting.isEmpty()) { auto *task=m_waiting.takeFirst(); m_active.append(task); task->start(); } emit queueChanged(); }
void DownloadQueue::taskFinished(DownloadTask *task) {
    m_active.removeAll(task); auto record=task->record(); m_repository.save(record);
    if(record.state=="Failed" && record.retryCount<3) { ++record.retryCount; record.state="Queued"; record.updatedAt=QDateTime::currentSecsSinceEpoch(); QTimer::singleShot(1000*(1<<record.retryCount),this,[this,record]{ enqueue(record); }); }
    task->deleteLater(); schedule(); emit queueChanged();
}
