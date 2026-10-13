#include "downloadmanager.h"
#include "securecredentialstore.h"
#include "traycontroller.h"
#include "applicationprotocol.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringDecoder>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QThread>
#include <QStringList>
#include <QStandardPaths>
#include <QSet>
#include <QVariantList>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QWindow>
#include <algorithm>
#include <utility>

namespace {
bool pathEntryExists(const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

QString windowsDefenderToolPath() {
#ifdef Q_OS_WIN
    const QString programData = qEnvironmentVariable("ProgramData");
    const QString platformRoot = QDir(programData).filePath(QStringLiteral("Microsoft/Windows Defender/Platform"));
    const QFileInfoList versions = QDir(platformRoot).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo &version : versions) {
        const QString candidate = QDir(version.absoluteFilePath()).filePath(QStringLiteral("MpCmdRun.exe"));
        if (QFileInfo(candidate).isFile()) return candidate;
    }
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString installed = QDir(programFiles).filePath(QStringLiteral("Windows Defender/MpCmdRun.exe"));
    if (QFileInfo(installed).isFile()) return installed;
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    const QString installedX86 = QDir(programFilesX86).filePath(QStringLiteral("Windows Defender/MpCmdRun.exe"));
    if (QFileInfo(installedX86).isFile()) return installedX86;
    return QStandardPaths::findExecutable(QStringLiteral("MpCmdRun.exe"));
#else
    return {};
#endif
}
}

