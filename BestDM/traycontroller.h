#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QSystemTrayIcon;
class QWindow;

class TrayController final : public QObject {
    Q_OBJECT
public:
    explicit TrayController(QObject *parent = nullptr);
    bool available() const { return m_icon != nullptr; }
    void setMainWindow(QWindow *window);
    void showWindow();
    void showNotification(const QString &title, const QString &message);

signals:
    void quitRequested();

private:
    QSystemTrayIcon *m_icon = nullptr;
    QPointer<QWindow> m_window;
};
