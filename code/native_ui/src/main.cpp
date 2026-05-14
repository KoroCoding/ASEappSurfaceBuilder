#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QStringList>

#include "AppStartup.h"
#include "MainWindow.h"
#include "SingleInstance.h"

int main(int argc, char* argv[]) {
    const QString qtPluginPath = aseapp::configureQtPluginPath(argc, argv);
    QApplication app(argc, argv);
    if (!qtPluginPath.isEmpty()) {
        QCoreApplication::addLibraryPath(qtPluginPath);
    }
    app.setOrganizationName("ASEapp");
    app.setApplicationName("ASEapp Surface Builder");
    app.setWindowIcon(QIcon(":/icons/aseapp_surface_builder_icon.png"));

    const QStringList requestedPaths = aseapp::structurePathsFromArguments(app.arguments());
    aseapp::SingleInstanceGuard singleInstance(requestedPaths);
    if (!singleInstance.shouldContinue()) {
        return 0;
    }

    MainWindow window;
    singleInstance.attachTo(window);

    for (const QString& path : requestedPaths) {
        window.loadStructureFile(path);
    }

    window.show();
    return app.exec();
}