DownloadManager::DownloadManager(QObject *parent)
    : QAbstractListModel(parent), m_repository(), m_queue(m_repository, this), m_records(m_queue.records()) {
    QSettings settings;
    const QStringList savedQueues = settings.value(QStringLiteral("downloads/queueNames"),
        QStringList{QStringLiteral("Default")}).toStringList();
    for (const QString &savedName : savedQueues) {
        const QString name = savedName.trimmed();
        if (!name.isEmpty() && name.size() <= 40
            && std::none_of(m_queueNames.cbegin(), m_queueNames.cend(), [&](const QString &existing) {
                return existing.compare(name, Qt::CaseInsensitive) == 0;
            })) m_queueNames.append(name);
    }
    for (const DownloadRecord &record : std::as_const(m_records)) {
        const QString name = record.queueName.trimmed();
        if (!name.isEmpty() && !m_queueNames.contains(name, Qt::CaseInsensitive))
            m_queueNames.append(name);
    }
    if (!m_queueNames.contains(QStringLiteral("Default"), Qt::CaseInsensitive))
        m_queueNames.prepend(QStringLiteral("Default"));
    m_selectedQueueName = settings.value(QStringLiteral("downloads/selectedQueue"), m_queueNames.constFirst()).toString();
    const auto selectedIt = std::find_if(m_queueNames.cbegin(), m_queueNames.cend(), [&](const QString &name) {
        return name.compare(m_selectedQueueName, Qt::CaseInsensitive) == 0;
    });
    m_selectedQueueName = selectedIt == m_queueNames.cend() ? m_queueNames.constFirst() : *selectedIt;
    settings.setValue(QStringLiteral("downloads/queueNames"), m_queueNames);
    settings.setValue(QStringLiteral("downloads/selectedQueue"), m_selectedQueueName);
    m_protocolRegistered = ApplicationProtocol::isRegistered();
    if (QCoreApplication::applicationName() == QStringLiteral("BestDM")
        && settings.value(QStringLiteral("integration/registerBestDmProtocol"), false).toBool()) {
        QString protocolError;
        if (ApplicationProtocol::setRegistered(true, &protocolError))
            m_protocolRegistered = ApplicationProtocol::isRegistered();
        else
            setLastError(protocolError);
    }
    if (qobject_cast<QApplication *>(QCoreApplication::instance())) {
        m_tray = new TrayController(this);
        connect(m_tray, &TrayController::quitRequested, this, [] { QCoreApplication::quit(); });
    }
    m_notificationsEnabled = settings.value(QStringLiteral("notifications/enabled"), true).toBool();
    m_minimizeToTray = settings.value(QStringLiteral("tray/minimizeToTray"), false).toBool() && trayAvailable();
    m_defaultDownloadDirectory = settings.value(QStringLiteral("downloads/defaultDestination")).toString().trimmed();
    if (m_defaultDownloadDirectory.isEmpty() || !QFileInfo(m_defaultDownloadDirectory).isDir())
        m_defaultDownloadDirectory.clear();
    if (m_defaultDownloadDirectory.isEmpty()) {
        const auto standardDownloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        m_defaultDownloadDirectory = standardDownloads.isEmpty() ? QDir::homePath() : standardDownloads;
    }
    m_theme = settings.value(QStringLiteral("appearance/theme"), QStringLiteral("System")).toString();
    const QString savedPostDownloadAction = settings.value(QStringLiteral("downloads/postDownloadAction"),
                                                            QStringLiteral("None")).toString();
    if (savedPostDownloadAction == QStringLiteral("Open file") || savedPostDownloadAction == QStringLiteral("Open folder")
        || savedPostDownloadAction == QStringLiteral("Run program"))
        m_postDownloadAction = savedPostDownloadAction;
    m_postDownloadProgram = settings.value(QStringLiteral("downloads/postDownloadProgram")).toString();
    if (m_postDownloadAction == QStringLiteral("Run program") && m_postDownloadProgram.isEmpty())
        m_postDownloadAction = QStringLiteral("None");
    m_defenderScanEnabled = qobject_cast<QApplication *>(QCoreApplication::instance()) && defenderScanAvailable()
        && settings.value(QStringLiteral("downloads/scanWithDefender"), false).toBool();
#ifdef Q_OS_WIN
    const QString savedQueuePowerAction = settings.value(QStringLiteral("downloads/queuePowerAction"),
        settings.value(QStringLiteral("downloads/shutdownAfterQueue"), false).toBool()
            ? QStringLiteral("Shutdown") : QStringLiteral("None")).toString();
    if (savedQueuePowerAction == QStringLiteral("Shutdown") || savedQueuePowerAction == QStringLiteral("Sleep")
        || savedQueuePowerAction == QStringLiteral("Hibernate")) m_queuePowerAction = savedQueuePowerAction;
    if (m_queuePowerAction != QStringLiteral("None") && !powerActionAvailable())
        m_queuePowerAction = QStringLiteral("None");
#endif
    m_shutdownCountdownTimer.setInterval(1000);
    connect(&m_shutdownCountdownTimer, &QTimer::timeout, this, [this] {
        if (m_shutdownCountdownSeconds > 0) --m_shutdownCountdownSeconds;
        emit powerActionCountdownChanged();
        if (m_shutdownCountdownSeconds <= 0) launchWindowsPowerAction();
    });
    setCategoryRules(settings.value(QStringLiteral("downloads/categoryRules")).toString());
    m_automaticCategoryFolders = settings.value(QStringLiteral("downloads/automaticCategoryFolders"), false).toBool();
    m_queue.setCategoryConfiguration(m_categoryRules, m_automaticCategoryFolders);
    m_clipboardMonitoringEnabled = settings.value(QStringLiteral("privacy/clipboardMonitoringEnabled"), false).toBool();
    m_proxyMode = settings.value(QStringLiteral("network/proxyMode"), QStringLiteral("Direct")).toString();
    m_proxyType = settings.value(QStringLiteral("network/proxyType"), QStringLiteral("HTTP")).toString();
    m_proxyHost = settings.value(QStringLiteral("network/proxyHost")).toString();
    m_proxyPort = settings.value(QStringLiteral("network/proxyPort"), 8080).toInt();
    m_proxyUsername = settings.value(QStringLiteral("network/proxyUsername")).toString();
    m_connectionsPerDownload = std::clamp(settings.value(QStringLiteral("downloads/connectionsPerDownload"), 8).toInt(), 1, 16);
    m_maximumBatchSize = std::clamp(settings.value(QStringLiteral("downloads/maximumBatchSize"), 100).toInt(), 1, 500);
    const QString collisionPolicy = settings.value(QStringLiteral("downloads/collisionPolicy"), QStringLiteral("Rename")).toString();
    if (collisionPolicy == QStringLiteral("Rename") || collisionPolicy == QStringLiteral("Ask")
        || collisionPolicy == QStringLiteral("Overwrite")) m_collisionPolicy = collisionPolicy;
    auto *guiApplication = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (guiApplication) {
        auto *clipboard = guiApplication->clipboard();
        m_lastClipboardText = clipboard->text().trimmed();
        connect(clipboard, &QClipboard::dataChanged, this, [this, clipboard] {
            const QString text = clipboard->text().trimmed();
            if (text == m_lastClipboardText) return;
            m_lastClipboardText = text;
            if (!m_clipboardMonitoringEnabled || text.isEmpty()) return;
            if (text.size() > 4096 || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))) return;
            const QUrl url(text, QUrl::StrictMode);
            const QString scheme = url.scheme().toLower();
            if (url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
                && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")))
                emit clipboardUrlDetected(url.toString(QUrl::FullyEncoded));
        });
    }
    for (const auto &record : std::as_const(m_records)) {
        m_categoryNames.insert(record.category);
        if (m_queuePowerAction != QStringLiteral("None") && record.state != DownloadState::Completed
            && record.state != DownloadState::Failed && record.state != DownloadState::Canceled)
            m_powerActionSessionIds.insert(record.id);
    }
    m_powerActionArmed = !m_powerActionSessionIds.isEmpty();
    connect(&m_queue, &DownloadQueue::recordAdded, this, [this](const DownloadRecord &record) {
        if (m_shutdownCountdownTimer.isActive() || m_powerActionPromptVisible) cancelPowerAction();
        if (m_queuePowerAction != QStringLiteral("None")) {
            if (m_powerActionSessionIds.isEmpty() && !m_powerActionArmed)
                m_powerActionSessionHasCompletion = false;
            m_powerActionSessionIds.insert(record.id);
            m_powerActionArmed = true;
        }
        beginInsertRows({}, 0, 0);
        m_records.prepend(record);
        m_categoryNames.insert(record.category);
        endInsertRows();
        emit countsChanged();
        evaluateQueuePowerAction();
    });
    connect(&m_queue, &DownloadQueue::recordChanged, this, [this](const DownloadRecord &record) {
        const int row = indexOf(record.id);
        if (row < 0) return;
        const QString oldCategory = m_records.at(row).category;
        const DownloadState oldState = m_records.at(row).state;
        if (m_powerActionSessionIds.contains(record.id) && oldState != DownloadState::Completed
            && record.state == DownloadState::Completed)
            m_powerActionSessionHasCompletion = true;
        m_records[row] = record;
        m_categoryNames.insert(record.category);
        if (oldCategory != record.category && std::none_of(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &item) {
                return item.category == oldCategory;
            })) m_categoryNames.remove(oldCategory);
        emit dataChanged(index(row, 0), index(row, 0));
        emit countsChanged();
        evaluateQueuePowerAction();
        if (m_tray && m_notificationsEnabled && oldState != record.state) {
            if (record.state == DownloadState::Completed)
                m_tray->showNotification(QStringLiteral("Download complete"), record.fileName);
            else if (record.state == DownloadState::Failed &&
                     (!record.retryable || record.retryCount >= m_queue.maximumRetries()))
                m_tray->showNotification(QStringLiteral("Download failed"), record.fileName);
        }
        if (oldState != record.state && record.state == DownloadState::Completed) {
            const DownloadRecord completedRecord = record;
            if (m_defenderScanEnabled) {
                const QString filePath = QDir(record.destination).filePath(record.fileName);
                const QString scanner = windowsDefenderToolPath();
                if (scanner.isEmpty()) {
                    reportPostDownloadProcessError(QStringLiteral("Microsoft Defender scan"),
                        QStringLiteral("Microsoft Defender scan is enabled, but MpCmdRun.exe could not be found."));
                } else if (!QFileInfo(filePath).isFile()) {
                    reportPostDownloadProcessError(QStringLiteral("Microsoft Defender scan"),
                        QStringLiteral("The completed file is missing; Microsoft Defender scan was not started."));
                } else {
                    runPostDownloadProcess(record.id + QStringLiteral(":defender"), scanner,
                        {QStringLiteral("-Scan"), QStringLiteral("-ScanType"), QStringLiteral("3"),
                         QStringLiteral("-File"), filePath}, 30 * 60 * 1000,
                        QStringLiteral("Microsoft Defender scan"), [this, completedRecord](bool success) {
                            const int currentRow = indexOf(completedRecord.id);
                            if (!success || currentRow < 0
                                || m_records.at(currentRow).state != DownloadState::Completed) return;
                            if (m_tray && m_notificationsEnabled)
                                m_tray->showNotification(QStringLiteral("Security scan complete"), m_records.at(currentRow).fileName);
                            performPostDownloadAction(m_records.at(currentRow));
                        });
                }
            } else {
                performPostDownloadAction(completedRecord);
            }
        }
    });
    connect(&m_queue, &DownloadQueue::recordRemoved, this, [this](const QString &id) {
        const int row = indexOf(id);
        if (row < 0) return;
        const QString scanKey = id + QStringLiteral(":defender");
        if (auto *scan = m_postDownloadProcesses.take(scanKey)) {
            if (auto *timeout = scan->findChild<QTimer *>()) timeout->stop();
            scan->disconnect(this);
            if (scan->state() != QProcess::NotRunning) {
                scan->kill();
                scan->waitForFinished(1000);
            }
            scan->deleteLater();
        }
        const QString removedCategory = m_records.at(row).category;
        beginRemoveRows({}, row, row);
        m_records.removeAt(row);
        endRemoveRows();
        if (std::none_of(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &record) {
                return record.category == removedCategory;
            })) m_categoryNames.remove(removedCategory);
        m_powerActionSessionIds.remove(id);
        if (m_powerActionSessionIds.isEmpty() && !m_powerActionSessionHasCompletion)
            m_powerActionArmed = false;
        emit countsChanged();
        evaluateQueuePowerAction();
    });
    connect(&m_queue, &DownloadQueue::queueChanged, this, &DownloadManager::countsChanged);
    connect(&m_queue, &DownloadQueue::persistenceError, this, [this](const QString &message) {
        setLastError(QStringLiteral("Could not save download information: %1").arg(message));
    });
    connect(&m_queue, &DownloadQueue::proxyTestFinished, this, [this](bool success, const QString &message) {
        m_proxyTestRunning = false;
        m_proxyTestMessage = message;
        setLastError(success ? QString{} : message);
        emit proxyTestChanged();
    });
    connect(&m_queue, &DownloadQueue::httpAuthenticationRequested,
            this, &DownloadManager::httpAuthenticationRequested);
    m_queue.setMaximumConcurrent(settings.value(QStringLiteral("downloads/maximumConcurrent"), 3).toInt());
    m_queue.setMaximumRetries(settings.value(QStringLiteral("downloads/maximumRetries"), 3).toInt());
    m_queue.setSpeedLimitBytesPerSecond(settings.value(QStringLiteral("downloads/speedLimitBytesPerSecond"), 0).toLongLong());
    if (!m_repository.initializationError().isEmpty())
        setLastError(QStringLiteral("Could not open the download database: %1").arg(m_repository.initializationError()));
    else if (!m_queue.startupPersistenceError().isEmpty())
        setLastError(QStringLiteral("Could not fully restore saved downloads: %1").arg(m_queue.startupPersistenceError()));
    m_queue.startPending();
}

DownloadManager::~DownloadManager() {
    if (m_powerStateThread && m_powerStateThread->isRunning()) m_powerStateThread->wait();
    const auto processes = m_postDownloadProcesses.values();
    for (auto *process : processes) {
        if (!process) continue;
        if (auto *timeout = process->findChild<QTimer *>()) timeout->stop();
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) process->kill();
    }
    QElapsedTimer shutdownWait;
    shutdownWait.start();
    for (auto *process : processes) {
        if (!process || process->state() == QProcess::NotRunning) continue;
        const int remainingMs = std::max(0, 1000 - int(shutdownWait.elapsed()));
        if (remainingMs == 0) break;
        process->waitForFinished(remainingMs);
    }
}

