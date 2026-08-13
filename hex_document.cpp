#include "hex_document.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <charconv>
#include <fstream>
#include <limits>

namespace hex_editor {
namespace {
std::wstring SystemError(DWORD code) {
    wchar_t* message = nullptr;
    const DWORD count = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                             FORMAT_MESSAGE_FROM_SYSTEM |
                                             FORMAT_MESSAGE_IGNORE_INSERTS,
                                         nullptr, code, 0,
                                         reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = count && message ? std::wstring(message, count) : L"Windows error " + std::to_wstring(code);
    if (message) ::LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

int HexDigit(wchar_t c) noexcept {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}
} // namespace

std::optional<FileStamp> HexDocument::ReadStamp(const std::filesystem::path& path) noexcept {
    std::error_code error;
    FileStamp stamp{std::filesystem::last_write_time(path, error), 0};
    if (error) return std::nullopt;
    stamp.size = std::filesystem::file_size(path, error);
    return error ? std::nullopt : std::optional<FileStamp>{stamp};
}

bool HexDocument::Open(const std::filesystem::path& path, std::wstring& error) {
    const auto stamp = ReadStamp(path);
    if (!stamp) { error = L"The file could not be inspected."; return false; }
    if (stamp->size > maximum_bytes) {
        error = L"This example safely loads files up to 256 MiB. The file was not opened.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = L"The file could not be opened for reading."; return false; }
    std::vector<std::byte> value(static_cast<std::size_t>(stamp->size));
    if (!value.empty()) input.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(value.size()));
    if (!input && !value.empty()) { error = L"The complete file could not be read."; return false; }
    bytes_ = std::move(value); original_ = bytes_; changes_.clear(); path_ = path; opened_stamp_ = stamp;
    return true;
}

void HexDocument::New(std::vector<std::byte> bytes) {
    bytes_ = std::move(bytes); original_ = bytes_; changes_.clear(); path_.clear(); opened_stamp_.reset();
}

bool HexDocument::IsChanged(std::size_t offset) const noexcept {
    return offset < bytes_.size() && offset < original_.size() && bytes_[offset] != original_[offset];
}

bool HexDocument::HasExternalChange() const noexcept {
    if (path_.empty() || !opened_stamp_) return false;
    const auto now = ReadStamp(path_);
    return !now || now->size != opened_stamp_->size || now->modified != opened_stamp_->modified;
}

bool HexDocument::Overwrite(std::size_t offset, std::byte value) noexcept {
    if (offset >= bytes_.size() || bytes_[offset] == value) return false;
    changes_.push_back({offset, bytes_[offset], value}); bytes_[offset] = value; return true;
}

bool HexDocument::Undo() noexcept {
    if (changes_.empty()) return false;
    const Change change = changes_.back(); changes_.pop_back(); bytes_[change.offset] = change.before; return true;
}

void HexDocument::Revert() noexcept { bytes_ = original_; changes_.clear(); }

std::optional<std::size_t> HexDocument::Find(std::span<const std::byte> pattern, std::size_t start) const noexcept {
    if (pattern.empty() || start >= bytes_.size()) return std::nullopt;
    const auto found = std::search(bytes_.begin() + static_cast<std::ptrdiff_t>(start), bytes_.end(), pattern.begin(), pattern.end());
    return found == bytes_.end() ? std::nullopt : std::optional<std::size_t>{static_cast<std::size_t>(found - bytes_.begin())};
}

void HexDocument::AcceptSaved(const std::filesystem::path& path) noexcept {
    path_ = path; original_ = bytes_; changes_.clear(); opened_stamp_ = ReadStamp(path_);
}

SaveResult HexDocument::WriteAtomically(const std::filesystem::path& destination, bool backup) noexcept {
    SaveResult result;
    const auto temporary = destination.parent_path() / (destination.filename().wstring() + L".mwfl-tmp");
    const auto backup_path = destination.parent_path() / (destination.filename().wstring() + L".bak");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { result.error = L"The temporary file could not be created."; return result; }
        if (!bytes_.empty()) output.write(reinterpret_cast<const char*>(bytes_.data()), static_cast<std::streamsize>(bytes_.size()));
        output.flush();
        if (!output.good()) { result.error = L"The complete temporary file could not be written."; return result; }
    }
    if (backup && std::filesystem::exists(destination)) {
        if (::ReplaceFileW(destination.c_str(), temporary.c_str(), backup_path.c_str(), REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) == FALSE) {
            result.error = L"Windows could not replace the original file: " + SystemError(::GetLastError());
            std::error_code ignored; std::filesystem::remove(temporary, ignored); return result;
        }
        result.backup_path = backup_path;
    } else if (::MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        result.error = L"Windows could not move the completed file into place: " + SystemError(::GetLastError());
        std::error_code ignored; std::filesystem::remove(temporary, ignored); return result;
    }
    result.succeeded = true; AcceptSaved(destination); return result;
}

SaveResult HexDocument::SaveAs(const std::filesystem::path& destination) noexcept { return WriteAtomically(destination, false); }
SaveResult HexDocument::SaveWithBackup() noexcept {
    if (path_.empty()) return {.error = L"Choose Save As for a document without a path."};
    if (HasExternalChange()) return {.error = L"The file changed outside Hex Editor. Reopen it or use Save As."};
    return WriteAtomically(path_, true);
}

std::optional<std::vector<std::byte>> ParseHexPattern(std::wstring_view text, std::wstring& error) {
    std::vector<int> digits;
    for (wchar_t c : text) {
        if (::iswspace(c)) continue;
        const int value = HexDigit(c);
        if (value < 0) { error = L"Use hexadecimal digits with optional spaces."; return std::nullopt; }
        digits.push_back(value);
    }
    if (digits.empty() || digits.size() % 2 != 0) { error = L"Enter complete byte pairs, for example 89 50 4E 47."; return std::nullopt; }
    std::vector<std::byte> bytes; bytes.reserve(digits.size() / 2);
    for (std::size_t i = 0; i < digits.size(); i += 2) bytes.push_back(static_cast<std::byte>((digits[i] << 4) | digits[i + 1]));
    return bytes;
}

std::wstring FormatUnsigned(std::span<const std::byte> bytes, bool little_endian) {
    const auto count = (std::min)(bytes.size(), sizeof(std::uint64_t)); std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) { const std::size_t source = little_endian ? count - 1 - i : i; value = (value << 8) | std::to_integer<unsigned>(bytes[source]); }
    return std::to_wstring(value);
}
std::wstring FormatSigned(std::span<const std::byte> bytes, bool little_endian) {
    const auto count = (std::min)(bytes.size(), sizeof(std::int64_t)); if (!count) return L"0";
    std::uint64_t value = std::stoull(FormatUnsigned(bytes.first(count), little_endian));
    if (count < 8 && (value & (std::uint64_t{1} << (count * 8 - 1)))) value |= ~((std::uint64_t{1} << (count * 8)) - 1);
    return std::to_wstring(static_cast<std::int64_t>(value));
}
} // namespace hex_editor
