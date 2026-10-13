#include "segmenteddownloader.h"
#include "safefile.h"

#include <QDateTime>
#include <QAuthenticator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <algorithm>
#include <limits>
#include <utility>

struct SegmentedDownloader::Segment {
    qint64 first = 0;
    qint64 last = -1;
    qint64 received = 0;
    QString path;
    QFile file;
    QNetworkReply *reply = nullptr;
    bool headersValid = false;
    bool replyHasFinished = false;
    bool complete = false;
    bool invalid = false;
};

namespace {
bool transientNetworkError(QNetworkReply::NetworkError error) {
    return error == QNetworkReply::RemoteHostClosedError
        || error == QNetworkReply::TemporaryNetworkFailureError
        || error == QNetworkReply::NetworkSessionFailedError
        || error == QNetworkReply::TimeoutError;
}

QString httpFailureMessage(int status) {
    if (status == 401) return QStringLiteral("The server requires credentials. Enter a username and password, then retry.");
    if (status == 407) return QStringLiteral("The proxy requires credentials or rejected its configured credentials (HTTP 407).");
    if (status == 403) return QStringLiteral("The server denied access to this file (HTTP 403).");
    if (status == 404) return QStringLiteral("The requested file was not found (HTTP 404).");
    if (status == 408) return QStringLiteral("The server timed out handling a segment request (HTTP 408), a transient response.");
    if (status == 429) return QStringLiteral("The server is receiving too many requests (HTTP 429).");
    return QStringLiteral("The server returned HTTP %1 for a segment request.").arg(status);
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

qint64 availableBytes(const QString &path) {
    QStorageInfo storage(path);
    storage.refresh();
    return storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
}
}

SegmentedDownloader::SegmentedDownloader(QString partPath, QUrl url, qint64 total, QString etag,
                                         QString lastModified, int connections,
                                         QNetworkAccessManager *network, BandwidthLimiter *limiter,
                                         const void *limiterKey, QObject *parent)
    : QObject(parent), m_partPath(std::move(partPath)), m_url(std::move(url)), m_total(total),
      m_etag(std::move(etag)), m_lastModified(std::move(lastModified)),
      m_connectionCount(std::clamp(connections, 2, 16)), m_network(network),
      m_limiter(limiter), m_limiterKey(limiterKey) {
    connect(m_network, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
        const bool belongsToTask = std::any_of(m_segments.cbegin(), m_segments.cend(),
            [reply](const auto &segment) { return segment->reply == reply; });
        if (!belongsToTask || m_failed || m_fallback) return;
        const QString origin = authenticationOrigin(reply->url());
        const auto credentials = m_httpCredentials.constFind(origin);
        if (credentials != m_httpCredentials.cend() && !m_authRepliesUsed.contains(reply)) {
            authenticator->setUser(credentials->first);
            authenticator->setPassword(credentials->second);
            m_authRepliesUsed.insert(reply);
            m_authChallengeInProgress = true;
            m_error.clear();
            return;
        }
        if (credentials == m_httpCredentials.cend()) {
            if (!m_authPromptPending) {
                m_authPromptPending = true;
                emit authenticationRequested(origin, authenticationHost(reply->url()), authenticator->realm());
            }
            fail(QStringLiteral("This server requires a username and password."), false);
        } else {
            if (!m_authPromptPending) {
                m_authPromptPending = true;
                emit authenticationRequested(origin, authenticationHost(reply->url()), authenticator->realm());
            }
            fail(QStringLiteral("The server rejected the supplied credentials."), false);
        }
    });
    m_drainTimer.setInterval(20);
    connect(&m_drainTimer, &QTimer::timeout, this, [this] {
        for (const auto &segment : m_segments) {
            if (segment->reply) drain(*segment);
            if (segment->reply && segment->replyHasFinished && segment->reply->bytesAvailable() == 0)
                replyFinished(*segment);
        }
        finishWhenIdle();
    });
    const int count = int(std::min<qint64>(m_total, m_connectionCount));
    const qint64 length = m_total / count + (m_total % count == 0 ? 0 : 1);
    for (int index = 0; index < count; ++index) {
        const qint64 first = qint64(index) * length;
        if (first >= m_total) break;
        auto segment = std::make_unique<Segment>();
        segment->first = first;
        segment->last = first + std::min(length, m_total - first) - 1;
        segment->path = m_partPath + QStringLiteral(".seg.%1").arg(index);
        segment->file.setFileName(segment->path);
        m_segments.push_back(std::move(segment));
    }
}

