#include "downloadtask.h"
#include "segmenteddownloader.h"
#include "safefile.h"
#include <QDateTime>
#include <QAuthenticator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStorageInfo>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace {
QString suggestedFilenameFromDisposition(const QByteArray &header) {
    const QString value = QString::fromLatin1(header);
    static const QRegularExpression extendedName(
        QStringLiteral("(?:^|;)\\s*filename\\*\\s*=\\s*(?:UTF-8''|UTF-8'[^']*')([^;]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression regularName(
        QStringLiteral("(?:^|;)\\s*filename\\s*=\\s*(\"(?:\\\\.|[^\"])*\"|[^;]*)"),
        QRegularExpression::CaseInsensitiveOption);
    QString candidate;
    auto match = extendedName.match(value);
    if (match.hasMatch()) {
        candidate = QUrl::fromPercentEncoding(match.captured(1).trimmed().toLatin1());
    } else {
        match = regularName.match(value);
        if (!match.hasMatch()) return {};
        candidate = match.captured(1).trimmed();
        if (candidate.size() >= 2 && candidate.startsWith(QLatin1Char('"')) && candidate.endsWith(QLatin1Char('"')))
            candidate = candidate.mid(1, candidate.size() - 2);
        candidate.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        candidate.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    }
    candidate.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    candidate = candidate.trimmed();
    while (candidate.endsWith(QLatin1Char('.')) || candidate.endsWith(QLatin1Char(' '))) candidate.chop(1);
    if (candidate.isEmpty() || candidate == QStringLiteral(".") || candidate == QStringLiteral("..")) return {};
    static const QRegularExpression reservedName(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reservedName.match(candidate).hasMatch()) candidate.prepend(QLatin1Char('_'));
    if (candidate.size() > 180) {
        const QFileInfo info(candidate);
        const QString suffix = info.suffix();
        const QString ending = suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix;
        if (ending.size() >= 180) return {};
        candidate = info.completeBaseName().left(180 - ending.size()) + ending;
    }
    return candidate;
}

QString safeNetworkError(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::RemoteHostClosedError: return QStringLiteral("The server closed the connection before the download finished.");
    case QNetworkReply::HostNotFoundError: return QStringLiteral("BestDM could not find the server host. Check the link and your connection.");
    case QNetworkReply::TimeoutError: return QStringLiteral("The server took too long to respond.");
    case QNetworkReply::TemporaryNetworkFailureError: return QStringLiteral("The network connection was interrupted temporarily.");
    case QNetworkReply::NetworkSessionFailedError: return QStringLiteral("The network connection was lost.");
    case QNetworkReply::SslHandshakeFailedError: return QStringLiteral("TLS certificate verification failed; the connection was stopped.");
    case QNetworkReply::TooManyRedirectsError: return QStringLiteral("The server redirected this request too many times; check the link and try again.");
    case QNetworkReply::InsecureRedirectError: return QStringLiteral("The server tried to redirect to a less secure connection; BestDM stopped the request.");
    case QNetworkReply::OperationCanceledError: return QStringLiteral("The network transfer was interrupted.");
    default: return QStringLiteral("The network transfer failed (error %1).").arg(int(error));
    }
}

QString safeHttpError(int status) {
    switch (status) {
    case 401: return QStringLiteral("The server requires credentials. Enter a username and password, then retry.");
    case 407: return QStringLiteral("The proxy requires credentials or rejected its configured credentials (HTTP 407).");
    case 403: return QStringLiteral("The server refused access to this resource (HTTP 403).");
    case 404: return QStringLiteral("The requested file was not found (HTTP 404).");
    case 408: return QStringLiteral("The server timed out handling this request (HTTP 408), a transient response.");
    case 416: return QStringLiteral("The saved partial file no longer matches the server resource (HTTP 416).");
    case 429: return QStringLiteral("The server is receiving too many requests (HTTP 429). Retry timing follows the server response and your retry settings.");
    default:
        if (status >= 500) return QStringLiteral("The remote server failed (HTTP %1).").arg(status);
        return QStringLiteral("The server returned HTTP %1.").arg(status);
    }
}

QString authenticationOrigin(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return {};
    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    QString host = url.host().toLower();
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['))) {
        host.prepend(QLatin1Char('['));
        host.append(QLatin1Char(']'));
    }
    return scheme + QStringLiteral("://") + host
        + QLatin1Char(':') + QString::number(url.port(defaultPort));
}

QString authenticationHost(const QUrl &url) {
    QString host = url.host();
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['))) {
        host.prepend(QLatin1Char('['));
        host.append(QLatin1Char(']'));
    }
    const int port = url.port(-1);
    const int defaultPort = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ? 443 : 80;
    if (port >= 0 && port != defaultPort) {
        host.append(QLatin1Char(':'));
        host.append(QString::number(port));
    }
    return host;
}

