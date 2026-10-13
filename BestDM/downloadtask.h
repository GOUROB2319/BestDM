#pragma once
#include "domain.h"
#include "bandwidthlimiter.h"
#include <QObject>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QSet>
#include <memory>
#include <functional>

class SegmentedDownloader;

// A task owns exactly one network reply and partial file; it is safe to run beside other tasks.
class DownloadTask final : public QObject {
    Q_OBJECT
public:
    explicit DownloadTask(DownloadRecord record, QObject *parent = nullptr);
    DownloadTask(DownloadRecord record, QNetworkAccessManager *network, QObject *parent = nullptr);
    DownloadTask(DownloadRecord record, QNetworkAccessManager *network, BandwidthLimiter *limiter, QObject *parent);
    ~DownloadTask() override;
    const DownloadRecord &record() const { return m_record; }
    void setFileName(const QString &fileName) { m_record.fileName = fileName; }
    void setFileLocation(const QString &fileName, const QString &destination, const QString &category) {
        m_record.fileName = fileName;
        m_record.destination = destination;
        m_record.category = category;
    }
    void setQueueName(const QString &queueName) { m_record.queueName = queueName; }
    void setHttpCredentials(const QString &origin, const QString &username, const QString &password) {
        m_httpCredentials.insert(origin, qMakePair(username, password));
    }
    bool isActive() const { return m_reply != nullptr || bool(m_segmentedDownloader); }
    void updateBandwidthBuffer();
    void start(); void pause(); void cancel(bool deletePartial = true);
signals:
    void changed();
    void finished(DownloadTask *task);
    void authenticationRequested(const QString &id, const QString &origin, const QString &host, const QString &realm);
private:
    QString partPath() const;
    void startTransfer();
    void startSegmentedTransfer();
    void persistProgress(qint64 received, qint64 total);
    void drainReplyData();
    void finishReply();
    DownloadRecord m_record;
    std::unique_ptr<QNetworkAccessManager> m_ownedNetwork;
    QNetworkAccessManager *m_network = nullptr;
    BandwidthLimiter *m_limiter = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile m_file;
    qint64 m_offset = 0;
    qint64 m_responseTotal = 0;
    qint64 m_expectedResponseBytes = -1;
    qint64 m_activeDurationAtStart = 0;
    bool m_pauseRequested = false;
    bool m_cancelRequested = false;
    bool m_deletePartialOnCancel = true;
    bool m_restartFromZero = false;
    QElapsedTimer m_speedClock;
    QElapsedTimer m_transferClock;
    QTimer m_speedIdleTimer;
    QTimer m_readTimer;
    qint64 m_speedSampleBytes = 0;
    bool m_replyFinished = false;
    bool m_diskWriteFailed = false;
    QHash<QString, QPair<QString, QString>> m_httpCredentials;
    bool m_authPromptPending = false;
    bool m_authRejected = false;
    bool m_authChallengeInProgress = false;
    QSet<QNetworkReply *> m_authRepliesUsed;
    std::function<void()> m_finishReply;
    std::unique_ptr<SegmentedDownloader> m_segmentedDownloader;
    bool m_forceSingleStream = false;
};
