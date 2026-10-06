#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QElapsedTimer>
#include "domain.h"
#include "persistencerepository.h"

class DownloadManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString url READ url NOTIFY downloadChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY downloadChanged)
    Q_PROPERTY(QString destination READ destination NOTIFY downloadChanged)
    Q_PROPERTY(qint64 received READ received NOTIFY downloadChanged)
    Q_PROPERTY(qint64 total READ total NOTIFY downloadChanged)
    Q_PROPERTY(QString state READ state NOTIFY downloadChanged)
    Q_PROPERTY(QString error READ error NOTIFY downloadChanged)
    Q_PROPERTY(bool active READ active NOTIFY downloadChanged)
    Q_PROPERTY(qint64 bytesPerSecond READ bytesPerSecond NOTIFY downloadChanged)
    Q_PROPERTY(qint64 etaSeconds READ etaSeconds NOTIFY downloadChanged)
public:
    explicit DownloadManager(QObject *parent = nullptr);
    QString url() const { return m_url; } QString fileName() const { return m_fileName; }
    QString destination() const { return m_destination; } qint64 received() const { return m_received; }
    qint64 total() const { return m_total; } QString state() const { return m_state; }
    QString error() const { return m_error; } bool active() const { return m_reply != nullptr; }
    qint64 bytesPerSecond() const { return m_bytesPerSecond; }
    qint64 etaSeconds() const { return m_bytesPerSecond > 0 && m_total > m_received ? (m_total-m_received)/m_bytesPerSecond : -1; }
    Q_INVOKABLE void start(const QString &url, const QString &destination);
    Q_INVOKABLE void pause(); Q_INVOKABLE void resume(); Q_INVOKABLE void cancel();
    Q_INVOKABLE QString defaultDestination() const;
signals: void downloadChanged(); void completed(const QString &path);
private:
    void beginRequest(); void persist() const; void restore();
    QString partPath() const; QString finalPath() const; QString filenameForUrl(const QUrl &url) const;
    QString uniqueFileName(const QString &desired) const;
    QNetworkAccessManager m_network; QNetworkReply *m_reply = nullptr; QFile m_file;
    QString m_url, m_fileName, m_destination, m_state = "Ready", m_error;
    QString m_id, m_etag, m_lastModified;
    qint64 m_createdAt = 0;
    qint64 m_received = 0, m_total = 0, m_resumeOffset = 0;
    bool m_pauseRequested = false;
    QElapsedTimer m_speedClock;
    qint64 m_lastSpeedBytes = 0, m_bytesPerSecond = 0;
    PersistenceRepository m_repository;
};