qint64 destinationAvailableBytes(const QString &path) {
    QStorageInfo storage(path);
    storage.refresh();
    return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
}

bool destinationHasSpace(const QString &path, qint64 requiredBytes) {
    if (requiredBytes <= 0) return true;
    const qint64 available = destinationAvailableBytes(path);
    return available < 0 || available >= requiredBytes;
}

bool publishFile(const QString &partialPath, const QString &destination,
                 const QString &desiredName, bool overwriteExisting, QString *publishedName) {
    const QDir directory(destination);
    if (overwriteExisting) {
        const QString target = directory.filePath(desiredName);
#ifdef Q_OS_WIN
        const bool published = MoveFileExW(reinterpret_cast<LPCWSTR>(partialPath.utf16()),
            reinterpret_cast<LPCWSTR>(target.utf16()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#elif defined(Q_OS_UNIX)
        const QByteArray sourceBytes = QFile::encodeName(partialPath);
        const QByteArray targetBytes = QFile::encodeName(target);
        const bool published = ::rename(sourceBytes.constData(), targetBytes.constData()) == 0;
#else
        const bool published = false;
#endif
        if (published && publishedName) *publishedName = desiredName;
        return published;
    }
    const QFileInfo source(desiredName);
    const QString stem = source.completeBaseName();
    const QString suffix = source.suffix();
    for (int number = 0; number < 100000; ++number) {
        const QString candidate = number == 0 ? desiredName
            : QStringLiteral("%1 (%2)%3").arg(stem).arg(number)
                  .arg(suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
        const QString target = directory.filePath(candidate);
        if (QFileInfo::exists(target)) continue;
#ifdef Q_OS_WIN
        const bool published = MoveFileW(reinterpret_cast<LPCWSTR>(partialPath.utf16()),
                                         reinterpret_cast<LPCWSTR>(target.utf16()));
        const DWORD moveError = published ? ERROR_SUCCESS : GetLastError();
        const bool collision = moveError == ERROR_FILE_EXISTS || moveError == ERROR_ALREADY_EXISTS;
#elif defined(Q_OS_UNIX)
        const QByteArray sourceBytes = QFile::encodeName(partialPath);
        const QByteArray targetBytes = QFile::encodeName(target);
        const bool published = ::link(sourceBytes.constData(), targetBytes.constData()) == 0;
        const int linkError = published ? 0 : errno;
        const bool collision = linkError == EEXIST;
        if (published) ::unlink(sourceBytes.constData());
#else
        // Fail closed on platforms without a no-replace publication primitive.
        const bool published = false;
        const bool collision = QFileInfo::exists(target);
#endif
        if (published) {
            if (publishedName) *publishedName = candidate;
            return true;
        }
        // The OS-level no-replace operation closes the check/rename race.
        if (!collision) return false;
    }
    return false;
}
}

DownloadTask::DownloadTask(DownloadRecord record, QObject *parent)
    : DownloadTask(std::move(record), nullptr, nullptr, parent) {}

DownloadTask::DownloadTask(DownloadRecord record, QNetworkAccessManager *network, QObject *parent)
    : DownloadTask(std::move(record), network, nullptr, parent) {}

DownloadTask::DownloadTask(DownloadRecord record, QNetworkAccessManager *network,
                           BandwidthLimiter *limiter, QObject *parent)
    : QObject(parent), m_record(std::move(record)), m_network(network), m_limiter(limiter) {
    if (!m_network) {
        m_ownedNetwork = std::make_unique<QNetworkAccessManager>();
        m_network = m_ownedNetwork.get();
    }
    connect(m_network, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
        if (!reply || reply != m_reply || m_record.state == DownloadState::Probing) return;
        const QString origin = authenticationOrigin(reply->url());
        const auto credentials = m_httpCredentials.constFind(origin);
        if (credentials != m_httpCredentials.cend() && !m_authRepliesUsed.contains(reply)) {
            authenticator->setUser(credentials->first);
            authenticator->setPassword(credentials->second);
            m_authRepliesUsed.insert(reply);
            m_authChallengeInProgress = true;
            m_record.error.clear();
            return;
        }
        if (credentials == m_httpCredentials.cend()) {
            m_record.error = QStringLiteral("This server requires a username and password.");
            if (!m_authPromptPending) {
                m_authPromptPending = true;
                emit authenticationRequested(m_record.id, origin, authenticationHost(reply->url()), authenticator->realm());
            }
        } else {
            m_authRejected = true;
            m_record.error = QStringLiteral("The server rejected the supplied credentials.");
            if (!m_authPromptPending) {
                m_authPromptPending = true;
                emit authenticationRequested(m_record.id, origin, authenticationHost(reply->url()), authenticator->realm());
            }
        }
        reply->abort();
    });
    m_speedIdleTimer.setInterval(500);
    connect(&m_speedIdleTimer, &QTimer::timeout, this, [this] {
        if (m_reply && m_speedClock.elapsed() >= 1500 && m_record.bytesPerSecond != 0) {
            m_record.bytesPerSecond = 0;
            emit changed();
        }
    });
    m_readTimer.setInterval(25);
    connect(&m_readTimer, &QTimer::timeout, this, [this] {
        drainReplyData();
        if (m_replyFinished && m_reply && m_reply->bytesAvailable() == 0)
            finishReply();
    });
}

DownloadTask::~DownloadTask() {
    m_speedIdleTimer.stop();
    m_readTimer.stop();
    if (m_limiter) m_limiter->removeConsumer(this);
    if (m_reply) {
        auto *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
    }
    m_file.close();
}
QString DownloadTask::partPath() const { return QDir(m_record.destination).filePath(m_record.fileName + ".part"); }
void DownloadTask::start() {
    if (isActive() || m_record.url.isEmpty()) return;
    m_diskWriteFailed = false;
    m_record.error.clear();
    m_record.retryable=false;
    m_record.bytesPerSecond=0;
    if (m_record.metadataProbePending) {
        transitionDownloadState(m_record, DownloadState::Probing);
        emit changed();
        QNetworkRequest probeRequest(QUrl(m_record.url));
        probeRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        probeRequest.setMaximumRedirectsAllowed(10);
        probeRequest.setTransferTimeout(15000);
        probeRequest.setRawHeader("Accept-Encoding", "identity");
        m_reply = m_network->head(probeRequest);
        auto *probeReply = m_reply;
        connect(probeReply, &QNetworkReply::finished, this, [this, probeReply] {
            if (m_reply != probeReply) return;
            if (!m_pauseRequested && !m_cancelRequested) {
                const int status = probeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (status >= 200 && status < 400) {
                    const QUrl resolved = probeReply->url();
                    const QString resolvedScheme = resolved.scheme().toLower();
                    if (resolved.isValid() && resolved.host().size() > 0 && resolved.userInfo().isEmpty()
                        && (resolvedScheme == QStringLiteral("http") || resolvedScheme == QStringLiteral("https")))
                        m_record.resolvedUrl = resolved.toString(QUrl::FullyEncoded);
                    bool lengthOk = false;
                    const qint64 length = probeReply->rawHeader("Content-Length").toLongLong(&lengthOk);
                    if (lengthOk && length >= 0) m_record.total = length;
                    const auto contentType = QString::fromUtf8(probeReply->rawHeader("Content-Type"));
                    if (!contentType.isEmpty()) m_record.contentType = contentType;
                    const auto etag = QString::fromUtf8(probeReply->rawHeader("ETag"));
                    const auto modified = QString::fromUtf8(probeReply->rawHeader("Last-Modified"));
                    if (!etag.isEmpty()) m_record.etag = etag;
                    if (!modified.isEmpty()) m_record.lastModified = modified;
                    const auto ranges = probeReply->rawHeader("Accept-Ranges").trimmed().toLower();
                    if (ranges == "bytes") m_record.rangeSupported = true;
                    else if (ranges == "none") m_record.rangeSupported = false;
                    if (m_record.serverFilenameAllowed) {
                        const auto suggested = suggestedFilenameFromDisposition(
                            probeReply->rawHeader("Content-Disposition"));
                        if (!suggested.isEmpty()) m_record.fileName = suggested;
                    }
                }
            }
            if (!m_pauseRequested && !m_cancelRequested) {
                m_record.metadataProbePending = false;
                m_record.serverFilenameAllowed = false;
            }
            m_reply = nullptr;
            probeReply->deleteLater();
            if (m_cancelRequested) {
                const auto partial = partPath();
                const bool removed = !m_deletePartialOnCancel || !QFileInfo::exists(partial) || QFile::remove(partial);
                transitionDownloadState(m_record, DownloadState::Canceled);
                m_record.error = removed ? QString{} : QStringLiteral("Download canceled, but the partial file could not be deleted.");
                m_record.received = QFileInfo::exists(partial) ? QFileInfo(partial).size() : 0;
                emit changed();
                emit finished(this);
            } else if (m_pauseRequested) {
                transitionDownloadState(m_record, DownloadState::Paused);
                emit changed();
                emit finished(this);
            } else {
                emit changed();
                startTransfer();
            }
        });
        return;
    }
    startTransfer();
}

void DownloadTask::startTransfer() {
    if (m_reply || m_segmentedDownloader) return;
    const QFileInfo partialInfo(partPath());
    if (partialInfo.isSymLink()) {
        transitionDownloadState(m_record, DownloadState::Failed);
        m_record.retryable = false;
        m_record.error = QStringLiteral("The partial-file path is a symbolic link. Choose another filename or remove the link before retrying.");
        emit changed();
        emit finished(this);
        return;
    }
    const bool canSegment = !m_forceSingleStream && m_record.connectionCount > 1 && m_record.rangeSupported
        && m_record.total > 0 && (!m_record.etag.isEmpty() || !m_record.lastModified.isEmpty())
        && partialInfo.size() == 0;
    if (!canSegment && m_record.total > 0) {
        const qint64 existingBytes = std::max<qint64>(0, QFileInfo(partPath()).size());
        const qint64 remainingBytes = std::max<qint64>(0, m_record.total - std::min(m_record.total, existingBytes));
        if (!destinationHasSpace(m_record.destination, remainingBytes)) {
            transitionDownloadState(m_record, DownloadState::Failed);
            m_record.retryable = false;
            m_record.error = QStringLiteral("Not enough free disk space in the destination to finish this download. Free space or choose another folder; any partial file was kept.");
            emit changed();
            emit finished(this);
            return;
        }
    }
    if (canSegment) {
        startSegmentedTransfer();
        return;
    }
    QDir().mkpath(m_record.destination); m_offset=QFileInfo(partPath()).size();
    m_responseTotal = 0;
    m_expectedResponseBytes = -1;
    if (!openFileNoFollow(m_file, partPath(), QIODevice::WriteOnly | QIODevice::Append)) { transitionDownloadState(m_record, DownloadState::Failed); m_record.retryable=false; m_record.error="Unable to safely open partial file without following filesystem links."; emit changed(); emit finished(this); return; }
    if (m_offset > 0 && m_record.etag.isEmpty() && m_record.lastModified.isEmpty()) {
        if (!m_file.resize(0) || !m_file.seek(0)) {
            transitionDownloadState(m_record, DownloadState::Failed);
            m_record.error="The partial file could not be safely restarted because no validator was saved.";
            m_file.close(); emit changed(); emit finished(this); return;
        }
        m_offset=0;
        m_record.received=0;
        m_record.total=0;
    }
    QNetworkRequest request(QUrl(m_record.url));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept-Encoding", "identity");
    if(m_offset) {
        request.setRawHeader("Range",QByteArray("bytes=")+QByteArray::number(m_offset)+"-");
        if (!m_record.etag.isEmpty()) request.setRawHeader("If-Range",m_record.etag.toUtf8());
        else if (!m_record.lastModified.isEmpty()) request.setRawHeader("If-Range",m_record.lastModified.toUtf8());
    }
    m_activeDurationAtStart=m_record.activeDurationMs;
    m_transferClock.start();
    m_speedClock.start(); m_speedSampleBytes=m_record.received;
    m_speedIdleTimer.start();
    if (m_limiter) m_limiter->addConsumer(this);
    transitionDownloadState(m_record, DownloadState::Downloading); m_reply=m_network->get(request);
    updateBandwidthBuffer();
    connect(m_reply,&QNetworkReply::metaDataChanged,this,[this]{
        if (!m_reply) return;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 206) m_record.rangeSupported = true;
        else if ((m_offset > 0 && status == 200)
                 || m_reply->rawHeader("Accept-Ranges").trimmed().toLower() == "none")
            m_record.rangeSupported = false;
        const auto responseEtag=QString::fromUtf8(m_reply->rawHeader("ETag"));
        const auto responseModified=QString::fromUtf8(m_reply->rawHeader("Last-Modified"));
        if (m_offset > 0 && status == 416) {
            m_restartFromZero=true;
            m_reply->abort();
            return;
        }
        if (status >= 400) {
            if (status == 401) {
                m_authChallengeInProgress = true;
                const QString origin = authenticationOrigin(m_reply->url());
                if (!m_httpCredentials.contains(origin)) {
                    m_record.error = QStringLiteral("This server requires a username and password.");
                    if (!m_authPromptPending) {
                        m_authPromptPending = true;
                        emit authenticationRequested(m_record.id, origin, authenticationHost(m_reply->url()), QString{});
                    }
                } else if (m_authRejected) {
                    m_record.error = QStringLiteral("The server rejected the supplied credentials.");
                    if (!m_authPromptPending) {
                        m_authPromptPending = true;
                        emit authenticationRequested(m_record.id, origin, authenticationHost(m_reply->url()), QString{});
                    }
                    m_reply->abort();
                } else {
                    m_record.error = QStringLiteral("Waiting for the server to verify the supplied credentials.");
                }
                return;
            }
            m_record.error=safeHttpError(status);
            m_reply->abort();
            return;
        }
        if (m_authChallengeInProgress) {
            m_authChallengeInProgress = false;
            m_record.error.clear();
        }
        if (m_offset == 0 && status > 0 && status != 200) {
            m_record.error=QStringLiteral("The server returned unexpected HTTP %1 for a full download.").arg(status);
            m_reply->abort();
            return;
        }
        if (m_offset > 0 && status != 200 && ((!m_record.etag.isEmpty() && responseEtag != m_record.etag)
            || (!m_record.lastModified.isEmpty() && responseModified != m_record.lastModified))) {
            m_restartFromZero=true;
            m_reply->abort();
            return;
        }
        if (m_offset > 0 && status == 200) {
            if (!m_file.resize(0) || !m_file.seek(0)) { m_record.error="Unable to restart the partial file safely."; m_reply->abort(); return; }
            m_offset=0; m_record.received=0; m_record.total=0; m_record.etag.clear(); m_record.lastModified.clear();
            m_speedSampleBytes=0; m_speedClock.restart();
        } else if (m_offset > 0 && status != 206) {
            m_record.error=status == 416 ? "The saved partial file no longer matches the server resource." : "The server did not confirm the requested byte range.";
            m_reply->abort(); return;
        }
        if (m_offset > 0) {
            const auto range=QString::fromLatin1(m_reply->rawHeader("Content-Range"));
            static const QRegularExpression rangePattern(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+|\\*)$"), QRegularExpression::CaseInsensitiveOption);
            const auto match=rangePattern.match(range);
            bool validStart = false;
            bool validEnd = false;
            const qint64 rangeStart = match.hasMatch() ? match.captured(1).toLongLong(&validStart) : -1;
            const qint64 rangeEnd = match.hasMatch() ? match.captured(2).toLongLong(&validEnd) : -1;
            bool validTotal = false;
            const qint64 rangeTotal = match.hasMatch() && match.captured(3) != QStringLiteral("*")
                ? match.captured(3).toLongLong(&validTotal) : 0;
            if (!match.hasMatch() || !validStart || !validEnd || rangeStart != m_offset
                || rangeEnd < rangeStart || !validTotal
                || (validTotal && rangeTotal <= rangeEnd)) {
                m_record.error="The server returned an invalid Content-Range for resume."; m_reply->abort();
            } else if (m_record.total > 0 && (match.captured(3) == QStringLiteral("*")
                       || rangeTotal != m_record.total)) {
                m_restartFromZero=true; m_reply->abort();
            } else {
                m_responseTotal = rangeTotal;
                m_expectedResponseBytes = rangeEnd - rangeStart + 1;
            }
        }
        if (m_offset == 0) {
            // Validators from HEAD (or an earlier attempt) must not be carried
            // forward when the actual full GET response does not provide them.
            m_record.etag = responseEtag;
            m_record.lastModified = responseModified;
        } else {
            if (!responseEtag.isEmpty()) m_record.etag=responseEtag;
            if (!responseModified.isEmpty()) m_record.lastModified=responseModified;
        }
        bool hasContentLength=false;
        const auto contentLength=m_reply->rawHeader("Content-Length").toLongLong(&hasContentLength);
        if (hasContentLength && contentLength >= 0) {
            if (m_offset > 0 && m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 206) {
                const auto range=QString::fromLatin1(m_reply->rawHeader("Content-Range"));
                static const QRegularExpression rangePattern(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+|\\*)$"), QRegularExpression::CaseInsensitiveOption);
                const auto match=rangePattern.match(range);
                bool startOk=false, endOk=false;
                const qint64 start=match.hasMatch()?match.captured(1).toLongLong(&startOk):-1;
                const qint64 end=match.hasMatch()?match.captured(2).toLongLong(&endOk):-1;
                if (!match.hasMatch() || !startOk || !endOk || contentLength <= 0
                    || end < start || end - start != contentLength - 1
                    || (m_expectedResponseBytes >= 0 && contentLength != m_expectedResponseBytes)) {
                    m_record.error="The server returned a mismatched Content-Length for the resumed range.";
                    m_reply->abort();
                }
            } else {
                m_responseTotal=contentLength;
            }
        }
        if (m_responseTotal > 0 || m_record.total == 0) m_record.total=m_responseTotal;
        m_record.contentType=QString::fromUtf8(m_reply->rawHeader("Content-Type"));
    });
    connect(m_reply,&QNetworkReply::readyRead,this,[this]{ drainReplyData(); });
    connect(m_reply,&QNetworkReply::downloadProgress,this,[this](qint64 done,qint64){
        m_record.activeDurationMs=m_activeDurationAtStart+m_transferClock.elapsed();
        persistProgress(m_offset+done,m_responseTotal > 0 ? m_responseTotal : m_record.total);
        const qint64 elapsed=m_speedClock.elapsed();
        if (elapsed>=750) { m_record.bytesPerSecond=(m_record.received-m_speedSampleBytes)*1000/elapsed; m_speedSampleBytes=m_record.received; m_speedClock.restart(); }
        emit changed();
    });
    m_finishReply = [this]{
        if (m_restartFromZero && !m_pauseRequested && !m_cancelRequested) {
            m_record.activeDurationMs=m_activeDurationAtStart+m_transferClock.elapsed();
            m_reply->readAll();
            m_reply->deleteLater();
            m_reply=nullptr;
            m_file.close();
            if (!openFileNoFollow(m_file, partPath(), QIODevice::WriteOnly | QIODevice::Truncate)) {
                m_speedIdleTimer.stop();
                m_record.bytesPerSecond=0;
                transitionDownloadState(m_record, DownloadState::Failed);
                m_record.error="The changed partial file could not be safely restarted.";
                m_restartFromZero=false;
                emit changed(); emit finished(this); return;
            }
            m_file.close();
            m_offset=0; m_record.received=0; m_record.total=0; m_record.bytesPerSecond=0;
            m_responseTotal=0;
            m_record.etag.clear(); m_record.lastModified.clear(); m_record.error.clear(); m_record.retryable=false;
            m_restartFromZero=false;
            start();
            return;
        }
        m_restartFromZero=false;
        m_speedIdleTimer.stop();
        m_record.activeDurationMs=m_activeDurationAtStart+m_transferClock.elapsed();
        m_record.bytesPerSecond=0;
        const int status=m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 503) {
            const QByteArray retryAfter = m_reply->rawHeader("Retry-After").trimmed();
            bool secondsOk = false;
            const qint64 delaySeconds = retryAfter.toLongLong(&secondsOk);
            qint64 requestedAt = 0;
            if (secondsOk && delaySeconds >= 0) {
                requestedAt = QDateTime::currentSecsSinceEpoch() + std::min<qint64>(delaySeconds, 86400);
            } else if (!retryAfter.isEmpty()) {
                const QDateTime date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
                if (date.isValid()) {
                    const qint64 delay = std::max<qint64>(0, date.toSecsSinceEpoch() - QDateTime::currentSecsSinceEpoch());
                    requestedAt = QDateTime::currentSecsSinceEpoch() + std::min<qint64>(delay, 86400);
                }
            }
            if (requestedAt > 0) m_record.nextRetryAt = requestedAt;
        }
        const auto tail=m_reply->readAll();
        const bool discardTail=status>=400 || !m_record.error.isEmpty() || m_cancelRequested;
        if (!discardTail && m_file.write(tail) != tail.size() && !m_pauseRequested) {
            m_diskWriteFailed = true;
            m_record.error = destinationAvailableBytes(m_record.destination) == 0
                ? QStringLiteral("The destination ran out of free disk space; the partial file was kept.")
                : QStringLiteral("A disk write failed; check the destination and available space. The partial file was kept.");
        }
        const auto networkError=m_reply->error();
        const bool httpTransient=status==408 || status==429 || status==500 || status==502 || status==503 || status==504;
        const bool networkTransient=networkError==QNetworkReply::RemoteHostClosedError
            || networkError==QNetworkReply::TemporaryNetworkFailureError
            || networkError==QNetworkReply::NetworkSessionFailedError
            || networkError==QNetworkReply::TimeoutError;
        m_record.retryable=!m_diskWriteFailed && (httpTransient || networkTransient);
        bool flushed = m_file.flush();
        if (!flushed) {
            m_diskWriteFailed = true;
            const QString diskError = destinationAvailableBytes(m_record.destination) == 0
                ? QStringLiteral("The destination ran out of free disk space while saving the partial file.")
                : QStringLiteral("Could not flush the partial file to disk; check available space and permissions.");
            if (m_record.error.isEmpty()) m_record.error = diskError;
            else m_record.error += QStringLiteral(" ") + diskError;
            m_record.retryable = false;
        }
        m_file.close();
        m_authRepliesUsed.remove(m_reply);
        m_reply->deleteLater(); m_reply=nullptr;
        m_record.received=QFileInfo(partPath()).size();
        bool ok=networkError==QNetworkReply::NoError && m_record.error.isEmpty() && !m_pauseRequested && !m_cancelRequested;
        if (ok && status == 206 && m_expectedResponseBytes >= 0
            && m_record.received - m_offset != m_expectedResponseBytes) {
            m_record.error = QStringLiteral("The server sent a different number of bytes than its Content-Range declared.");
            m_record.retryable = false;
            ok = false;
        }
        if (ok || !m_record.retryable) m_record.nextRetryAt = 0;
        if(m_pauseRequested && !m_diskWriteFailed) { transitionDownloadState(m_record, DownloadState::Paused); m_record.error.clear(); m_record.retryable=false; }
        else if(m_cancelRequested) {
            transitionDownloadState(m_record, DownloadState::Canceled);
            m_record.retryable=false;
            const auto partial=partPath();
            if (m_deletePartialOnCancel && QFileInfo::exists(partial) && !QFile::remove(partial))
                m_record.error="Download canceled, but the partial file could not be deleted.";
            m_record.received=QFileInfo::exists(partial) ? QFileInfo(partial).size() : 0;
        }
        else if (ok && status == 206 && m_expectedResponseBytes > 0
                 && m_record.total > 0 && m_record.received < m_record.total) {
            // Some servers cap a valid open-ended range response before EOF.
            // Continue from the exact flushed byte count with the same validator.
            m_expectedResponseBytes = -1;
            m_record.error.clear();
            m_record.retryable = false;
            m_record.bytesPerSecond = 0;
            emit changed();
            startTransfer();
            return;
        }
        else {
            transitionDownloadState(m_record, ok?DownloadState::Completed:DownloadState::Failed);
            m_record.error=ok?QString{}:(m_record.error.isEmpty()?safeNetworkError(networkError):m_record.error);
            if(ok) {
                if(m_record.total>0 && m_record.received!=m_record.total) {
                    transitionDownloadState(m_record, DownloadState::Failed);
                    m_record.error="The downloaded size did not match the server's declared size.";
                } else {
                    QString publishedName;
                    if (publishFile(partPath(), m_record.destination, m_record.fileName,
                                    m_record.overwriteExisting, &publishedName)) {
                        m_record.fileName = publishedName;
                    } else {
                        transitionDownloadState(m_record, DownloadState::Failed);
                        m_record.error = QStringLiteral("Could not safely publish the completed file. Existing files were left unchanged and the partial file was kept.");
                    }
                }
            }
        }
        m_pauseRequested=false; m_cancelRequested=false; emit changed(); emit finished(this);
    };
    connect(m_reply,&QNetworkReply::finished,this,[this]{
        drainReplyData();
        if (m_reply && m_reply->bytesAvailable() > 0 && !m_cancelRequested
            && !m_restartFromZero && m_record.error.isEmpty()) {
            m_replyFinished = true;
            m_readTimer.start();
            return;
        }
        finishReply();
    });
    emit changed();
}

