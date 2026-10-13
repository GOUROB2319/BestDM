#include "downloadqueue.h"
#include "securecredentialstore.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QTimer>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QNetworkRequest>
#include <QSettings>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
class SystemProxyFactory final : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query = QNetworkProxyQuery()) override {
        return QNetworkProxyFactory::systemProxyForQuery(query);
    }
};

QString canonicalHttpOrigin(const QString &origin) {
    const QUrl url(origin, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        || !url.path().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty()) return {};
    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    QString host = url.host().toLower();
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('['))) {
        host.prepend(QLatin1Char('['));
        host.append(QLatin1Char(']'));
    }
    return scheme + QStringLiteral("://") + host
        + QLatin1Char(':') + QString::number(url.port(defaultPort));
}
}

DownloadQueue::DownloadQueue(PersistenceRepository &repository, QObject *parent)
    : QObject(parent), m_repository(repository) {
    QSettings settings;
    m_proxyMode = settings.value(QStringLiteral("network/proxyMode"), QStringLiteral("Direct")).toString();
    m_proxyType = settings.value(QStringLiteral("network/proxyType"), QStringLiteral("HTTP")).toString();
    m_proxyHost = settings.value(QStringLiteral("network/proxyHost")).toString();
    m_proxyPort = settings.value(QStringLiteral("network/proxyPort"), 8080).toInt();
    m_proxyUsername = settings.value(QStringLiteral("network/proxyUsername")).toString();
    m_schedule.enabled = settings.value(QStringLiteral("schedule/enabled"), false).toBool();
    const auto defaultDays = QStringList{QStringLiteral("Mon"), QStringLiteral("Tue"), QStringLiteral("Wed"),
                                        QStringLiteral("Thu"), QStringLiteral("Fri")};
    for (const auto &day : settings.value(QStringLiteral("schedule/days"), defaultDays).toStringList()) {
        const int number = ScheduleWindow::dayNumber(day);
        if (number > 0) m_schedule.daysOfWeek.insert(number);
    }
    m_schedule.startTime = QTime::fromString(settings.value(QStringLiteral("schedule/startTime"),
                                                              QStringLiteral("09:00")).toString(), QStringLiteral("HH:mm"));
    m_schedule.endTime = QTime::fromString(settings.value(QStringLiteral("schedule/endTime"),
                                                            QStringLiteral("17:00")).toString(), QStringLiteral("HH:mm"));
    if (!m_schedule.startTime.isValid()) m_schedule.startTime = QTime(9, 0);
    if (!m_schedule.endTime.isValid()) m_schedule.endTime = QTime(17, 0);
    QString ignoredError;
    if (!setProxyConfiguration(m_proxyMode, m_proxyType, m_proxyHost, m_proxyPort,
                               m_proxyUsername, {}, false, &ignoredError))
        setProxyConfiguration(QStringLiteral("Direct"), QStringLiteral("HTTP"), {}, 8080, {}, {}, false);
    QString restoreError;
    m_records = m_repository.restoreAll(&restoreError);
    if (!restoreError.isEmpty()) m_startupPersistenceError = restoreError;
    for (auto &record : m_records) {
        const bool incomplete = record.state == DownloadState::Queued
            || record.state == DownloadState::WaitingForSchedule || record.state == DownloadState::Retrying
            || record.state == DownloadState::Downloading || record.state == DownloadState::Verifying
            || record.state == DownloadState::Probing || record.state == DownloadState::Paused;
        if (incomplete) {
            const QFileInfo destinationInfo(record.destination);
            if (record.destination.trimmed().isEmpty()
                || (destinationInfo.exists() && (!destinationInfo.isDir() || !destinationInfo.isWritable()))
                || (!destinationInfo.exists() && !QDir().mkpath(record.destination))) {
                if (transitionDownloadState(record, DownloadState::Failed)) {
                    record.retryable = false;
                    record.nextRetryAt = 0;
                    record.error = QStringLiteral("The saved destination folder is missing or unavailable. Choose a writable folder and retry the download.");
                    persist(record);
                }
                continue;
            }
        }
        if (record.state == DownloadState::Downloading || record.state == DownloadState::Verifying
            || record.state == DownloadState::Probing) {
            const auto partial = QDir(record.destination).filePath(record.fileName + ".part");
            const qint64 partialSize = QFileInfo(partial).size();
            qint64 segmentedBytes = 0;
            for (int index = 0; index < std::clamp(record.connectionCount, 1, 16); ++index)
                segmentedBytes += QFileInfo(partial + QStringLiteral(".seg.%1").arg(index)).size();
            const bool hasPartial = partialSize > 0 || segmentedBytes > 0;
            transitionDownloadState(record, hasPartial ? DownloadState::Paused : DownloadState::Failed);
            if (hasPartial) record.received = partialSize + segmentedBytes;
            if (record.state == DownloadState::Failed)
                record.error = QStringLiteral("The interrupted download has no partial file to resume.");
            persist(record);
        } else if (record.state == DownloadState::Paused) {
            const auto partial = QDir(record.destination).filePath(record.fileName + ".part");
            const qint64 partialSize = QFileInfo(partial).size();
            qint64 segmentedBytes = 0;
            for (int index = 0; index < std::clamp(record.connectionCount, 1, 16); ++index)
                segmentedBytes += QFileInfo(partial + QStringLiteral(".seg.%1").arg(index)).size();
            const qint64 savedBytes = partialSize + segmentedBytes;
            if (record.received != savedBytes || (savedBytes == 0 && record.error.isEmpty())) {
                record.received = savedBytes;
                if (savedBytes == 0)
                    record.error = QStringLiteral("No partial data was found. Resuming will restart this download from the beginning.");
                persist(record);
            }
        } else if (record.state == DownloadState::Completed) {
            const auto completedPath = QDir(record.destination).filePath(record.fileName);
            if (!QFileInfo::exists(completedPath)) {
                transitionDownloadState(record, DownloadState::Failed);
                record.error = QStringLiteral("The completed file is missing from its destination. Retry the download to restore it.");
                record.retryable = false;
                persist(record);
            }
        }
    }
    // History/model order is newest first; rebuild the waiting queue oldest first
    // so a restart does not reverse the user's FIFO order.
    for (auto it = m_records.crbegin(); it != m_records.crend(); ++it) {
        const auto &record = *it;
        if (record.state == DownloadState::Queued || record.state == DownloadState::WaitingForSchedule)
            createTask(record.id);
        else if (record.state == DownloadState::Retrying)
            scheduleRetry(record.id);
    }
    connect(&m_scheduleTimer, &QTimer::timeout, this, &DownloadQueue::updateSchedule);
    m_scheduleTimer.start(15000);
}

