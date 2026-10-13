#include "diagnosticlogger.h"

#include "safefile.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QUrl>
#include <QtLogging>
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace {
QMutex handlerMutex;
DiagnosticLogger *installedLogger = nullptr;
QtMessageHandler previousHandler = nullptr;

QString levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARNING");
    case QtCriticalMsg: return QStringLiteral("CRITICAL");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

bool pathEntryExists(const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}
}

DiagnosticLogger::DiagnosticLogger(QString directory, qint64 maximumFileBytes, int backupCount, QObject *parent)
    : QObject(parent), m_directory(directory.isEmpty() ? QString{} : QDir::cleanPath(std::move(directory))),
      m_maximumFileBytes(std::clamp<qint64>(maximumFileBytes, 1024, 16 * 1024 * 1024)),
      m_backupCount(std::clamp(backupCount, 0, 10)) {}

DiagnosticLogger::~DiagnosticLogger() {
    uninstall();
}

bool DiagnosticLogger::install() {
    QMutexLocker lock(&handlerMutex);
    if (installedLogger == this) return true;
    if (installedLogger || m_directory.isEmpty() || !QDir().mkpath(m_directory)) return false;
    installedLogger = this;
    previousHandler = qInstallMessageHandler(&DiagnosticLogger::messageHandler);
    return true;
}

void DiagnosticLogger::uninstall() {
    QMutexLocker lock(&handlerMutex);
    if (installedLogger != this) return;
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    installedLogger = nullptr;
}

QString DiagnosticLogger::activeLogFilePath() const {
    return QDir(m_directory).filePath(QStringLiteral("bestdm.log"));
}

bool DiagnosticLogger::exportToFile(const QUrl &destination) {
    const auto setStatus = [this](const QString &status) {
        if (m_exportStatus == status) return;
        m_exportStatus = status;
        emit exportStatusChanged();
    };
    if (!destination.isLocalFile()) {
        setStatus(QStringLiteral("Choose a local .txt file for the diagnostic export."));
        return false;
    }
    const QString outputPath = QDir::cleanPath(QFileInfo(destination.toLocalFile()).absoluteFilePath());
    if (QFileInfo(outputPath).suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0) {
        setStatus(QStringLiteral("Diagnostic exports must use a .txt file."));
        return false;
    }

    QStringList logPaths;
    for (int backup = m_backupCount; backup >= 1; --backup)
        logPaths.append(activeLogFilePath() + QStringLiteral(".%1").arg(backup));
    logPaths.append(activeLogFilePath());
    for (const QString &logPath : std::as_const(logPaths)) {
        const QString sourcePath = QDir::cleanPath(QFileInfo(logPath).absoluteFilePath());
#ifdef Q_OS_WIN
        const Qt::CaseSensitivity pathSensitivity = Qt::CaseInsensitive;
#else
        const Qt::CaseSensitivity pathSensitivity = Qt::CaseSensitive;
#endif
        if (sourcePath.compare(outputPath, pathSensitivity) == 0) {
            setStatus(QStringLiteral("Choose a different destination from the active diagnostic logs."));
            return false;
        }
    }

    QByteArray exportData;
    qint64 totalBytes = 0;
    bool hasLogData = false;
    QString readError;
    {
        QMutexLocker lock(&m_mutex);
        for (const QString &logPath : std::as_const(logPaths)) {
            const QFileInfo logInfo(logPath);
            if (!logInfo.exists() && !logInfo.isSymLink()) continue;
            QFile input;
            if (!openFileNoFollow(input, logPath, QIODevice::ReadOnly)
                || input.size() > m_maximumFileBytes
                || totalBytes > (m_maximumFileBytes * (m_backupCount + 1)) - input.size()) {
                readError = QStringLiteral("A diagnostic log could not be safely read for export.");
                break;
            }
            const qint64 fileSize = input.size();
            const QByteArray contents = input.readAll();
            if (input.error() != QFileDevice::NoError || contents.size() != fileSize) {
                readError = QStringLiteral("A diagnostic log could not be completely read for export.");
                break;
            }
            totalBytes += contents.size();
            if (contents.isEmpty()) continue;
            const QByteArray safeContents = redact(QString::fromUtf8(contents)).toUtf8();
            exportData.append(safeContents);
            hasLogData = true;
        }
    }
    if (!readError.isEmpty()) {
        setStatus(readError);
        return false;
    }
    if (!hasLogData) {
        setStatus(QStringLiteral("There are no diagnostic logs to export yet."));
        return false;
    }
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("Could not save the complete diagnostic export."));
        return false;
    }
    if (output.write(exportData) != exportData.size() || !output.commit()) {
        output.cancelWriting();
        setStatus(QStringLiteral("Could not save the complete diagnostic export."));
        return false;
    }
    setStatus(QStringLiteral("Exported redacted diagnostic logs only."));
    return true;
}

