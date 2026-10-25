#include "fields.h"
#include "save.h"

#include <codecvt>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::size_t kTestSaveSize = 34028 + 8 * 112;

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::wstring ToWideUtf8(const std::string& utf8) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(utf8);
}

bool WriteBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "re6-save-editor-linux-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();
    if (!fs::create_directories(dir, ec) || ec) {
        std::cerr << "FAIL: create temporary test directory\n";
        return 1;
    }

    // Use a non-ASCII filename to exercise the Linux UTF-8 path conversion.
    const std::string filenameUtf8 = "savedata-测试.bin";
    const fs::path sourcePath = dir / fs::u8path(filenameUtf8);
    const std::wstring sourceWide = ToWideUtf8(sourcePath.string());

    std::vector<uint8_t> original(kTestSaveSize, 0);
    const uint32_t declaredSize = static_cast<uint32_t>(original.size());
    std::memcpy(original.data() + 4, &declaredSize, sizeof(declaredSize));
    if (!Check(WriteBytes(sourcePath, original), "write synthetic save file")) return 1;

    SaveFile save;
    std::wstring error;
    if (!Check(save.Load(sourceWide, error), "load correctly sized synthetic PC save")) {
        std::wcerr << L"Load error: " << error << L'\n';
        return 1;
    }

    Field skillPoints;
    bool foundSkillPoints = false;
    for (const Page& page : BuildPages()) {
        if (page.path != L"General") continue;
        for (const Field& field : page.fields) {
            if (field.name == L"Skill points") {
                skillPoints = field;
                foundSkillPoints = true;
                break;
            }
        }
    }
    if (!Check(foundSkillPoints, "find Skill points field from shared field map")) return 1;
    if (!Check(save.Get(skillPoints) == 0, "read initial Skill points value")) return 1;

    save.Set(skillPoints, 12345);
    if (!Check(save.Get(skillPoints) == 12345, "update Skill points in memory")) return 1;
    if (!Check(save.dirty, "mark save dirty after field update")) return 1;
    if (!Check(save.Save(sourceWide, error), "save edited file")) {
        std::wcerr << L"Save error: " << error << L'\n';
        return 1;
    }

    const fs::path backupPath = fs::path(sourcePath.string() + ".bak");
    if (!Check(fs::exists(backupPath), "create .bak backup beside existing save")) return 1;
    if (!Check(ReadBytes(backupPath) == original, "backup exactly preserves original bytes")) return 1;

    const std::vector<uint8_t> editedBytes = ReadBytes(sourcePath);
    if (!Check(editedBytes.size() == kTestSaveSize, "preserve save file size")) return 1;
    uint32_t rawWrittenSkillPoints = 0;
    std::memcpy(&rawWrittenSkillPoints, editedBytes.data() + skillPoints.offset,
                sizeof(rawWrittenSkillPoints));
    if (!Check(rawWrittenSkillPoints == 12345, "persist field bytes to disk")) return 1;

    SaveFile reloaded;
    if (!Check(reloaded.Load(sourceWide, error), "reload edited save")) {
        std::wcerr << L"Reload error: " << error << L'\n';
        return 1;
    }
    if (!Check(reloaded.Get(skillPoints) == 12345, "persist edited Skill points value")) return 1;
    if (!Check(reloaded.StoredChecksum() == reloaded.ComputeChecksum(),
               "stored checksum matches recomputed checksum")) return 1;
    if (!Check(!reloaded.dirty, "loaded save starts clean")) return 1;

    // Invalid header sizes must be rejected without accepting malformed data.
    const fs::path malformedPath = dir / "malformed.bin";
    std::vector<uint8_t> malformed = original;
    const uint32_t wrongSize = 16;
    std::memcpy(malformed.data() + 4, &wrongSize, sizeof(wrongSize));
    if (!Check(WriteBytes(malformedPath, malformed), "write malformed save fixture")) return 1;
    SaveFile invalid;
    if (!Check(!invalid.Load(ToWideUtf8(malformedPath.string()), error),
               "reject mismatched declared save size")) return 1;

    fs::remove_all(dir, ec);
    std::cout << "Linux save round-trip, UTF-8 path, backup, checksum and validation tests passed.\n";
    return 0;
}