QStringList DownloadQueue::scheduleDays() const {
    QStringList names;
    for (int day = 1; day <= 7; ++day)
        if (m_schedule.daysOfWeek.contains(day)) names.append(ScheduleWindow::dayName(day));
    return names;
}

bool DownloadQueue::setScheduleSettings(bool enabled, const QStringList &days, const QString &startTime,
                                        const QString &endTime, QString *error) {
    ScheduleWindow next;
    next.enabled = enabled;
    next.startTime = QTime::fromString(startTime.trimmed(), QStringLiteral("HH:mm"));
    next.endTime = QTime::fromString(endTime.trimmed(), QStringLiteral("HH:mm"));
    if (!next.startTime.isValid() || !next.endTime.isValid()) {
        if (error) *error = QStringLiteral("Enter valid schedule times in 24-hour HH:mm format.");
        return false;
    }
    for (const auto &day : days) {
        const int number = ScheduleWindow::dayNumber(day);
        if (number == 0) {
            if (error) *error = QStringLiteral("Choose schedule days from Monday through Sunday.");
            return false;
        }
        next.daysOfWeek.insert(number);
    }
    if (enabled && next.daysOfWeek.isEmpty()) {
        if (error) *error = QStringLiteral("Select at least one day for the download schedule.");
        return false;
    }
    m_schedule = next;
    QSettings settings;
    settings.setValue(QStringLiteral("schedule/enabled"), enabled);
    settings.setValue(QStringLiteral("schedule/days"), scheduleDays());
    settings.setValue(QStringLiteral("schedule/startTime"), scheduleStartTime());
    settings.setValue(QStringLiteral("schedule/endTime"), scheduleEndTime());
    if (error) error->clear();
    updateSchedule();
    emit queueChanged();
    return true;
}

