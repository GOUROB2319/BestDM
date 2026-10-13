#include "securecredentialstore.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace {
QString normalizedProxyType(const QString &type) { return type.trimmed().toUpper(); }
}

QString SecureCredentialStore::proxyTarget(const QString &type, const QString &host, int port) {
    return QStringLiteral("BestDM.Proxy.%1.%2:%3")
        .arg(normalizedProxyType(type), host.trimmed().toLower()).arg(port);
}

bool SecureCredentialStore::isAvailable() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool SecureCredentialStore::saveProxyPassword(const QString &target, const QString &username,
                                              const QString &password, QString *error) {
#ifdef Q_OS_WIN
    if (target.isEmpty() || username.isEmpty() || password.isEmpty()) {
        if (error) *error = QStringLiteral("Enter both a proxy username and password.");
        return false;
    }
    const auto passwordBytes = password.toUtf16();
    if (passwordBytes.size() * qsizetype(sizeof(ushort)) > qsizetype(CRED_MAX_CREDENTIAL_BLOB_SIZE)) {
        if (error) *error = QStringLiteral("The proxy password is too long to store securely.");
        return false;
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(username.utf16()));
    credential.CredentialBlobSize = DWORD(passwordBytes.size() * sizeof(ushort));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ushort *>(passwordBytes.constData()));
    if (!CredWriteW(&credential, 0)) {
        if (error) *error = QStringLiteral("Windows Credential Manager could not store the proxy password.");
        return false;
    }
    if (error) error->clear();
    return true;
#else
    Q_UNUSED(username);
    Q_UNUSED(password);
    Q_UNUSED(target);
    if (error) *error = QStringLiteral("Secure proxy password storage is unavailable on this platform.");
    return false;
#endif
}

bool SecureCredentialStore::loadProxyPassword(const QString &target, QString *username,
                                              QString *password, QString *error) {
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        if (error) error->clear();
        return false;
    }
    if (username) *username = QString::fromWCharArray(credential->UserName ? credential->UserName : L"");
    if (password) {
        const auto *data = reinterpret_cast<const ushort *>(credential->CredentialBlob);
        *password = QString::fromUtf16(data, int(credential->CredentialBlobSize / sizeof(ushort)));
    }
    CredFree(credential);
    if (error) error->clear();
    return true;
#else
    Q_UNUSED(username);
    Q_UNUSED(password);
    Q_UNUSED(target);
    if (error) *error = QStringLiteral("Secure proxy password storage is unavailable on this platform.");
    return false;
#endif
}

bool SecureCredentialStore::clearProxyPassword(const QString &target, QString *error) {
#ifdef Q_OS_WIN
    if (!CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)) {
        if (GetLastError() == ERROR_NOT_FOUND) {
            if (error) error->clear();
            return true;
        }
        if (error) *error = QStringLiteral("Windows Credential Manager could not remove the saved proxy password.");
        return false;
    }
    if (error) error->clear();
    return true;
#else
    Q_UNUSED(target);
    if (error) *error = QStringLiteral("Secure proxy password storage is unavailable on this platform.");
    return false;
#endif
}