QString DiagnosticLogger::redact(const QString &message) {
    static const QRegularExpression urlPattern(
        QStringLiteral(R"((https?://[^\s<>"']+))"), QRegularExpression::CaseInsensitiveOption);
    QString result;
    result.reserve(message.size());
    qsizetype copiedUntil = 0;
    auto matches = urlPattern.globalMatch(message);
    while (matches.hasNext()) {
        const auto match = matches.next();
        QString candidate = match.captured(1);
        qsizetype punctuationLength = 0;
        while (!candidate.isEmpty() && QStringLiteral(".,;:!)]}").contains(candidate.back())) {
            candidate.chop(1);
            ++punctuationLength;
        }
        result += message.mid(copiedUntil, match.capturedStart() - copiedUntil);
        const QUrl url(candidate, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty()) {
            result += QStringLiteral("[url-redacted]");
        } else {
            QUrl safeUrl = url;
            safeUrl.setUserInfo({});
            safeUrl.setQuery(url.hasQuery() ? QStringLiteral("[redacted]") : QString{});
            safeUrl.setFragment({});
            result += safeUrl.toString(QUrl::FullyEncoded);
        }
        if (punctuationLength > 0) result += match.captured(1).right(punctuationLength);
        copiedUntil = match.capturedEnd();
    }
    result += message.mid(copiedUntil);

    static const QRegularExpression credentialPattern(
        QStringLiteral(R"((?im)\b(?:authorization|proxy-authorization|cookie|set-cookie|password|passwd|credential|credentials|access[_-]?token|refresh[_-]?token|token|api[_-]?key|secret)\b\s*[:=]\s*)([^\r\n]*)"));
    result.replace(credentialPattern, QStringLiteral("\\1[redacted]"));
    static const QRegularExpression authorizationValue(
        QStringLiteral(R"((?i)\b(Bearer|Basic)\s+[A-Za-z0-9._~+/-]+=*)"));
    result.replace(authorizationValue, QStringLiteral("\\1 [redacted]"));
    const QString home = QDir::cleanPath(QDir::homePath());
    if (!home.isEmpty() && home != QStringLiteral("/") && home != QStringLiteral("\\")) {
        const auto options =
#ifdef Q_OS_WIN
            QRegularExpression::CaseInsensitiveOption;
#else
            QRegularExpression::NoPatternOption;
#endif
        const QRegularExpression homePathPattern(QRegularExpression::escape(home), options);
        result.replace(homePathPattern, QStringLiteral("~"));
        const QString alternateSeparators = home.contains(QLatin1Char('/'))
            ? QStringLiteral("\\") : QStringLiteral("/");
        QString alternateHome = home;
        alternateHome.replace(home.contains(QLatin1Char('/')) ? QLatin1Char('/') : QLatin1Char('\\'),
                              alternateSeparators.at(0));
        result.replace(QRegularExpression(QRegularExpression::escape(alternateHome), options), QStringLiteral("~"));
    }
    return result;
}

bool DiagnosticLogger::rotateIfNeeded(qint64 incomingBytes) {
    const QString active = activeLogFilePath();
    const QFileInfo activeInfo(active);
    if (activeInfo.size() + incomingBytes <= m_maximumFileBytes) return true;
    if (m_backupCount == 0) {
        QFile file;
        return openFileNoFollow(file, active, QIODevice::WriteOnly | QIODevice::Truncate);
    }
    for (int backup = m_backupCount; backup >= 1; --backup) {
        const QString source = backup == 1 ? active : active + QStringLiteral(".%1").arg(backup - 1);
        const QString target = active + QStringLiteral(".%1").arg(backup);
        if (!pathEntryExists(source)) continue;
        if (pathEntryExists(target) && !QFile::remove(target)) return false;
        if (!QFile::rename(source, target)) return false;
    }
    return true;
}

void DiagnosticLogger::writeMessage(QtMsgType type, const QString &category, const QString &message) {
    QMutexLocker lock(&m_mutex);
    QString safeMessage = redact(message);
    QString safeCategory = redact(category);
    static const QRegularExpression lineBreaks(QStringLiteral("[\\r\\n\\t]+"));
    safeMessage.replace(lineBreaks, QStringLiteral(" "));
    safeCategory.replace(lineBreaks, QStringLiteral(" "));
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QByteArray line = QStringLiteral("%1 [%2] [%3] %4\n")
        .arg(timestamp, levelName(type), safeCategory, safeMessage).toUtf8();
    if (line.size() > m_maximumFileBytes) {
        line = line.left(m_maximumFileBytes - 1);
        line.append('\n');
    }
    if (!QDir().mkpath(m_directory) || !rotateIfNeeded(line.size())) return;
    QFile file;
    if (!openFileNoFollow(file, activeLogFilePath(), QIODevice::WriteOnly | QIODevice::Append)) return;
    file.write(line);
    file.flush();
    if (type == QtFatalMsg) std::abort();
}

void DiagnosticLogger::messageHandler(QtMsgType type, const QMessageLogContext &context,
                                      const QString &message) {
    QMutexLocker lock(&handlerMutex);
    if (installedLogger) {
        installedLogger->writeMessage(type, QString::fromUtf8(context.category ? context.category : ""), message);
    } else if (previousHandler) {
        previousHandler(type, context, redact(message));
    }
    if (type == QtFatalMsg) std::abort();
}