bool DownloadQueue::setProxyConfiguration(const QString &mode, const QString &type,
                                          const QString &host, int port,
                                          const QString &username, const QString &password,
                                          bool clearPassword, QString *error) {
    if (mode != QStringLiteral("Direct") && mode != QStringLiteral("System")
        && mode != QStringLiteral("Manual")) {
        if (error) *error = QStringLiteral("Choose Direct, System, or Manual proxy mode.");
        return false;
    }
    if (type != QStringLiteral("HTTP") && type != QStringLiteral("SOCKS5")) {
        if (error) *error = QStringLiteral("Choose HTTP or SOCKS5 for a manual proxy.");
        return false;
    }
    QString normalizedHost = host.trimmed();
    if (mode == QStringLiteral("Manual")) {
        QString urlHost = normalizedHost;
        if (urlHost.contains(QLatin1Char(':')) && !urlHost.startsWith(QLatin1Char('[')))
            urlHost = QStringLiteral("[") + urlHost + QStringLiteral("]");
        const QUrl parsed(QStringLiteral("http://") + urlHost, QUrl::StrictMode);
        if (normalizedHost.isEmpty() || !parsed.isValid() || parsed.host().isEmpty()
            || !parsed.path().isEmpty() || parsed.port(-1) != -1 || !parsed.userInfo().isEmpty()
            || parsed.host().compare(normalizedHost.startsWith(QLatin1Char('[')) && normalizedHost.endsWith(QLatin1Char(']'))
                                         ? normalizedHost.mid(1, normalizedHost.size() - 2) : normalizedHost,
                                     Qt::CaseInsensitive) != 0
            || port < 1 || port > 65535) {
            if (error) *error = QStringLiteral("Enter a valid proxy host and port from 1 to 65535.");
            return false;
        }
        normalizedHost = parsed.host();
    }
    const QString normalizedUsername = username.trimmed();
    const QString credentialTarget = SecureCredentialStore::proxyTarget(type, normalizedHost, port);
    const QString currentCredentialTarget = SecureCredentialStore::proxyTarget(m_proxyType, m_proxyHost, m_proxyPort);
    if (mode == QStringLiteral("Manual") && !password.isEmpty()
        && !SecureCredentialStore::saveProxyPassword(credentialTarget, normalizedUsername, password, error))
        return false;
    if (clearPassword && !SecureCredentialStore::clearProxyPassword(currentCredentialTarget, error)) return false;

    m_proxyMode = mode;
    m_proxyType = type;
    m_proxyHost = normalizedHost;
    m_proxyPort = port;
    m_proxyUsername = normalizedUsername;
    QSettings settings;
    settings.setValue(QStringLiteral("network/proxyMode"), m_proxyMode);
    settings.setValue(QStringLiteral("network/proxyType"), m_proxyType);
    settings.setValue(QStringLiteral("network/proxyHost"), m_proxyHost);
    settings.setValue(QStringLiteral("network/proxyPort"), m_proxyPort);
    settings.setValue(QStringLiteral("network/proxyUsername"), m_proxyUsername);

    if (m_proxyTestReply) {
        m_proxyTestReply->disconnect(this);
        m_proxyTestReply->abort();
        m_proxyTestReply->deleteLater();
        m_proxyTestReply = nullptr;
    }
    if (mode == QStringLiteral("System")) {
        m_network.setProxy(QNetworkProxy::DefaultProxy);
        m_network.setProxyFactory(new SystemProxyFactory);
    } else if (mode == QStringLiteral("Manual")) {
        m_network.setProxyFactory(nullptr);
        QNetworkProxy proxy(type == QStringLiteral("SOCKS5") ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy,
                            m_proxyHost, quint16(m_proxyPort));
        QString savedUser;
        QString savedPassword;
        if (SecureCredentialStore::loadProxyPassword(credentialTarget, &savedUser, &savedPassword)
            && savedUser == m_proxyUsername) {
            proxy.setUser(savedUser);
            proxy.setPassword(savedPassword);
        }
        m_network.setProxy(proxy);
    } else {
        m_network.setProxyFactory(nullptr);
        m_network.setProxy(QNetworkProxy::NoProxy);
    }
    if (error) error->clear();
    return true;
}

void DownloadQueue::testProxyConnection(const QUrl &url) {
    if (m_proxyTestReply) {
        m_proxyTestReply->disconnect(this);
        m_proxyTestReply->abort();
        m_proxyTestReply->deleteLater();
        m_proxyTestReply = nullptr;
    }
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        emit proxyTestFinished(false, QStringLiteral("Enter a valid HTTP or HTTPS test URL without embedded credentials."));
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    auto *reply = m_network.head(request);
    m_proxyTestReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_proxyTestReply != reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 400;
        const QString message = success
            ? QStringLiteral("Connection test succeeded (HTTP %1).").arg(status)
            : (status == 407
                ? QStringLiteral("The proxy requires credentials or rejected the saved credentials (HTTP 407).")
                : (status > 0
                    ? QStringLiteral("Proxy test received HTTP %1.").arg(status)
                    : QStringLiteral("Proxy test failed. Check the proxy address, network, and TLS connection.")));
        m_proxyTestReply = nullptr;
        reply->deleteLater();
        emit proxyTestFinished(success, message);
    });
}

DownloadQueue::~DownloadQueue() {
    const auto tasks = m_tasks.values();
    m_tasks.clear();
    m_waiting.clear();
    m_active.clear();
    for (auto *task : tasks) {
        task->disconnect(this);
        delete task;
    }
}

int DownloadQueue::indexOf(const QString &id) const {
    for (qsizetype i = 0; i < m_records.size(); ++i)
        if (m_records.at(i).id == id) return int(i);
    return -1;
}

DownloadRecord *DownloadQueue::findRecord(const QString &id) {
    const int index = indexOf(id);
    return index < 0 ? nullptr : &m_records[index];
}

