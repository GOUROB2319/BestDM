#include "downloadtask.h"
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>

DownloadTask::DownloadTask(DownloadRecord record, QObject *parent) : QObject(parent), m_record(std::move(record)) {}
QString DownloadTask::partPath() const { return QDir(m_record.destination).filePath(m_record.fileName + ".part"); }
void DownloadTask::start() {
    if (m_reply || m_record.url.isEmpty()) return;
    QDir().mkpath(m_record.destination); m_offset=QFileInfo(partPath()).size(); m_file.setFileName(partPath());
    if (!m_file.open(QIODevice::WriteOnly|QIODevice::Append)) { m_record.state="Failed"; m_record.error="Unable to open partial file."; emit changed(); emit finished(this); return; }
    QNetworkRequest request(QUrl(m_record.url)); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy); if(m_offset) request.setRawHeader("Range",QByteArray("bytes=")+QByteArray::number(m_offset)+"-");
    m_record.state="Downloading"; m_reply=m_network.get(request);
    connect(m_reply,&QNetworkReply::readyRead,this,[this]{ m_file.write(m_reply->readAll()); });
    connect(m_reply,&QNetworkReply::downloadProgress,this,[this](qint64 done,qint64 total){ persistProgress(m_offset+done,total<0?0:m_offset+total); emit changed(); });
    connect(m_reply,&QNetworkReply::finished,this,[this]{ m_file.write(m_reply->readAll()); const auto error=m_reply->errorString(); const bool ok=m_reply->error()==QNetworkReply::NoError; m_file.close(); m_reply->deleteLater(); m_reply=nullptr; m_record.received=QFileInfo(partPath()).size(); m_record.state=ok?"Completed":"Failed"; m_record.error=ok?QString{}:error; if(ok) { const auto target=QDir(m_record.destination).filePath(m_record.fileName); if(!QFileInfo::exists(target) && QFile::rename(partPath(),target)) {} else { m_record.state="Failed"; m_record.error="Could not finalize without overwriting an existing file."; } } emit changed(); emit finished(this); });
    emit changed();
}
void DownloadTask::pause() { if(m_reply) { m_record.state="Paused"; m_reply->abort(); } }
void DownloadTask::cancel() { if(m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply=nullptr; } m_file.close(); QFile::remove(partPath()); m_record.state="Canceled"; emit changed(); emit finished(this); }
void DownloadTask::persistProgress(qint64 received,qint64 total) { m_record.received=received; m_record.total=total; }
