#include "fields.h"
#include "save.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <codecvt>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::size_t kMinimumSaveSize = 34028 + 8 * 112;

bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::wstring ToWideUtf8(const std::string& utf8) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(utf8);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool WriteBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

uint32_t ReadLe32(const std::vector<uint8_t>& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) return 0;
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint32_t FieldWidth(FieldType type) {
    switch (type) {
        case FieldType::U8:
        case FieldType::Flag8:
        case FieldType::Bits8:
            return 1;
        case FieldType::I16:
        case FieldType::Weapon:
            return 2;
        case FieldType::Bits32:
        case FieldType::U32:
        case FieldType::Hex32:
            return 4;
    }
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: RE6RealSaveFileTests <path-to-real-savedata.bin>\n";
        return 2;
    }

    const fs::path fixturePath = fs::absolute(fs::u8path(argv[1]));
    const std::vector<uint8_t> fixtureBytes = ReadBytes(fixturePath);
    if (!Check(!fixtureBytes.empty(), "read the provided savedata.bin fixture")) return 1;
    if (!Check(fixtureBytes.size() >= kMinimumSaveSize, "real save is at least the minimum supported size")) return 1;
    if (!Check(fixtureBytes.size() % 4 == 0, "real save length is word-aligned")) return 1;
    if (!Check(ReadLe32(fixtureBytes, 4) == fixtureBytes.size(),
               "real save header declares the actual file size")) return 1;

    // Open the provided fixture read-only. All writes below target a temporary copy.
    const std::wstring fixtureWide = ToWideUtf8(fixturePath.string());
    SaveFile inspected;
    std::wstring error;
    if (!Check(inspected.Load(fixtureWide, error), "load the real save with the Linux parser")) {
        std::wcerr << L"Load error: " << error << L'\n';
        return 1;
    }
    if (!Check(inspected.StoredChecksum() == inspected.ComputeChecksum(),
               "real save checksum is valid before editing")) return 1;

    const std::vector<Page> pages = BuildPages();
    const Field* skillPoints = nullptr;
    std::size_t fieldCount = 0;
    for (const Page& page : pages) {
        if (!Check(!page.fields.empty(), "every editor page has fields")) return 1;
        for (const Field& field : page.fields) {
            const std::size_t width = FieldWidth(field.type);
            if (!Check(width != 0 && field.offset <= inspected.data.size() &&
                       width <= inspected.data.size() - field.offset,
                       "all editor field offsets fit inside the real save")) return 1;
            ++fieldCount;
            if (page.path == L"General" && field.name == L"Skill points")
                skillPoints = &field;
        }
    }
    if (!Check(fieldCount > 0, "enumerate the editor's real-save fields")) return 1;
    if (!Check(skillPoints != nullptr, "find Skill points in the shared field map")) return 1;

    const int64_t originalSkillPoints = inspected.Get(*skillPoints);
    const int64_t replacementSkillPoints = originalSkillPoints == 12345 ? 12346 : 12345;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tempDir = fs::temp_directory_path() /
        ("re6-real-save-regression-" + std::to_string(nonce));
    std::error_code ec;
    if (!Check(fs::create_directories(tempDir, ec) && !ec,
               "create temporary directory for copy-only test")) return 1;

    const fs::path copyPath = tempDir / "savedata.bin";
    const std::wstring copyWide = ToWideUtf8(copyPath.string());
    if (!Check(WriteBytes(copyPath, fixtureBytes), "copy real fixture to temporary test path")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }

    SaveFile working;
    if (!Check(working.Load(copyWide, error), "load temporary copy of real save")) {
        std::wcerr << L"Load error: " << error << L'\n';
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(working.Get(*skillPoints) == originalSkillPoints,
               "read Skill points from the real save")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }

    // A no-op save must preserve every original byte and create an exact backup.
    if (!Check(working.Save(copyWide, error), "save the unmodified real-save copy")) {
        std::wcerr << L"Save error: " << error << L'\n';
        fs::remove_all(tempDir, ec);
        return 1;
    }
    const fs::path backupPath = fs::path(copyPath.string() + ".bak");
    if (!Check(ReadBytes(copyPath) == fixtureBytes, "no-op save preserves all real-save bytes")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(ReadBytes(backupPath) == fixtureBytes, "backup preserves the real save byte-for-byte")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }

    // Exercise an actual field write and checksum refresh, never touching the fixture.
    working.Set(*skillPoints, replacementSkillPoints);
    if (!Check(working.dirty, "mark real-save copy dirty after field edit")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(working.Save(copyWide, error), "save an edited copy of the real save")) {
        std::wcerr << L"Save error: " << error << L'\n';
        fs::remove_all(tempDir, ec);
        return 1;
    }

    const std::vector<uint8_t> editedBytes = ReadBytes(copyPath);
    if (!Check(editedBytes.size() == fixtureBytes.size(), "editing preserves real-save file size")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(ReadBytes(backupPath) == fixtureBytes, "subsequent save does not overwrite the original backup")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }

    // Only the checksum and edited four-byte field may change.
    for (std::size_t i = 0; i < fixtureBytes.size(); ++i) {
        if (fixtureBytes[i] == editedBytes[i]) continue;
        const bool expected = (i >= 8 && i < 12) ||
            (i >= skillPoints->offset && i < skillPoints->offset + 4);
        if (!expected) {
            std::cerr << "Unexpected byte difference at offset 0x" << std::hex << i << std::dec << '\n';
            fs::remove_all(tempDir, ec);
            return 1;
        }
    }

    SaveFile reloaded;
    if (!Check(reloaded.Load(copyWide, error), "reload the edited real-save copy")) {
        std::wcerr << L"Reload error: " << error << L'\n';
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(reloaded.Get(*skillPoints) == replacementSkillPoints,
               "edited Skill points value persists after reload")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(reloaded.StoredChecksum() == reloaded.ComputeChecksum(),
               "edited real-save copy has a valid recomputed checksum")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(!reloaded.dirty, "reloaded real-save copy starts clean")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }
    if (!Check(ReadBytes(fixturePath) == fixtureBytes,
               "provided savedata.bin fixture remains completely unmodified")) {
        fs::remove_all(tempDir, ec);
        return 1;
    }

    fs::remove_all(tempDir, ec);
    std::cout << "Real savedata.bin regression passed: " << fixtureBytes.size()
              << " bytes, " << fieldCount
              << " fields validated, no-op save, backup, edit, reload and checksum verified.\n";
    return 0;
}