void DownloadTask::startSegmentedTransfer() {
    if (m_segmentedDownloader || m_reply) return;
    if (!QDir().mkpath(m_record.destination)) {
        transitionDownloadState(m_record, DownloadState::Failed);
        m_record.retryable = false;
        m_record.error = QStringLiteral("Could not create the destination folder for segmented download.");
        emit changed();
        emit finished(this);
        return;
    }
    m_activeDurationAtStart = m_record.activeDurationMs;
    m_transferClock.start();
    m_speedClock.start();
    m_speedSampleBytes = m_record.received;
    m_speedIdleTimer.start();
    if (m_limiter) m_limiter->addConsumer(this);
    transitionDownloadState(m_record, DownloadState::Downloading);
    m_segmentedDownloader = std::make_unique<SegmentedDownloader>(
        partPath(), QUrl(m_record.url), m_record.total, m_record.etag, m_record.lastModified,
        m_record.connectionCount, m_network, m_limiter, this, this);
    for (auto it = m_httpCredentials.cbegin(); it != m_httpCredentials.cend(); ++it)
        m_segmentedDownloader->setHttpCredentials(it.key(), it->first, it->second);
    connect(m_segmentedDownloader.get(), &SegmentedDownloader::authenticationRequested, this,
            [this](const QString &origin, const QString &host, const QString &realm) {
        if (m_authPromptPending) return;
        m_authPromptPending = true;
        emit authenticationRequested(m_record.id, origin, host, realm);
    });
    connect(m_segmentedDownloader.get(), &SegmentedDownloader::progress, this,
            [this](qint64 received, qint64 total) {
        m_record.activeDurationMs = m_activeDurationAtStart + m_transferClock.elapsed();
        m_record.received = received;
        m_record.total = total;
        const qint64 elapsed = m_speedClock.elapsed();
        if (elapsed >= 750) {
            m_record.bytesPerSecond = (received - m_speedSampleBytes) * 1000 / elapsed;
            m_speedSampleBytes = received;
            m_speedClock.restart();
        }
        emit changed();
    });
    connect(m_segmentedDownloader.get(), &SegmentedDownloader::verifying, this, [this] {
        transitionDownloadState(m_record, DownloadState::Verifying);
        emit changed();
    });
    connect(m_segmentedDownloader.get(), &SegmentedDownloader::completed, this,
            [this](bool success, bool fallback, bool canceled, bool retryable,
                   const QString &error, qint64 received, qint64 nextRetryAt) {
        m_speedIdleTimer.stop();
        m_record.activeDurationMs = m_activeDurationAtStart + m_transferClock.elapsed();
        m_record.bytesPerSecond = 0;
        m_record.received = received;
        m_record.retryable = retryable;
        m_record.nextRetryAt = nextRetryAt;
        auto *downloader = m_segmentedDownloader.release();
        downloader->deleteLater();
        if (m_limiter) m_limiter->removeConsumer(this);
        if (fallback) {
            m_forceSingleStream = true;
            m_record.rangeSupported = false;
            m_record.received = 0;
            m_record.total = 0;
            m_record.error.clear();
            m_record.nextRetryAt = 0;
            startTransfer();
            return;
        }
        if (m_cancelRequested || canceled) {
            transitionDownloadState(m_record, DownloadState::Canceled);
            m_record.retryable = false;
            if (m_deletePartialOnCancel && QFileInfo::exists(partPath()) && !QFile::remove(partPath()))
                m_record.error = QStringLiteral("Download canceled, but the partial file could not be deleted.");
            else
                m_record.error = error;
            if (m_deletePartialOnCancel) m_record.received = QFileInfo::exists(partPath()) ? QFileInfo(partPath()).size() : 0;
        } else if (m_pauseRequested && error.isEmpty()) {
            transitionDownloadState(m_record, DownloadState::Paused);
            m_record.retryable = false;
            m_record.error.clear();
        } else if (success) {
            m_record.received = QFileInfo(partPath()).size();
            transitionDownloadState(m_record, DownloadState::Verifying);
            if (m_record.received != m_record.total) {
                transitionDownloadState(m_record, DownloadState::Failed);
                m_record.error = QStringLiteral("The merged download size did not match the server's declared size.");
            } else {
                QString publishedName;
                if (publishFile(partPath(), m_record.destination, m_record.fileName,
                                m_record.overwriteExisting, &publishedName)) {
                    m_record.fileName = publishedName;
                    transitionDownloadState(m_record, DownloadState::Completed);
                    m_record.error.clear();
                } else {
                    transitionDownloadState(m_record, DownloadState::Failed);
                    m_record.error = QStringLiteral("Could not safely publish the completed segmented download. Existing files were left unchanged and the partial file was kept.");
                }
            }
        } else {
            transitionDownloadState(m_record, DownloadState::Failed);
            m_record.error = error.isEmpty() ? QStringLiteral("Segmented download failed.") : error;
        }
        m_pauseRequested = false;
        m_cancelRequested = false;
        emit changed();
        emit finished(this);
    });
    updateBandwidthBuffer();
    emit changed();
    m_segmentedDownloader->start();
}
void DownloadTask::pause() {
    if (m_segmentedDownloader) {
        m_pauseRequested = true;
        transitionDownloadState(m_record, DownloadState::Paused);
        m_segmentedDownloader->pause();
    } else if (m_reply) {
        m_pauseRequested = true;
        transitionDownloadState(m_record, DownloadState::Paused);
        m_reply->abort();
    }
}
void DownloadTask::updateBandwidthBuffer() {
    if (m_reply) m_reply->setReadBufferSize(m_limiter ? m_limiter->bufferSizeFor(this) : 256 * 1024);
    if (m_segmentedDownloader) m_segmentedDownloader->updateBandwidthBuffer();
}
void DownloadTask::cancel(bool deletePartial) {
    m_deletePartialOnCancel = deletePartial;
    if (m_segmentedDownloader) {
        m_cancelRequested = true;
        m_segmentedDownloader->cancel(deletePartial);
        return;
    }
    if(m_reply) { m_cancelRequested=true; m_reply->abort(); return; }
    m_file.close();
    const auto partial=partPath();
    const bool removed=!deletePartial || !QFileInfo::exists(partial) || QFile::remove(partial);
    transitionDownloadState(m_record, DownloadState::Canceled);
    m_record.retryable=false;
    m_record.received=QFileInfo::exists(partial) ? QFileInfo(partial).size() : 0;
    m_record.error=removed?QString{}:QStringLiteral("Download canceled, but the partial file could not be deleted.");
    emit changed(); emit finished(this);
}
void DownloadTask::persistProgress(qint64 received,qint64 total) { m_record.received=received; m_record.total=total; }