SegmentedDownloader::~SegmentedDownloader() {
    m_drainTimer.stop();
    for (const auto &segment : m_segments) {
        segment->file.close();
        if (segment->reply) {
            auto *reply = segment->reply;
            segment->reply = nullptr;
            reply->disconnect(this);
            reply->abort();
        }
    }
}

void SegmentedDownloader::start() {
    if (m_total <= 0 || m_segments.empty() || !m_network) {
        fail(QStringLiteral("Segmented download requires a known positive file size."), false);
        finishWhenIdle();
        return;
    }
    qint64 existingBytes = 0;
    for (const auto &segment : m_segments) {
        if (QFileInfo(segment->path).isSymLink()) {
            fail(QStringLiteral("A temporary segment path is a symbolic link. Remove it before retrying."), false);
            finishWhenIdle();
            return;
        }
        const qint64 expected = segment->last - segment->first + 1;
        existingBytes += std::min(expected, std::max<qint64>(0, QFileInfo(segment->path).size()));
    }
    const qint64 remainingBytes = std::max<qint64>(0, m_total - existingBytes);
    const qint64 requiredBytes = remainingBytes > std::numeric_limits<qint64>::max() - m_total
        ? std::numeric_limits<qint64>::max() : remainingBytes + m_total;
    const qint64 available = availableBytes(QFileInfo(m_partPath).absolutePath());
    if (available >= 0 && available < requiredBytes) {
        fail(QStringLiteral("Not enough free disk space to download and merge this file. Free space or choose another folder; existing segments were kept."), false);
        finishWhenIdle();
        return;
    }
    for (const auto &segment : m_segments) {
        const qint64 expected = segment->last - segment->first + 1;
        const qint64 saved = QFileInfo(segment->path).size();
        if (saved > expected) {
            if (!QFile::remove(segment->path) && QFileInfo::exists(segment->path)) {
                fail(QStringLiteral("An oversized temporary segment could not be safely reset."), false);
                break;
            }
            segment->received = 0;
        } else {
            segment->received = saved;
        }
        segment->complete = segment->received == expected;
        if (!segment->complete && !m_failed) startSegment(*segment);
        if (m_failed) break;
    }
    emit progress(receivedBytes(), m_total);
    if (std::any_of(m_segments.cbegin(), m_segments.cend(), [](const auto &segment) { return segment->reply; }))
        m_drainTimer.start();
    else
        finishWhenIdle();
}

