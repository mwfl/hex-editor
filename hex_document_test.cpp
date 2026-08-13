#include "hex_document.h"
#include <cassert>
#include <fstream>

int main() {
    using namespace hex_editor;
    std::wstring error;
    const auto parsed = ParseHexPattern(L"89 50 4e 47", error);
    assert(parsed && parsed->size() == 4 && std::to_integer<int>((*parsed)[2]) == 0x4e);
    assert(!ParseHexPattern(L"123", error));
    HexDocument document; document.New({std::byte{0x10}, std::byte{0x20}, std::byte{0x30}});
    assert(document.Overwrite(1, std::byte{0xff}) && document.IsDirty() && document.IsChanged(1));
    assert(document.Find(std::array{std::byte{0xff}, std::byte{0x30}}) == 1);
    assert(document.Undo() && !document.IsChanged(1));
    assert(FormatUnsigned(std::array{std::byte{0x34}, std::byte{0x12}}, true) == L"4660");

    const auto path = std::filesystem::temp_directory_path() / L"mwfl-hex-editor-test.bin";
    document.New({std::byte{1}, std::byte{2}}); auto saved = document.SaveAs(path); assert(saved.succeeded);
    assert(document.Overwrite(0, std::byte{9})); saved = document.SaveWithBackup(); assert(saved.succeeded);
    assert(std::filesystem::exists(saved.backup_path));
    std::ifstream backup(saved.backup_path, std::ios::binary); char first = 0; backup.read(&first, 1); assert(first == 1);
    std::error_code ignored; std::filesystem::remove(path, ignored); std::filesystem::remove(saved.backup_path, ignored);
}
