#include "downloadmanager.h"
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QDateTime>
#include <QUuid>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) { restore(); }
QString DownloadManager::defaultDestination() const { const auto path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation); return path.isEmpty() ? QDir::homePath() : path; }
QString DownloadManager::filenameForUrl(const QUrl &url) const { auto name = QFileInfo(url.path()).fileName(); if (name.isEmpty()) name = "download"; return name.replace(QRegularExpression(R"([\\/:*?\"<>|])"), "_"); }
void DownloadManager::start(const QString &address, const QString &destination) {
    const QUrl parsed(address.trimmed());
    if (!parsed.isValid() || (parsed.scheme() != "http" && parsed.scheme() != "https")) { m_error = "Enter a valid HTTP or HTTPS URL."; m_state = "Needs attention"; emit downloadChanged(); return; }
    if (m_reply) cancel(); m_id = QUuid::createUuid().toString(QUuid::WithoutBraces); m_createdAt = QDateTime::currentSecsSinceEpoch(); m_url = parsed.toString(); m_destination = destination.trimmed().isEmpty() ? defaultDestination() : destination.trimmed(); QDir().mkpath(m_destination); m_fileName = uniqueFileName(filenameForUrl(parsed)); m_received = QFileInfo(partPath()).size(); m_total = 0; m_error.clear(); beginRequest();
}
void DownloadManager::beginRequest() {
    m_file.setFileName(partPath()); if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) { m_error = "BestDM could not write to the selected destination."; m_state = "Failed"; emit downloadChanged(); return; }
    QNetworkRequest request{QUrl(m_url)}; request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy); request.setRawHeader("User-Agent", "BestDM/0.1"); if (m_received > 0) { request.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(m_received) + "-"); if (!m_etag.isEmpty()) request.setRawHeader("If-Range", m_etag.toUtf8()); else if (!m_lastModified.isEmpty()) request.setRawHeader("If-Range", m_lastModified.toUtf8()); }
    m_resumeOffset = m_received; m_lastSpeedBytes = m_received; m_bytesPerSecond = 0; m_speedClock.restart(); m_state = "Downloading"; m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, [this] {
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A server that ignored Range must restart cleanly, never append a second copy.
        if (m_resumeOffset > 0 && status == 200) {
            m_file.close();
            if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                m_reply->abort();
                m_error = "BestDM could not restart the partial file safely.";
                m_state = "Failed";
            } else {
                m_resumeOffset = m_received = 0;
            }
        }
        m_etag = QString::fromUtf8(m_reply->rawHeader("ETag"));
        m_lastModified = QString::fromUtf8(m_reply->rawHeader("Last-Modified"));
    });
    connect(m_reply, &QNetworkReply::readyRead, this, [this] { m_file.write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) { m_received = m_resumeOffset + done; m_total = total > 0 ? m_resumeOffset + total : 0; const qint64 elapsed = m_speedClock.elapsed(); if (elapsed >= 750) { m_bytesPerSecond = ((m_received - m_lastSpeedBytes) * 1000) / elapsed; m_lastSpeedBytes = m_received; m_speedClock.restart(); } emit downloadChanged(); });
    connect(m_reply, &QNetworkReply::finished, this, [this] { m_file.write(m_reply->readAll()); const auto code = m_reply->error(); const auto detail = m_reply->errorString(); m_file.close(); m_reply->deleteLater(); m_reply = nullptr; m_received = QFileInfo(partPath()).size(); if (code != QNetworkReply::NoError) { m_state = "Paused"; m_error = m_pauseRequested ? QString{} : detail; } else if (QFileInfo::exists(finalPath())) { m_state = "Failed"; m_error = "A file with this name already exists; it was not overwritten."; } else if (QFile::rename(partPath(), finalPath())) { m_state = "Completed"; m_total = m_received; m_error.clear(); emit completed(finalPath()); } else { m_state = "Failed"; m_error = "Could not finalize the downloaded file."; } m_pauseRequested = false; persist(); emit downloadChanged(); });
    persist(); emit downloadChanged();
}
void DownloadManager::pause() { if (!m_reply) return; m_pauseRequested = true; m_bytesPerSecond = 0; m_state = "Paused"; m_reply->abort(); persist(); emit downloadChanged(); }
void DownloadManager::resume() { if (!m_reply && !m_url.isEmpty()) { m_received = QFileInfo(partPath()).size(); m_error.clear(); beginRequest(); } }
void DownloadManager::cancel() { if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; } m_file.close(); QFile::remove(partPath()); m_repository.remove(m_id); m_received = m_total = 0; m_url.clear(); m_fileName.clear(); m_id.clear(); m_state = "Ready"; m_error.clear(); m_pauseRequested = false; emit downloadChanged(); }
QString DownloadManager::partPath() const { return QDir(m_destination).filePath(m_fileName + ".part"); }
QString DownloadManager::finalPath() const { return QDir(m_destination).filePath(m_fileName); }
QString DownloadManager::uniqueFileName(const QString &desired) const {
    const QFileInfo source(desired); const QString stem = source.completeBaseName(); const QString suffix = source.suffix();
    for (int number = 0; ; ++number) {
        const QString candidate = number == 0 ? desired : QStringLiteral("%1 (%2)%3").arg(stem).arg(number).arg(suffix.isEmpty() ? "" : "." + suffix);
        if (!QFileInfo::exists(QDir(m_destination).filePath(candidate)) && !QFileInfo::exists(QDir(m_destination).filePath(candidate + ".part"))) return candidate;
    }
}
void DownloadManager::persist() const { if (m_id.isEmpty()) return; DownloadRecord r; r.id=m_id; r.url=m_url; r.fileName=m_fileName; r.destination=m_destination; r.state=m_state; r.error=m_error; r.etag=m_etag; r.lastModified=m_lastModified; r.received=m_received; r.total=m_total; r.updatedAt=QDateTime::currentSecsSinceEpoch(); r.createdAt=m_createdAt; m_repository.save(r); }
void DownloadManager::restore() { const auto r=m_repository.restoreLatest(); m_id=r.id; m_url=r.url; m_destination=r.destination; m_fileName=r.fileName; m_state=r.state.isEmpty()?"Ready":r.state; m_error=r.error; m_etag=r.etag; m_lastModified=r.lastModified; m_total=r.total; m_createdAt=r.createdAt; if (!m_url.isEmpty() && QFileInfo::exists(partPath())) { m_received=QFileInfo(partPath()).size(); m_state="Paused"; } else if (m_state != "Completed") m_state="Ready"; }
