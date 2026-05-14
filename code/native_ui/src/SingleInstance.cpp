#include "SingleInstance.h"

#include "MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QLockFile>
#include <QLocalSocket>
#include <QObject>
#include <QStandardPaths>

#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aseapp {
namespace {
QString serverName() {
    return QStringLiteral("ASEappSurfaceBuilder.SingleInstance");
}

QString activatePayload() {
    return QStringLiteral("__activate__");
}

QStringList payloadForRequestedPaths(const QStringList& requestedPaths) {
    if (!requestedPaths.empty()) {
        return requestedPaths;
    }
    return {activatePayload()};
}
}  // namespace

QStringList structurePathsFromArguments(const QStringList& arguments) {
    QStringList requestedPaths;
    for (int i = 1; i < arguments.size(); ++i) {
        if (!arguments[i].startsWith(QStringLiteral("--"))) {
            requestedPaths << QFileInfo(arguments[i]).absoluteFilePath();
        }
    }
    return requestedPaths;
}

SingleInstanceGuard::SingleInstanceGuard(QStringList requestedPaths)
    : m_requestedPaths(std::move(requestedPaths)) {
#ifdef Q_OS_WIN
    m_singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ASEappSurfaceBuilder.SingleInstance.Mutex");
    if (m_singleInstanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        sendToExistingInstance();
        activateExistingWindow();
        m_shouldContinue = false;
        return;
    }
#else
    const QString lockDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_singleInstanceLock = std::make_unique<QLockFile>(QDir(lockDir).filePath(QStringLiteral("ASEappSurfaceBuilder.lock")));
    if (!m_singleInstanceLock->tryLock(100)) {
        sendToExistingInstance();
        m_shouldContinue = false;
        return;
    }
#endif

    startLocalServer();
}

SingleInstanceGuard::~SingleInstanceGuard() {
#ifdef Q_OS_WIN
    if (m_singleInstanceMutex != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_singleInstanceMutex));
    }
#endif
}

bool SingleInstanceGuard::shouldContinue() const {
    return m_shouldContinue;
}

void SingleInstanceGuard::attachTo(MainWindow& window) {
    if (!m_serverEnabled) {
        return;
    }

    QObject::connect(&m_server, &QLocalServer::newConnection, &window, [this, &window]() {
        while (QLocalSocket* client = m_server.nextPendingConnection()) {
            auto processPayload = [client, &window]() {
                const QString payload = QString::fromUtf8(client->readAll());
                const QStringList paths = payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& path : paths) {
                    if (path != activatePayload()) {
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

bool SingleInstanceGuard::sendToExistingInstance() const {
    QLocalSocket existingInstance;
    existingInstance.connectToServer(serverName(), QIODevice::ReadWrite);
    if (!existingInstance.waitForConnected(1500)) {
        return false;
    }

    const QStringList payload = payloadForRequestedPaths(m_requestedPaths);
    existingInstance.write(payload.join(QLatin1Char('\n')).toUtf8());
    existingInstance.flush();
    existingInstance.waitForBytesWritten(1000);
    return true;
}

void SingleInstanceGuard::activateExistingWindow() const {
#ifdef Q_OS_WIN
    if (HWND existingWindow = FindWindowW(nullptr, L"ASEapp Surface Builder")) {
        ShowWindow(existingWindow, SW_RESTORE);
        SetForegroundWindow(existingWindow);
    }
#endif
}

void SingleInstanceGuard::startLocalServer() {
    m_serverEnabled = m_server.listen(serverName());
    if (m_serverEnabled) {
        return;
    }

    if (sendToExistingInstance()) {
        m_shouldContinue = false;
        return;
    }

    QLocalServer::removeServer(serverName());
    m_serverEnabled = m_server.listen(serverName());
}

}  // namespace aseapp