int DownloadManager::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_records.size());
}

QVariant DownloadManager::data(const QModelIndex &modelIndex, int role) const {
    if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= m_records.size()) return {};
    const auto &record = m_records.at(modelIndex.row());
    switch (role) {
    case IdRole: return record.id;
    case UrlRole: return record.url;
    case FileNameRole: return record.fileName;
    case CategoryRole: return record.category;
    case QueueNameRole: return record.queueName;
    case DestinationRole: return record.destination;
    case StateRole: return downloadStateName(record.state);
    case ReceivedRole: return record.received;
    case TotalRole: return record.total;
    case SpeedRole: return record.bytesPerSecond;
    case AverageSpeedRole: return record.activeDurationMs > 0
            ? qint64(double(record.received) * 1000.0 / double(record.activeDurationMs)) : 0;
    case PercentRole: return record.total > 0
            ? std::clamp(double(record.received) * 100.0 / double(record.total), 0.0, 100.0) : -1.0;
    case EtaRole: return record.bytesPerSecond > 0 && record.total > record.received
            ? (record.total - record.received) / record.bytesPerSecond : -1;
    case ErrorRole: return record.error;
    case CreatedAtRole: return record.createdAt;
    case RetryCountRole: return record.retryCount;
    case NextRetryAtRole: return record.nextRetryAt;
    default: return {};
    }
}

QHash<int, QByteArray> DownloadManager::roleNames() const {
    return {{IdRole, "downloadId"}, {UrlRole, "url"}, {FileNameRole, "fileName"},
            {DestinationRole, "destination"}, {StateRole, "downloadState"}, {ReceivedRole, "received"},
            {TotalRole, "total"}, {SpeedRole, "bytesPerSecond"}, {AverageSpeedRole, "averageBytesPerSecond"},
            {PercentRole, "percent"}, {EtaRole, "etaSeconds"},
            {ErrorRole, "error"}, {CreatedAtRole, "createdAt"},
            {RetryCountRole, "retryCount"}, {NextRetryAtRole, "nextRetryAt"}, {CategoryRole, "category"},
            {QueueNameRole, "queueName"}};
}

int DownloadManager::activeCount() const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [](const auto &r) {
        return r.state == DownloadState::Downloading || r.state == DownloadState::Probing
            || r.state == DownloadState::Verifying;
    }));
}
int DownloadManager::queuedCount() const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [](const auto &r) {
        return r.state == DownloadState::Queued || r.state == DownloadState::WaitingForSchedule
            || r.state == DownloadState::Retrying;
    }));
}
int DownloadManager::completedCount() const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [](const auto &r) { return r.state == DownloadState::Completed; }));
}
int DownloadManager::failedCount() const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [](const auto &r) { return r.state == DownloadState::Failed; }));
}
int DownloadManager::pausedCount() const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [](const auto &r) { return r.state == DownloadState::Paused; }));
}
int DownloadManager::canceledCount() const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [](const auto &r) { return r.state == DownloadState::Canceled; }));
}

QString DownloadManager::defaultDestination() const {
    return m_defaultDownloadDirectory;
}

bool DownloadManager::setDefaultDownloadDirectory(const QString &path) {
    const QString trimmed = path.trimmed();
    const QFileInfo info(trimmed);
    if (trimmed.isEmpty() || !info.exists() || !info.isDir() || !info.isWritable()) {
        setLastError(QStringLiteral("Choose an existing writable download folder."));
        return false;
    }
    const QString normalized = QDir::cleanPath(info.absoluteFilePath());
    if (m_defaultDownloadDirectory == normalized) {
        setLastError({});
        return true;
    }
    m_defaultDownloadDirectory = normalized;
    QSettings().setValue(QStringLiteral("downloads/defaultDestination"), normalized);
    setLastError({});
    emit defaultDownloadDirectoryChanged();
    return true;
}

QString DownloadManager::readUrlListFile(const QUrl &fileUrl) {
    if (!fileUrl.isValid() || !fileUrl.isLocalFile()) {
        setLastError(QStringLiteral("Choose a local text file containing one URL per line."));
        return {};
    }
    const QString path = fileUrl.toLocalFile();
    const QFileInfo info(path);
    if (!info.isFile() || info.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0) {
        setLastError(QStringLiteral("Choose a .txt file containing one URL per line."));
        return {};
    }
    constexpr qint64 maximumBytes = 1024 * 1024;
    if (info.size() < 0 || info.size() > maximumBytes) {
        setLastError(QStringLiteral("URL list files must be no larger than 1 MiB."));
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("BestDM could not read the selected URL list file."));
        return {};
    }
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximumBytes) {
        setLastError(QStringLiteral("BestDM could not read the selected URL list file completely."));
        return {};
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString contents = decoder(bytes);
    if (decoder.hasError()) {
        setLastError(QStringLiteral("The selected URL list file is not valid UTF-8 text."));
        return {};
    }
    if (!contents.isEmpty() && contents.front() == QChar(0xfeff)) contents.remove(0, 1);
    if (contents.trimmed().isEmpty()) {
        setLastError(QStringLiteral("The selected URL list file is empty."));
        return {};
    }
    setLastError({});
    return contents;
}

int DownloadManager::matchingCount(const QString &filter, const QString &search, const QString &queueName) const {
    const QString needle = search.trimmed();
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &record) {
        if (!queueName.isEmpty() && record.queueName.compare(queueName, Qt::CaseInsensitive) != 0) return false;
        bool stateMatch = filter == QStringLiteral("All");
        if (filter == QStringLiteral("Active"))
            stateMatch = record.state == DownloadState::Downloading || record.state == DownloadState::Probing
                || record.state == DownloadState::Verifying;
        else if (filter == QStringLiteral("Queued"))
            stateMatch = record.state == DownloadState::Queued || record.state == DownloadState::WaitingForSchedule
                || record.state == DownloadState::Retrying;
        else if (filter == QStringLiteral("Paused"))
            stateMatch = record.state == DownloadState::Paused;
        else if (filter == QStringLiteral("Completed"))
            stateMatch = record.state == DownloadState::Completed;
        else if (filter == QStringLiteral("Failed"))
            stateMatch = record.state == DownloadState::Failed;
        else if (filter == QStringLiteral("History"))
            stateMatch = record.state == DownloadState::Completed || record.state == DownloadState::Failed || record.state == DownloadState::Canceled;
        const bool textMatch = needle.isEmpty() || record.fileName.contains(needle, Qt::CaseInsensitive)
            || record.url.contains(needle, Qt::CaseInsensitive);
        return stateMatch && textMatch;
    }));
}

int DownloadManager::queueCount(const QString &queueName, const QString &filter) const {
    return int(std::count_if(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &record) {
        if (record.queueName.compare(queueName, Qt::CaseInsensitive) != 0) return false;
        if (filter == QStringLiteral("All")) return true;
        if (filter == QStringLiteral("Completed")) return record.state == DownloadState::Completed;
        if (filter == QStringLiteral("History"))
            return record.state == DownloadState::Completed || record.state == DownloadState::Failed
                || record.state == DownloadState::Canceled;
        if (filter == QStringLiteral("Queued"))
            return record.state == DownloadState::Queued || record.state == DownloadState::WaitingForSchedule
                || record.state == DownloadState::Retrying;
        if (filter == QStringLiteral("Running"))
            return record.state == DownloadState::Downloading || record.state == DownloadState::Probing
                || record.state == DownloadState::Verifying;
        if (filter == QStringLiteral("Active"))
            return record.state == DownloadState::Downloading || record.state == DownloadState::Probing
                || record.state == DownloadState::Verifying;
        if (filter == QStringLiteral("Paused")) return record.state == DownloadState::Paused;
        if (filter == QStringLiteral("Failed")) return record.state == DownloadState::Failed;
        if (filter == QStringLiteral("Canceled")) return record.state == DownloadState::Canceled;
        return false;
    }));
}