void SegmentedDownloader::startSegment(Segment &segment) {
    const qint64 expected = segment.last - segment.first + 1;
    if (QFileInfo(segment.path).isSymLink()) {
        fail(QStringLiteral("A temporary segment path is a symbolic link. Remove it before retrying."), false);
        return;
    }
    if (!openFileNoFollow(segment.file, segment.path, QIODevice::WriteOnly | QIODevice::Append)) {
        fail(QStringLiteral("Unable to open a temporary segment file for writing."), false);
        return;
    }
    if (segment.received < 0 || segment.received > expected) {
        segment.file.close();
        QFile::remove(segment.path);
        segment.received = 0;
        if (!openFileNoFollow(segment.file, segment.path, QIODevice::WriteOnly | QIODevice::Truncate)) {
            fail(QStringLiteral("Unable to restart a temporary segment file."), false);
            return;
        }
    }
    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept-Encoding", "identity");
    const qint64 offset = segment.first + segment.received;
    request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(offset)
                         + '-' + QByteArray::number(segment.last));
    if (!m_etag.isEmpty()) request.setRawHeader("If-Range", m_etag.toUtf8());
    else if (!m_lastModified.isEmpty()) request.setRawHeader("If-Range", m_lastModified.toUtf8());
    segment.reply = m_network->get(request);
    segment.reply->setReadBufferSize(m_limiter ? m_limiter->bufferSizeFor(m_limiterKey) : 256 * 1024);
    connect(segment.reply, &QNetworkReply::metaDataChanged, this, [this, &segment] {
        if (!segment.reply || segment.headersValid || m_failed || m_fallback) return;
        const int status = segment.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            m_fallback = true;
            abortReplies();
            return;
        }
        if (status == 416) {
            m_fallback = true;
            abortReplies();
            return;
        }
        if (status >= 400) {
            if (status == 401) {
                m_authChallengeInProgress = true;
                const QString origin = authenticationOrigin(segment.reply->url());
                if (!m_httpCredentials.contains(origin)) {
                    m_error = QStringLiteral("This server requires a username and password.");
                    if (!m_authPromptPending) {
                        m_authPromptPending = true;
                        emit authenticationRequested(origin, authenticationHost(segment.reply->url()), QString{});
                    }
                } else if (!m_authRepliesUsed.contains(segment.reply)) {
                    m_error = QStringLiteral("Waiting for the server to verify the supplied credentials.");
                }
                return;
            }
            const auto after = segment.reply->rawHeader("Retry-After").trimmed();
            bool numeric = false;
            const qint64 seconds = after.toLongLong(&numeric);
            if ((status == 429 || status == 503) && numeric && seconds >= 0)
                m_nextRetryAt = QDateTime::currentSecsSinceEpoch() + std::min<qint64>(seconds, 86400);
            else if ((status == 429 || status == 503) && !after.isEmpty()) {
                const auto date = QDateTime::fromString(QString::fromLatin1(after), Qt::RFC2822Date);
                if (date.isValid())
                    m_nextRetryAt = QDateTime::currentSecsSinceEpoch()
                        + std::min<qint64>(std::max<qint64>(0, date.toSecsSinceEpoch() - QDateTime::currentSecsSinceEpoch()), 86400);
            }
            fail(httpFailureMessage(status), status == 408 || status == 429 || status == 500 || status == 502 || status == 503 || status == 504,
                 m_nextRetryAt);
            return;
        }
        const auto responseEtag = QString::fromUtf8(segment.reply->rawHeader("ETag"));
        const auto responseModified = QString::fromUtf8(segment.reply->rawHeader("Last-Modified"));
        if ((!m_etag.isEmpty() && responseEtag != m_etag)
            || (m_etag.isEmpty() && !m_lastModified.isEmpty() && responseModified != m_lastModified)) {
            m_fallback = true;
            abortReplies();
            return;
        }
        static const QRegularExpression pattern(QStringLiteral("^bytes (\\d+)-(\\d+)/(\\d+)$"),
                                               QRegularExpression::CaseInsensitiveOption);
        const auto match = pattern.match(QString::fromLatin1(segment.reply->rawHeader("Content-Range")));
        bool startOk = false, endOk = false, totalOk = false;
        const qint64 expectedStart = segment.first + segment.received;
        const qint64 start = match.hasMatch() ? match.captured(1).toLongLong(&startOk) : -1;
        const qint64 end = match.hasMatch() ? match.captured(2).toLongLong(&endOk) : -1;
        const qint64 total = match.hasMatch() ? match.captured(3).toLongLong(&totalOk) : -1;
        if (status != 206 || !match.hasMatch() || !startOk || !endOk || !totalOk
            || start != expectedStart || end != segment.last || total != m_total) {
            m_fallback = true;
            abortReplies();
            return;
        }
        bool contentLengthOk = false;
        const qint64 contentLength = segment.reply->rawHeader("Content-Length").toLongLong(&contentLengthOk);
        if (contentLengthOk && contentLength != end - start + 1) {
            m_fallback = true;
            abortReplies();
            return;
        }
        segment.headersValid = true;
        if (m_authChallengeInProgress) {
            m_authChallengeInProgress = false;
            m_error.clear();
        }
        drain(segment);
    });
    connect(segment.reply, &QNetworkReply::readyRead, this, [this, &segment] { drain(segment); });
    connect(segment.reply, &QNetworkReply::finished, this, [this, &segment] {
        segment.replyHasFinished = true;
        drain(segment);
        if (segment.reply && segment.reply->bytesAvailable() == 0) replyFinished(segment);
        finishWhenIdle();
    });
}

