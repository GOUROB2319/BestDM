#include "applicationprotocol.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <string>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
constexpr qsizetype maximumProtocolUriLength = 8192;
#ifdef Q_OS_WIN
constexpr wchar_t protocolKey[] = L"Software\\Classes\\bestdm";
constexpr wchar_t protocolCommandKey[] = L"Software\\Classes\\bestdm\\shell\\open\\command";
constexpr wchar_t protocolDescription[] = L"URL:BestDM Protocol";

bool readRegistryString(const wchar_t *keyPath, const wchar_t *valueName, QString *value) {
    DWORD byteCount = 0;
    const LONG sizeResult = RegGetValueW(HKEY_CURRENT_USER, keyPath, valueName, RRF_RT_REG_SZ,
                                         nullptr, nullptr, &byteCount);
    if (sizeResult != ERROR_SUCCESS || byteCount < sizeof(wchar_t)) return false;
    QVector<wchar_t> buffer(int(byteCount / sizeof(wchar_t)) + 1, L'\0');
    DWORD actualBytes = byteCount;
    if (RegGetValueW(HKEY_CURRENT_USER, keyPath, valueName, RRF_RT_REG_SZ,
                     nullptr, buffer.data(), &actualBytes) != ERROR_SUCCESS) return false;
    *value = QString::fromWCharArray(buffer.constData());
    return true;
}

bool writeRegistryString(HKEY key, const wchar_t *name, const QString &value) {
    const std::wstring wide = value.toStdWString();
    return RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(wide.c_str()),
                          DWORD((wide.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool hasBestDmProtocolMarker() {
    QString description;
    QString protocolMarker;
    return readRegistryString(protocolKey, nullptr, &description)
        && description == QString::fromWCharArray(protocolDescription)
        && readRegistryString(protocolKey, L"URL Protocol", &protocolMarker);
}

bool commandBelongsToBestDm(const QString &command) {
    const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const QString expectedEnding = QStringLiteral("\"") + executable + QStringLiteral("\" \"%1\"");
    return command.endsWith(expectedEnding, Qt::CaseInsensitive);
}

LONG deleteRegistryTree() {
    const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, protocolKey);
    return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
}
#endif
}

bool ApplicationProtocol::available() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool ApplicationProtocol::isSafeDownloadUrl(const QString &input) {
    if (input.isEmpty() || input.size() > maximumProtocolUriLength) return false;
    const QUrl url(input, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    return url.isValid() && url.host().size() > 0 && url.userInfo().isEmpty()
        && url.fragment().isEmpty()
        && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"));
}

bool ApplicationProtocol::parseHandlerUri(const QString &input, QString *downloadUrl, QString *error) {
    if (downloadUrl) downloadUrl->clear();
    if (input.size() > maximumProtocolUriLength) {
        if (error) *error = QStringLiteral("The BestDM link is too long.");
        return false;
    }
    const QUrl handler(input, QUrl::StrictMode);
    if (!handler.isValid() || handler.scheme().compare(QStringLiteral("bestdm"), Qt::CaseInsensitive) != 0
        || handler.host().compare(QStringLiteral("add"), Qt::CaseInsensitive) != 0
        || !handler.path().isEmpty() || !handler.userInfo().isEmpty() || handler.port(-1) != -1
        || !handler.fragment().isEmpty()) {
        if (error) *error = QStringLiteral("Use a valid bestdm://add link.");
        return false;
    }
    const QUrlQuery query(handler);
    const auto items = query.queryItems(QUrl::FullyDecoded);
    if (items.size() != 1 || items.constFirst().first != QStringLiteral("url")
        || !isSafeDownloadUrl(items.constFirst().second)) {
        if (error) *error = QStringLiteral("The BestDM link must contain one valid HTTP or HTTPS url parameter.");
        return false;
    }
    if (downloadUrl) *downloadUrl = items.constFirst().second;
    if (error) error->clear();
    return true;
}

bool ApplicationProtocol::isRegistered() {
#ifdef Q_OS_WIN
    QString command;
    return hasBestDmProtocolMarker()
        && readRegistryString(protocolCommandKey, nullptr, &command)
        && commandBelongsToBestDm(command)
        && command.contains(QCoreApplication::applicationFilePath(), Qt::CaseInsensitive);
#else
    return false;
#endif
}

bool ApplicationProtocol::setRegistered(bool enabled, QString *error) {
#ifdef Q_OS_WIN
    if (!enabled) {
        QString command;
        if (!hasBestDmProtocolMarker()) {
            if (error) error->clear();
            return true;
        }
        if (!readRegistryString(protocolCommandKey, nullptr, &command) || !commandBelongsToBestDm(command)) {
            if (error) *error = QStringLiteral("BestDM did not unregister the protocol because its registry command belongs to another application.");
            return false;
        }
        const LONG result = deleteRegistryTree();
        if (result != ERROR_SUCCESS) {
            if (error) *error = QStringLiteral("Windows could not remove the BestDM protocol registration (error %1).")
                                    .arg(qulonglong(result));
            return false;
        }
        if (error) error->clear();
        return true;
    }

    DWORD disposition = 0;
    HKEY key = nullptr;
    const LONG createResult = RegCreateKeyExW(HKEY_CURRENT_USER, protocolKey, 0, nullptr, 0,
                                               KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr,
                                               &key, &disposition);
    if (createResult != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("Windows could not create the BestDM protocol registration (error %1).")
                                .arg(qulonglong(createResult));
        return false;
    }
    RegCloseKey(key);
    if (disposition == REG_OPENED_EXISTING_KEY && !hasBestDmProtocolMarker()) {
        if (error) *error = QStringLiteral("The bestdm:// protocol is already registered by another application.");
        return false;
    }

    const LONG commandCreateResult = RegCreateKeyExW(HKEY_CURRENT_USER, protocolCommandKey, 0, nullptr, 0,
                                                      KEY_SET_VALUE, nullptr, &key, nullptr);
    if (commandCreateResult != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("Windows could not configure the BestDM protocol command (error %1).")
                                .arg(qulonglong(commandCreateResult));
        return false;
    }
    const QString executable = QCoreApplication::applicationFilePath();
    const QString command = QStringLiteral("\"") + executable + QStringLiteral("\" \"%1\"");
    const bool commandWritten = writeRegistryString(key, nullptr, command);
    RegCloseKey(key);
    if (!commandWritten) {
        if (error) *error = QStringLiteral("Windows could not save the BestDM protocol command.");
        return false;
    }

    if (RegOpenKeyExW(HKEY_CURRENT_USER, protocolKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("Windows could not finish the BestDM protocol registration.");
        return false;
    }
    const QString marker = QString::fromWCharArray(protocolDescription);
    const bool descriptionWritten = writeRegistryString(key, nullptr, marker);
    const bool protocolWritten = writeRegistryString(key, L"URL Protocol", {});
    RegCloseKey(key);
    if (!descriptionWritten || !protocolWritten) {
        if (error) *error = QStringLiteral("Windows could not finish the BestDM protocol registration.");
        return false;
    }
    if (error) error->clear();
    return true;
#else
    Q_UNUSED(enabled)
    if (error) *error = QStringLiteral("Custom URL protocol registration is only available on Windows.");
    return false;
#endif
}