QStringList DownloadManager::categoryNames() const {
    QStringList result{QStringLiteral("All categories")};
    auto sorted = m_categoryNames.values();
    sorted.sort(Qt::CaseInsensitive);
    result.append(sorted);
    return result;
}

bool DownloadManager::proxyPasswordSaved() const {
    QString username;
    QString password;
    const auto target = SecureCredentialStore::proxyTarget(m_proxyType, m_proxyHost, m_proxyPort);
    return SecureCredentialStore::loadProxyPassword(target, &username, &password)
        && !password.isEmpty() && username == m_proxyUsername;
}

bool DownloadManager::secureProxyStorageAvailable() const {
    return SecureCredentialStore::isAvailable();
}

bool DownloadManager::setProxySettings(const QString &mode, const QString &type, const QString &host,
                                       int port, const QString &username, const QString &password,
                                       bool clearPassword) {
    QString error;
    if (!m_queue.setProxyConfiguration(mode, type, host, port, username, password, clearPassword, &error)) {
        setLastError(error);
        return false;
    }
    m_proxyMode = mode;
    m_proxyType = type;
    m_proxyHost = host.trimmed();
    m_proxyPort = port;
    m_proxyUsername = username.trimmed();
    m_proxyTestRunning = false;
    m_proxyTestMessage.clear();
    setLastError({});
    emit proxySettingsChanged();
    emit proxyTestChanged();
    return true;
}

void DownloadManager::testProxyConnection(const QString &address) {
    const QUrl url(address.trimmed(), QUrl::StrictMode);
    m_proxyTestRunning = true;
    m_proxyTestMessage = QStringLiteral("Testing the current network settings...");
    emit proxyTestChanged();
    m_queue.testProxyConnection(url);
}

bool DownloadManager::setScheduleSettings(bool enabled, const QStringList &days,
                                          const QString &startTime, const QString &endTime) {
    QString error;
    if (!m_queue.setScheduleSettings(enabled, days, startTime, endTime, &error)) {
        setLastError(error);
        return false;
    }
    setLastError({});
    return true;
}

bool DownloadManager::setCategoryRules(const QString &rulesText) {
    CategoryRules parsed;
    QString error;
    if (!parsed.setText(rulesText, &error)) {
        setLastError(error);
        return false;
    }
    if (m_categoryRules.text() == parsed.text()) {
        setLastError({});
        return true;
    }
    m_categoryRules = std::move(parsed);
    m_queue.setCategoryConfiguration(m_categoryRules, m_automaticCategoryFolders);
    QSettings().setValue(QStringLiteral("downloads/categoryRules"), m_categoryRules.text());
    emit categoryRulesChanged();
    emit countsChanged();
    setLastError({});
    return true;
}

void DownloadManager::setAutomaticCategoryFolders(bool enabled) {
    if (m_automaticCategoryFolders == enabled) return;
    m_automaticCategoryFolders = enabled;
    m_queue.setCategoryConfiguration(m_categoryRules, m_automaticCategoryFolders);
    QSettings().setValue(QStringLiteral("downloads/automaticCategoryFolders"), enabled);
    emit categoryFolderSettingsChanged();
}

void DownloadManager::setClipboardMonitoringEnabled(bool enabled) {
    if (m_clipboardMonitoringEnabled == enabled) return;
    m_clipboardMonitoringEnabled = enabled;
    if (auto *guiApplication = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        m_lastClipboardText = guiApplication->clipboard()->text().trimmed();
    QSettings().setValue(QStringLiteral("privacy/clipboardMonitoringEnabled"), enabled);
    emit clipboardMonitoringChanged();
}

bool DownloadManager::trayAvailable() const {
    return m_tray && m_tray->available();
}

void DownloadManager::registerMainWindow(QObject *window) {
    m_mainWindow = qobject_cast<QWindow *>(window);
    if (m_tray) m_tray->setMainWindow(m_mainWindow);
}

bool DownloadManager::protocolRegistrationAvailable() const {
    return ApplicationProtocol::available();
}

bool DownloadManager::setProtocolRegistered(bool enabled) {
    QString error;
    if (!ApplicationProtocol::setRegistered(enabled, &error)) {
        setLastError(error);
        return false;
    }
    m_protocolRegistered = enabled && ApplicationProtocol::isRegistered();
    QSettings().setValue(QStringLiteral("integration/registerBestDmProtocol"), m_protocolRegistered);
    setLastError({});
    emit protocolRegistrationChanged();
    return true;
}

void DownloadManager::handleProtocolUrl(const QString &uri) {
    QString url;
    QString error;
    if (!ApplicationProtocol::parseHandlerUri(uri, &url, &error)) {
        setLastError(error);
        return;
    }
    activateMainWindow();
    emit protocolUrlReceived(url);
}

void DownloadManager::activateMainWindow() {
    if (!m_mainWindow) return;
    if (m_mainWindow->visibility() == QWindow::Minimized)
        m_mainWindow->showNormal();
    else
        m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->requestActivate();
}

void DownloadManager::setMinimizeToTray(bool enabled) {
    const bool bounded = enabled && trayAvailable();
    if (m_minimizeToTray == bounded) return;
    m_minimizeToTray = bounded;
    QSettings().setValue(QStringLiteral("tray/minimizeToTray"), bounded);
    emit traySettingsChanged();
}

void DownloadManager::setNotificationsEnabled(bool enabled) {
    if (m_notificationsEnabled == enabled) return;
    m_notificationsEnabled = enabled;
    QSettings().setValue(QStringLiteral("notifications/enabled"), enabled);
    emit traySettingsChanged();
}

QString DownloadManager::suggestedFileName(const QString &address) const {
    const QUrl url(address.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()) return {};
    return filenameForUrl(url);
}

QVariantMap DownloadManager::inspectUrls(const QString &urls) {
    setLastError({});
    QVariantMap inspection;
    QStringList validUrls;
    QStringList allValidUrls;
    QVariantList invalidUrls;
    QVariantList duplicates;
    QSet<QString> seen;
    const auto addresses = urls.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    if (addresses.isEmpty()) {
        inspection.insert(QStringLiteral("error"), QStringLiteral("Enter at least one HTTP or HTTPS URL."));
    } else if (addresses.size() > m_maximumBatchSize) {
        inspection.insert(QStringLiteral("error"), QStringLiteral("You can add up to %1 URLs at a time.").arg(m_maximumBatchSize));
    } else {
        for (const auto &input : addresses) {
            const QString address = input.trimmed();
            const QUrl parsed(address, QUrl::StrictMode);
            const QString scheme = parsed.scheme().toLower();
            if (!parsed.isValid() || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
                || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
                QString preview = address.left(240);
                if (address.size() > preview.size()) preview += QChar(0x2026);
                invalidUrls.append(QVariantMap{
                    {QStringLiteral("url"), preview},
                    {QStringLiteral("reason"), QStringLiteral("Use a valid HTTP/HTTPS link without embedded credentials.")}
                });
            } else {
                const QString normalized = parsed.toString(QUrl::FullyEncoded);
                allValidUrls.append(normalized);
                const bool alreadyQueued = std::any_of(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &record) {
                    return record.url == normalized && record.state != DownloadState::Canceled;
                });
                if (seen.contains(normalized) || alreadyQueued) {
                    duplicates.append(QVariantMap{
                        {QStringLiteral("url"), normalized.left(240)},
                        {QStringLiteral("reason"), alreadyQueued
                            ? QStringLiteral("This link is already in your download list.")
                            : QStringLiteral("This link appears more than once in this batch.")}
                    });
                } else {
                    validUrls.append(normalized);
                }
                seen.insert(normalized);
            }
        }
    }
    if (!inspection.contains(QStringLiteral("error"))) inspection.insert(QStringLiteral("error"), QString{});
    inspection.insert(QStringLiteral("validUrls"), validUrls);
    inspection.insert(QStringLiteral("allValidUrls"), allValidUrls);
    inspection.insert(QStringLiteral("invalidUrls"), invalidUrls);
    inspection.insert(QStringLiteral("duplicates"), duplicates);
    if (!inspection.value(QStringLiteral("error")).toString().isEmpty())
        setLastError(inspection.value(QStringLiteral("error")).toString());
    return inspection;
}