void SegmentedDownloader::setHttpCredentials(const QString &origin, const QString &username, const QString &password) {
    m_httpCredentials.insert(origin, qMakePair(username, password));
}

void SegmentedDownloader::drain(Segment &segment) {
    if (!segment.reply) return;
    if (!segment.headersValid || m_failed || m_fallback || m_pausing || m_canceling) {
        if (m_failed || m_fallback || m_pausing || m_canceling) segment.reply->readAll();
        return;
    }
    while (segment.reply && segment.reply->bytesAvailable() > 0) {
        const qint64 expected = segment.last - segment.first + 1;
        const qint64 remaining = expected - segment.received;
        if (remaining <= 0) {
            segment.invalid = true;
            fail(QStringLiteral("The server sent more bytes than the requested segment range."), false);
            return;
        }
        const qint64 maximum = std::min<qint64>({segment.reply->bytesAvailable(), 16 * 1024, remaining});
        const qint64 allowance = m_limiter ? m_limiter->takeAvailable(m_limiterKey, maximum) : maximum;
        if (allowance <= 0) return;
        const auto bytes = segment.reply->read(allowance);
        if (segment.file.write(bytes) != bytes.size()) {
            const qint64 free = availableBytes(QFileInfo(m_partPath).absolutePath());
            fail(free == 0
                     ? QStringLiteral("The destination ran out of free disk space; partial segments were kept.")
                     : QStringLiteral("A segment could not be written to disk; check available space and permissions. Partial segments were kept."),
                 false);
            return;
        }
        segment.received += bytes.size();
        emit progress(receivedBytes(), m_total);
    }
}

void SegmentedDownloader::replyFinished(Segment &segment) {
    if (!segment.reply) return;
    auto *reply = segment.reply;
    const auto networkError = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!m_pausing && !m_canceling && !m_fallback && !m_failed) {
        if (networkError != QNetworkReply::NoError) {
            fail(QStringLiteral("A network error interrupted one or more download segments."), transientNetworkError(networkError));
        } else if (!segment.headersValid) {
            fail(status >= 400 ? httpFailureMessage(status)
                               : QStringLiteral("The server did not validate a requested download segment."), false);
        } else if (segment.received != segment.last - segment.first + 1) {
            fail(QStringLiteral("A download segment ended before all expected bytes arrived."), true);
        } else {
            segment.complete = true;
        }
    }
    if (!segment.file.flush() && !m_canceling && !m_fallback) {
        const QString diskError = availableBytes(QFileInfo(m_partPath).absolutePath()) == 0
            ? QStringLiteral("The destination ran out of free disk space while saving a segment; partial segments were kept.")
            : QStringLiteral("Could not flush a download segment to disk; partial segments were kept.");
        if (m_error.isEmpty()) m_error = diskError;
        else m_error += QStringLiteral(" ") + diskError;
        m_retryable = false;
        m_nextRetryAt = 0;
        m_failed = true;
        m_pausing = false;
        abortReplies();
    }
    segment.received = QFileInfo(segment.path).size();
    segment.file.close();
    if (segment.invalid) {
        QFile::remove(segment.path);
        segment.received = 0;
    }
    segment.reply = nullptr;
    m_authRepliesUsed.remove(reply);
    reply->deleteLater();
    finishWhenIdle();
}

