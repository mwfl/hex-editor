#pragma once
#include "hex_document.h"
#include <mwfl/mwfl.h>
#include <functional>

namespace hex_editor {
class HexView final : public mwfl::NativeControl {
public:
    bool Create(HWND parent, mwfl::ControlId id, mwfl::RectDip bounds);
    void SetDocument(HexDocument* document) noexcept;
    void SetEditable(bool editable) noexcept;
    void SetAppearance(const mwfl::AppearanceState& appearance) noexcept;
    void IncreaseFontSize() noexcept;
    void DecreaseFontSize() noexcept;
    void ResetFontSize() noexcept;
    int FontPointSize() const noexcept { return font_point_size_; }
    void Select(std::size_t offset) noexcept;
    std::size_t Selection() const noexcept { return selection_; }
    std::function<void()> changed;
    std::function<void()> zoom_changed;
    // Public only so the Win32 class registration helper can take its address;
    // all messages immediately route to the private instance handler.
    static LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM) noexcept;
private:
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM) noexcept;
    void Paint(HWND) noexcept;
    void EnsureVisible(HWND) noexcept;
    void UpdateScrollBar(HWND) noexcept;
    HexDocument* document_ = nullptr;
    std::size_t selection_ = 0, first_row_ = 0;
    bool editable_ = false, high_nibble_ = true;
    int font_point_size_ = 11;
    mwfl::AppearanceState appearance_{};
};
} // namespace hex_editor