QVariantMap DownloadManager::details(const QString &id) const {
    const int row = indexOf(id);
    if (row < 0) return {};
    const auto &record = m_records.at(row);
    static const QRegularExpression sensitiveKey(
        QStringLiteral("(^|[-_])(token|access[-_]?token|auth|authorization|password|passwd|secret|key|signature|sig|credential)([-_]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto sanitizedUrl = [](const QString &address) {
        QUrl safeUrl(address);
        safeUrl.setUserName(QString{});
        safeUrl.setPassword(QString{});
        safeUrl.setFragment(QString{});
        QUrlQuery sourceQuery(safeUrl);
        QUrlQuery redactedQuery;
        for (const auto &item : sourceQuery.queryItems(QUrl::FullyDecoded)) {
            const QString value = sensitiveKey.match(item.first).hasMatch()
                ? QStringLiteral("[redacted]") : item.second;
            redactedQuery.addQueryItem(item.first, value);
        }
        safeUrl.setQuery(redactedQuery);
        return safeUrl.toString(QUrl::FullyEncoded);
    };
    return {
        {QStringLiteral("id"), record.id},
        {QStringLiteral("url"), sanitizedUrl(record.url)},
        {QStringLiteral("resolvedUrl"), sanitizedUrl(record.resolvedUrl)},
        {QStringLiteral("fileName"), record.fileName},
        {QStringLiteral("category"), record.category},
        {QStringLiteral("queueName"), record.queueName},
        {QStringLiteral("destination"), record.destination},
        {QStringLiteral("filePath"), QDir(record.destination).filePath(record.fileName)},
        {QStringLiteral("state"), downloadStateName(record.state)},
        {QStringLiteral("received"), record.received},
        {QStringLiteral("total"), record.total},
        {QStringLiteral("bytesPerSecond"), record.bytesPerSecond},
        {QStringLiteral("averageBytesPerSecond"), record.activeDurationMs > 0
             ? qint64(double(record.received) * 1000.0 / double(record.activeDurationMs)) : 0},
        {QStringLiteral("percent"), record.total > 0
             ? std::clamp(double(record.received) * 100.0 / double(record.total), 0.0, 100.0) : -1.0},
        {QStringLiteral("error"), record.error},
        {QStringLiteral("contentType"), record.contentType},
        {QStringLiteral("rangeSupported"), record.rangeSupported},
        {QStringLiteral("retryCount"), record.retryCount},
        {QStringLiteral("connectionCount"), record.connectionCount},
        {QStringLiteral("nextRetryAt"), record.nextRetryAt},
        {QStringLiteral("createdAt"), record.createdAt},
        {QStringLiteral("completedAt"), record.completedAt}
    };
}

QString DownloadManager::filenameForUrl(const QUrl &url) const {
    auto name = QFileInfo(url.path()).fileName();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) name.chop(1);
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"), QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(name).hasMatch()) name.prepend(QLatin1Char('_'));
    return name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")
            ? QStringLiteral("download") : name;
}