void SegmentedDownloader::fail(QString message, bool retryable, qint64 retryAt) {
    if (m_failed || m_fallback || m_pausing || m_canceling) return;
    m_failed = true;
    m_error = std::move(message);
    m_retryable = retryable;
    m_nextRetryAt = std::max(m_nextRetryAt, retryAt);
    abortReplies();
}

void SegmentedDownloader::abortReplies() {
    for (const auto &segment : m_segments)
        if (segment->reply) segment->reply->abort();
}

void SegmentedDownloader::pause() {
    if (m_finished) return;
    m_pausing = true;
    abortReplies();
    finishWhenIdle();
}

void SegmentedDownloader::cancel(bool deletePartial) {
    if (m_finished) return;
    m_canceling = true;
    m_deletePartial = deletePartial;
    abortReplies();
    finishWhenIdle();
}

void SegmentedDownloader::updateBandwidthBuffer() {
    const qint64 size = m_limiter ? m_limiter->bufferSizeFor(m_limiterKey) : 256 * 1024;
    for (const auto &segment : m_segments)
        if (segment->reply) segment->reply->setReadBufferSize(size);
}

qint64 SegmentedDownloader::receivedBytes() const {
    qint64 sum = 0;
    for (const auto &segment : m_segments) sum += segment->received;
    return sum;
}

void SegmentedDownloader::finishWhenIdle() {
    if (m_finished || m_merging) return;
    const bool active = std::any_of(m_segments.cbegin(), m_segments.cend(), [](const auto &segment) {
        return segment->reply != nullptr;
    });
    if (active) {
        m_drainTimer.start();
        return;
    }
    m_drainTimer.stop();
    QString cleanupError;
    if (m_canceling) {
        m_finished = true;
        if (m_deletePartial) removeSegmentFiles(&cleanupError);
        qint64 remaining = 0;
        for (const auto &segment : m_segments) remaining += QFileInfo(segment->path).size();
        emit completed(false, false, true, false, cleanupError, remaining, 0);
        return;
    }
    if (m_pausing) {
        m_finished = true;
        emit completed(false, false, false, false, {}, receivedBytes(), 0);
        return;
    }
    if (m_fallback) {
        m_finished = true;
        removeSegmentFiles(&cleanupError);
        emit completed(false, cleanupError.isEmpty(), false, false,
                       cleanupError.isEmpty() ? QString{} : cleanupError, 0, 0);
        return;
    }
    if (m_failed) {
        m_finished = true;
        emit completed(false, false, false, m_retryable, m_error, receivedBytes(), m_nextRetryAt);
        return;
    }
    if (!std::all_of(m_segments.cbegin(), m_segments.cend(), [](const auto &segment) { return segment->complete; })) {
        m_finished = true;
        emit completed(false, false, false, false,
                       QStringLiteral("Not all download segments were completed."), receivedBytes(), 0);
        return;
    }
    mergeSegments();
}

void SegmentedDownloader::mergeSegments() {
    const qint64 available = availableBytes(QFileInfo(m_partPath).absolutePath());
    if (available >= 0 && available < m_total) {
        m_finished = true;
        emit completed(false, false, false, false,
                       QStringLiteral("Not enough free disk space to merge the segments. Existing segment files were kept."),
                       receivedBytes(), 0);
        return;
    }
    m_merging = true;
    emit verifying();
    m_mergeOutput = std::make_unique<QSaveFile>(m_partPath);
    if (!m_mergeOutput->open(QIODevice::WriteOnly)) {
        m_mergeOutput.reset();
        m_merging = false;
        m_finished = true;
        emit completed(false, false, false, false,
                       QStringLiteral("Could not create the merged partial file."), receivedBytes(), 0);
        return;
    }
    QTimer::singleShot(0, this, &SegmentedDownloader::mergeNextChunk);
}

