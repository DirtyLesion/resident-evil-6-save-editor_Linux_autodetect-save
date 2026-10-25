#include "steam_save_discovery.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QVector>

#include <algorithm>

namespace {
QString comparablePath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QStringList steamRoots(const QString& homePath,
                       const QString& xdgDataHome,
                       const QString& configuredSteamDir) {
    const QDir home(homePath);
    QStringList roots = {
        home.filePath(QStringLiteral(".local/share/Steam")),
        home.filePath(QStringLiteral(".steam/steam")),
        home.filePath(QStringLiteral(".steam/root")),
        home.filePath(QStringLiteral(".steam/debian-installation")),
        home.filePath(QStringLiteral(".var/app/com.valvesoftware.Steam/.local/share/Steam")),
        home.filePath(QStringLiteral(".var/app/com.valvesoftware.Steam/.steam/steam"))
    };

    if (!xdgDataHome.trimmed().isEmpty())
        roots.prepend(QDir(xdgDataHome).filePath(QStringLiteral("Steam")));
    if (!configuredSteamDir.trimmed().isEmpty())
        roots.prepend(configuredSteamDir);

    QStringList uniqueRoots;
    QSet<QString> seen;
    for (const QString& root : roots) {
        const QString key = comparablePath(root);
        if (seen.contains(key)) continue;
        seen.insert(key);
        uniqueRoots.push_back(root);
    }
    return uniqueRoots;
}

struct Candidate {
    QString path;
    QDateTime modified;
};
}

QStringList FindResidentEvil6SaveCandidates(const QString& homePath,
                                           const QString& xdgDataHome,
                                           const QString& steamDir) {
    QVector<Candidate> found;
    QSet<QString> seenFiles;

    for (const QString& root : steamRoots(homePath, xdgDataHome, steamDir)) {
        const QDir userdata(QDir(root).filePath(QStringLiteral("userdata")));
        if (!userdata.exists()) continue;

        const QFileInfoList accounts = userdata.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& account : accounts) {
            const QString candidatePath = QDir(account.absoluteFilePath()).filePath(
                QStringLiteral("221040/remote/savedata.bin"));
            const QFileInfo fileInfo(candidatePath);
            if (!fileInfo.isFile() || !fileInfo.isReadable()) continue;

            const QString canonical = comparablePath(candidatePath);
            if (seenFiles.contains(canonical)) continue;
            seenFiles.insert(canonical);
            found.push_back({canonical, fileInfo.lastModified()});
        }
    }

    std::sort(found.begin(), found.end(), [](const Candidate& left, const Candidate& right) {
        if (left.modified != right.modified) return left.modified > right.modified;
        return left.path < right.path;
    });

    QStringList paths;
    paths.reserve(static_cast<qsizetype>(found.size()));
    for (const Candidate& candidate : found) paths.push_back(candidate.path);
    return paths;
}