QString DownloadManager::uniqueFileName(const QString &desired, const QString &destination,
                                        const QList<DownloadRecord> &additionalRecords,
                                        bool overwriteExisting) const {
    const QFileInfo source(desired);
    const QString stem = source.completeBaseName();
    const QString suffix = source.suffix();
    for (int number = 0; number < 100000; ++number) {
        const QString candidate = number == 0 ? desired
            : QStringLiteral("%1 (%2)%3").arg(stem).arg(number).arg(suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
        const QDir dir(destination);
        const QFileInfo existingOutput(dir.filePath(candidate));
        if ((pathEntryExists(existingOutput.filePath()) && (!overwriteExisting || existingOutput.isDir()))
            || pathEntryExists(dir.filePath(candidate + QStringLiteral(".part")))) continue;
        bool hasSegment = false;
        for (int index = 0; index < 16; ++index) {
            if (pathEntryExists(dir.filePath(candidate + QStringLiteral(".part.seg.%1").arg(index)))) {
                hasSegment = true;
                break;
            }
        }
        if (hasSegment) continue;
        const bool reserved = std::any_of(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &record) {
            return record.destination == destination && record.fileName.compare(candidate, Qt::CaseInsensitive) == 0
                && record.state != DownloadState::Canceled;
        }) || std::any_of(additionalRecords.cbegin(), additionalRecords.cend(), [&](const DownloadRecord &record) {
            return record.destination == destination && record.fileName.compare(candidate, Qt::CaseInsensitive) == 0;
        });
        if (!reserved) return candidate;
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + (suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
}

QStringList DownloadManager::existingOutputConflicts(const QString &urls, const QString &destination,
                                                     const QString &fileName) const {
    const QString customName = fileName.trimmed();
    static const QRegularExpression reservedName(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (!customName.isEmpty() && (customName == QStringLiteral(".") || customName == QStringLiteral("..")
        || customName.endsWith(QLatin1Char('.')) || customName.endsWith(QLatin1Char(' '))
        || customName.contains(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")))
        || reservedName.match(customName).hasMatch())) return {};
    QString target = destination.trimmed();
    if (target.isEmpty()) target = defaultDestination();
    target = QDir(target).absolutePath();
    QStringList conflicts;
    const QStringList addresses = urls.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &address : addresses) {
        const QUrl parsed(address.trimmed(), QUrl::StrictMode);
        const QString scheme = parsed.scheme().toLower();
        if (!parsed.isValid() || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
            || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) continue;
        const QString desiredName = customName.isEmpty() ? filenameForUrl(parsed) : customName;
        const QString category = m_categoryRules.categoryFor(parsed, desiredName);
        const QString folder = m_automaticCategoryFolders
            ? QDir(target).filePath(CategoryRules::safeFolderName(category)) : target;
        const QFileInfo existing(QDir(folder).filePath(desiredName));
        const bool alreadyAssigned = std::any_of(m_records.cbegin(), m_records.cend(), [&](const DownloadRecord &record) {
            return record.destination.compare(folder, Qt::CaseInsensitive) == 0
                && record.fileName.compare(desiredName, Qt::CaseInsensitive) == 0
                && record.state != DownloadState::Canceled;
        });
        if (pathEntryExists(existing.filePath()) && !existing.isDir() && !alreadyAssigned
            && !conflicts.contains(desiredName, Qt::CaseInsensitive))
            conflicts.append(desiredName);
    }
    return conflicts;
}

bool DownloadManager::addUrls(const QString &urls, const QString &destination, const QString &requestedFileName,
                              bool includeDuplicates, int connectionCount,
                              bool overwriteExisting, bool collisionReviewed) {
    setLastError({});
    const auto inspection = inspectUrls(urls);
    const auto addresses = inspection.value(includeDuplicates ? QStringLiteral("allValidUrls")
                                                              : QStringLiteral("validUrls")).toStringList();
    if (!inspection.value(QStringLiteral("error")).toString().isEmpty()) return false;
    if (!inspection.value(QStringLiteral("invalidUrls")).toList().isEmpty()) {
        setLastError(QStringLiteral("Review and remove invalid URLs before adding this batch."));
        return false;
    }
    if (addresses.isEmpty()) {
        setLastError(QStringLiteral("All links in this batch are already listed. Choose the duplicate option to add them again."));
        return false;
    }
    const QString customName = requestedFileName.trimmed();
    if (!customName.isEmpty() && addresses.size() != 1) {
        setLastError(QStringLiteral("Choose a filename only when adding one URL at a time.")); return false;
    }
    static const QRegularExpression reservedName(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"), QRegularExpression::CaseInsensitiveOption);
    if (!customName.isEmpty() && (customName == QStringLiteral(".") || customName == QStringLiteral("..")
        || customName.endsWith(QLatin1Char('.')) || customName.endsWith(QLatin1Char(' '))
        || customName.contains(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")))
        || reservedName.match(customName).hasMatch())) {
        setLastError(QStringLiteral("That filename is unsafe or reserved. Remove path separators and invalid characters.")); return false;
    }
    const bool hasOutputConflicts = !existingOutputConflicts(addresses.join(QLatin1Char('\n')),
                                                              destination, customName).isEmpty();
    if (m_collisionPolicy == QStringLiteral("Ask") && hasOutputConflicts && !collisionReviewed) {
        setLastError(QStringLiteral("Review the existing file conflict before adding this download batch."));
        return false;
    }
    const bool allowOverwrite = m_collisionPolicy == QStringLiteral("Overwrite")
        || (m_collisionPolicy == QStringLiteral("Ask") && collisionReviewed && overwriteExisting);
    QString target = destination.trimmed();
    if (target.isEmpty()) target = defaultDestination();
    target = QDir(target).absolutePath();
    struct PendingRecord { DownloadRecord record; QString folder; };
    QList<PendingRecord> pending;
    QList<DownloadRecord> pendingNames;
    pending.reserve(addresses.size());
    for (const auto &input : addresses) {
        const QUrl parsed(input, QUrl::StrictMode);
        DownloadRecord record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.url = parsed.toString(QUrl::FullyEncoded);
        record.queueName = m_selectedQueueName;
        const QString desiredName = customName.isEmpty() ? filenameForUrl(parsed) : customName;
        record.category = m_categoryRules.categoryFor(parsed, desiredName);
        QString folder = target;
        if (m_automaticCategoryFolders) {
            folder = QDir(target).filePath(CategoryRules::safeFolderName(record.category));
        }
        record.destination = folder;
        record.baseDestination = target;
        record.overwriteExisting = allowOverwrite;
        record.fileName = uniqueFileName(desiredName, record.destination, pendingNames, allowOverwrite);
        // New records start in Queued; the default is the canonical initial state.
        record.createdAt = QDateTime::currentSecsSinceEpoch();
        record.updatedAt = record.createdAt;
        record.metadataProbePending = true;
        record.serverFilenameAllowed = customName.isEmpty();
        record.automaticCategoryFolder = m_automaticCategoryFolders;
        record.connectionCount = std::clamp(connectionCount > 0 ? connectionCount : m_connectionsPerDownload, 1, 16);
        pending.append({record, folder});
        pendingNames.append(record);
    }
    QSet<QString> folders;
    for (const auto &item : std::as_const(pending)) folders.insert(item.folder);
    for (const auto &folder : std::as_const(folders)) {
        if (!QDir().mkpath(folder)) {
            setLastError(QStringLiteral("BestDM could not create the selected destination folder."));
            return false;
        }
    }
    for (const auto &item : std::as_const(pending)) m_queue.enqueue(item.record);
    return !pending.isEmpty();
}

void DownloadManager::pauseDownload(const QString &id) { m_queue.pause(id); }
void DownloadManager::resumeDownload(const QString &id) { m_queue.resume(id); }
void DownloadManager::cancelDownload(const QString &id, bool deletePartial) { m_queue.cancel(id, deletePartial); }
void DownloadManager::removeDownload(const QString &id, bool deleteFiles) { m_queue.remove(id, deleteFiles); }
bool DownloadManager::canChangeDestination(const QString &id) const {
    return m_queue.canChangeDestination(id)
        && !m_postDownloadProcesses.contains(id + QStringLiteral(":defender"))
        && !m_postDownloadProcesses.contains(id + QStringLiteral(":program"));
}
bool DownloadManager::setDownloadDestination(const QString &id, const QString &destination) {
    QString error;
    const bool changed = canChangeDestination(id) && m_queue.changeDestination(id, destination, &error);
    if (!changed && error.isEmpty())
        error = QStringLiteral("Wait for the post-download action to finish before changing its destination.");
    setLastError(error);
    return changed;
}
void DownloadManager::pauseDownloads(const QVariantList &ids) {
    for (const auto &value : ids) m_queue.pause(value.toString());
}
void DownloadManager::resumeDownloads(const QVariantList &ids) {
    for (const auto &value : ids) m_queue.resume(value.toString());
}
void DownloadManager::cancelDownloads(const QVariantList &ids, bool deletePartial) {
    for (const auto &value : ids) m_queue.cancel(value.toString(), deletePartial);
}
void DownloadManager::removeDownloads(const QVariantList &ids, bool deleteFiles) {
    for (const auto &value : ids) m_queue.remove(value.toString(), deleteFiles);
}
QVariantList DownloadManager::historyIds(const QString &queueName) const {
    QVariantList ids;
    for (const DownloadRecord &record : m_records) {
        if (record.queueName.compare(queueName, Qt::CaseInsensitive) == 0
            && (record.state == DownloadState::Completed || record.state == DownloadState::Failed
                || record.state == DownloadState::Canceled))
            ids.append(record.id);
    }
    return ids;
}
bool DownloadManager::createQueue(const QString &name) {
    const QString normalized = name.trimmed();
    if (normalized.isEmpty() || normalized.size() > 40
        || normalized.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")))) {
        setLastError(QStringLiteral("Queue names must contain 1 to 40 printable characters."));
        return false;
    }
    const auto existing = std::find_if(m_queueNames.cbegin(), m_queueNames.cend(), [&](const QString &candidate) {
        return candidate.compare(normalized, Qt::CaseInsensitive) == 0;
    });
    if (existing != m_queueNames.cend()) {
        setLastError(QStringLiteral("A queue with that name already exists."));
        return false;
    }
    m_queueNames.append(normalized);
    m_selectedQueueName = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("downloads/queueNames"), m_queueNames);
    settings.setValue(QStringLiteral("downloads/selectedQueue"), m_selectedQueueName);
    setLastError({});
    emit queueSelectionChanged();
    return true;
}

bool DownloadManager::selectQueue(const QString &name) {
    const auto selected = std::find_if(m_queueNames.cbegin(), m_queueNames.cend(), [&](const QString &candidate) {
        return candidate.compare(name.trimmed(), Qt::CaseInsensitive) == 0;
    });
    if (selected == m_queueNames.cend()) {
        setLastError(QStringLiteral("Choose an existing download queue."));
        return false;
    }
    if (m_selectedQueueName == *selected) return true;
    m_selectedQueueName = *selected;
    QSettings().setValue(QStringLiteral("downloads/selectedQueue"), m_selectedQueueName);
    emit queueSelectionChanged();
    return true;
}

bool DownloadManager::moveDownloadsToQueue(const QVariantList &ids, const QString &name) {
    const auto destination = std::find_if(m_queueNames.cbegin(), m_queueNames.cend(), [&](const QString &candidate) {
        return candidate.compare(name.trimmed(), Qt::CaseInsensitive) == 0;
    });
    if (destination == m_queueNames.cend()) {
        setLastError(QStringLiteral("Choose an existing download queue."));
        return false;
    }
    bool allMoved = true;
    QString error;
    for (const QVariant &value : ids) {
        QString currentError;
        if (!m_queue.changeQueueName(value.toString(), *destination, &currentError)) {
            allMoved = false;
            if (error.isEmpty()) error = currentError;
        }
    }
    setLastError(error);
    return allMoved;
}
void DownloadManager::openFile(const QString &id) {
    const int row=indexOf(id);
    if (row<0 || m_records.at(row).state!=DownloadState::Completed) return;
    const auto path=QDir(m_records.at(row).destination).filePath(m_records.at(row).fileName);
    if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        setLastError(QStringLiteral("BestDM could not open the completed file."));
}
void DownloadManager::openFolder(const QString &id) {
    const int row=indexOf(id);
    if (row<0) return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_records.at(row).destination)))
        setLastError(QStringLiteral("BestDM could not open the destination folder."));
}
void DownloadManager::copyUrl(const QString &id) {
    const int row=indexOf(id);
    if (row>=0) {
        const QString url = m_records.at(row).url;
        m_lastClipboardText = url;
        if (auto *guiApplication = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            guiApplication->clipboard()->setText(url);
    }
}
void DownloadManager::copyDestination(const QString &id) {
    const int row=indexOf(id);
    if (row>=0) {
        if (auto *guiApplication = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            guiApplication->clipboard()->setText(QDir(m_records.at(row).destination).filePath(m_records.at(row).fileName));
    }
}
void DownloadManager::retryAllFailed() { m_queue.retryAllFailed(); }
void DownloadManager::retryFailedInQueue(const QString &queueName) { m_queue.retryFailedInQueue(queueName); }
bool DownloadManager::provideHttpCredentials(const QString &id, const QString &origin,
                                            const QString &username, const QString &password) {
    return m_queue.provideHttpCredentials(id, origin, username, password);
}

void DownloadManager::setMaximumConcurrent(int limit) {
    const int old = m_queue.maximumConcurrent();
    m_queue.setMaximumConcurrent(limit);
    if (old == m_queue.maximumConcurrent()) return;
    QSettings().setValue(QStringLiteral("downloads/maximumConcurrent"), m_queue.maximumConcurrent());
    emit maximumConcurrentChanged();
}

void DownloadManager::setMaximumBatchSize(int limit) {
    const int bounded = std::clamp(limit, 1, 500);
    if (m_maximumBatchSize == bounded) return;
    m_maximumBatchSize = bounded;
    QSettings().setValue(QStringLiteral("downloads/maximumBatchSize"), bounded);
    emit maximumBatchSizeChanged();
}

void DownloadManager::setCollisionPolicy(const QString &policy) {
    if (policy != QStringLiteral("Rename") && policy != QStringLiteral("Ask")
        && policy != QStringLiteral("Overwrite")) return;
    if (m_collisionPolicy == policy) return;
    m_collisionPolicy = policy;
    QSettings().setValue(QStringLiteral("downloads/collisionPolicy"), policy);
    emit collisionPolicyChanged();
}

void DownloadManager::setMaximumRetries(int limit) {
    const int old = m_queue.maximumRetries();
    m_queue.setMaximumRetries(limit);
    if (old == m_queue.maximumRetries()) return;
    QSettings().setValue(QStringLiteral("downloads/maximumRetries"), m_queue.maximumRetries());
    emit maximumRetriesChanged();
}

void DownloadManager::setConnectionsPerDownload(int count) {
    const int bounded = std::clamp(count, 1, 16);
    if (m_connectionsPerDownload == bounded) return;
    m_connectionsPerDownload = bounded;
    QSettings().setValue(QStringLiteral("downloads/connectionsPerDownload"), bounded);
    emit connectionsPerDownloadChanged();
}

void DownloadManager::setSpeedLimitKiBPerSecond(int limit) {
    const qint64 bounded = std::clamp(limit, 0, 1'048'576);
    const qint64 bytesPerSecond = bounded * 1024;
    if (speedLimitBytesPerSecond() == bytesPerSecond) return;
    m_queue.setSpeedLimitBytesPerSecond(bytesPerSecond);
    QSettings().setValue(QStringLiteral("downloads/speedLimitBytesPerSecond"), bytesPerSecond);
    emit speedLimitChanged();
}

void DownloadManager::setTheme(const QString &themeName) {
    if (themeName != QStringLiteral("System") && themeName != QStringLiteral("Light") && themeName != QStringLiteral("Dark")) return;
    if (m_theme == themeName) return;
    m_theme = themeName;
    QSettings().setValue(QStringLiteral("appearance/theme"), m_theme);
    emit themeChanged();
}

bool DownloadManager::setPostDownloadAction(const QString &action) {
    if (action != QStringLiteral("None") && action != QStringLiteral("Open file")
        && action != QStringLiteral("Open folder") && action != QStringLiteral("Run program")) {
        setLastError(QStringLiteral("Choose None, Open file, Open folder, or Run program for completed downloads."));
        return false;
    }
    if (action == QStringLiteral("Run program") && m_postDownloadProgram.isEmpty()) {
        setLastError(QStringLiteral("Choose a post-download program before enabling Run program."));
        return false;
    }
    if (m_postDownloadAction == action) {
        setLastError({});
        return true;
    }
    m_postDownloadAction = action;
    QSettings().setValue(QStringLiteral("downloads/postDownloadAction"), action);
    setLastError({});
    emit postDownloadActionChanged();
    return true;
}

bool DownloadManager::setPostDownloadProgram(const QString &programPath) {
    const QString trimmed = programPath.trimmed();
    const QFileInfo executable(trimmed);
#ifdef Q_OS_WIN
    const bool executableAllowed = executable.isFile()
        && executable.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0;
#else
    const bool executableAllowed = executable.isFile() && executable.isExecutable();
#endif
    if (!executableAllowed) {
        setLastError(QStringLiteral("Choose an existing executable program."));
        return false;
    }
    const QString normalized = executable.absoluteFilePath();
    if (m_postDownloadProgram == normalized) {
        setLastError({});
        return true;
    }
    m_postDownloadProgram = normalized;
    QSettings().setValue(QStringLiteral("downloads/postDownloadProgram"), normalized);
    setLastError({});
    emit postDownloadProgramChanged();
    return true;
}

bool DownloadManager::defenderScanAvailable() const {
    return !windowsDefenderToolPath().isEmpty();
}

void DownloadManager::setDefenderScanEnabled(bool enabled) {
    const bool bounded = enabled && defenderScanAvailable();
    if (m_defenderScanEnabled == bounded) return;
    m_defenderScanEnabled = bounded;
    QSettings().setValue(QStringLiteral("downloads/scanWithDefender"), bounded);
    emit defenderScanSettingsChanged();
    setLastError(bounded || !enabled ? QString{} : QStringLiteral("Microsoft Defender's MpCmdRun.exe could not be found."));
}

void DownloadManager::performPostDownloadAction(const DownloadRecord &record) {
    if (m_postDownloadAction == QStringLiteral("Open file")) {
        openFile(record.id);
        return;
    }
    if (m_postDownloadAction == QStringLiteral("Open folder")) {
        openFolder(record.id);
        return;
    }
    if (m_postDownloadAction != QStringLiteral("Run program")) return;
    const QFileInfo executable(m_postDownloadProgram);
#ifdef Q_OS_WIN
    const bool executableAllowed = executable.isFile()
        && executable.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0;
#else
    const bool executableAllowed = executable.isFile() && executable.isExecutable();
#endif
    if (!executableAllowed) {
        setLastError(QStringLiteral("The configured post-download program is missing or is not executable."));
        return;
    }
    const QString filePath = QDir(record.destination).filePath(record.fileName);
    if (!QFileInfo(filePath).isFile()) {
        setLastError(QStringLiteral("The completed file is missing; the post-download program was not started."));
        return;
    }
    runPostDownloadProcess(record.id + QStringLiteral(":program"), executable.absoluteFilePath(),
        {filePath}, 120000, QStringLiteral("Post-download program"));
}

void DownloadManager::runPostDownloadProcess(const QString &key, const QString &program,
                                             const QStringList &arguments, int timeoutMs,
                                             const QString &description,
                                             std::function<void(bool)> finished) {
    if (m_postDownloadProcesses.contains(key)) {
        reportPostDownloadProcessError(description, description + QStringLiteral(" is already running for this download."));
        if (finished) finished(false);
        return;
    }
    auto *process = new QProcess(this);
    m_postDownloadProcesses.insert(key, process);
    process->setProgram(program);
    process->setArguments(arguments);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    timeout->setInterval(timeoutMs);
    connect(process, &QProcess::readyReadStandardOutput, process, [process] { process->readAllStandardOutput(); });
    connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
    connect(timeout, &QTimer::timeout, process, [this, process, description] {
        process->setProperty("bestdmTimedOut", true);
        reportPostDownloadProcessError(description, description + QStringLiteral(" exceeded its time limit."));
        process->terminate();
        QTimer::singleShot(3000, process, [process] {
            if (process->state() != QProcess::NotRunning) process->kill();
        });
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, key, process, timeout, description, finished](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_postDownloadProcesses.value(key) != process) return;
        timeout->stop();
        m_postDownloadProcesses.remove(key);
        reportPostDownloadProcessError(description, QStringLiteral("BestDM could not start ") + description + QLatin1Char('.'));
        if (finished) finished(false);
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, key, process, timeout, description, finished](int exitCode, QProcess::ExitStatus exitStatus) {
        process->readAllStandardOutput();
        process->readAllStandardError();
        timeout->stop();
        if (m_postDownloadProcesses.value(key) != process) return;
        m_postDownloadProcesses.remove(key);
        const bool success = !process->property("bestdmTimedOut").toBool()
            && exitStatus == QProcess::NormalExit && exitCode == 0;
        if (!success && !process->property("bestdmTimedOut").toBool())
            reportPostDownloadProcessError(description,
                description + QStringLiteral(" exited with an error (code %1).").arg(exitCode));
        if (finished) finished(success);
        process->deleteLater();
    });
    timeout->start();
    process->start();
}

void DownloadManager::reportPostDownloadProcessError(const QString &description, const QString &message) {
    setLastError(message);
    if (description == QStringLiteral("Microsoft Defender scan")) {
        if (m_tray && m_notificationsEnabled)
            m_tray->showNotification(QStringLiteral("Security scan failed"), message);
        emit postDownloadActionFailed(message);
    }
}

bool DownloadManager::powerActionAvailable() const {
#ifdef Q_OS_WIN
    QLibrary powerProfile(QStringLiteral("PowrProf"));
    return !QStandardPaths::findExecutable(QStringLiteral("shutdown.exe")).isEmpty()
        && powerProfile.load() && powerProfile.resolve("SetSuspendState");
#else
    return false;
#endif
}

bool DownloadManager::setQueuePowerAction(const QString &action) {
    if (action != QStringLiteral("None") && action != QStringLiteral("Shutdown")
        && action != QStringLiteral("Sleep") && action != QStringLiteral("Hibernate")) {
        setLastError(QStringLiteral("Choose None, Shutdown, Sleep, or Hibernate."));
        return false;
    }
    if (action != QStringLiteral("None") && !powerActionAvailable()) {
        setLastError(QStringLiteral("Windows power actions are unavailable on this system."));
        return false;
    }
    if (m_queuePowerAction == action) return true;
    const bool wasEnabled = m_queuePowerAction != QStringLiteral("None");
    m_queuePowerAction = action;
    QSettings().setValue(QStringLiteral("downloads/queuePowerAction"), action);
    if (action == QStringLiteral("None")) {
        cancelPowerAction();
        m_powerActionArmed = false;
    } else if (!wasEnabled) {
        m_powerActionSessionIds.clear();
        for (const auto &record : std::as_const(m_records)) {
            if (record.state == DownloadState::Completed || record.state == DownloadState::Failed
                || record.state == DownloadState::Canceled) continue;
            m_powerActionSessionIds.insert(record.id);
        }
        m_powerActionSessionHasCompletion = false;
        m_powerActionArmed = !m_powerActionSessionIds.isEmpty();
    } else {
        for (const auto &record : std::as_const(m_records)) {
            if (record.state != DownloadState::Completed && record.state != DownloadState::Failed
                && record.state != DownloadState::Canceled)
                m_powerActionSessionIds.insert(record.id);
        }
        m_powerActionArmed = m_powerActionArmed || !m_powerActionSessionIds.isEmpty();
    }
    emit powerActionSettingsChanged();
    setLastError({});
    return true;
}

bool DownloadManager::hasNonterminalDownloads() const {
    return std::any_of(m_records.cbegin(), m_records.cend(), [](const DownloadRecord &record) {
        return record.state != DownloadState::Completed && record.state != DownloadState::Failed
            && record.state != DownloadState::Canceled;
    });
}

void DownloadManager::evaluateQueuePowerAction() {
    if (m_queuePowerAction == QStringLiteral("None")) return;
    if (hasNonterminalDownloads()) {
        if (m_shutdownCountdownTimer.isActive() || m_powerActionPromptVisible) {
            m_shutdownCountdownTimer.stop();
            m_shutdownCountdownSeconds = 0;
            m_powerActionPromptVisible = false;
            emit powerActionCountdownChanged();
            emit powerActionPromptDismissed();
        }
        m_powerActionArmed = true;
        return;
    }
    if (!m_powerActionArmed || m_powerActionPromptVisible || m_shutdownCountdownTimer.isActive()) return;
    if (!m_powerActionSessionHasCompletion) {
        m_powerActionArmed = false;
        m_powerActionSessionIds.clear();
        m_powerActionSessionHasCompletion = false;
        return;
    }
    m_powerActionArmed = false;
    m_powerActionPromptVisible = true;
    m_powerActionSessionIds.clear();
    m_powerActionSessionHasCompletion = false;
    emit powerActionPromptRequested();
}

bool DownloadManager::startPowerActionCountdown() {
    if (m_queuePowerAction == QStringLiteral("None") || !m_powerActionPromptVisible || hasNonterminalDownloads()
        || !powerActionAvailable()) {
        setLastError(QStringLiteral("The power action is not ready: confirm after the active download queue has finished."));
        return false;
    }
    m_powerActionPromptVisible = false;
    m_shutdownCountdownSeconds = 60;
    m_shutdownCountdownTimer.start();
    emit powerActionCountdownChanged();
    setLastError({});
    return true;
}

void DownloadManager::cancelPowerAction() {
    const bool wasPending = m_powerActionPromptVisible || m_shutdownCountdownTimer.isActive();
    m_shutdownCountdownTimer.stop();
    m_shutdownCountdownSeconds = 0;
    m_powerActionPromptVisible = false;
    m_powerActionArmed = false;
    m_powerActionSessionIds.clear();
    m_powerActionSessionHasCompletion = false;
    emit powerActionCountdownChanged();
    if (wasPending) emit powerActionPromptDismissed();
}

void DownloadManager::launchWindowsPowerAction() {
    m_shutdownCountdownTimer.stop();
    m_shutdownCountdownSeconds = 0;
    emit powerActionCountdownChanged();
    m_powerActionPromptVisible = false;
#ifdef Q_OS_WIN
    if (m_queuePowerAction == QStringLiteral("Shutdown")) {
        const QString program = QStandardPaths::findExecutable(QStringLiteral("shutdown.exe"));
        const bool started = !program.isEmpty()
            && QProcess::startDetached(program, {QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0")});
        if (!started) setLastError(QStringLiteral("BestDM could not start the Windows shutdown action."));
    } else if (m_queuePowerAction == QStringLiteral("Sleep") || m_queuePowerAction == QStringLiteral("Hibernate")) {
        const bool hibernate = m_queuePowerAction == QStringLiteral("Hibernate");
        m_powerStateThread = QThread::create([this, hibernate] {
            QLibrary powerProfile(QStringLiteral("PowrProf"));
            using SetSuspendStateFunction = unsigned char (__stdcall *)(unsigned char, unsigned char, unsigned char);
            const bool loaded = powerProfile.load();
            const auto setSuspendState = loaded
                ? reinterpret_cast<SetSuspendStateFunction>(powerProfile.resolve("SetSuspendState")) : nullptr;
            if (!setSuspendState || !setSuspendState(hibernate ? 1 : 0, 0, 0)) {
                QMetaObject::invokeMethod(this, [this] {
                    setLastError(QStringLiteral("Windows could not enter the selected power state; check system sleep and hibernation settings."));
                }, Qt::QueuedConnection);
            }
        });
        connect(m_powerStateThread, &QThread::finished, this, [this] {
            if (m_powerStateThread) {
                m_powerStateThread->deleteLater();
                m_powerStateThread = nullptr;
            }
        });
        m_powerStateThread->start();
    }
#else
    setLastError(QStringLiteral("Queue power actions are available only on Windows."));
#endif
    emit powerActionPromptDismissed();
}

int DownloadManager::indexOf(const QString &id) const {
    for (qsizetype i = 0; i < m_records.size(); ++i)
        if (m_records.at(i).id == id) return int(i);
    return -1;
}

void DownloadManager::setLastError(const QString &error) {
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}
