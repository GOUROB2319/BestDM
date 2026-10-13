#include "../BestDM/downloadqueue.h"
#include "../BestDM/downloadmanager.h"
#include "../BestDM/downloadtask.h"
#include "../BestDM/safefile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QSignalSpy>
#include <QSet>
#include <QStandardPaths>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <functional>
#include <algorithm>
#include <utility>

class LocalHttpFixture final : public QObject {
    Q_OBJECT
public:
    using Handler = std::function<QByteArray(const QByteArray &request)>;

    explicit LocalHttpFixture(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                auto *socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (m_processed.contains(socket)) return;
                    m_buffers[socket].append(socket->readAll());
                    const auto request = m_buffers.value(socket);
                    if (!request.contains("\r\n\r\n")) return;
                    m_processed.insert(socket);
                    m_requests.append(request);
                    const auto response = m_handler ? m_handler(request) : responseBytes(200, "ok");
                    if (m_holdResponses) m_pending.append({socket, response});
                    else send(socket, response);
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    m_buffers.remove(socket);
                    m_processed.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::AnyIPv4, 0); }
    QString url(const QString &path = QStringLiteral("/file")) const {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path);
    }
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    int requestCount() const { return int(m_requests.size()); }
    QList<QByteArray> requests() const { return m_requests; }
    void setHoldResponses(bool hold) { m_holdResponses = hold; }
    void releasePending() {
        m_holdResponses = false;
        QList<QPair<QPointer<QTcpSocket>, QByteArray>> pending;
        pending.swap(m_pending);
        for (const auto &entry : pending)
            if (entry.first) send(entry.first, entry.second);
    }

    static QByteArray responseBytes(int status, const QByteArray &body, const QByteArray &headers = {}) {
        QByteArray phrase = "OK";
        if (status == 301) phrase = "Moved Permanently";
        else if (status == 302) phrase = "Found";
        else if (status == 307) phrase = "Temporary Redirect";
        else if (status == 308) phrase = "Permanent Redirect";
        else if (status == 206) phrase = "Partial Content";
        else if (status == 401) phrase = "Unauthorized";
        else if (status == 404) phrase = "Not Found";
        else if (status == 407) phrase = "Proxy Authentication Required";
        else if (status == 416) phrase = "Range Not Satisfiable";
        else if (status == 503) phrase = "Service Unavailable";
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + phrase + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n";
        response += headers;
        response += "\r\n";
        response += body;
        return response;
    }

private:
    static void send(QTcpSocket *socket, const QByteArray &response) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    Handler m_handler;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QSet<QTcpSocket *> m_processed;
    QList<QByteArray> m_requests;
    QList<QPair<QPointer<QTcpSocket>, QByteArray>> m_pending;
    bool m_holdResponses = false;
};

class DownloadTaskTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("BestDMNetworkTests"));
        QCoreApplication::setApplicationName(QStringLiteral("BestDMNetworkTests"));
        QSettings().setValue(QStringLiteral("network/proxyMode"), QStringLiteral("Direct"));
        QSettings().setValue(QStringLiteral("network/proxyType"), QStringLiteral("HTTP"));
        QSettings().setValue(QStringLiteral("schedule/enabled"), false);
        QSettings().setValue(QStringLiteral("downloads/collisionPolicy"), QStringLiteral("Rename"));
        QSettings().remove(QStringLiteral("downloads/defaultDestination"));
        const QDir appData(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        QVERIFY(QDir().mkpath(appData.path()));
        QFile::remove(appData.filePath(QStringLiteral("bestdm.sqlite")));
    }

    void cleanup() {
        QSettings().remove(QStringLiteral("downloads/defaultDestination"));
        QSettings().setValue(QStringLiteral("schedule/enabled"), false);
        QSettings().setValue(QStringLiteral("downloads/maximumConcurrent"), 3);
        QSettings().setValue(QStringLiteral("downloads/maximumRetries"), 3);
        QSettings().setValue(QStringLiteral("downloads/maximumBatchSize"), 100);
        QSettings().setValue(QStringLiteral("downloads/collisionPolicy"), QStringLiteral("Rename"));
        const QDir appData(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        QFile::remove(appData.filePath(QStringLiteral("bestdm.sqlite-wal")));
        QFile::remove(appData.filePath(QStringLiteral("bestdm.sqlite-shm")));
        QFile::remove(appData.filePath(QStringLiteral("bestdm.sqlite-journal")));
        QFile::remove(appData.filePath(QStringLiteral("bestdm.sqlite")));
    }

    void downloadsFullResponse() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) {
            return LocalHttpFixture::responseBytes(200, "hello world", "ETag: \"v1\"\r\nAccept-Ranges: bytes\r\n");
        });

        auto record = makeRecord(server, destination.path(), QStringLiteral("full.txt"));
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(task.record().received, qint64(11));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("full.txt"))), QByteArray("hello world"));
        QCOMPARE(server.requestCount(), 1);
    }

    void httpBasicAuthenticationRetriesWithSessionCredential() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (!request.contains("Authorization: Basic dXNlcjpwYXNz"))
                return LocalHttpFixture::responseBytes(401, "", "WWW-Authenticate: Basic realm=\"BestDM test\"\r\n");
            return LocalHttpFixture::responseBytes(200, "authenticated bytes");
        });

        auto record = makeRecord(server, destination.path(), QStringLiteral("auth.txt"));
        const QUrl url(record.url);
        const QString origin = url.scheme().toLower() + QStringLiteral("://") + url.host().toLower()
            + QLatin1Char(':') + QString::number(url.port(80));
        DownloadTask task(record);
        task.setHttpCredentials(origin, QStringLiteral("user"), QStringLiteral("pass"));
        task.start();

        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed
                                 || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("auth.txt"))), QByteArray("authenticated bytes"));
        QCOMPARE(server.requestCount(), 2);
    }

    void httpBasicAuthenticationRejectsBadCredentialWithoutLooping() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) {
            return LocalHttpFixture::responseBytes(401, "", "WWW-Authenticate: Basic realm=\"BestDM test\"\r\n");
        });

        auto record = makeRecord(server, destination.path(), QStringLiteral("auth-rejected.txt"));
        const QUrl url(record.url);
        const QString origin = url.scheme().toLower() + QStringLiteral("://") + url.host().toLower()
            + QLatin1Char(':') + QString::number(url.port(80));
        DownloadTask task(record);
        task.setHttpCredentials(origin, QStringLiteral("user"), QStringLiteral("wrong-password"));
        QSignalSpy authenticationSpy(&task, &DownloadTask::authenticationRequested);
        task.start();

        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed
                                 || task.record().state == DownloadState::Failed, 5000);
        QVERIFY(task.record().state == DownloadState::Failed);
        QVERIFY(task.record().error.contains(QStringLiteral("rejected"), Qt::CaseInsensitive));
        QVERIFY(!task.record().error.contains(QStringLiteral("wrong-password")));
        QCOMPARE(authenticationSpy.count(), 1);
        QCOMPARE(server.requestCount(), 2);
    }

    void noFollowFileOpenSupportsAppendTruncateAndRead() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("safe-file.txt"));
        QFile file;
        QVERIFY(openFileNoFollow(file, path, QIODevice::WriteOnly | QIODevice::Append));
        QCOMPARE(file.write("first"), qint64(5));
        QVERIFY(file.flush());
        file.close();

        QVERIFY(openFileNoFollow(file, path, QIODevice::WriteOnly | QIODevice::Append));
        QCOMPARE(file.write(" second"), qint64(7));
        QVERIFY(file.flush());
        file.close();

        QVERIFY(openFileNoFollow(file, path, QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("final"), qint64(5));
        QVERIFY(file.flush());
        file.close();

        QVERIFY(openFileNoFollow(file, path, QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("final"));
    }

    void partialSymlinkIsRejectedWithoutTouchingTarget() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        const QString targetPath = destination.filePath(QStringLiteral("outside.txt"));
        writeFile(targetPath, "keep this file");
        const QString partialPath = destination.filePath(QStringLiteral("linked.txt.part"));
        if (!QFile::link(targetPath, partialPath) || !QFileInfo(partialPath).isSymLink())
            QSKIP("This platform or test environment cannot create a symbolic link.");
        QFile safeOpenAttempt;
        QVERIFY(!openFileNoFollow(safeOpenAttempt, partialPath, QIODevice::WriteOnly | QIODevice::Append));

        LocalHttpFixture server;
        QVERIFY(server.listen());
        auto record = makeRecord(server, destination.path(), QStringLiteral("linked.txt"));
        DownloadTask task(record);
        task.start();

        QVERIFY(task.record().state == DownloadState::Failed);
        QVERIFY(!task.record().retryable);
        QVERIFY(task.record().error.contains(QStringLiteral("symbolic link")));
        QCOMPARE(readFile(targetPath), QByteArray("keep this file"));
        QCOMPARE(server.requestCount(), 0);
    }

    void segmentSymlinkIsRejectedWithoutTouchingTarget() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        const QString targetPath = destination.filePath(QStringLiteral("outside-segment.txt"));
        writeFile(targetPath, "keep segment target");
        const QString segmentPath = destination.filePath(QStringLiteral("linked-segment.txt.part.seg.0"));
        if (!QFile::link(targetPath, segmentPath) || !QFileInfo(segmentPath).isSymLink())
            QSKIP("This platform or test environment cannot create a symbolic link.");
        QFile safeOpenAttempt;
        QVERIFY(!openFileNoFollow(safeOpenAttempt, segmentPath, QIODevice::WriteOnly | QIODevice::Append));

        LocalHttpFixture server;
        QVERIFY(server.listen());
        auto record = makeRecord(server, destination.path(), QStringLiteral("linked-segment.txt"));
        record.total = 1;
        record.etag = QStringLiteral("\"stable\"");
        record.rangeSupported = true;
        record.connectionCount = 2;
        DownloadTask task(record);
        task.start();

        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Failed, 1000);
        QVERIFY(task.record().error.contains(QStringLiteral("symbolic link")));
        QCOMPARE(readFile(targetPath), QByteArray("keep segment target"));
        QCOMPARE(server.requestCount(), 0);
    }

    void renamePolicyKeepsExistingOutputUntouched() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "new bytes"); });
        const QString fileName = QStringLiteral("collision.txt");
        writeFile(destination.filePath(fileName), "old bytes");
        auto record = makeRecord(server, destination.path(), fileName);
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed
                                 || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("old bytes"));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("collision (1).txt"))), QByteArray("new bytes"));
    }

    void overwritePolicyReplacesOnlyAfterSuccessfulTransfer() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "new bytes"); });
        const QString fileName = QStringLiteral("replace.txt");
        writeFile(destination.filePath(fileName), "old bytes");
        auto record = makeRecord(server, destination.path(), fileName);
        record.overwriteExisting = true;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed
                                 || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("new bytes"));
    }

    void overwriteFailurePreservesExistingOutput() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(404, "missing"); });
        const QString fileName = QStringLiteral("preserved.txt");
        writeFile(destination.filePath(fileName), "old bytes");
        auto record = makeRecord(server, destination.path(), fileName);
        record.overwriteExisting = true;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed
                                 || task.record().state == DownloadState::Failed, 5000);
        QVERIFY(task.record().state == DownloadState::Failed);
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("old bytes"));
    }

    void overwritePreferenceFlowsFromManagerToSuccessfulTask() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD ")) return LocalHttpFixture::responseBytes(200, QByteArray{});
            return LocalHttpFixture::responseBytes(200, "replacement", "Content-Type: text/plain\r\n");
        });
        const QString fileName = QStringLiteral("manager-overwrite.txt");
        writeFile(destination.filePath(fileName), "original");
        DownloadManager manager;
        manager.setMaximumRetries(0);
        manager.setCollisionPolicy(QStringLiteral("Overwrite"));
        QVERIFY2(manager.addUrls(server.url(), destination.path(), fileName), qPrintable(manager.lastError()));
        QTRY_COMPARE_WITH_TIMEOUT(manager.completedCount(), 1, 7000);
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("replacement"));
    }

    void askPolicyRequiresExplicitCollisionReview() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        writeFile(destination.filePath(QStringLiteral("ask.txt")), "preserve this");
        DownloadManager manager;
        manager.setCollisionPolicy(QStringLiteral("Ask"));
        const QStringList conflicts = manager.existingOutputConflicts(server.url(), destination.path(),
                                                                       QStringLiteral("ask.txt"));
        QCOMPARE(conflicts, QStringList{QStringLiteral("ask.txt")});
        QVERIFY(!manager.addUrls(server.url(), destination.path(), QStringLiteral("ask.txt")));
        QVERIFY(manager.lastError().contains(QStringLiteral("Review")));
        QCOMPARE(manager.rowCount(), 0);
        QCOMPARE(readFile(destination.filePath(QStringLiteral("ask.txt"))), QByteArray("preserve this"));
    }

    void askPolicyTreatsSymlinkOutputAsConflict() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        const QString targetPath = destination.filePath(QStringLiteral("external-target.txt"));
        writeFile(targetPath, "keep target");
        const QString linkPath = destination.filePath(QStringLiteral("linked-output.txt"));
        if (!QFile::link(targetPath, linkPath) || !QFileInfo(linkPath).isSymLink())
            QSKIP("This platform or test environment cannot create a symbolic link.");

        DownloadManager manager;
        manager.setCollisionPolicy(QStringLiteral("Ask"));
        QCOMPARE(manager.existingOutputConflicts(server.url(), destination.path(),
                                                  QStringLiteral("linked-output.txt")),
                 QStringList{QStringLiteral("linked-output.txt")});
        QVERIFY(!manager.addUrls(server.url(), destination.path(), QStringLiteral("linked-output.txt")));
        QCOMPARE(readFile(targetPath), QByteArray("keep target"));
        QCOMPARE(server.requestCount(), 0);
    }

    void collisionPolicyPreferencePersistsAcrossManagerRestart() {
        QSettings().setValue(QStringLiteral("downloads/collisionPolicy"), QStringLiteral("Rename"));
        {
            DownloadManager manager;
            manager.setCollisionPolicy(QStringLiteral("Ask"));
            QCOMPARE(manager.collisionPolicy(), QStringLiteral("Ask"));
        }
        DownloadManager restored;
        QCOMPARE(restored.collisionPolicy(), QStringLiteral("Ask"));
    }

    void redirectLoopStopsAtConfiguredHopLimit() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        const QByteArray location = QByteArrayLiteral("Location: ")
            + server.url(QStringLiteral("/loop")).toUtf8() + QByteArrayLiteral("\r\n");
        server.setHandler([location](const QByteArray &) {
            return LocalHttpFixture::responseBytes(302, QByteArray{}, location);
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("redirect.txt"));
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed
                                 || task.record().state == DownloadState::Failed, 5000);
        QVERIFY(task.record().state == DownloadState::Failed);
        QVERIFY(task.record().error.contains(QStringLiteral("redirect"), Qt::CaseInsensitive));
        QVERIFY(server.requestCount() > 0);
        QVERIFY(server.requestCount() <= 12);
        QVERIFY(!QFileInfo::exists(destination.filePath(QStringLiteral("redirect.txt"))));
    }

    void defaultDownloadFolderValidatesAndPersists() {
        QTemporaryDir selected;
        QVERIFY(selected.isValid());
        {
            DownloadManager manager;
            QVERIFY2(manager.setDefaultDownloadDirectory(selected.path()), qPrintable(manager.lastError()));
            QCOMPARE(manager.defaultDownloadDirectory(), QDir(selected.path()).absolutePath());
            QCOMPARE(manager.defaultDestination(), QDir(selected.path()).absolutePath());
            QVERIFY(!manager.setDefaultDownloadDirectory(QDir(selected.path()).filePath(QStringLiteral("missing"))));
            QVERIFY(!manager.lastError().isEmpty());
        }
        {
            DownloadManager restored;
            QCOMPARE(restored.defaultDownloadDirectory(), QDir(selected.path()).absolutePath());
        }
    }

    void blankBatchDestinationUsesSavedDefaultFolder() {
        QTemporaryDir selected;
        QVERIFY(selected.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        DownloadManager manager;
        QVERIFY2(manager.setDefaultDownloadDirectory(selected.path()), qPrintable(manager.lastError()));
        QVERIFY(manager.addUrls(server.url(), QString{}));
        QCOMPARE(manager.rowCount(), 1);
        QCOMPARE(manager.data(manager.index(0, 0), DownloadManager::DestinationRole).toString(),
                 QDir(selected.path()).absolutePath());
    }

    void managerBulkPauseAndResumeActionsApplyToSelectedIds() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        DownloadManager manager;
        manager.setMaximumConcurrent(2);
        manager.setMaximumRetries(0);
        QVERIFY2(manager.setDefaultDownloadDirectory(destination.path()), qPrintable(manager.lastError()));
        const QString urls = server.url(QStringLiteral("/first")) + QLatin1Char('\n')
            + server.url(QStringLiteral("/second"));
        QVERIFY(manager.addUrls(urls, destination.path()));
        QCOMPARE(manager.rowCount(), 2);
        QVariantList ids;
        for (int row = 0; row < manager.rowCount(); ++row)
            ids.append(manager.data(manager.index(row, 0), DownloadManager::IdRole));
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 2, 5000);
        manager.pauseDownloads(QVariantList{ids.at(0), QStringLiteral("stale-id")});
        manager.pauseDownloads(QVariantList{ids.at(1)});
        QTRY_VERIFY_WITH_TIMEOUT(manager.pausedCount() == 2, 5000);
        server.releasePending();
        manager.resumeDownloads(ids);
        QTRY_VERIFY_WITH_TIMEOUT(manager.completedCount() == 2
                                 || manager.failedCount() == 2, 10000);
        QCOMPARE(manager.completedCount(), 2);
    }

    void managerEnforcesSafeBatchUrlLimit() {
        QSettings().setValue(QStringLiteral("downloads/maximumBatchSize"), 100);
        DownloadManager manager;
        const int before = manager.rowCount();
        QStringList urls;
        for (int i = 0; i < 101; ++i)
            urls.append(QStringLiteral("https://example.test/file-%1.bin").arg(i));
        const auto inspection = manager.inspectUrls(urls.join(QLatin1Char('\n')));
        QVERIFY(inspection.value(QStringLiteral("error")).toString().contains(QStringLiteral("100")));
        QVERIFY(!manager.addUrls(urls.join(QLatin1Char('\n')), QDir::tempPath()));
        QCOMPARE(manager.rowCount(), before);
    }

    void managerUsesPersistedConfigurableBatchUrlLimit() {
        QSettings().setValue(QStringLiteral("downloads/maximumBatchSize"), 100);
        {
            DownloadManager manager;
            manager.setMaximumBatchSize(2);
            QCOMPARE(manager.maximumBatchSize(), 2);
        }
        DownloadManager restored;
        QCOMPARE(restored.maximumBatchSize(), 2);
        QStringList urls;
        for (int i = 0; i < 3; ++i)
            urls.append(QStringLiteral("https://example.test/configured-%1.bin").arg(i));
        const int before = restored.rowCount();
        const auto inspection = restored.inspectUrls(urls.join(QLatin1Char('\n')));
        QVERIFY(inspection.value(QStringLiteral("error")).toString().contains(QStringLiteral("2")));
        QVERIFY(!restored.addUrls(urls.join(QLatin1Char('\n')), QDir::tempPath()));
        QCOMPARE(restored.rowCount(), before);
        QSettings().setValue(QStringLiteral("downloads/maximumBatchSize"), 100);
    }

    void managerImportsOnlyBoundedValidUtf8UrlLists() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DownloadManager manager;
        const QString path = directory.filePath(QStringLiteral("links.txt"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray contents = QByteArray::fromHex("efbbbf") + "https://one.example/a\r\nhttps://two.example/b\n";
            QCOMPARE(file.write(contents), qint64(contents.size()));
        }
        QCOMPARE(manager.readUrlListFile(QUrl::fromLocalFile(path)),
                 QStringLiteral("https://one.example/a\r\nhttps://two.example/b\n"));
        QVERIFY(manager.lastError().isEmpty());

        const QString invalidPath = directory.filePath(QStringLiteral("invalid.txt"));
        {
            QFile file(invalidPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray::fromHex("fffe")), qint64(2));
        }
        QVERIFY(manager.readUrlListFile(QUrl::fromLocalFile(invalidPath)).isEmpty());
        QVERIFY(manager.lastError().contains(QStringLiteral("UTF-8")));

        const QString wrongExtensionPath = directory.filePath(QStringLiteral("links.csv"));
        {
            QFile file(wrongExtensionPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray contents("https://one.example/a\n");
            QCOMPARE(file.write(contents), qint64(contents.size()));
        }
        QVERIFY(manager.readUrlListFile(QUrl::fromLocalFile(wrongExtensionPath)).isEmpty());
        QVERIFY(manager.lastError().contains(QStringLiteral(".txt")));
        QVERIFY(manager.readUrlListFile(QUrl(QStringLiteral("https://example.test/links.txt"))).isEmpty());
        QVERIFY(manager.lastError().contains(QStringLiteral("local"), Qt::CaseInsensitive));

        const QString emptyPath = directory.filePath(QStringLiteral("empty.txt"));
        QFile emptyFile(emptyPath);
        QVERIFY(emptyFile.open(QIODevice::WriteOnly));
        emptyFile.close();
        QVERIFY(manager.readUrlListFile(QUrl::fromLocalFile(emptyPath)).isEmpty());
        QVERIFY(manager.lastError().contains(QStringLiteral("empty"), Qt::CaseInsensitive));

        const QString oversizedPath = directory.filePath(QStringLiteral("oversized.txt"));
        {
            QFile file(oversizedPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(1024 * 1024 + 1, 'x')), qint64(1024 * 1024 + 1));
        }
        QVERIFY(manager.readUrlListFile(QUrl::fromLocalFile(oversizedPath)).isEmpty());
        QVERIFY(manager.lastError().contains(QStringLiteral("1 MiB")));
    }

    void probesHttpMetadataAsynchronouslyBeforeTransfer() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD "))
                return LocalHttpFixture::responseBytes(200, "hello world", "Content-Type: application/test\r\nETag: \"probe-v1\"\r\nAccept-Ranges: bytes\r\nContent-Disposition: attachment; filename*=UTF-8''server%20name.bin\r\n");
            return LocalHttpFixture::responseBytes(200, "hello world", "Content-Type: application/test\r\nETag: \"probe-v1\"\r\n");
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("metadata.bin"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = true;
        DownloadTask task(record);
        QSignalSpy changes(&task, &DownloadTask::changed);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QVERIFY(changes.count() >= 2);
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(server.requests().first().startsWith("HEAD /file "));
        QVERIFY(server.requests().last().startsWith("GET /file "));
        QCOMPARE(task.record().total, qint64(11));
        QCOMPARE(task.record().contentType, QStringLiteral("application/test"));
        QCOMPARE(task.record().etag, QStringLiteral("\"probe-v1\""));
        QVERIFY(task.record().rangeSupported);
        QCOMPARE(task.record().fileName, QStringLiteral("server name.bin"));
        QVERIFY(!task.record().metadataProbePending);
        QVERIFY(!task.record().serverFilenameAllowed);
        QCOMPARE(readFile(destination.filePath(QStringLiteral("server name.bin"))), QByteArray("hello world"));
    }

    void serverFilenameCannotEscapeOrOverwriteExistingFiles() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        writeFile(destination.filePath(QStringLiteral("taken.bin")), "keep me");
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD "))
                return LocalHttpFixture::responseBytes(200, "new", "Content-Disposition: attachment; filename=\"taken.bin\"\r\n");
            return LocalHttpFixture::responseBytes(200, "new");
        });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumRetries(0);
        auto record = makeRecord(server, destination.path(), QStringLiteral("source.bin"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = true;
        const QString id = record.id;
        queue.enqueue(record);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
            return item.id == id && (item.state == DownloadState::Completed || item.state == DownloadState::Failed);
        }), 5000);
        const auto item = *std::find_if(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &entry) {
            return entry.id == id;
        });
        QVERIFY2(item.state == DownloadState::Completed, qPrintable(item.error));
        QCOMPARE(item.fileName, QStringLiteral("taken (1).bin"));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("taken.bin"))), QByteArray("keep me"));
        QCOMPARE(readFile(destination.filePath(item.fileName)), QByteArray("new"));
    }

    void unsafeServerFilenameIsReducedToSafeBasename() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD "))
                return LocalHttpFixture::responseBytes(200, "new", "Content-Disposition: attachment; filename=\"../../escape.bin\"\r\n");
            return LocalHttpFixture::responseBytes(200, "new");
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("source.bin"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = true;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(task.record().fileName, QStringLiteral(".._.._escape.bin"));
        QVERIFY(QFileInfo(destination.filePath(task.record().fileName)).exists());
        QVERIFY(!QFileInfo::exists(QDir(destination.path()).filePath(QStringLiteral("../escape.bin"))));
    }

    void explicitFilenameTakesPrecedenceOverServerSuggestion() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD "))
                return LocalHttpFixture::responseBytes(200, "data", "Content-Disposition: attachment; filename=\"remote.bin\"\r\n");
            return LocalHttpFixture::responseBytes(200, "data");
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("chosen.bin"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = false;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(task.record().fileName, QStringLiteral("chosen.bin"));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("chosen.bin"))), QByteArray("data"));
    }

    void serverFilenameWithDifferentExtensionDoesNotChangeCategoryPath() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD "))
                return LocalHttpFixture::responseBytes(200, "data", "Content-Disposition: attachment; filename=\"remote.zip\"\r\n");
            return LocalHttpFixture::responseBytes(200, "data");
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("source.bin"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = true;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(task.record().fileName, QStringLiteral("source.bin"));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("source.bin"))), QByteArray("data"));
    }

    void rejectedMetadataProbeFallsBackToGet() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD ")) return LocalHttpFixture::responseBytes(405, "");
            return LocalHttpFixture::responseBytes(200, "fallback");
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("fallback.txt"));
        record.metadataProbePending = true;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(server.requests().first().startsWith("HEAD /file "));
        QVERIFY(server.requests().last().startsWith("GET /file "));
        QCOMPARE(readFile(destination.filePath(QStringLiteral("fallback.txt"))), QByteArray("fallback"));
    }

    void cancelDuringMetadataProbeDoesNotStartGet() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        auto record = makeRecord(server, destination.path(), QStringLiteral("probe-cancel.txt"));
        record.metadataProbePending = true;
        DownloadTask task(record);
        task.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        task.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Canceled, 5000);
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(task.record().metadataProbePending);
        QVERIFY(!QFileInfo::exists(destination.filePath(QStringLiteral("probe-cancel.txt.part"))));
    }

    void cancelDuringMetadataProbeCanKeepPartialForLaterResume() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        const QString partialPath = destination.filePath(QStringLiteral("probe-keep.txt.part"));
        writeFile(partialPath, "saved bytes");
        auto record = makeRecord(server, destination.path(), QStringLiteral("probe-keep.txt"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = true;
        DownloadTask task(record);
        task.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        task.cancel(false);
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Canceled, 5000);
        QCOMPARE(readFile(partialPath), QByteArray("saved bytes"));
        QCOMPARE(task.record().received, qint64(11));
    }

    void pauseDuringMetadataProbeRepeatsProbeOnResume() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        server.setHandler([](const QByteArray &request) {
            if (request.startsWith("HEAD "))
                return LocalHttpFixture::responseBytes(200, "payload", "Content-Type: text/plain\r\nAccept-Ranges: bytes\r\nContent-Disposition: attachment; filename=\"resumed.txt\"\r\n");
            return LocalHttpFixture::responseBytes(200, "payload");
        });
        auto record = makeRecord(server, destination.path(), QStringLiteral("probe-pause.txt"));
        record.metadataProbePending = true;
        record.serverFilenameAllowed = true;
        DownloadTask pausedTask(record);
        pausedTask.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        pausedTask.pause();
        QTRY_VERIFY_WITH_TIMEOUT(pausedTask.record().state == DownloadState::Paused, 5000);
        QVERIFY(pausedTask.record().metadataProbePending);
        QVERIFY(pausedTask.record().serverFilenameAllowed);
        QVERIFY(!QFileInfo::exists(destination.filePath(QStringLiteral("probe-pause.txt.part"))));

        server.releasePending();
        DownloadTask resumedTask(pausedTask.record());
        resumedTask.start();
        QTRY_VERIFY_WITH_TIMEOUT(resumedTask.record().state == DownloadState::Completed
                                  || resumedTask.record().state == DownloadState::Failed, 5000);
        QVERIFY2(resumedTask.record().state == DownloadState::Completed, qPrintable(resumedTask.record().error));
        QVERIFY(server.requests().at(1).startsWith("HEAD /file "));
        QVERIFY(server.requests().last().startsWith("GET /file "));
        QCOMPARE(resumedTask.record().fileName, QStringLiteral("resumed.txt"));
    }

    void resumesOnlyValidatedByteRange() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) {
            return LocalHttpFixture::responseBytes(206, "world", "Content-Range: bytes 6-10/11\r\nETag: \"v1\"\r\n");
        });
        const QString fileName = QStringLiteral("resume.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "hello ");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 6;
        record.total = 0;
        record.etag = QStringLiteral("\"v1\"");
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QVERIFY(server.requests().first().contains("Range: bytes=6-"));
        QVERIFY(server.requests().first().contains("If-Range: \"v1\""));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("hello world"));
        QCOMPARE(task.record().total, qint64(11));
    }

    void continuesWhenServerCapsResumeRangeBeforeResourceEnd() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.contains("Range: bytes=6-"))
                return LocalHttpFixture::responseBytes(206, "6789A", "Content-Range: bytes 6-10/20\r\nETag: \"v1\"\r\n");
            if (request.contains("Range: bytes=11-"))
                return LocalHttpFixture::responseBytes(206, "BCDEFGHIJ", "Content-Range: bytes 11-19/20\r\nETag: \"v1\"\r\n");
            return LocalHttpFixture::responseBytes(500, "unexpected request");
        });
        const QString fileName = QStringLiteral("capped-range.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "012345");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 6;
        record.etag = QStringLiteral("\"v1\"");
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(server.requests().at(1).contains("Range: bytes=11-"));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("0123456789ABCDEFGHIJ"));
    }

    void ignoredRangeRestartsFromZero() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "fresh body"); });
        const QString fileName = QStringLiteral("ignored-range.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "stale!");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 6;
        record.etag = QStringLiteral("\"v1\"");
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QVERIFY(server.requests().first().contains("Range: bytes=6-"));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("fresh body"));
    }

    void partialWithoutValidatorRestartsBeforeRequestingRange() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "complete fresh body"); });
        const QString fileName = QStringLiteral("unvalidated.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "stale");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 5;
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QVERIFY(!server.requests().first().contains("Range:"));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("complete fresh body"));
    }

    void changedValidatorRestartsWithoutAppendingOldBytes() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.contains("Range: bytes=11-"))
                return LocalHttpFixture::responseBytes(206, "", "Content-Range: bytes 11-10/11\r\nETag: \"v2\"\r\n");
            return LocalHttpFixture::responseBytes(200, "new version", "ETag: \"v2\"\r\n");
        });
        const QString fileName = QStringLiteral("changed.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "old version");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 11;
        record.total = 11;
        record.etag = QStringLiteral("\"v1\"");
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(server.requests().at(1).startsWith("GET /file "));
        QVERIFY(!server.requests().at(1).contains("Range:"));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("new version"));
    }

    void unsatisfiedRangeRestartsFromZero() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &request) {
            if (request.contains("Range: bytes=6-"))
                return LocalHttpFixture::responseBytes(416, "stale", "Content-Range: bytes */5\r\n");
            return LocalHttpFixture::responseBytes(200, "fresh");
        });
        const QString fileName = QStringLiteral("unsatisfied.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "stale!");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 6;
        record.total = 11;
        record.etag = QStringLiteral("\"v1\"");
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY2(task.record().state == DownloadState::Completed, qPrintable(task.record().error));
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(!server.requests().at(1).contains("Range:"));
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("fresh"));
    }

    void httpErrorDoesNotBecomeDownloadedFileOrLeakUrl() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(404, "not found"); });
        auto record = makeRecord(server, destination.path(), QStringLiteral("missing.txt"));
        record.url += QStringLiteral("?token=secret-value");
        DownloadTask task(record);
        task.start();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Completed || task.record().state == DownloadState::Failed, 5000);
        QVERIFY(task.record().state == DownloadState::Failed);
        QVERIFY(task.record().error.contains(QStringLiteral("404")));
        QVERIFY(!task.record().error.contains(QStringLiteral("secret-value")));
        QVERIFY(!QFileInfo::exists(destination.filePath(QStringLiteral("missing.txt"))));
    }

    void cancelRemovesPartialFile() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "never sent"); });
        auto record = makeRecord(server, destination.path(), QStringLiteral("cancel.txt"));
        DownloadTask task(record);
        task.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        task.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(task.record().state == DownloadState::Canceled, 5000);
        QVERIFY(!QFileInfo::exists(destination.filePath(QStringLiteral("cancel.txt.part"))));
    }

    void queueNeverExceedsConfiguredConcurrency() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "x"); });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumConcurrent(1);
        for (int i = 0; i < 3; ++i)
            queue.enqueue(makeRecord(server, destination.path(), QStringLiteral("queued-%1.txt").arg(i)));
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        QCOMPARE(queue.activeCount(), 1);
        server.releasePending();
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(queue.activeCount() == 0, 5000);
        QCOMPARE(queue.records().size(), qsizetype(3));
        for (const auto &record : queue.records())
            QVERIFY(record.state == DownloadState::Completed);
    }

    void canceledDownloadWithKeptPartialCanResume() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        server.setHandler([](const QByteArray &) {
            return LocalHttpFixture::responseBytes(206, "new", "Content-Range: bytes 3-5/6\r\nETag: \"v1\"\r\n");
        });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumRetries(0);
        const QString fileName = QStringLiteral("kept-cancel.txt");
        writeFile(destination.filePath(fileName + QStringLiteral(".part")), "old");
        auto record = makeRecord(server, destination.path(), fileName);
        record.received = 3;
        record.total = 6;
        record.etag = QStringLiteral("\"v1\"");
        const QString id = record.id;
        queue.enqueue(record);
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        queue.cancel(id, false);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
            return item.id == id && item.state == DownloadState::Canceled;
        }), 5000);
        QCOMPARE(QFileInfo(destination.filePath(fileName + QStringLiteral(".part"))).size(), qint64(3));
        server.releasePending();
        queue.resume(id);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
            return item.id == id && item.state == DownloadState::Completed;
        }), 5000);
        QCOMPARE(readFile(destination.filePath(fileName)), QByteArray("oldnew"));
    }

    void removeCompletedTaskCanKeepOrDeleteItsFile() {
        for (const bool deleteFiles : {false, true}) {
            QTemporaryDir destination;
            QVERIFY(destination.isValid());
            LocalHttpFixture server;
            QVERIFY(server.listen());
            server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "final"); });
            PersistenceRepository repository;
            DownloadQueue queue(repository);
            queue.setMaximumRetries(0);
            const QString fileName = deleteFiles ? QStringLiteral("delete-final.txt") : QStringLiteral("keep-final.txt");
            auto record = makeRecord(server, destination.path(), fileName);
            const QString id = record.id;
            queue.enqueue(record);
            QTRY_VERIFY_WITH_TIMEOUT(queue.records().first().state == DownloadState::Completed, 5000);
            QVERIFY(QFileInfo::exists(destination.filePath(fileName)));
            QSignalSpy removed(&queue, &DownloadQueue::recordRemoved);
            queue.remove(id, deleteFiles);
            QCOMPARE(removed.count(), 1);
            QCOMPARE(QFileInfo::exists(destination.filePath(fileName)), !deleteFiles);
            QVERIFY(std::none_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
                return item.id == id;
            }));
        }
    }

    void removeActiveTaskHonorsPartialFileChoice() {
        for (const bool deleteFiles : {false, true}) {
            QTemporaryDir destination;
            QVERIFY(destination.isValid());
            LocalHttpFixture server;
            QVERIFY(server.listen());
            server.setHoldResponses(true);
            server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "partial"); });
            PersistenceRepository repository;
            DownloadQueue queue(repository);
            const QString fileName = deleteFiles ? QStringLiteral("delete-active.txt") : QStringLiteral("keep-active.txt");
            auto record = makeRecord(server, destination.path(), fileName);
            const QString id = record.id;
            queue.enqueue(record);
            QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
            QSignalSpy removed(&queue, &DownloadQueue::recordRemoved);
            queue.remove(id, deleteFiles);
            QTRY_COMPARE_WITH_TIMEOUT(removed.count(), 1, 5000);
            QCOMPARE(QFileInfo::exists(destination.filePath(fileName + QStringLiteral(".part"))), !deleteFiles);
            QVERIFY(!QFileInfo::exists(destination.filePath(fileName)));
        }
    }

    void removeKeepsRowWhenRequestedFileDeletionFails() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        const QString fileName = QStringLiteral("blocked-delete.txt");
        const QString partialPath = destination.filePath(fileName + QStringLiteral(".part"));
        QVERIFY(QDir().mkpath(partialPath));
        writeFile(QDir(partialPath).filePath(QStringLiteral("child")), "keep directory non-empty");
        PersistenceRepository repository;
        DownloadRecord record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.url = QStringLiteral("https://example.test/blocked-delete");
        record.fileName = fileName;
        record.destination = destination.path();
        record.state = DownloadState::Paused;
        QVERIFY(repository.save(record));
        DownloadQueue queue(repository);
        queue.remove(record.id, true);
        QCOMPARE(queue.records().size(), qsizetype(1));
        QCOMPARE(queue.records().first().id, record.id);
        QVERIFY(queue.records().first().error.contains(QStringLiteral("Could not delete")));
        QVERIFY(QFileInfo::exists(QDir(partialPath).filePath(QStringLiteral("child"))));
    }

    void queueWaitsUntilAnAllowedScheduleDay() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "scheduled"); });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        QStringList allowedDays;
        for (int day = 1; day <= 7; ++day)
            if (day != QDate::currentDate().dayOfWeek()) allowedDays.append(ScheduleWindow::dayName(day));
        QString error;
        QVERIFY2(queue.setScheduleSettings(true, allowedDays, QStringLiteral("00:00"), QStringLiteral("00:00"), &error), qPrintable(error));
        auto record = makeRecord(server, destination.path(), QStringLiteral("scheduled.txt"));
        const QString id = record.id;
        queue.enqueue(record);
        QVERIFY(queue.records().first().state == DownloadState::WaitingForSchedule);
        QCOMPARE(server.requestCount(), 0);

        QVERIFY2(queue.setScheduleSettings(true, QStringList{ScheduleWindow::dayName(QDate::currentDate().dayOfWeek())},
                                           QStringLiteral("00:00"), QStringLiteral("00:00"), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
            return item.id == id && item.state == DownloadState::Completed;
        }), 5000);
        QCOMPARE(server.requestCount(), 1);
    }

    void activeDownloadPausesAtScheduleBoundaryAndResumesLater() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHoldResponses(true);
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(200, "scheduled"); });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        const QString today = ScheduleWindow::dayName(QDate::currentDate().dayOfWeek());
        QString error;
        QVERIFY2(queue.setScheduleSettings(true, QStringList{today}, QStringLiteral("00:00"), QStringLiteral("00:00"), &error), qPrintable(error));
        auto record = makeRecord(server, destination.path(), QStringLiteral("boundary.txt"));
        const QString id = record.id;
        queue.enqueue(record);
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000);
        QStringList closedDays;
        for (int day = 1; day <= 7; ++day)
            if (day != QDate::currentDate().dayOfWeek()) closedDays.append(ScheduleWindow::dayName(day));
        QVERIFY2(queue.setScheduleSettings(true, closedDays, QStringLiteral("00:00"), QStringLiteral("00:00"), &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
            return item.id == id && item.state == DownloadState::WaitingForSchedule;
        }), 5000);

        QVERIFY2(queue.setScheduleSettings(true, QStringList{today}, QStringLiteral("00:00"), QStringLiteral("00:00"), &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 2, 5000);
        server.releasePending();
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(queue.records().cbegin(), queue.records().cend(), [&](const DownloadRecord &item) {
            return item.id == id && item.state == DownloadState::Completed;
        }), 5000);
    }

    void scheduleSettingsPersistAcrossQueueRecreation() {
        const QStringList days{QStringLiteral("Tue"), QStringLiteral("Thu"), QStringLiteral("Sun")};
        QString error;
        {
            PersistenceRepository repository;
            DownloadQueue queue(repository);
            QVERIFY2(queue.setScheduleSettings(true, days, QStringLiteral("21:30"), QStringLiteral("05:45"), &error), qPrintable(error));
        }
        {
            PersistenceRepository repository;
            DownloadQueue queue(repository);
            QVERIFY(queue.scheduleEnabled());
            QCOMPARE(queue.scheduleDays(), days);
            QCOMPARE(queue.scheduleStartTime(), QStringLiteral("21:30"));
            QCOMPARE(queue.scheduleEndTime(), QStringLiteral("05:45"));
        }
    }

    void globalBandwidthLimitIsSharedAcrossConcurrentTasks() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        const QByteArray payload(64 * 1024, 'b');
        server.setHandler([payload](const QByteArray &) {
            return LocalHttpFixture::responseBytes(200, payload);
        });

        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumConcurrent(2);
        queue.setMaximumRetries(0);
        queue.setSpeedLimitBytesPerSecond(24 * 1024);
        auto first = makeRecord(server, destination.path(), QStringLiteral("limited-a.bin"));
        auto second = makeRecord(server, destination.path(), QStringLiteral("limited-b.bin"));
        const QString firstId = first.id;
        const QString secondId = second.id;
        queue.enqueue(first);
        queue.enqueue(second);
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 2, 5000);

        QTest::qWait(1000);
        const qint64 written = QFileInfo(destination.filePath(QStringLiteral("limited-a.bin.part"))).size()
            + QFileInfo(destination.filePath(QStringLiteral("limited-b.bin.part"))).size();
        QVERIFY2(written > 0, "Both active transfers should make progress under the shared cap.");
        QVERIFY2(written < 36 * 1024,
                 qPrintable(QStringLiteral("The combined writes exceeded the cap allowance: %1 bytes.").arg(written)));

        const auto bothCompleted = [&queue, &firstId, &secondId] {
            bool firstCompleted = false;
            bool secondCompleted = false;
            for (const auto &record : queue.records()) {
                if (record.id == firstId) firstCompleted = record.state == DownloadState::Completed;
                if (record.id == secondId) secondCompleted = record.state == DownloadState::Completed;
            }
            return firstCompleted && secondCompleted;
        };
        QTRY_VERIFY_WITH_TIMEOUT(bothCompleted(), 10000);
        QCOMPARE(QFileInfo(destination.filePath(QStringLiteral("limited-a.bin"))).size(), qint64(payload.size()));
        QCOMPARE(QFileInfo(destination.filePath(QStringLiteral("limited-b.bin"))).size(), qint64(payload.size()));
    }

    void queueRetriesTemporaryServerFailure() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([&server](const QByteArray &) {
            if (server.requestCount() == 1)
                return LocalHttpFixture::responseBytes(503, "temporary");
            return LocalHttpFixture::responseBytes(200, "recovered");
        });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumConcurrent(1);
        queue.enqueue(makeRecord(server, destination.path(), QStringLiteral("retry.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(queue.records().first().state == DownloadState::Completed
                                 || queue.records().first().state == DownloadState::Failed, 10000);
        QVERIFY2(queue.records().first().state == DownloadState::Completed, qPrintable(queue.records().first().error));
        QCOMPARE(server.requestCount(), 2);
        QCOMPARE(queue.records().first().retryCount, 1);
        QCOMPARE(readFile(destination.filePath(QStringLiteral("retry.txt"))), QByteArray("recovered"));
    }

    void retryingStateIsVisibleAndCancellationStopsTheTimer() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(503, "temporary"); });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumRetries(3);
        auto record = makeRecord(server, destination.path(), QStringLiteral("cancel-retry.txt"));
        const QString id = record.id;
        queue.enqueue(record);
        QTRY_VERIFY_WITH_TIMEOUT(queue.records().first().state == DownloadState::Retrying, 5000);
        QVERIFY(queue.records().first().nextRetryAt > QDateTime::currentSecsSinceEpoch());
        QCOMPARE(queue.records().first().retryCount, 1);
        queue.cancel(id);
        QVERIFY(queue.records().first().state == DownloadState::Canceled);
        QCOMPARE(queue.records().first().nextRetryAt, qint64(0));
        QTest::qWait(2200);
        QCOMPARE(server.requestCount(), 1);
    }

    void zeroRetryLimitLeavesTransientFailureFailed() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        LocalHttpFixture server;
        QVERIFY(server.listen());
        server.setHandler([](const QByteArray &) { return LocalHttpFixture::responseBytes(503, "temporary"); });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        queue.setMaximumRetries(0);
        queue.enqueue(makeRecord(server, destination.path(), QStringLiteral("no-retry.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(queue.records().first().state == DownloadState::Failed, 5000);
        QCOMPARE(queue.records().first().retryCount, 0);
        QCOMPARE(queue.records().first().nextRetryAt, qint64(0));
        QCOMPARE(server.requestCount(), 1);
    }

    void manualHttpProxyRoutesAndTestsConnection() {
        LocalHttpFixture proxyServer;
        QVERIFY(proxyServer.listen());
        proxyServer.setHandler([](const QByteArray &) {
            return LocalHttpFixture::responseBytes(200, "");
        });
        PersistenceRepository repository;
        DownloadQueue queue(repository);
        QString error;
        QVERIFY2(queue.setProxyConfiguration(QStringLiteral("Manual"), QStringLiteral("HTTP"),
                                             QStringLiteral("127.0.0.1"), proxyServer.serverPort(),
                                             {}, {}, false, &error), qPrintable(error));
        QSignalSpy resultSpy(&queue, &DownloadQueue::proxyTestFinished);
        queue.testProxyConnection(QUrl(QStringLiteral("http://user:secret-value@destination.invalid/health")));
        QCOMPARE(resultSpy.count(), 1);
        const auto rejectedAddress = resultSpy.takeFirst();
        QVERIFY(!rejectedAddress.at(0).toBool());
        QVERIFY(!rejectedAddress.at(1).toString().contains(QStringLiteral("secret-value")));
        QCOMPARE(proxyServer.requestCount(), 0);
        queue.testProxyConnection(QUrl(QStringLiteral("http://destination.invalid/health")));
        QTRY_COMPARE_WITH_TIMEOUT(resultSpy.count(), 1, 5000);
        const auto result = resultSpy.takeFirst();
        QVERIFY(result.at(0).toBool());
        QVERIFY(result.at(1).toString().contains(QStringLiteral("succeeded")));
        QCOMPARE(proxyServer.requestCount(), 1);
        QVERIFY(proxyServer.requests().first().startsWith("HEAD http://destination.invalid/health "));
        proxyServer.setHandler([](const QByteArray &) {
            return LocalHttpFixture::responseBytes(407, "auth required");
        });
        queue.testProxyConnection(QUrl(QStringLiteral("http://destination.invalid/health")));
        QTRY_COMPARE_WITH_TIMEOUT(resultSpy.count(), 1, 5000);
        const auto rejected = resultSpy.takeFirst();
        QVERIFY(!rejected.at(0).toBool());
        QVERIFY(rejected.at(1).toString().contains(QStringLiteral("HTTP 407")));
        QVERIFY(!rejected.at(1).toString().contains(QStringLiteral("password"), Qt::CaseInsensitive));
        QCOMPARE(proxyServer.requestCount(), 2);
        QVERIFY(queue.setProxyConfiguration(QStringLiteral("Direct"), QStringLiteral("HTTP"),
                                            {}, 8080, {}, {}, false, &error));
    }

private:
    static DownloadRecord makeRecord(const LocalHttpFixture &server, const QString &destination, const QString &fileName) {
        DownloadRecord record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.url = server.url();
        record.fileName = fileName;
        record.destination = destination;
        record.state = DownloadState::Queued;
        return record;
    }

    static void writeFile(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return;
        file.write(bytes);
    }

    static QByteArray readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
};

QTEST_GUILESS_MAIN(DownloadTaskTest)
#include "test_download_task.moc"
