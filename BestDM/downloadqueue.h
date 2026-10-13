#pragma once

#include "downloadtask.h"
#include "schedulewindow.h"
#include "categoryrules.h"
#include "persistencerepository.h"
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class DownloadQueue final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int maximumConcurrent READ maximumConcurrent WRITE setMaximumConcurrent NOTIFY queueChanged)
    Q_PROPERTY(int maximumRetries READ maximumRetries WRITE setMaximumRetries NOTIFY queueChanged)
    Q_PROPERTY(qint64 speedLimitBytesPerSecond READ speedLimitBytesPerSecond WRITE setSpeedLimitBytesPerSecond NOTIFY queueChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY queueChanged)
public:
    explicit DownloadQueue(PersistenceRepository &repository, QObject *parent = nullptr);
    ~DownloadQueue() override;
    const QList<DownloadRecord> &records() const { return m_records; }
    QString startupPersistenceError() const { return m_startupPersistenceError; }
    int maximumConcurrent() const { return m_maximumConcurrent; }
    int maximumRetries() const { return m_maximumRetries; }
    qint64 speedLimitBytesPerSecond() const { return m_bandwidthLimiter.limit(); }
    int activeCount() const { return int(m_active.size()); }
    bool scheduleEnabled() const { return m_schedule.enabled; }
    QStringList scheduleDays() const;
    QString scheduleStartTime() const { return m_schedule.startTime.toString(QStringLiteral("HH:mm")); }
    QString scheduleEndTime() const { return m_schedule.endTime.toString(QStringLiteral("HH:mm")); }
    void setMaximumConcurrent(int limit);
    void setMaximumRetries(int limit);
    void setSpeedLimitBytesPerSecond(qint64 limit);
    void setCategoryConfiguration(const CategoryRules &rules, bool automaticFolders);
    bool setScheduleSettings(bool enabled, const QStringList &days, const QString &startTime,
                             const QString &endTime, QString *error = nullptr);
    bool setProxyConfiguration(const QString &mode, const QString &type, const QString &host,
                               int port, const QString &username, const QString &password,
                               bool clearPassword, QString *error = nullptr);
    void testProxyConnection(const QUrl &url);
    void startPending();
    void enqueue(DownloadRecord record);
    void pause(const QString &id);
    void resume(const QString &id);
    void cancel(const QString &id, bool deletePartial = true);
    void remove(const QString &id, bool deleteFiles = false);
    bool canChangeDestination(const QString &id) const;
    bool changeDestination(const QString &id, const QString &destination, QString *error = nullptr);
    bool changeQueueName(const QString &id, const QString &queueName, QString *error = nullptr);
    void retryAllFailed();
    void retryFailedInQueue(const QString &queueName);
    bool provideHttpCredentials(const QString &id, const QString &origin, const QString &username, const QString &password);
signals:
    void queueChanged();
    void recordAdded(const DownloadRecord &record);
    void recordChanged(const DownloadRecord &record);
    void recordRemoved(const QString &id);
    void persistenceError(const QString &message);
    void proxyTestFinished(bool success, const QString &message);
    void httpAuthenticationRequested(const QString &id, const QString &origin, const QString &host, const QString &realm);
private:
    int indexOf(const QString &id) const;
    DownloadRecord *findRecord(const QString &id);
    QString reserveSuggestedFileName(const QString &id, const QString &desired, const QString &destination,
                                     bool overwriteExisting = false) const;
    void createTask(const QString &id);
    void persist(const DownloadRecord &record);
    void schedule();
    void updateSchedule();
    void taskFinished(DownloadTask *task);
    void scheduleRetry(const QString &id);
    PersistenceRepository &m_repository;
    QNetworkAccessManager m_network;
    BandwidthLimiter m_bandwidthLimiter;
    ScheduleWindow m_schedule;
    CategoryRules m_categoryRules;
    bool m_automaticCategoryFolders = false;
    QTimer m_scheduleTimer;
    QList<DownloadRecord> m_records;
    QString m_startupPersistenceError;
    QHash<QString, DownloadTask *> m_tasks;
    QList<DownloadTask *> m_waiting;
    QList<DownloadTask *> m_active;
    QHash<QString, bool> m_removeAfterCancel;
    QSet<QString> m_schedulePausing;
    QSet<QString> m_retryTimers;
    QHash<QString, qint64> m_lastProgressPersistMs;
    QHash<QString, QHash<QString, QPair<QString, QString>>> m_httpCredentials;
    QSet<QString> m_authResumePending;
    QString m_proxyMode = QStringLiteral("Direct");
    QString m_proxyType = QStringLiteral("HTTP");
    QString m_proxyHost;
    QString m_proxyUsername;
    int m_proxyPort = 8080;
    QPointer<QNetworkReply> m_proxyTestReply;
    int m_maximumConcurrent = 3;
    int m_maximumRetries = 3;
};
