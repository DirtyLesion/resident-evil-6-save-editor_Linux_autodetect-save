#include "steam_save_discovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool createSave(const QString& path) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write("synthetic test data", 19) == 19;
}

QString savePath(const QString& root, const QString& account) {
    return QDir(root).filePath(QStringLiteral("userdata/") + account +
                               QStringLiteral("/221040/remote/savedata.bin"));
}
}

int main() {
    QTemporaryDir temp;
    check(temp.isValid(), "create a temporary test home");
    if (!temp.isValid()) return 1;

    const QString home = temp.path();
    const QString standardSteam = QDir(home).filePath(QStringLiteral(".local/share/Steam"));
    const QString standardSave = savePath(standardSteam, QStringLiteral("123456789"));
    check(createSave(standardSave), "create synthetic save in the standard Steam root");

    QStringList candidates = FindResidentEvil6SaveCandidates(home);
    check(candidates.size() == 1, "detect one save in the standard Steam root");
    if (!candidates.isEmpty())
        check(candidates.front() == QFileInfo(standardSave).canonicalFilePath(),
              "return the full detected save path");

    // A Steam installation with a custom root can be supplied by STEAM_DIR.
    const QString customSteam = QDir(home).filePath(QStringLiteral("custom-steam"));
    const QString customSave = savePath(customSteam, QStringLiteral("987654321"));
    check(createSave(customSave), "create synthetic save in a configured Steam root");
    candidates = FindResidentEvil6SaveCandidates(home, QString(), customSteam);
    check(candidates.contains(QFileInfo(standardSave).canonicalFilePath()),
          "retain the standard Steam installation candidate");
    check(candidates.contains(QFileInfo(customSave).canonicalFilePath()),
          "find saves in a custom STEAM_DIR root");

    // Flatpak Steam uses its own per-user data directory.
    const QString flatpakSteam = QDir(home).filePath(
        QStringLiteral(".var/app/com.valvesoftware.Steam/.local/share/Steam"));
    const QString flatpakSave = savePath(flatpakSteam, QStringLiteral("222333444"));
    check(createSave(flatpakSave), "create synthetic Flatpak Steam save");
    candidates = FindResidentEvil6SaveCandidates(home);
    check(candidates.contains(QFileInfo(flatpakSave).canonicalFilePath()),
          "find a save under the Flatpak Steam root");

    // XDG_DATA_HOME and alternate traditional Steam roots should also be recognized.
    const QString xdgData = QDir(home).filePath(QStringLiteral("alternate-data"));
    const QString xdgSteam = QDir(xdgData).filePath(QStringLiteral("Steam"));
    const QString xdgSave = savePath(xdgSteam, QStringLiteral("333444555"));
    check(createSave(xdgSave), "create synthetic save under XDG_DATA_HOME");
    candidates = FindResidentEvil6SaveCandidates(home, xdgData);
    check(candidates.contains(QFileInfo(xdgSave).canonicalFilePath()),
          "find a save under XDG_DATA_HOME");

    const QString debianSteam = QDir(home).filePath(QStringLiteral(".steam/debian-installation"));
    const QString debianSave = savePath(debianSteam, QStringLiteral("444555666"));
    check(createSave(debianSave), "create synthetic save under alternate Steam root");
    candidates = FindResidentEvil6SaveCandidates(home);
    check(candidates.contains(QFileInfo(debianSave).canonicalFilePath()),
          "find a save under .steam/debian-installation");

    // The detector only considers RE6's Steam remote-save path.
    const QString unrelated = QDir(home).filePath(QStringLiteral("Documents/savedata.bin"));
    check(createSave(unrelated), "create unrelated savedata.bin");
    candidates = FindResidentEvil6SaveCandidates(home);
    check(!candidates.contains(QFileInfo(unrelated).canonicalFilePath()),
          "ignore savedata.bin files outside Steam's RE6 remote directory");

    QTemporaryDir emptyHome;
    candidates = FindResidentEvil6SaveCandidates(emptyHome.path());
    check(candidates.isEmpty(), "return no candidates when no Steam save is present");

    if (failures == 0)
        std::cout << "Steam savedata auto-detection tests passed.\n";
    else
        std::cerr << failures << " auto-detection test(s) failed.\n";
    return failures == 0 ? 0 : 1;
}
