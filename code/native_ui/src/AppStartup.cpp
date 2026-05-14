#include "AppStartup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>

namespace aseapp {

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

}  // namespace aseapp
