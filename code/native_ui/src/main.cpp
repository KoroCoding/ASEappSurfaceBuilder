#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSplashScreen>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

#include "AppStartup.h"
#include "MainWindow.h"
#include "SingleInstance.h"

namespace {
#ifdef Q_OS_MACOS
QPixmap startupSplashPixmap() {
    QPixmap pixmap(520, 220);
    pixmap.fill(QColor("#F7F8FB"));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#1F5E8C"));
    painter.drawRoundedRect(QRectF(28, 34, 62, 62), 14, 14);

    painter.setPen(QColor("#FFFFFF"));
    QFont markFont = painter.font();
    markFont.setBold(true);
    markFont.setPointSize(23);
    painter.setFont(markFont);
    painter.drawText(QRectF(28, 34, 62, 62), Qt::AlignCenter, QStringLiteral("ASE"));

    painter.setPen(QColor("#182235"));
    QFont titleFont = painter.font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QPointF(112, 62), QStringLiteral("ASEapp Surface Builder"));

    painter.setPen(QColor("#566275"));
    QFont detailFont = painter.font();
    detailFont.setPointSize(13);
    detailFont.setBold(false);
    painter.setFont(detailFont);
    painter.drawText(QPointF(114, 92), QStringLiteral("Preparing the native UI"));

    painter.setPen(QPen(QColor("#D6DAE3"), 1));
    painter.drawLine(QPointF(32, 156), QPointF(488, 156));
    return pixmap;
}

void showSplashMessage(QSplashScreen& splash, const QString& message) {
    splash.showMessage(message, Qt::AlignLeft | Qt::AlignBottom, QColor("#334155"));
    qApp->processEvents();
}
#endif
}  // namespace

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

#ifdef Q_OS_MACOS
    QSplashScreen splash(startupSplashPixmap());
    splash.show();
    showSplashMessage(splash, QStringLiteral("Preparing startup files..."));
#endif

    MainWindow window;
    singleInstance.attachTo(window);

#ifdef Q_OS_MACOS
    showSplashMessage(splash, QStringLiteral("Opening main window..."));
#endif
    window.show();
#ifdef Q_OS_MACOS
    splash.finish(&window);
    window.raise();
    window.activateWindow();
#endif
    if (!requestedPaths.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, requestedPaths]() {
            for (const QString& path : requestedPaths) {
                window.loadStructureFile(path);
            }
        });
    }
    return app.exec();
}
