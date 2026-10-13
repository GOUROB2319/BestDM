#include "singleinstancecoordinator.h"

#include "applicationprotocol.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <utility>

namespace {
constexpr qsizetype maximumMessageSize = 16384;
constexpr int maximumForwardedUrls = 100;
}

SingleInstanceCoordinator::SingleInstanceCoordinator(const QStringList &arguments, QObject *parent)
    : QObject(parent), m_serverName(serverName()) {
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstanceCoordinator::acceptConnections);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    QStringList urls;
    for (const QString &argument : arguments) {
        if (!argument.startsWith(QStringLiteral("bestdm:"), Qt::CaseInsensitive)) continue;
        QString url;
        QString error;
        if (!ApplicationProtocol::parseHandlerUri(argument, &url, &error)) {
            m_error = error;
            return;
        }
        urls.append(url);
        if (urls.size() > maximumForwardedUrls) {
            m_error = QStringLiteral("A BestDM link request can contain at most 100 URLs.");
            return;
        }
    }
    if (!m_server.listen(m_serverName)) {
        QLocalSocket probe;
        probe.connectToServer(m_serverName, QIODevice::ReadWrite);
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            m_forwarded = forwardToPrimary(urls);
            if (!m_forwarded) m_error = QStringLiteral("BestDM is already running but did not accept the request.");
            return;
        }
        QLocalServer::removeServer(m_serverName);
        if (!m_server.listen(m_serverName)) {
            m_error = QStringLiteral("BestDM could not create its single-instance endpoint: ") + m_server.errorString();
            return;
        }
    }
    m_initialUrls = urls;
}

QString SingleInstanceCoordinator::serverName() const {
    const QByteArray identity = (qEnvironmentVariable("USERDOMAIN") + QLatin1Char('\\')
        + qEnvironmentVariable("USERNAME") + QStringLiteral("/BestDM" )).toUtf8();
    const QByteArray digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(24);
    return QStringLiteral("BestDM-") + QString::fromLatin1(digest);
}

bool SingleInstanceCoordinator::forwardToPrimary(const QStringList &urls) {
    QLocalSocket socket;
    socket.connectToServer(m_serverName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(1000)) return false;
    QJsonArray jsonUrls;
    for (const QString &url : urls) jsonUrls.append(url);
    const QByteArray message = QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("open")},
        {QStringLiteral("urls"), jsonUrls}}).toJson(QJsonDocument::Compact) + '\n';
    if (message.size() > maximumMessageSize || socket.write(message) != message.size()
        || !socket.waitForBytesWritten(1000) || !socket.waitForReadyRead(2000)) return false;
    return socket.readLine().trimmed() == QByteArrayLiteral("OK");
}

void SingleInstanceCoordinator::dispatchInitial() {
    if (m_initialUrls.isEmpty()) return;
    emit activationRequested();
    for (const QString &url : std::as_const(m_initialUrls)) emit downloadUrlReceived(url);
    m_initialUrls.clear();
}

void SingleInstanceCoordinator::acceptConnections() {
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        socket->setReadBufferSize(maximumMessageSize + 1);
        socket->setProperty("bestdmRequest", QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, &SingleInstanceCoordinator::readRequest);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(3000, socket, [socket] {
            if (socket->state() != QLocalSocket::UnconnectedState) socket->disconnectFromServer();
        });
    }
}

void SingleInstanceCoordinator::readRequest() {
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) return;
    QByteArray data = socket->property("bestdmRequest").toByteArray();
    data += socket->readAll();
    if (data.size() > maximumMessageSize) {
        socket->write("ERR\n"); socket->disconnectFromServer(); return;
    }
    const qsizetype newline = data.indexOf('\n');
    if (newline < 0) { socket->setProperty("bestdmRequest", data); return; }
    if (newline != data.size() - 1) {
        socket->write("ERR\n"); socket->disconnectFromServer(); return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(data.left(newline));
    const QJsonObject object = document.object();
    const QJsonArray urls = object.value(QStringLiteral("urls")).toArray();
    if (!document.isObject() || object.size() != 2
        || !object.contains(QStringLiteral("type")) || !object.contains(QStringLiteral("urls"))
        || !object.value(QStringLiteral("urls")).isArray()
        || object.value(QStringLiteral("type")).toString() != QStringLiteral("open")
        || urls.size() > maximumForwardedUrls) {
        socket->write("ERR\n"); socket->disconnectFromServer(); return;
    }
    QStringList accepted;
    for (const QJsonValue &value : urls) {
        if (!value.isString() || !ApplicationProtocol::isSafeDownloadUrl(value.toString())) {
            socket->write("ERR\n"); socket->disconnectFromServer(); return;
        }
        accepted.append(value.toString());
    }
    socket->write("OK\n");
    socket->flush();
    emit activationRequested();
    for (const QString &url : std::as_const(accepted)) emit downloadUrlReceived(url);
    socket->disconnectFromServer();
}
