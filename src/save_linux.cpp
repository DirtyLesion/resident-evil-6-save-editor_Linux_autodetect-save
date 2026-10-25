#include "save.h"

#include <codecvt>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <system_error>

namespace fs = std::filesystem;
namespace {
constexpr uint32_t kHeaderSize = 0x18;
constexpr uint32_t kMinSize = 34028 + 8 * 112;

uint16_t ReadU16(const std::vector<uint8_t>& d, uint32_t off) {
    uint16_t v = 0;
    if (off + 2 <= d.size()) std::memcpy(&v, d.data() + off, 2);
    return v;
}
void WriteU16(std::vector<uint8_t>& d, uint32_t off, uint16_t v) {
    if (off + 2 <= d.size()) std::memcpy(d.data() + off, &v, 2);
}
fs::path NativePath(const std::wstring& value) {
    // POSIX std::filesystem::path does not reliably accept wchar_t paths.
    // Qt supplies Unicode wide strings; convert explicitly to UTF-8 so saves
    // stored under non-ASCII home-directory or filename paths work on Linux.
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return fs::u8path(converter.to_bytes(value));
}
}

uint32_t SaveFile::ReadU32(uint32_t off) const {
    uint32_t v = 0;
    if (off + 4 <= data.size()) std::memcpy(&v, data.data() + off, 4);
    return v;
}
void SaveFile::WriteU32(uint32_t off, uint32_t v) {
    const std::size_t index = static_cast<std::size_t>(off);
    if (index > data.size() || data.size() - index < sizeof(v)) return;

    // The save format is explicitly little-endian; do not depend on host byte order.
    data[index] = static_cast<uint8_t>(v & 0xFFu);
    data[index + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    data[index + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    data[index + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
void SaveFile::WriteU8(uint32_t off, uint8_t v) {
    if (off < data.size()) data[off] = v;
}
uint32_t SaveFile::ComputeChecksum() const {
    uint32_t sum = 0;
    for (uint32_t off = kHeaderSize; off + 4 <= data.size(); off += 4) sum += ReadU32(off);
    return sum;
}
bool SaveFile::Load(const std::wstring& file, std::wstring& error) {
    std::ifstream in(NativePath(file), std::ios::binary);
    if (!in) { error = L"Could not open the file."; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < kMinSize) {
        error = L"The file is too small (" + std::to_wstring(bytes.size()) +
                L" bytes) to be a Resident Evil 6 save.";
        return false;
    }
    uint32_t declared = 0;
    std::memcpy(&declared, bytes.data() + 4, 4);
    if (declared != bytes.size() || bytes.size() % 4 != 0) {
        error = L"The header's size field doesn't match the file size. This may not be an unencrypted Resident Evil 6 PC save.";
        return false;
    }
    data = std::move(bytes);
    if (path != file) backedUpPath_.clear();
    path = file;
    dirty = false;
    return true;
}
bool SaveFile::Save(const std::wstring& file, std::wstring& error) {
    const fs::path target = NativePath(file);
    std::error_code ec;

    // If checking the destination fails, abort instead of potentially
    // replacing a file without first creating the promised backup.
    const bool targetExists = fs::exists(target, ec);
    if (ec) {
        error = L"Could not inspect the save destination.";
        return false;
    }
    if (backedUpPath_ != file && targetExists) {
        fs::path backup = target;
        backup += ".bak";
        ec.clear();
        fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) { error = L"Could not create backup file."; return false; }
    }

    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = L"Could not write temporary save file."; return false; }
        // Calculate checksum on the in-memory save and place it in the output
        // buffer only after the backup has been created successfully.
        WriteU32(8, ComputeChecksum());
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            out.close();
            fs::remove(temp, ec);
            error = L"Could not finish writing temporary save file.";
            return false;
        }
    }
    // On POSIX, rename within the same directory atomically replaces the
    // destination. Never delete the original as a fallback if rename fails.
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp);
        error = L"Could not replace the save file.";
        return false;
    }
    backedUpPath_ = file;
    path = file;
    dirty = false;
    return true;
}
int64_t SaveFile::Get(const Field& f) const {
    switch (f.type) {
        case FieldType::U8: return ReadU8(f.offset);
        case FieldType::Flag8: return ReadU8(f.offset) != 0;
        case FieldType::Bits8: return (ReadU8(f.offset) & f.mask) != 0;
        case FieldType::Bits32: return (ReadU32(f.offset) & f.mask) != 0;
        case FieldType::I16: return static_cast<int16_t>(ReadU16(data, f.offset));
        case FieldType::U32:
        case FieldType::Hex32: return ReadU32(f.offset);
        case FieldType::Weapon: return ReadU16(data, f.offset) & 0x0FFF;
    }
    return 0;
}
void SaveFile::Set(const Field& f, int64_t v) {
    if (f.readOnly) return;
    switch (f.type) {
        case FieldType::U8: WriteU8(f.offset, static_cast<uint8_t>(v)); break;
        case FieldType::Flag8: WriteU8(f.offset, v ? 1 : 0); break;
        case FieldType::Bits8: {
            uint8_t b = ReadU8(f.offset);
            WriteU8(f.offset, static_cast<uint8_t>(v ? (b | f.mask) : (b & ~f.mask))); break;
        }
        case FieldType::Bits32: {
            uint32_t w = ReadU32(f.offset);
            WriteU32(f.offset, v ? (w | f.mask) : (w & ~f.mask)); break;
        }
        case FieldType::I16: WriteU16(data, f.offset, static_cast<uint16_t>(static_cast<int16_t>(v))); break;
        case FieldType::U32:
        case FieldType::Hex32: WriteU32(f.offset, static_cast<uint32_t>(v)); break;
        case FieldType::Weapon: {
            uint16_t flags = ReadU16(data, f.offset) & 0xF000;
            WriteU16(data, f.offset, static_cast<uint16_t>(flags | (v & 0x0FFF))); break;
        }
    }
    dirty = true;
}
void FieldRange(const Field& f, int64_t& lo, int64_t& hi) {
    switch (f.type) {
        case FieldType::U8: lo = 0; hi = 0xFF; break;
        case FieldType::Flag8:
        case FieldType::Bits8:
        case FieldType::Bits32: lo = 0; hi = 1; break;
        case FieldType::I16: lo = -32768; hi = 32767; break;
        case FieldType::U32:
        case FieldType::Hex32: lo = 0; hi = 0xFFFFFFFFll; break;
        case FieldType::Weapon: lo = 0; hi = 0x0FFF; break;
    }
}
