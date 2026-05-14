#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QIODevice>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QStringList>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "MainWindow.h"

namespace {
QString configureQtPluginPath(int argc, char* argv[]) {
    QString executableDir;
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        QFileInfo executableInfo(QString::fromLocal8Bit(argv[0]));
        if (executableInfo.isRelative()) {
            executableInfo = QFileInfo(QDir::current(), executableInfo.filePath());
        }
        executableDir = executableInfo.absolutePath();
    }

    QStringList candidateRoots;
    if (!executableDir.isEmpty()) {
        const QDir dir(executableDir);
        candidateRoots << dir.filePath(QStringLiteral("plugins"));
        candidateRoots << dir.filePath(QStringLiteral("../plugins"));
    }
#ifdef Q_OS_WIN
    const QString condaPrefix = qEnvironmentVariable("CONDA_PREFIX");
    if (!condaPrefix.isEmpty()) {
        candidateRoots << QDir(condaPrefix).filePath(QStringLiteral("Library/lib/qt6/plugins"));
    }
#endif

    for (const QString& root : candidateRoots) {
        const QDir pluginDir(root);
        const QString platformDll = pluginDir.filePath(QStringLiteral("platforms/qwindows.dll"));
        if (!QFileInfo::exists(platformDll)) {
            continue;
        }
        const QString canonicalRoot = QFileInfo(root).absoluteFilePath();
        const QString platformRoot = QDir(canonicalRoot).filePath(QStringLiteral("platforms"));
        qputenv("QT_PLUGIN_PATH", QFile::encodeName(QDir::toNativeSeparators(canonicalRoot)));
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QFile::encodeName(QDir::toNativeSeparators(platformRoot)));
        return canonicalRoot;
    }
    return {};
}
}  // namespace

int main(int argc, char* argv[]) {
    const QString qtPluginPath = configureQtPluginPath(argc, argv);
    QApplication app(argc, argv);
    if (!qtPluginPath.isEmpty()) {
        QCoreApplication::addLibraryPath(qtPluginPath);
    }
    app.setOrganizationName("ASEapp");
    app.setApplicationName("ASEapp Surface Builder");
    app.setWindowIcon(QIcon(":/icons/aseapp_surface_builder_icon.png"));

    QStringList requestedPaths;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (!args[i].startsWith("--")) {
            requestedPaths << QFileInfo(args[i]).absoluteFilePath();
        }
    }

    const QString singleInstanceServerName = QStringLiteral("ASEappSurfaceBuilder.SingleInstance");
    auto sendToExistingInstance = [&singleInstanceServerName, &requestedPaths]() {
        QLocalSocket existingInstance;
        existingInstance.connectToServer(singleInstanceServerName, QIODevice::ReadWrite);
        if (!existingInstance.waitForConnected(1500)) {
            return false;
        }
        QStringList payload = requestedPaths;
        if (payload.empty()) {
            payload << QStringLiteral("__activate__");
        }
        existingInstance.write(payload.join(QLatin1Char('\n')).toUtf8());
        existingInstance.flush();
        existingInstance.waitForBytesWritten(1000);
        return true;
    };
#ifdef Q_OS_WIN
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ASEappSurfaceBuilder.SingleInstance.Mutex");
    if (singleInstanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        sendToExistingInstance();
        if (HWND existingWindow = FindWindowW(nullptr, L"ASEapp Surface Builder")) {
            ShowWindow(existingWindow, SW_RESTORE);
            SetForegroundWindow(existingWindow);
        }
        CloseHandle(singleInstanceMutex);
        return 0;
    }
#endif
#ifndef Q_OS_WIN
    const QString lockDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QLockFile singleInstanceLock(QDir(lockDir).filePath(QStringLiteral("ASEappSurfaceBuilder.lock")));
    if (!singleInstanceLock.tryLock(100)) {
        sendToExistingInstance();
        return 0;
    }
#endif

    QLocalServer singleInstanceServer;
    bool singleInstanceEnabled = singleInstanceServer.listen(singleInstanceServerName);
    if (!singleInstanceEnabled) {
        if (sendToExistingInstance()) {
            return 0;
        }
        QLocalServer::removeServer(singleInstanceServerName);
        singleInstanceEnabled = singleInstanceServer.listen(singleInstanceServerName);
    }

    MainWindow window;
    if (singleInstanceEnabled) {
        QObject::connect(&singleInstanceServer, &QLocalServer::newConnection, &window, [&singleInstanceServer, &window]() {
            while (QLocalSocket* client = singleInstanceServer.nextPendingConnection()) {
                auto processPayload = [client, &window]() {
                    const QString payload = QString::fromUtf8(client->readAll());
                    const QStringList paths = payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    for (const QString& path : paths) {
                        if (path != QStringLiteral("__activate__")) {
                            window.loadStructureFile(path);
                        }
                    }
                    window.show();
                    window.raise();
                    window.activateWindow();
                };
                QObject::connect(client, &QLocalSocket::readyRead, &window, processPayload);
                QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                if (client->bytesAvailable() > 0) {
                    processPayload();
                }
            }
        });
    }

    for (const QString& path : requestedPaths) {
        if (!path.startsWith("--")) {
            window.loadStructureFile(path);
        }
    }
    window.show();
    const int exitCode = app.exec();
#ifdef Q_OS_WIN
    if (singleInstanceMutex != nullptr) {
        CloseHandle(singleInstanceMutex);
    }
#endif
    return exitCode;
}
