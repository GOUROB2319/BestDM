#include "downloadmanager.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("BestDM");
    app.setApplicationName("BestDM");
    app.setApplicationDisplayName("BestDM");
    app.setWindowIcon(QIcon(":/assets/Glossy 3D Download App Icon.png"));
    DownloadManager downloads;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("downloadManager", &downloads);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/bestdm/main.qml")));
    return engine.rootObjects().isEmpty() ? -1 : app.exec();
}
