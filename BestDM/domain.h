#pragma once

#include <QString>
#include <QtGlobal>

enum class DownloadState { Queued, Downloading, Paused, Completed, Failed, Canceled };

struct DownloadRecord {
    QString id;
    QString url;
    QString fileName;
    QString destination;
    QString state;
    QString error;
    QString etag;
    QString lastModified;
    qint64 received = 0;
    qint64 total = 0;
    int retryCount = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};
