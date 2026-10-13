#include "downloadmanager.h"
#include "downloadlistproxy.h"
#include "diagnosticlogger.h"
#include "singleinstancecoordinator.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QIcon>
#include <QStandardPaths>
#include <QUrl>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("BestDM");
    app.setApplicationName("BestDM");
    app.setApplicationDisplayName("BestDM");
    app.setWindowIcon(QIcon(":/assets/Glossy 3D Download App Icon.png"));
    const QString localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    DiagnosticLogger diagnostics(localData.isEmpty() ? QString{} : QDir(localData).filePath(QStringLiteral("logs")));
    diagnostics.install();
    SingleInstanceCoordinator instance(app.arguments().mid(1));
    if (instance.isForwarded()) return 0;
    if (instance.hasError()) return -1;
    DownloadManager downloads;
    QObject::connect(&instance, &SingleInstanceCoordinator::activationRequested,
                     &downloads, &DownloadManager::activateMainWindow);
    QObject::connect(&instance, &SingleInstanceCoordinator::downloadUrlReceived,
                     &downloads, [&downloads](const QString &url) {
        downloads.handleProtocolUrl(QStringLiteral("bestdm://add?url=")
            + QString::fromLatin1(QUrl::toPercentEncoding(url)));
    });
    DownloadListProxy downloadList;
    downloadList.setSourceModel(&downloads);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("downloadManager", &downloads);
    engine.rootContext()->setContextProperty("downloadProxy", &downloadList);
    engine.rootContext()->setContextProperty("diagnosticLogger", &diagnostics);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BestDM/App/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;
    instance.dispatchInitial();
    return app.exec();
}
