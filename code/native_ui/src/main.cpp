#include <QApplication>
#include <QDir>
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

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ASEappSurfaceBuilder.SingleInstance.Mutex");
    if (singleInstanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existingWindow = FindWindowW(nullptr, L"ASEapp Surface Builder")) {
            ShowWindow(existingWindow, SW_RESTORE);
            SetForegroundWindow(existingWindow);
        }
        CloseHandle(singleInstanceMutex);
        return 0;
    }
#endif

    QApplication app(argc, argv);
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
