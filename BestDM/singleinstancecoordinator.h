#pragma once

#include <QObject>
#include <QStringList>
#include <QLocalServer>

class QLocalSocket;

class SingleInstanceCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit SingleInstanceCoordinator(const QStringList &arguments, QObject *parent = nullptr);
    bool isForwarded() const { return m_forwarded; }
    bool hasError() const { return !m_error.isEmpty(); }
    QString errorString() const { return m_error; }
    void dispatchInitial();
signals:
    void activationRequested();
    void downloadUrlReceived(const QString &url);
private slots:
    void acceptConnections();
    void readRequest();
private:
    bool becomePrimary();
    bool forwardToPrimary(const QStringList &urls);
    QString serverName() const;
    QLocalServer m_server;
    QString m_serverName;
    QStringList m_initialUrls;
    QString m_error;
    bool m_forwarded = false;
};
