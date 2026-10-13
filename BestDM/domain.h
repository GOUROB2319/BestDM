#pragma once

#include <QString>
#include <QtGlobal>

enum class DownloadState { Queued, Probing, WaitingForSchedule, Retrying, Downloading, Paused, Verifying, Completed, Failed, Canceled };

inline QString downloadStateName(DownloadState state) {
    switch (state) {
    case DownloadState::Queued: return QStringLiteral("Queued");
    case DownloadState::Probing: return QStringLiteral("Probing");
    case DownloadState::WaitingForSchedule: return QStringLiteral("Waiting for schedule");
    case DownloadState::Retrying: return QStringLiteral("Retrying");
    case DownloadState::Downloading: return QStringLiteral("Downloading");
    case DownloadState::Paused: return QStringLiteral("Paused");
    case DownloadState::Verifying: return QStringLiteral("Verifying");
    case DownloadState::Completed: return QStringLiteral("Completed");
    case DownloadState::Failed: return QStringLiteral("Failed");
    case DownloadState::Canceled: return QStringLiteral("Canceled");
    }
    return QStringLiteral("Failed");
}

inline DownloadState downloadStateFromName(const QString &name) {
    if (name == QStringLiteral("Queued")) return DownloadState::Queued;
    if (name == QStringLiteral("Probing")) return DownloadState::Probing;
    if (name == QStringLiteral("Waiting for schedule")) return DownloadState::WaitingForSchedule;
    if (name == QStringLiteral("Retrying")) return DownloadState::Retrying;
    if (name == QStringLiteral("Downloading")) return DownloadState::Downloading;
    if (name == QStringLiteral("Paused")) return DownloadState::Paused;
    if (name == QStringLiteral("Verifying")) return DownloadState::Verifying;
    if (name == QStringLiteral("Completed")) return DownloadState::Completed;
    if (name == QStringLiteral("Canceled")) return DownloadState::Canceled;
    return DownloadState::Failed;
}

struct DownloadRecord {
    QString id;
    QString url;
    QString resolvedUrl;
    QString fileName;
    QString category = QStringLiteral("Uncategorized");
    QString queueName = QStringLiteral("Default");
    QString destination;
    QString baseDestination;
    DownloadState state = DownloadState::Queued;
    QString contentType;
    QString error;
    QString etag;
    QString lastModified;
    qint64 received = 0;
    qint64 total = 0;
    qint64 bytesPerSecond = 0;
    int retryCount = 0;
    int connectionCount = 1;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    qint64 completedAt = 0;
    qint64 nextRetryAt = 0;
    qint64 activeDurationMs = 0;
    bool rangeSupported = false;
    bool retryable = false;
    bool metadataProbePending = false;
    bool serverFilenameAllowed = false;
    bool automaticCategoryFolder = false;
    bool overwriteExisting = false;
};

// All runtime state changes pass through this function so UI, queue, and
// transfer callbacks share one transition policy.
inline bool transitionDownloadState(DownloadRecord &record, DownloadState next) {
    if (record.state == next) return true;
    bool allowed = false;
    switch (record.state) {
    case DownloadState::Queued:
        allowed = next == DownloadState::Probing || next == DownloadState::WaitingForSchedule
            || next == DownloadState::Retrying || next == DownloadState::Downloading || next == DownloadState::Paused
            || next == DownloadState::Failed || next == DownloadState::Canceled;
        break;
    case DownloadState::Probing:
        allowed = next == DownloadState::Queued || next == DownloadState::WaitingForSchedule || next == DownloadState::Paused
            || next == DownloadState::Downloading || next == DownloadState::Failed || next == DownloadState::Canceled;
        break;
    case DownloadState::WaitingForSchedule:
        allowed = next == DownloadState::Queued || next == DownloadState::Downloading
            || next == DownloadState::Paused || next == DownloadState::Canceled || next == DownloadState::Failed;
        break;
    case DownloadState::Retrying:
        allowed = next == DownloadState::Queued || next == DownloadState::Canceled || next == DownloadState::Failed;
        break;
    case DownloadState::Downloading:
        allowed = next == DownloadState::Paused || next == DownloadState::Verifying
            || next == DownloadState::Completed || next == DownloadState::Failed || next == DownloadState::Canceled;
        break;
    case DownloadState::Paused:
        allowed = next == DownloadState::Queued || next == DownloadState::Downloading
            || next == DownloadState::WaitingForSchedule || next == DownloadState::Failed || next == DownloadState::Canceled;
        break;
    case DownloadState::Verifying:
        allowed = next == DownloadState::Completed || next == DownloadState::Failed
            || next == DownloadState::Paused || next == DownloadState::Canceled;
        break;
    case DownloadState::Failed:
        allowed = next == DownloadState::Queued || next == DownloadState::Retrying || next == DownloadState::Canceled;
        break;
    case DownloadState::Completed:
        allowed = next == DownloadState::Failed;
        break;
    case DownloadState::Canceled:
        allowed = next == DownloadState::Queued;
        break;
    }
    if (allowed) record.state = next;
    return allowed;
}