void SegmentedDownloader::mergeNextChunk() {
    if (!m_merging || m_finished) return;
    if (!m_mergeInput) {
        if (m_mergeIndex >= m_segments.size()) {
            if (m_mergeWritten != m_total) {
                m_mergeOutput->cancelWriting();
                m_mergeOutput.reset();
                m_merging = false;
                m_finished = true;
                emit completed(false, false, false, false,
                               QStringLiteral("The merged file did not match the server's declared size."), receivedBytes(), 0);
                return;
            }
            if (!m_mergeOutput->commit()) {
                const qint64 free = availableBytes(QFileInfo(m_partPath).absolutePath());
                m_mergeOutput->cancelWriting();
                m_mergeOutput.reset();
                m_merging = false;
                m_finished = true;
                emit completed(false, false, false, false,
                               free == 0
                                   ? QStringLiteral("The destination ran out of free disk space while finalizing; existing segment files were kept.")
                                   : QStringLiteral("Could not finalize the merged file; check destination permissions. Existing segment files were kept."),
                               receivedBytes(), 0);
                return;
            }
            m_mergeOutput.reset();
            m_merging = false;
            QString cleanupError;
            removeSegmentFiles(&cleanupError);
            m_finished = true;
            emit completed(cleanupError.isEmpty(), false, false, false, cleanupError,
                           cleanupError.isEmpty() ? m_total : receivedBytes(), 0);
            return;
        }
        const auto &segment = m_segments[m_mergeIndex];
        m_mergeInput = std::make_unique<QFile>(segment->path);
        const qint64 expected = segment->last - segment->first + 1;
        if (!openFileNoFollow(*m_mergeInput, segment->path, QIODevice::ReadOnly) || m_mergeInput->size() != expected) {
            m_mergeOutput->cancelWriting();
            m_mergeOutput.reset();
            m_mergeInput.reset();
            m_merging = false;
            m_finished = true;
            emit completed(false, false, false, false,
                           QStringLiteral("A completed segment file could not be safely reopened for merging."), receivedBytes(), 0);
            return;
        }
    }
    QByteArray buffer(256 * 1024, '\0');
    const qint64 count = m_mergeInput->read(buffer.data(), buffer.size());
    if (count < 0 || (count > 0 && m_mergeOutput->write(buffer.constData(), count) != count)) {
        const qint64 free = availableBytes(QFileInfo(m_partPath).absolutePath());
        m_mergeOutput->cancelWriting();
        m_mergeOutput.reset();
        m_mergeInput.reset();
        m_merging = false;
        m_finished = true;
        emit completed(false, false, false, false,
                       free == 0
                           ? QStringLiteral("The destination ran out of free disk space while merging; existing segment files were kept.")
                           : QStringLiteral("A segment could not be safely merged to disk; existing segment files were kept."),
                       receivedBytes(), 0);
        return;
    }
    if (count == 0) {
        m_mergeInput->close();
        m_mergeInput.reset();
        ++m_mergeIndex;
    } else {
        m_mergeWritten += count;
    }
    QTimer::singleShot(0, this, &SegmentedDownloader::mergeNextChunk);
}

void SegmentedDownloader::removeSegmentFiles(QString *error) {
    bool allRemoved = true;
    QString blockedPath;
    for (const auto &segment : m_segments) {
        if (QFileInfo::exists(segment->path) && !QFile::remove(segment->path)) {
            allRemoved = false;
            blockedPath = segment->path;
        }
    }
    if (!allRemoved && error)
        *error = QStringLiteral("Could not remove temporary segment file: %1").arg(blockedPath);
}
