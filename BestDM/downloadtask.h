#pragma once
#include "domain.h"
#include <QObject>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// A task owns exactly one network reply and partial file; it is safe to run beside other tasks.
class DownloadTask final : public QObject {
    Q_OBJECT
public:
    explicit DownloadTask(DownloadRecord record, QObject *parent = nullptr);
    const DownloadRecord &record() const { return m_record; }
    bool isActive() const { return m_reply != nullptr; }
    void start(); void pause(); void cancel();
signals:
    void changed();
    void finished(DownloadTask *task);
private:
    QString partPath() const;
    void persistProgress(qint64 received, qint64 total);
    DownloadRecord m_record;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QFile m_file;
    qint64 m_offset = 0;
};
