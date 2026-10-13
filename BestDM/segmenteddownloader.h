#pragma once

#include "bandwidthlimiter.h"

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QTimer>
#include <memory>
#include <vector>

class QFile;
class QSaveFile;

class SegmentedDownloader final : public QObject {
    Q_OBJECT
public:
    SegmentedDownloader(QString partPath, QUrl url, qint64 total, QString etag,
                        QString lastModified, int connections, QNetworkAccessManager *network,
                        BandwidthLimiter *limiter, const void *limiterKey, QObject *parent = nullptr);
    ~SegmentedDownloader() override;
    void start();
    void pause();
    void cancel(bool deletePartial);
    void updateBandwidthBuffer();
    void setHttpCredentials(const QString &origin, const QString &username, const QString &password);
    qint64 receivedBytes() const;

signals:
    void progress(qint64 received, qint64 total);
    void verifying();
    void completed(bool success, bool fallback, bool canceled, bool retryable,
                   const QString &error, qint64 received, qint64 nextRetryAt);
    void authenticationRequested(const QString &origin, const QString &host, const QString &realm);

private:
    struct Segment;
    void startSegment(Segment &segment);
    void drain(Segment &segment);
    void replyFinished(Segment &segment);
    void fail(QString message, bool retryable, qint64 retryAt = 0);
    void abortReplies();
    void finishWhenIdle();
    void mergeSegments();
    void mergeNextChunk();
    void removeSegmentFiles(QString *error = nullptr);

    QString m_partPath;
    QUrl m_url;
    qint64 m_total = 0;
    QString m_etag;
    QString m_lastModified;
    int m_connectionCount = 1;
    QNetworkAccessManager *m_network = nullptr;
    BandwidthLimiter *m_limiter = nullptr;
    const void *m_limiterKey = nullptr;
    std::vector<std::unique_ptr<Segment>> m_segments;
    std::unique_ptr<QSaveFile> m_mergeOutput;
    std::unique_ptr<QFile> m_mergeInput;
    size_t m_mergeIndex = 0;
    qint64 m_mergeWritten = 0;
    QTimer m_drainTimer;
    bool m_pausing = false;
    bool m_canceling = false;
    bool m_deletePartial = true;
    bool m_fallback = false;
    bool m_failed = false;
    bool m_finished = false;
    bool m_merging = false;
    bool m_retryable = false;
    QString m_error;
    qint64 m_nextRetryAt = 0;
    QHash<QString, QPair<QString, QString>> m_httpCredentials;
    bool m_authPromptPending = false;
    bool m_authChallengeInProgress = false;
    QSet<QNetworkReply *> m_authRepliesUsed;
};
