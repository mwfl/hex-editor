#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hex_editor {

struct FileStamp {
    std::filesystem::file_time_type modified{};
    std::uintmax_t size = 0;
};

struct SaveResult {
    bool succeeded = false;
    std::filesystem::path backup_path;
    std::wstring error;
};

// HexDocument deliberately supports overwrite editing only. Keeping the byte
// count stable makes offsets predictable and prevents an accidental nibble from
// shifting the remainder of a large or structured binary file.
class HexDocument final {
public:
    static constexpr std::uintmax_t maximum_bytes = 256ull * 1024 * 1024;

    bool Open(const std::filesystem::path& path, std::wstring& error);
    void New(std::vector<std::byte> bytes = {});

    std::size_t Size() const noexcept { return bytes_.size(); }
    std::byte ByteAt(std::size_t offset) const noexcept { return bytes_[offset]; }
    std::span<const std::byte> Bytes() const noexcept { return bytes_; }
    const std::filesystem::path& Path() const noexcept { return path_; }
    bool IsDirty() const noexcept { return !changes_.empty(); }
    bool IsChanged(std::size_t offset) const noexcept;
    bool HasExternalChange() const noexcept;

    bool Overwrite(std::size_t offset, std::byte value) noexcept;
    bool Undo() noexcept;
    void Revert() noexcept;
    std::optional<std::size_t> Find(std::span<const std::byte> pattern,
                                    std::size_t start = 0) const noexcept;

    SaveResult SaveAs(const std::filesystem::path& destination) noexcept;
    SaveResult SaveWithBackup() noexcept;

private:
    struct Change { std::size_t offset; std::byte before; std::byte after; };
    static std::optional<FileStamp> ReadStamp(const std::filesystem::path&) noexcept;
    SaveResult WriteAtomically(const std::filesystem::path&, bool backup) noexcept;
    void AcceptSaved(const std::filesystem::path&) noexcept;

    std::vector<std::byte> bytes_;
    std::vector<std::byte> original_;
    std::vector<Change> changes_;
    std::filesystem::path path_;
    std::optional<FileStamp> opened_stamp_;
};

std::optional<std::vector<std::byte>> ParseHexPattern(std::wstring_view text,
                                                      std::wstring& error);
std::wstring FormatUnsigned(std::span<const std::byte> bytes, bool little_endian);
std::wstring FormatSigned(std::span<const std::byte> bytes, bool little_endian);

} // namespace hex_editor