void DownloadTask::drainReplyData() {
    if (!m_reply) { m_readTimer.stop(); return; }
    if (m_cancelRequested || m_restartFromZero || !m_record.error.isEmpty()) {
        m_reply->readAll();
        m_readTimer.stop();
        return;
    }
    while (m_reply->bytesAvailable() > 0) {
        const qint64 available = std::min<qint64>(m_reply->bytesAvailable(), 16 * 1024);
        const qint64 allowed = m_limiter ? m_limiter->takeAvailable(this, available) : available;
        if (allowed <= 0) { m_readTimer.start(); return; }
        const auto bytes = m_reply->read(allowed);
        if (m_file.write(bytes) != bytes.size()) {
            m_diskWriteFailed = true;
            m_record.error = destinationAvailableBytes(m_record.destination) == 0
                ? QStringLiteral("The destination ran out of free disk space; the partial file was kept.")
                : QStringLiteral("A disk write failed; check the destination and available space. The partial file was kept.");
            m_reply->abort();
            return;
        }
    }
    m_readTimer.stop();
}

void DownloadTask::finishReply() {
    m_readTimer.stop();
    m_replyFinished = false;
    if (m_limiter) m_limiter->removeConsumer(this);
    if (!m_finishReply) return;
    auto finish = std::move(m_finishReply);
    finish();
}
