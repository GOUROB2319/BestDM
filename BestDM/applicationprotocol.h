#pragma once

#include <QString>

class ApplicationProtocol final {
public:
    static bool available();
    static bool parseHandlerUri(const QString &input, QString *downloadUrl, QString *error = nullptr);
    static bool isSafeDownloadUrl(const QString &input);
    static bool isRegistered();
    static bool setRegistered(bool enabled, QString *error = nullptr);
};
