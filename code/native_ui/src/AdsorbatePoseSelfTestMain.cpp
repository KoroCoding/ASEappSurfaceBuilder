#include <QApplication>
#include <QDir>
#include <QString>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("ASEapp");
    app.setApplicationName("ASEapp Adsorbate Pose Self Test");

    QString outputDirectory = QDir::temp().filePath(QStringLiteral("aseapp_adsorbate_pose_self_test"));
    for (int i = 1; i + 1 < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QStringLiteral("--test-output")) {
            outputDirectory = QString::fromLocal8Bit(argv[i + 1]);
            break;
        }
    }

    QDir().mkpath(outputDirectory);
    MainWindow window;
    QString errorMessage;
    return window.runAdsorbatePoseSelfTest(outputDirectory, &errorMessage) ? 0 : 2;
}
