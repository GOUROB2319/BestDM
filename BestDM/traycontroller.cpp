#include "traycontroller.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWindow>

TrayController::TrayController(QObject *parent) : QObject(parent) {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_icon = new QSystemTrayIcon(QIcon(QStringLiteral(":/assets/Glossy 3D Download App Icon.png")), this);
    m_icon->setToolTip(QStringLiteral("BestDM"));
    auto *menu = new QMenu;
    menu->QObject::setParent(this);
    auto *showAction = menu->addAction(QStringLiteral("Open BestDM"));
    auto *quitAction = menu->addAction(QStringLiteral("Quit"));
    connect(showAction, &QAction::triggered, this, &TrayController::showWindow);
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    m_icon->setContextMenu(menu);
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showWindow();
    });
    m_icon->show();
}

void TrayController::setMainWindow(QWindow *window) {
    m_window = window;
}

void TrayController::showWindow() {
    if (!m_window) return;
    m_window->show();
    m_window->showNormal();
    m_window->raise();
    m_window->requestActivate();
}

void TrayController::showNotification(const QString &title, const QString &message) {
    if (m_icon && QSystemTrayIcon::supportsMessages())
        m_icon->showMessage(title, message, QSystemTrayIcon::Information, 7000);
}
