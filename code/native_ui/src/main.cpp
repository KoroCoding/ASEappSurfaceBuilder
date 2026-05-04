#include <QApplication>
#include <QIcon>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("ASEapp");
    app.setApplicationName("ASEapp Surface Builder");
    app.setWindowIcon(QIcon(":/icons/aseapp_surface_builder_icon.png"));

    MainWindow window;
    if (argc > 1) {
        const QString path = QString::fromLocal8Bit(argv[1]);
        if (!path.startsWith("--")) {
            window.loadStructureFile(path);
        }
    }
    window.show();
    return app.exec();
}