QString DownloadQueue::reserveSuggestedFileName(const QString &id, const QString &desired,
                                                const QString &destination, bool overwriteExisting) const {
    const QFileInfo source(desired);
    const QString stem = source.completeBaseName();
    const QString suffix = source.suffix();
    const QDir directory(destination);
    for (int number = 0; number < 100000; ++number) {
        const QString candidate = number == 0 ? desired
            : QStringLiteral("%1 (%2)%3").arg(stem).arg(number)
                  .arg(suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
        bool hasTemporarySegments = false;
        for (int segment = 0; segment < 16; ++segment) {
            if (QFileInfo::exists(directory.filePath(candidate + QStringLiteral(".part.seg.%1").arg(segment)))) {
                hasTemporarySegments = true;
                break;
            }
        }
        const QFileInfo existing(directory.filePath(candidate));
        if (hasTemporarySegments || (existing.exists() && (!overwriteExisting || existing.isDir()))
            || QFileInfo::exists(directory.filePath(candidate + QStringLiteral(".part")))) continue;
        const bool alreadyReserved = std::any_of(m_records.cbegin(), m_records.cend(),
            [&](const DownloadRecord &record) {
                return record.id != id && record.destination.compare(destination, Qt::CaseInsensitive) == 0
                    && record.fileName.compare(candidate, Qt::CaseInsensitive) == 0
                    && record.state != DownloadState::Canceled;
            });
        if (!alreadyReserved) return candidate;
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
        + (suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
}

void DownloadQueue::setCategoryConfiguration(const CategoryRules &rules, bool automaticFolders) {
    m_categoryRules = rules;
    m_automaticCategoryFolders = automaticFolders;
}

void DownloadQueue::setMaximumConcurrent(int limit) {
    const int bounded = std::clamp(limit, 1, 16);
    if (m_maximumConcurrent == bounded) return;
    m_maximumConcurrent = bounded;
    schedule();
    emit queueChanged();
}

void DownloadQueue::setMaximumRetries(int limit) {
    const int bounded = std::clamp(limit, 0, 10);
    if (m_maximumRetries == bounded) return;
    m_maximumRetries = bounded;
    for (auto &record : m_records) {
        if (record.state == DownloadState::Retrying && record.retryCount > bounded) {
            transitionDownloadState(record, DownloadState::Failed);
            record.nextRetryAt = 0;
            record.error = QStringLiteral("The retry limit was lowered before this retry began.");
            persist(record);
            emit recordChanged(record);
        }
    }
    emit queueChanged();
}

void DownloadQueue::setSpeedLimitBytesPerSecond(qint64 limit) {
    m_bandwidthLimiter.setLimit(limit);
    for (auto *task : std::as_const(m_active)) task->updateBandwidthBuffer();
    emit queueChanged();
}

void DownloadQueue::startPending() {
    schedule();
    for (const auto &record : std::as_const(m_records))
        if (record.state == DownloadState::Retrying) scheduleRetry(record.id);
}

void DownloadQueue::enqueue(DownloadRecord record) {
    if (record.id.isEmpty() || indexOf(record.id) >= 0) return;
    transitionDownloadState(record, DownloadState::Queued);
    record.updatedAt = QDateTime::currentSecsSinceEpoch();
    if (!record.createdAt) record.createdAt = record.updatedAt;
    m_records.prepend(record);
    persist(record);
    emit recordAdded(record);
    createTask(record.id);
    schedule();
}

void DownloadQueue::createTask(const QString &id) {
    const auto *record = findRecord(id);
    if (!record || m_tasks.contains(id)) return;
    auto *task = new DownloadTask(*record, &m_network, &m_bandwidthLimiter, this);
    const auto credentials = m_httpCredentials.constFind(id);
    if (credentials != m_httpCredentials.cend()) {
        for (auto it = credentials->cbegin(); it != credentials->cend(); ++it)
            task->setHttpCredentials(it.key(), it->first, it->second);
    }
    m_tasks.insert(id, task);
    connect(task, &DownloadTask::changed, this, [this, task] {
        if (auto *current = findRecord(task->record().id)) {
            if (current->fileName != task->record().fileName) {
                const auto &taskRecord = task->record();
                const QString category = m_categoryRules.categoryFor(QUrl(taskRecord.url), taskRecord.fileName);
                QString destination = taskRecord.baseDestination.isEmpty()
                    ? taskRecord.destination : taskRecord.baseDestination;
                if (taskRecord.automaticCategoryFolder)
                    destination = QDir(destination).filePath(CategoryRules::safeFolderName(category));
                if (QDir().mkpath(destination)) {
                    const QString fileName = reserveSuggestedFileName(taskRecord.id, taskRecord.fileName, destination,
                                                                      taskRecord.overwriteExisting);
                    task->setFileLocation(fileName, destination, category);
                } else {
                    task->setFileLocation(current->fileName, current->destination, current->category);
                }
            }
            *current = task->record();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            current->updatedAt = now / 1000;
            const qint64 lastPersist = m_lastProgressPersistMs.value(current->id, 0);
            if (current->state != DownloadState::Downloading || lastPersist == 0 || now - lastPersist >= 500) {
                persist(*current);
                m_lastProgressPersistMs.insert(current->id, now);
            }
            emit recordChanged(*current);
        }
    });
    connect(task, &DownloadTask::finished, this, &DownloadQueue::taskFinished);
    connect(task, &DownloadTask::authenticationRequested, this,
            &DownloadQueue::httpAuthenticationRequested);
    m_waiting.append(task);
}

void DownloadQueue::pause(const QString &id) {
    auto *record = findRecord(id);
    auto *task = m_tasks.value(id, nullptr);
    if (!record || !task) return;
    if (!task->isActive()) {
        m_waiting.removeAll(task);
        m_tasks.remove(id);
        task->deleteLater();
        transitionDownloadState(*record, DownloadState::Paused);
        record->updatedAt = QDateTime::currentSecsSinceEpoch();
        persist(*record);
        emit recordChanged(*record);
        emit queueChanged();
        return;
    }
    task->pause();
}

void DownloadQueue::resume(const QString &id) {
    auto *record = findRecord(id);
    if (!record || (record->state != DownloadState::Paused && record->state != DownloadState::Failed
                    && record->state != DownloadState::Canceled)) return;
    if (record->state == DownloadState::Canceled) {
        const auto partial = QDir(record->destination).filePath(record->fileName + QStringLiteral(".part"));
        bool hasPartial = QFileInfo::exists(partial) && QFileInfo(partial).size() > 0;
        for (int index = 0; !hasPartial && index < std::clamp(record->connectionCount, 1, 16); ++index) {
            const auto segment = partial + QStringLiteral(".seg.%1").arg(index);
            hasPartial = QFileInfo::exists(segment) && QFileInfo(segment).size() > 0;
        }
        if (!hasPartial) return;
    }
    record->error.clear();
    transitionDownloadState(*record, DownloadState::Queued);
    record->updatedAt = QDateTime::currentSecsSinceEpoch();
    record->retryable = false;
    record->retryCount = 0;
    record->nextRetryAt = 0;
    persist(*record);
    emit recordChanged(*record);
    createTask(id);
    schedule();
}

void DownloadQueue::cancel(const QString &id, bool deletePartial) {
    auto *record = findRecord(id);
    if (!record) return;
    if (record->state != DownloadState::Queued && record->state != DownloadState::Probing
        && record->state != DownloadState::WaitingForSchedule && record->state != DownloadState::Downloading
        && record->state != DownloadState::Verifying && record->state != DownloadState::Paused && record->state != DownloadState::Failed
        && record->state != DownloadState::Retrying) return;
    auto *task = m_tasks.value(id, nullptr);
    if (task) {
        task->cancel(deletePartial);
        return;
    }
    const auto partial = QDir(record->destination).filePath(record->fileName + QStringLiteral(".part"));
    QStringList undeleted;
    if (deletePartial && QFileInfo::exists(partial) && !QFile::remove(partial)) undeleted.append(partial);
    qint64 retainedBytes = QFileInfo::exists(partial) ? QFileInfo(partial).size() : 0;
    for (int index = 0; index < std::clamp(record->connectionCount, 1, 16); ++index) {
        const auto segment = partial + QStringLiteral(".seg.%1").arg(index);
        if (QFileInfo::exists(segment)) {
            retainedBytes += QFileInfo(segment).size();
            if (deletePartial && !QFile::remove(segment)) undeleted.append(segment);
        }
    }
    transitionDownloadState(*record, DownloadState::Canceled);
    record->received = deletePartial && undeleted.isEmpty() ? 0 : retainedBytes;
    record->bytesPerSecond = 0;
    record->nextRetryAt = 0;
    record->error = undeleted.isEmpty() ? QString{} : QStringLiteral("Download canceled, but partial files could not be deleted: %1").arg(undeleted.join(QStringLiteral(", ")));
    record->updatedAt = QDateTime::currentSecsSinceEpoch();
    persist(*record);
    emit recordChanged(*record);
    emit queueChanged();
}

void DownloadQueue::remove(const QString &id, bool deleteFiles) {
    auto *record = findRecord(id);
    if (!record) return;
    auto *task = m_tasks.value(id, nullptr);
    if (task && task->isActive()) {
        // Keep the row until the in-flight reply aborts and any requested file
        // deletion has been attempted.
        m_removeAfterCancel.insert(id, deleteFiles);
        task->cancel(deleteFiles);
        return;
    }
    if (deleteFiles) {
        const auto partial = QDir(record->destination).filePath(record->fileName + QStringLiteral(".part"));
        const auto completed = QDir(record->destination).filePath(record->fileName);
        QStringList undeleted;
        if (QFileInfo::exists(partial) && !QFile::remove(partial)) undeleted.append(partial);
        if (QFileInfo::exists(completed) && !QFile::remove(completed)) undeleted.append(completed);
        for (int index = 0; index < std::clamp(record->connectionCount, 1, 16); ++index) {
            const auto segment = partial + QStringLiteral(".seg.%1").arg(index);
            if (QFileInfo::exists(segment) && !QFile::remove(segment)) undeleted.append(segment);
        }
        if (!undeleted.isEmpty()) {
            record->error = QStringLiteral("Could not delete: %1").arg(undeleted.join(QStringLiteral(", ")));
            record->updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(*record);
            emit recordChanged(*record);
            return;
        }
    }
    QString persistenceError;
    if (!m_repository.remove(id, &persistenceError)) { emit this->persistenceError(persistenceError); return; }
    if (task) {
        m_waiting.removeAll(task);
        m_tasks.remove(id);
        task->deleteLater();
    }
    const int index = indexOf(id);
    if (index >= 0) m_records.removeAt(index);
    m_httpCredentials.remove(id);
    m_authResumePending.remove(id);
    emit recordRemoved(id);
    schedule();
}

bool DownloadQueue::canChangeDestination(const QString &id) const {
    const int index = indexOf(id);
    if (index < 0) return false;
    const auto *record = &m_records.at(index);
    if (record->state != DownloadState::Queued && record->state != DownloadState::WaitingForSchedule
        && record->state != DownloadState::Paused && record->state != DownloadState::Canceled
        && record->state != DownloadState::Failed && record->state != DownloadState::Completed) return false;
    const auto *task = m_tasks.value(id, nullptr);
    return !task || !task->isActive();
}

bool DownloadQueue::changeDestination(const QString &id, const QString &destination, QString *error) {
    auto *record = findRecord(id);
    if (!record || !canChangeDestination(id)) {
        if (error) *error = QStringLiteral("This download can only change destination while inactive.");
        return false;
    }
    const QFileInfo folderInfo(destination.trimmed());
    if (destination.trimmed().isEmpty() || !folderInfo.exists() || !folderInfo.isDir() || !folderInfo.isWritable()) {
        if (error) *error = QStringLiteral("Choose an existing writable download folder.");
        return false;
    }
    const QString targetDirectory = QDir::cleanPath(folderInfo.absoluteFilePath());
    if (targetDirectory.compare(QDir::cleanPath(record->destination), Qt::CaseInsensitive) == 0) {
        if (error) error->clear();
        return true;
    }
    QTemporaryFile writeProbe(QDir(targetDirectory).filePath(QStringLiteral(".bestdm-write-check-XXXXXX")));
    if (!writeProbe.open()) {
        if (error) *error = QStringLiteral("BestDM cannot write to the selected destination folder.");
        return false;
    }
    writeProbe.close();
    writeProbe.remove();

    const QString newName = reserveSuggestedFileName(id, record->fileName, targetDirectory,
                                                      record->overwriteExisting);
    const QString oldPart = QDir(record->destination).filePath(record->fileName + QStringLiteral(".part"));
    const QString newPart = QDir(targetDirectory).filePath(newName + QStringLiteral(".part"));
    QList<QPair<QString, QString>> moves;
    if (record->state == DownloadState::Completed) {
        const QString oldOutput = QDir(record->destination).filePath(record->fileName);
        if (!QFileInfo::exists(oldOutput)) {
            if (error) *error = QStringLiteral("The completed file is missing from its current destination.");
            return false;
        }
        moves.append({oldOutput, QDir(targetDirectory).filePath(newName)});
    } else {
        if (QFileInfo::exists(oldPart)) moves.append({oldPart, newPart});
        for (int index = 0; index < std::clamp(record->connectionCount, 1, 16); ++index) {
            const QString suffix = QStringLiteral(".seg.%1").arg(index);
            const QString oldSegment = oldPart + suffix;
            if (QFileInfo::exists(oldSegment)) moves.append({oldSegment, newPart + suffix});
        }
    }
    for (const auto &move : std::as_const(moves)) {
        if (QFileInfo::exists(move.second)) {
            if (error) *error = QStringLiteral("A file already exists in the selected folder.");
            return false;
        }
    }
    QList<int> renamed;
    QList<int> copied;
    const auto rollbackDestination = [&] {
        QStringList failures;
        for (auto it = renamed.crbegin(); it != renamed.crend(); ++it) {
            const auto &move = moves.at(*it);
            if (!QFile::rename(move.second, move.first)) failures.append(move.second);
        }
        for (const int index : std::as_const(copied)) {
            const auto &move = moves.at(index);
            if (QFileInfo::exists(move.second) && !QFile::remove(move.second)) failures.append(move.second);
        }
        return failures;
    };
    for (int index = 0; index < moves.size(); ++index) {
        const auto &move = moves.at(index);
        if (QFile::rename(move.first, move.second)) {
            renamed.append(index);
        } else if (QFile::copy(move.first, move.second)
                   && QFileInfo(move.first).size() == QFileInfo(move.second).size()) {
            copied.append(index);
        } else {
            QStringList failures;
            if (QFileInfo::exists(move.second) && !QFile::remove(move.second))
                failures.append(move.second);
            failures.append(rollbackDestination());
            if (error) *error = failures.isEmpty()
                ? QStringLiteral("BestDM could not move the partial download files.")
                : QStringLiteral("Could not move partial files or restore their old paths: %1").arg(failures.join(QStringLiteral(", ")));
            return false;
        }
    }
    QList<int> removedSources;
    for (const int index : std::as_const(copied)) {
        const auto &move = moves.at(index);
        if (!QFile::remove(move.first)) {
            QStringList failures;
            QSet<int> preserveCopies;
            for (const int removedIndex : std::as_const(removedSources)) {
                const auto &removed = moves.at(removedIndex);
                if (!QFile::copy(removed.second, removed.first)
                    || QFileInfo(removed.second).size() != QFileInfo(removed.first).size()) {
                    failures.append(QStringLiteral("%1 (recoverable copy remains at %2)")
                                        .arg(removed.first, removed.second));
                    preserveCopies.insert(removedIndex);
                    if (QFileInfo::exists(removed.first)) QFile::remove(removed.first);
                }
            }
            for (auto it = renamed.crbegin(); it != renamed.crend(); ++it) {
                const auto &renamedMove = moves.at(*it);
                if (!QFile::rename(renamedMove.second, renamedMove.first)) failures.append(renamedMove.second);
            }
            for (const int copiedIndex : std::as_const(copied)) {
                if (preserveCopies.contains(copiedIndex)) continue;
                const auto &copiedMove = moves.at(copiedIndex);
                if (QFileInfo::exists(copiedMove.second) && !QFile::remove(copiedMove.second))
                    failures.append(copiedMove.second);
            }
            if (error) *error = failures.isEmpty()
                ? QStringLiteral("BestDM could not finish moving the partial download files.")
                : QStringLiteral("Could not finish moving or restore partial files: %1").arg(failures.join(QStringLiteral(", ")));
            return false;
        }
        removedSources.append(index);
    }

    record->fileName = newName;
    record->destination = targetDirectory;
    record->baseDestination = targetDirectory;
    record->automaticCategoryFolder = false;
    record->updatedAt = QDateTime::currentSecsSinceEpoch();
    if (auto *task = m_tasks.value(id, nullptr))
        task->setFileLocation(record->fileName, record->destination, record->category);
    persist(*record);
    emit recordChanged(*record);
    emit queueChanged();
    if (error) error->clear();
    return true;
}

bool DownloadQueue::changeQueueName(const QString &id, const QString &queueName, QString *error) {
    auto *record = findRecord(id);
    const QString normalized = queueName.trimmed();
    if (!record || normalized.isEmpty() || normalized.size() > 40
        || normalized.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")))) {
        if (error) *error = QStringLiteral("Choose a valid download queue.");
        return false;
    }
    if (record->queueName == normalized) {
        if (error) error->clear();
        return true;
    }
    const QString previousName = record->queueName;
    const qint64 previousUpdatedAt = record->updatedAt;
    record->queueName = normalized;
    if (auto *task = m_tasks.value(id, nullptr))
        task->setQueueName(normalized);
    record->updatedAt = QDateTime::currentSecsSinceEpoch();
    QString persistenceError;
    if (!m_repository.save(*record, &persistenceError)) {
        record->queueName = previousName;
        record->updatedAt = previousUpdatedAt;
        if (auto *task = m_tasks.value(id, nullptr)) task->setQueueName(previousName);
        emit persistenceError(persistenceError);
        if (error) *error = QStringLiteral("Could not save the queue change: %1").arg(persistenceError);
        return false;
    }
    emit recordChanged(*record);
    emit queueChanged();
    if (error) error->clear();
    return true;
}

void DownloadQueue::retryAllFailed() {
    const auto records = m_records;
    for (const auto &record : records)
        if (record.state == DownloadState::Failed) resume(record.id);
}

void DownloadQueue::retryFailedInQueue(const QString &queueName) {
    const auto records = m_records;
    for (const auto &record : records) {
        if (record.state == DownloadState::Failed
            && record.queueName.compare(queueName, Qt::CaseInsensitive) == 0)
            resume(record.id);
    }
}

bool DownloadQueue::provideHttpCredentials(const QString &id, const QString &origin,
                                           const QString &username, const QString &password) {
    auto *record = findRecord(id);
    const QString normalizedUsername = username.trimmed();
    const QString normalizedOrigin = canonicalHttpOrigin(origin);
    if (!record || normalizedUsername.isEmpty()
        || normalizedUsername.size() > 512 || password.size() > 4096 || normalizedOrigin.isEmpty())
        return false;
    auto &credentials = m_httpCredentials[id];
    const auto previous = credentials.constFind(normalizedOrigin);
    const bool hadPrevious = previous != credentials.cend();
    const QPair<QString, QString> previousCredentials = hadPrevious ? *previous : QPair<QString, QString>{};
    credentials.insert(normalizedOrigin, qMakePair(normalizedUsername, password));
    auto *task = m_tasks.value(id, nullptr);
    if (task) {
        m_authResumePending.insert(id);
        return true;
    }
    if (record->state == DownloadState::Failed) {
        resume(id);
        return true;
    }
    if (hadPrevious) credentials.insert(normalizedOrigin, previousCredentials);
    else credentials.remove(normalizedOrigin);
    if (credentials.isEmpty()) m_httpCredentials.remove(id);
    return false;
}

void DownloadQueue::persist(const DownloadRecord &record) {
    QString error;
    if (!m_repository.save(record, &error) && !error.isEmpty()) {
        if (m_startupPersistenceError.isEmpty()) m_startupPersistenceError = error;
        qWarning("Could not persist download state: %s", qPrintable(error));
        emit persistenceError(error);
    }
}

void DownloadQueue::updateSchedule() {
    const bool allowedNow = m_schedule.allows(QDateTime::currentDateTime());
    if (!allowedNow) {
        const auto activeTasks = m_active;
        for (auto *task : activeTasks) {
            if (!task) continue;
            const QString id = task->record().id;
            if (task->record().state != DownloadState::Downloading
                && task->record().state != DownloadState::Probing
                && task->record().state != DownloadState::Verifying) continue;
            m_schedulePausing.insert(id);
            task->pause();
        }
        for (auto *task : std::as_const(m_waiting)) {
            if (!task) continue;
            auto *record = findRecord(task->record().id);
            if (!record || record->state != DownloadState::Queued) continue;
            transitionDownloadState(*record, DownloadState::WaitingForSchedule);
            record->updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(*record);
            emit recordChanged(*record);
        }
    } else {
        for (auto &record : m_records) {
            if (record.state != DownloadState::WaitingForSchedule) continue;
            transitionDownloadState(record, DownloadState::Queued);
            record.updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(record);
            emit recordChanged(record);
            if (!m_tasks.contains(record.id)) createTask(record.id);
        }
    }
    schedule();
}

void DownloadQueue::schedule() {
    if (!m_schedule.allows(QDateTime::currentDateTime())) {
        for (auto *task : std::as_const(m_waiting)) {
            if (!task) continue;
            auto *record = findRecord(task->record().id);
            if (!record || record->state != DownloadState::Queued) continue;
            transitionDownloadState(*record, DownloadState::WaitingForSchedule);
            record->updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(*record);
            emit recordChanged(*record);
        }
        emit queueChanged();
        return;
    }
    while (m_active.size() < m_maximumConcurrent && !m_waiting.isEmpty()) {
        auto *task = m_waiting.takeFirst();
        if (!m_tasks.contains(task->record().id)) continue;
        if (auto *record = findRecord(task->record().id);
            record && record->state == DownloadState::WaitingForSchedule) {
            transitionDownloadState(*record, DownloadState::Queued);
            record->updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(*record);
            emit recordChanged(*record);
        }
        m_active.append(task);
        task->start();
        for (auto *activeTask : std::as_const(m_active)) activeTask->updateBandwidthBuffer();
    }
    emit queueChanged();
}

void DownloadQueue::taskFinished(DownloadTask *task) {
    if (!task) return;
    const QString id = task->record().id;
    m_waiting.removeAll(task);
    m_active.removeAll(task);
    m_tasks.remove(id);
    m_lastProgressPersistMs.remove(id);
    if (auto *record = findRecord(id)) {
        *record = task->record();
        if (m_schedulePausing.remove(id) && record->state == DownloadState::Paused)
            transitionDownloadState(*record, DownloadState::WaitingForSchedule);
        record->updatedAt = QDateTime::currentSecsSinceEpoch();
        if (record->state == DownloadState::Completed)
            record->completedAt = record->updatedAt;
        persist(*record);
        emit recordChanged(*record);
        const bool authResume = m_authResumePending.remove(id) && record->state == DownloadState::Failed;
        if (authResume) {
            record->error.clear();
            record->retryable = false;
            record->retryCount = 0;
            record->nextRetryAt = 0;
            transitionDownloadState(*record, DownloadState::Queued);
            record->updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(*record);
            emit recordChanged(*record);
            createTask(id);
        } else if (record->state == DownloadState::Failed && record->retryable && record->retryCount < m_maximumRetries) {
            ++record->retryCount;
            transitionDownloadState(*record, DownloadState::Retrying);
            record->retryable = false;
            record->bytesPerSecond = 0;
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            const int delayMs = 1000 * (1 << record->retryCount);
            const qint64 backoffAt = now + (delayMs + 999) / 1000;
            // Retry-After, when supplied for 429/503, is the earliest retry time.
            record->nextRetryAt = std::max(backoffAt, std::min(record->nextRetryAt, now + 86400));
            record->updatedAt = QDateTime::currentSecsSinceEpoch();
            persist(*record);
            emit recordChanged(*record);
            scheduleRetry(id);
        } else if (record->state == DownloadState::Failed) {
            record->nextRetryAt = 0;
            persist(*record);
            emit recordChanged(*record);
        }
        if (m_removeAfterCancel.contains(id)) {
            const bool deleteFiles = m_removeAfterCancel.take(id);
            if (deleteFiles && !record->error.isEmpty()) {
                // A failed file deletion keeps the row so the user can retry.
                persist(*record);
                emit recordChanged(*record);
            } else {
                QString persistenceError;
                if (!m_repository.remove(id, &persistenceError)) {
                    emit this->persistenceError(persistenceError);
                } else {
                    const int index = indexOf(id);
                    if (index >= 0) m_records.removeAt(index);
                    m_httpCredentials.remove(id);
                    m_authResumePending.remove(id);
                    emit recordRemoved(id);
                }
            }
        }
    }
    task->deleteLater();
    for (auto *activeTask : std::as_const(m_active)) activeTask->updateBandwidthBuffer();
    schedule();
}

void DownloadQueue::scheduleRetry(const QString &id) {
    auto *record = findRecord(id);
    if (!record || record->state != DownloadState::Retrying || m_retryTimers.contains(id)) return;
    m_retryTimers.insert(id);
    const qint64 remainingSeconds = record->nextRetryAt > 0
        ? std::max<qint64>(0, record->nextRetryAt - QDateTime::currentSecsSinceEpoch()) : 0;
    const qint64 maximumDelaySeconds = std::numeric_limits<int>::max() / 1000;
    const int delayMs = int(std::min(remainingSeconds, maximumDelaySeconds) * 1000);
    QTimer::singleShot(delayMs, this, [this, id] {
        m_retryTimers.remove(id);
        auto *current = findRecord(id);
        if (!current || current->state != DownloadState::Retrying) return;
        if (current->retryCount > m_maximumRetries) {
            transitionDownloadState(*current, DownloadState::Failed);
            current->nextRetryAt = 0;
            current->error = QStringLiteral("The configured retry limit has been reached.");
            persist(*current);
            emit recordChanged(*current);
            return;
        }
        transitionDownloadState(*current, DownloadState::Queued);
        current->nextRetryAt = 0;
        current->updatedAt = QDateTime::currentSecsSinceEpoch();
        persist(*current);
        emit recordChanged(*current);
        createTask(id);
        schedule();
    });
}
