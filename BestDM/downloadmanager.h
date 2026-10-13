#pragma once

#include "downloadqueue.h"
#include "categoryrules.h"
#include "persistencerepository.h"
#include <QPointer>
#include <QAbstractListModel>
#include <QHash>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QSet>
#include <QTimer>
#include <functional>

class QProcess;
class QThread;
class QWindow;

class DownloadManager final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countsChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY countsChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY countsChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY countsChanged)
    Q_PROPERTY(int pausedCount READ pausedCount NOTIFY countsChanged)
    Q_PROPERTY(int canceledCount READ canceledCount NOTIFY countsChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY countsChanged)
    Q_PROPERTY(int maximumConcurrent READ maximumConcurrent WRITE setMaximumConcurrent NOTIFY maximumConcurrentChanged)
    Q_PROPERTY(int maximumBatchSize READ maximumBatchSize NOTIFY maximumBatchSizeChanged)
    Q_PROPERTY(QString collisionPolicy READ collisionPolicy NOTIFY collisionPolicyChanged)
    Q_PROPERTY(int maximumRetries READ maximumRetries WRITE setMaximumRetries NOTIFY maximumRetriesChanged)
    Q_PROPERTY(int connectionsPerDownload READ connectionsPerDownload NOTIFY connectionsPerDownloadChanged)
    Q_PROPERTY(qint64 speedLimitBytesPerSecond READ speedLimitBytesPerSecond NOTIFY speedLimitChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString defaultDownloadDirectory READ defaultDownloadDirectory NOTIFY defaultDownloadDirectoryChanged)
    Q_PROPERTY(QStringList queueNames READ queueNames NOTIFY queueSelectionChanged)
    Q_PROPERTY(QString selectedQueueName READ selectedQueueName NOTIFY queueSelectionChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString postDownloadAction READ postDownloadAction NOTIFY postDownloadActionChanged)
    Q_PROPERTY(QString postDownloadProgram READ postDownloadProgram NOTIFY postDownloadProgramChanged)
    Q_PROPERTY(bool defenderScanAvailable READ defenderScanAvailable CONSTANT)
    Q_PROPERTY(bool defenderScanEnabled READ defenderScanEnabled NOTIFY defenderScanSettingsChanged)
    Q_PROPERTY(bool powerActionAvailable READ powerActionAvailable CONSTANT)
    Q_PROPERTY(QString queuePowerAction READ queuePowerAction NOTIFY powerActionSettingsChanged)
    Q_PROPERTY(int shutdownCountdownSeconds READ shutdownCountdownSeconds NOTIFY powerActionCountdownChanged)
    Q_PROPERTY(QString categoryRulesText READ categoryRulesText NOTIFY categoryRulesChanged)
    Q_PROPERTY(bool automaticCategoryFolders READ automaticCategoryFolders NOTIFY categoryFolderSettingsChanged)
    Q_PROPERTY(bool clipboardMonitoringEnabled READ clipboardMonitoringEnabled NOTIFY clipboardMonitoringChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray NOTIFY traySettingsChanged)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled NOTIFY traySettingsChanged)
    Q_PROPERTY(bool protocolRegistrationAvailable READ protocolRegistrationAvailable CONSTANT)
    Q_PROPERTY(bool protocolRegistered READ protocolRegistered NOTIFY protocolRegistrationChanged)
    Q_PROPERTY(QStringList categoryNames READ categoryNames NOTIFY countsChanged)
    Q_PROPERTY(QString proxyMode READ proxyMode NOTIFY proxySettingsChanged)
    Q_PROPERTY(QString proxyType READ proxyType NOTIFY proxySettingsChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost NOTIFY proxySettingsChanged)
    Q_PROPERTY(int proxyPort READ proxyPort NOTIFY proxySettingsChanged)
    Q_PROPERTY(QString proxyUsername READ proxyUsername NOTIFY proxySettingsChanged)
    Q_PROPERTY(bool proxyPasswordSaved READ proxyPasswordSaved NOTIFY proxySettingsChanged)
    Q_PROPERTY(bool secureProxyStorageAvailable READ secureProxyStorageAvailable CONSTANT)
    Q_PROPERTY(bool proxyTestRunning READ proxyTestRunning NOTIFY proxyTestChanged)
    Q_PROPERTY(QString proxyTestMessage READ proxyTestMessage NOTIFY proxyTestChanged)
    Q_PROPERTY(bool scheduleEnabled READ scheduleEnabled NOTIFY countsChanged)
    Q_PROPERTY(QStringList scheduleDays READ scheduleDays NOTIFY countsChanged)
    Q_PROPERTY(QString scheduleStartTime READ scheduleStartTime NOTIFY countsChanged)
    Q_PROPERTY(QString scheduleEndTime READ scheduleEndTime NOTIFY countsChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, UrlRole, FileNameRole, DestinationRole, StateRole,
        ReceivedRole, TotalRole, SpeedRole, AverageSpeedRole, PercentRole, EtaRole, ErrorRole,
        CreatedAtRole, RetryCountRole, NextRetryAtRole, CategoryRole, QueueNameRole
    };
    Q_ENUM(Roles)

    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;
    int count() const { return int(m_records.size()); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int activeCount() const;
    int queuedCount() const;
    int completedCount() const;
    int failedCount() const;
    int pausedCount() const;
    int canceledCount() const;
    int historyCount() const { return completedCount() + failedCount() + canceledCount(); }
    int maximumConcurrent() const { return m_queue.maximumConcurrent(); }
    int maximumBatchSize() const { return m_maximumBatchSize; }
    QString collisionPolicy() const { return m_collisionPolicy; }
    int maximumRetries() const { return m_queue.maximumRetries(); }
    int connectionsPerDownload() const { return m_connectionsPerDownload; }
    qint64 speedLimitBytesPerSecond() const { return m_queue.speedLimitBytesPerSecond(); }
    QString lastError() const { return m_lastError; }
    QString defaultDownloadDirectory() const { return m_defaultDownloadDirectory; }
    QStringList queueNames() const { return m_queueNames; }
    QString selectedQueueName() const { return m_selectedQueueName; }
    QString theme() const { return m_theme; }
    QString postDownloadAction() const { return m_postDownloadAction; }
    QString postDownloadProgram() const { return m_postDownloadProgram; }
    bool defenderScanAvailable() const;
    bool defenderScanEnabled() const { return m_defenderScanEnabled; }
    bool powerActionAvailable() const;
    QString queuePowerAction() const { return m_queuePowerAction; }
    int shutdownCountdownSeconds() const { return m_shutdownCountdownSeconds; }
    QString categoryRulesText() const { return m_categoryRules.text(); }
    bool automaticCategoryFolders() const { return m_automaticCategoryFolders; }
    bool clipboardMonitoringEnabled() const { return m_clipboardMonitoringEnabled; }
    bool trayAvailable() const;
    bool minimizeToTray() const { return m_minimizeToTray; }
    bool notificationsEnabled() const { return m_notificationsEnabled; }
    bool protocolRegistrationAvailable() const;
    bool protocolRegistered() const { return m_protocolRegistered; }
    QStringList categoryNames() const;
    QString proxyMode() const { return m_proxyMode; }
    QString proxyType() const { return m_proxyType; }
    QString proxyHost() const { return m_proxyHost; }
    int proxyPort() const { return m_proxyPort; }
    QString proxyUsername() const { return m_proxyUsername; }
    bool proxyPasswordSaved() const;
    bool secureProxyStorageAvailable() const;
    bool proxyTestRunning() const { return m_proxyTestRunning; }
    QString proxyTestMessage() const { return m_proxyTestMessage; }
    bool scheduleEnabled() const { return m_queue.scheduleEnabled(); }
    QStringList scheduleDays() const { return m_queue.scheduleDays(); }
    QString scheduleStartTime() const { return m_queue.scheduleStartTime(); }
    QString scheduleEndTime() const { return m_queue.scheduleEndTime(); }
    Q_INVOKABLE QString defaultDestination() const;
    Q_INVOKABLE bool setDefaultDownloadDirectory(const QString &path);
    Q_INVOKABLE QString readUrlListFile(const QUrl &fileUrl);
    Q_INVOKABLE QString suggestedFileName(const QString &url) const;
    Q_INVOKABLE QVariantMap inspectUrls(const QString &urls);
    Q_INVOKABLE QStringList existingOutputConflicts(const QString &urls, const QString &destination,
                                                    const QString &fileName = {}) const;
    Q_INVOKABLE QVariantMap details(const QString &id) const;
    Q_INVOKABLE int matchingCount(const QString &filter, const QString &search, const QString &queueName) const;
    Q_INVOKABLE int queueCount(const QString &queueName, const QString &filter) const;
    Q_INVOKABLE bool hasDownload(const QString &id) const { return indexOf(id) >= 0; }
    Q_INVOKABLE bool createQueue(const QString &name);
    Q_INVOKABLE bool selectQueue(const QString &name);
    Q_INVOKABLE bool moveDownloadsToQueue(const QVariantList &ids, const QString &name);
    Q_INVOKABLE QVariantList historyIds(const QString &queueName) const;
    Q_INVOKABLE bool addUrls(const QString &urls, const QString &destination, const QString &fileName = {},
                             bool includeDuplicates = false, int connectionCount = -1,
                             bool overwriteExisting = false, bool collisionReviewed = false);
    Q_INVOKABLE void start(const QString &url, const QString &destination) { addUrls(url, destination); }
    Q_INVOKABLE void pauseDownload(const QString &id);
    Q_INVOKABLE void resumeDownload(const QString &id);
    Q_INVOKABLE void cancelDownload(const QString &id, bool deletePartial = true);
    Q_INVOKABLE void removeDownload(const QString &id, bool deleteFiles = false);
    Q_INVOKABLE bool canChangeDestination(const QString &id) const;
    Q_INVOKABLE bool setDownloadDestination(const QString &id, const QString &destination);
    Q_INVOKABLE void pauseDownloads(const QVariantList &ids);
    Q_INVOKABLE void resumeDownloads(const QVariantList &ids);
    Q_INVOKABLE void cancelDownloads(const QVariantList &ids, bool deletePartial = true);
    Q_INVOKABLE void removeDownloads(const QVariantList &ids, bool deleteFiles = false);
    Q_INVOKABLE void openFile(const QString &id);
    Q_INVOKABLE void openFolder(const QString &id);
    Q_INVOKABLE void copyUrl(const QString &id);
    Q_INVOKABLE void copyDestination(const QString &id);
    Q_INVOKABLE void retryAllFailed();
    Q_INVOKABLE void retryFailedInQueue(const QString &queueName);
    Q_INVOKABLE bool provideHttpCredentials(const QString &id, const QString &origin,
                                            const QString &username, const QString &password);
    Q_INVOKABLE void setMaximumConcurrent(int limit);
    Q_INVOKABLE void setMaximumBatchSize(int limit);
    Q_INVOKABLE void setCollisionPolicy(const QString &policy);
    Q_INVOKABLE void setMaximumRetries(int limit);
    Q_INVOKABLE void setConnectionsPerDownload(int count);
    Q_INVOKABLE void setSpeedLimitKiBPerSecond(int limit);
    Q_INVOKABLE void setTheme(const QString &theme);
    Q_INVOKABLE bool setPostDownloadAction(const QString &action);
    Q_INVOKABLE bool setPostDownloadProgram(const QString &programPath);
    Q_INVOKABLE void setDefenderScanEnabled(bool enabled);
    Q_INVOKABLE bool setQueuePowerAction(const QString &action);
    Q_INVOKABLE bool startPowerActionCountdown();
    Q_INVOKABLE void cancelPowerAction();
    Q_INVOKABLE bool setCategoryRules(const QString &rules);
    Q_INVOKABLE void setAutomaticCategoryFolders(bool enabled);
    Q_INVOKABLE void setClipboardMonitoringEnabled(bool enabled);
    Q_INVOKABLE void registerMainWindow(QObject *window);
    Q_INVOKABLE bool setProtocolRegistered(bool enabled);
    Q_INVOKABLE void handleProtocolUrl(const QString &uri);
    Q_INVOKABLE void activateMainWindow();
    Q_INVOKABLE void setMinimizeToTray(bool enabled);
    Q_INVOKABLE void setNotificationsEnabled(bool enabled);
    Q_INVOKABLE bool setProxySettings(const QString &mode, const QString &type, const QString &host,
                                      int port, const QString &username, const QString &password,
                                      bool clearPassword = false);
    Q_INVOKABLE void testProxyConnection(const QString &url);
    Q_INVOKABLE bool setScheduleSettings(bool enabled, const QStringList &days,
                                         const QString &startTime, const QString &endTime);
signals:
    void countsChanged();
    void maximumConcurrentChanged();
    void maximumBatchSizeChanged();
    void collisionPolicyChanged();
    void maximumRetriesChanged();
    void connectionsPerDownloadChanged();
    void speedLimitChanged();
    void lastErrorChanged();
    void defaultDownloadDirectoryChanged();
    void themeChanged();
    void postDownloadActionChanged();
    void postDownloadProgramChanged();
    void defenderScanSettingsChanged();
    void powerActionSettingsChanged();
    void powerActionCountdownChanged();
    void powerActionPromptRequested();
    void powerActionPromptDismissed();
    void postDownloadActionFailed(const QString &message);
    void categoryRulesChanged();
    void categoryFolderSettingsChanged();
    void clipboardMonitoringChanged();
    void clipboardUrlDetected(const QString &url);
    void traySettingsChanged();
    void protocolRegistrationChanged();
    void protocolUrlReceived(const QString &url);
    void queueSelectionChanged();
    void proxySettingsChanged();
    void proxyTestChanged();
    void httpAuthenticationRequested(const QString &id, const QString &origin,
                                    const QString &host, const QString &realm);
private:
    QString filenameForUrl(const QUrl &url) const;
    QString uniqueFileName(const QString &desired, const QString &destination,
                           const QList<DownloadRecord> &additionalRecords = {},
                           bool overwriteExisting = false) const;
    int indexOf(const QString &id) const;
    void setLastError(const QString &error);
    void evaluateQueuePowerAction();
    bool hasNonterminalDownloads() const;
    void launchWindowsPowerAction();
    void runPostDownloadProcess(const QString &key, const QString &program, const QStringList &arguments,
                                int timeoutMs, const QString &description,
                                std::function<void(bool)> finished = {});
    void reportPostDownloadProcessError(const QString &description, const QString &message);
    void performPostDownloadAction(const DownloadRecord &record);
    PersistenceRepository m_repository;
    DownloadQueue m_queue;
    QList<DownloadRecord> m_records;
    QSet<QString> m_categoryNames;
    QStringList m_queueNames{QStringLiteral("Default")};
    QString m_selectedQueueName = QStringLiteral("Default");
    QSet<QString> m_powerActionSessionIds;
    QString m_lastError;
    QString m_defaultDownloadDirectory;
    QString m_theme = QStringLiteral("System");
    QString m_postDownloadAction = QStringLiteral("None");
    QString m_postDownloadProgram;
    QHash<QString, QProcess *> m_postDownloadProcesses;
    QTimer m_shutdownCountdownTimer;
    QThread *m_powerStateThread = nullptr;
    int m_shutdownCountdownSeconds = 0;
    CategoryRules m_categoryRules;
    bool m_automaticCategoryFolders = false;
    bool m_clipboardMonitoringEnabled = false;
    QString m_queuePowerAction = QStringLiteral("None");
    bool m_powerActionArmed = false;
    bool m_powerActionPromptVisible = false;
    bool m_powerActionSessionHasCompletion = false;
    bool m_defenderScanEnabled = false;
    QString m_lastClipboardText;
    QString m_proxyMode = QStringLiteral("Direct");
    QString m_proxyType = QStringLiteral("HTTP");
    QString m_proxyHost;
    QString m_proxyUsername;
    int m_proxyPort = 8080;
    int m_connectionsPerDownload = 8;
    int m_maximumBatchSize = 100;
    QString m_collisionPolicy = QStringLiteral("Rename");
    class TrayController *m_tray = nullptr;
    QPointer<QWindow> m_mainWindow;
    bool m_protocolRegistered = false;
    bool m_minimizeToTray = false;
    bool m_notificationsEnabled = true;
    bool m_proxyTestRunning = false;
    QString m_proxyTestMessage;
};
