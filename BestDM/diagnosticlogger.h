#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

struct QMessageLogContext;

class DiagnosticLogger final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportStatusChanged)
public:
    explicit DiagnosticLogger(QString directory, qint64 maximumFileBytes = 1024 * 1024,
                              int backupCount = 3, QObject *parent = nullptr);
    ~DiagnosticLogger();
    DiagnosticLogger(const DiagnosticLogger &) = delete;
    DiagnosticLogger &operator=(const DiagnosticLogger &) = delete;

    bool install();
    void uninstall();
    void writeMessage(QtMsgType type, const QString &category, const QString &message);
    QString activeLogFilePath() const;
    QString exportStatus() const { return m_exportStatus; }
    Q_INVOKABLE bool exportToFile(const QUrl &destination);
    static QString redact(const QString &message);

signals:
    void exportStatusChanged();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message);
    bool rotateIfNeeded(qint64 incomingBytes);

    QString m_directory;
    qint64 m_maximumFileBytes = 1024 * 1024;
    int m_backupCount = 3;
    mutable QMutex m_mutex;
    QString m_exportStatus;
};
