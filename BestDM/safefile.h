#pragma once

#include <QFile>
#include <QFileInfo>
#include <cstdint>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// Opens the final path component without following a symbolic link or reparse
// point. Parent-directory links are left to the user's selected destination.
inline bool openFileNoFollow(QFile &file, const QString &path, QIODevice::OpenMode mode) {
    file.close();
    file.setFileName(path);
    const bool readOnly = mode.testFlag(QIODevice::ReadOnly);
    const bool append = mode.testFlag(QIODevice::Append);
    const bool truncate = mode.testFlag(QIODevice::Truncate);

#ifdef Q_OS_WIN
    const DWORD access = readOnly ? GENERIC_READ : GENERIC_WRITE;
    const DWORD creation = readOnly ? OPEN_EXISTING : OPEN_ALWAYS;
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    BY_HANDLE_FILE_INFORMATION info{};
    const bool validFile = GetFileInformationByHandle(handle, &info)
        && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    if (!validFile) {
        CloseHandle(handle);
        return false;
    }
    if (truncate) {
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN) || !SetEndOfFile(handle)) {
            CloseHandle(handle);
            return false;
        }
    }

    const int descriptorFlags = _O_BINARY | (readOnly ? _O_RDONLY : _O_WRONLY) | (append ? _O_APPEND : 0);
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), descriptorFlags);
    if (descriptor < 0) {
        CloseHandle(handle);
        return false;
    }
    const QIODevice::OpenMode deviceMode = readOnly ? QIODevice::ReadOnly
        : (append ? QIODevice::WriteOnly | QIODevice::Append : QIODevice::WriteOnly);
    if (!file.open(descriptor, deviceMode, QFileDevice::AutoCloseHandle)) {
        _close(descriptor);
        return false;
    }
    return true;
#else
#ifndef O_NOFOLLOW
    Q_UNUSED(readOnly);
    Q_UNUSED(append);
    Q_UNUSED(truncate);
    return false;
#else
    int flags = O_NOFOLLOW;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    if (readOnly) {
        flags |= O_RDONLY;
    } else {
        flags |= O_WRONLY | O_CREAT;
        if (append) flags |= O_APPEND;
    }
    const QByteArray encodedPath = QFile::encodeName(path);
    const int descriptor = ::open(encodedPath.constData(), flags, 0600);
    if (descriptor < 0) return false;

    struct stat info{};
    if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)
        || (truncate && ::ftruncate(descriptor, 0) != 0)) {
        ::close(descriptor);
        return false;
    }
    const QIODevice::OpenMode deviceMode = readOnly ? QIODevice::ReadOnly
        : (append ? QIODevice::WriteOnly | QIODevice::Append : QIODevice::WriteOnly);
    if (!file.open(descriptor, deviceMode, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return false;
    }
    return true;
#endif
#endif
}
