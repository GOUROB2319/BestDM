#pragma once

#include <QString>

class SecureCredentialStore final {
public:
    static bool isAvailable();
    static QString proxyTarget(const QString &type, const QString &host, int port);
    static bool saveProxyPassword(const QString &target, const QString &username, const QString &password, QString *error = nullptr);
    static bool loadProxyPassword(const QString &target, QString *username, QString *password, QString *error = nullptr);
    static bool clearProxyPassword(const QString &target, QString *error = nullptr);
};
