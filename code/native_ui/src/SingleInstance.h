#pragma once

#include <QLocalServer>
#include <QStringList>

#include <memory>

class QLockFile;
class MainWindow;

namespace aseapp {

QStringList structurePathsFromArguments(const QStringList& arguments);

class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(QStringList requestedPaths);
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    bool shouldContinue() const;
    void attachTo(MainWindow& window);

private:
    bool sendToExistingInstance() const;
    void activateExistingWindow() const;
    void startLocalServer();

    QStringList m_requestedPaths;
    QLocalServer m_server;
    bool m_shouldContinue = true;
    bool m_serverEnabled = false;
#ifndef Q_OS_WIN
    std::unique_ptr<QLockFile> m_singleInstanceLock;
#endif
#ifdef Q_OS_WIN
    void* m_singleInstanceMutex = nullptr;
#endif
};

}  // namespace aseapp
