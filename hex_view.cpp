#include "hex_view.h"
#include <algorithm>
#include <array>

namespace hex_editor {
namespace {
constexpr std::size_t bytes_per_row = 16;
constexpr int margin = 10, offset_width = 102, byte_width = 30, ascii_gap = 18;
ATOM RegisterHexView() noexcept {
    static const ATOM atom = [] {
        WNDCLASSEXW type{sizeof(type)};
        type.style = CS_HREDRAW | CS_VREDRAW;
        type.lpfnWndProc = &HexView::WindowProcedure;
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.hCursor = ::LoadCursorW(nullptr, IDC_IBEAM);
        type.lpszClassName = L"mwfl.HexEditor.View";
        return ::RegisterClassExW(&type);
    }();
    return atom;
}
int HexValue(WPARAM key) noexcept {
    if (key >= '0' && key <= '9') return static_cast<int>(key - '0');
    if (key >= 'A' && key <= 'F') return static_cast<int>(key - 'A' + 10);
    return -1;
}
}

bool HexView::Create(HWND parent, mwfl::ControlId id, mwfl::RectDip bounds) {
    if (!RegisterHexView() || !CreateNative(L"mwfl.HexEditor.View", parent, id, L"", bounds,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL, WS_EX_CLIENTEDGE)) return false;
    return ::SetPropW(GetHwnd(), L"mwfl.HexView.This", this) != FALSE;
}
void HexView::SetDocument(HexDocument* value) noexcept { document_ = value; selection_ = first_row_ = 0; high_nibble_ = true; if (IsWindow()) { UpdateScrollBar(GetHwnd()); ::InvalidateRect(GetHwnd(), nullptr, TRUE); } }
void HexView::SetEditable(bool value) noexcept { editable_ = value; high_nibble_ = true; if (IsWindow()) ::InvalidateRect(GetHwnd(), nullptr, TRUE); }
void HexView::SetAppearance(const mwfl::AppearanceState& value) noexcept { appearance_ = value; if (IsWindow()) ::InvalidateRect(GetHwnd(), nullptr, TRUE); }
void HexView::Select(std::size_t offset) noexcept { if (document_ && document_->Size()) selection_ = (std::min)(offset, document_->Size() - 1); EnsureVisible(GetHwnd()); if (IsWindow()) ::InvalidateRect(GetHwnd(), nullptr, FALSE); }

LRESULT CALLBACK HexView::WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    HexView* self = reinterpret_cast<HexView*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = reinterpret_cast<HexView*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); }
    // CreateNative cannot pass lpCreateParams, so recover the wrapper through the
    // parent after WM_CREATE using the control ID property installed below.
    if (!self) self = reinterpret_cast<HexView*>(::GetPropW(window, L"mwfl.HexView.This"));
    return self ? self->Handle(window, message, wparam, lparam) : ::DefWindowProcW(window, message, wparam, lparam);
}

LRESULT HexView::Handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    switch (message) {
        case WM_PAINT: Paint(window); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            first_row_ = delta > 0 ? first_row_ > 3 ? first_row_ - 3 : 0 : first_row_ + 3;
            UpdateScrollBar(window); ::InvalidateRect(window, nullptr, TRUE); return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_ALL}; ::GetScrollInfo(window, SB_VERT, &info);
            std::size_t next = first_row_;
            switch (LOWORD(wparam)) { case SB_LINEUP: if(next)--next; break; case SB_LINEDOWN: ++next; break; case SB_PAGEUP: next=next>static_cast<std::size_t>(info.nPage)?next-info.nPage:0; break; case SB_PAGEDOWN: next+=info.nPage; break; case SB_THUMBTRACK: next=static_cast<std::size_t>(info.nTrackPos); break; default: return 0; }
            const std::size_t maximum = info.nMax >= static_cast<int>(info.nPage) ? static_cast<std::size_t>(info.nMax-static_cast<int>(info.nPage)+1) : 0;
            first_row_=(std::min)(next,maximum); UpdateScrollBar(window); ::InvalidateRect(window,nullptr,TRUE); return 0;
        }
        case WM_LBUTTONDOWN: {
            ::SetFocus(window); HDC dc = ::GetDC(window); TEXTMETRICW metrics{}; ::GetTextMetricsW(dc, &metrics); ::ReleaseDC(window, dc);
            const int row = (GET_Y_LPARAM(lparam) - margin) / (std::max)(1, static_cast<int>(metrics.tmHeight) + 3) - 1;
            const int x = GET_X_LPARAM(lparam) - margin - offset_width;
            if (row >= 0 && x >= 0) Select((first_row_ + static_cast<std::size_t>(row)) * bytes_per_row + static_cast<std::size_t>(x / byte_width));
            return 0;
        }
        case WM_KEYDOWN: {
            if (!document_ || !document_->Size()) return 0;
            std::size_t next = selection_;
            if (wparam == VK_LEFT && next) --next;
            else if (wparam == VK_RIGHT && next + 1 < document_->Size()) ++next;
            else if (wparam == VK_UP && next >= bytes_per_row) next -= bytes_per_row;
            else if (wparam == VK_DOWN && next + bytes_per_row < document_->Size()) next += bytes_per_row;
            else if (wparam == VK_HOME) next -= next % bytes_per_row;
            else if (wparam == VK_END) next = (std::min)(document_->Size() - 1, next - next % bytes_per_row + bytes_per_row - 1);
            else return ::DefWindowProcW(window, message, wparam, lparam);
            Select(next); return 0;
        }
        case WM_CHAR: {
            if (!editable_ || !document_ || selection_ >= document_->Size()) return 0;
            const int digit = HexValue(wparam); if (digit < 0) return 0;
            const unsigned old = std::to_integer<unsigned>(document_->ByteAt(selection_));
            const unsigned value = high_nibble_ ? ((static_cast<unsigned>(digit) << 4) | (old & 0xf)) : ((old & 0xf0) | static_cast<unsigned>(digit));
            document_->Overwrite(selection_, static_cast<std::byte>(value));
            if (!high_nibble_ && selection_ + 1 < document_->Size()) ++selection_;
            high_nibble_ = !high_nibble_; EnsureVisible(window); ::InvalidateRect(window, nullptr, FALSE); if (changed) changed(); return 0;
        }
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

void HexView::EnsureVisible(HWND window) noexcept {
    RECT area{}; ::GetClientRect(window, &area); HDC dc = ::GetDC(window); TEXTMETRICW m{}; ::GetTextMetricsW(dc, &m); ::ReleaseDC(window, dc);
    const std::size_t visible = static_cast<std::size_t>((std::max)(1L, (area.bottom - 2 * margin) / (m.tmHeight + 3) - 1)); const std::size_t row = selection_ / bytes_per_row;
    if (row < first_row_) first_row_ = row; else if (row >= first_row_ + visible) first_row_ = row - visible + 1; UpdateScrollBar(window);
}

void HexView::UpdateScrollBar(HWND window) noexcept {
    if (!window) return; RECT area{}; ::GetClientRect(window,&area); HDC dc=::GetDC(window); TEXTMETRICW m{}; ::GetTextMetricsW(dc,&m); ::ReleaseDC(window,dc);
    const int rows=document_?static_cast<int>((document_->Size()+bytes_per_row-1)/bytes_per_row):0; const UINT page=static_cast<UINT>((std::max)(1L,(area.bottom-2*margin)/(m.tmHeight+3)-1));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,(std::max)(0,rows-1),page,static_cast<int>(first_row_),0}; ::SetScrollInfo(window,SB_VERT,&info,TRUE);
}

void HexView::Paint(HWND window) noexcept {
    PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(window, &paint); RECT area{}; ::GetClientRect(window, &area);
    const bool dark = appearance_.IsDark(); const COLORREF background = dark ? RGB(15,17,21) : RGB(255,255,255); const COLORREF text = dark ? RGB(220,225,232) : RGB(34,38,48);
    HBRUSH brush = ::CreateSolidBrush(background); ::FillRect(dc, &area, brush); ::DeleteObject(brush); ::SetBkMode(dc, TRANSPARENT); ::SetTextColor(dc, text);
    HFONT font = ::CreateFontW(-MulDiv(11, ::GetDeviceCaps(dc, LOGPIXELSY), 72), 0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Cascadia Mono"); const HGDIOBJ old_font = ::SelectObject(dc, font); TEXTMETRICW m{}; ::GetTextMetricsW(dc, &m); const int line = m.tmHeight + 3;
    ::SetTextColor(dc,dark?RGB(139,166,204):RGB(72,91,130)); ::TextOutW(dc,margin,margin,L"OFFSET",6); ::TextOutW(dc,margin+offset_width,margin,L"HEX BYTES",9); ::TextOutW(dc,margin+offset_width+bytes_per_row*byte_width+ascii_gap,margin,L"ASCII",5);
    HPEN separator=::CreatePen(PS_SOLID,1,dark?RGB(54,61,72):RGB(214,220,230)); const HGDIOBJ old_pen=::SelectObject(dc,separator); ::MoveToEx(dc,margin,margin+line-2,nullptr); ::LineTo(dc,area.right-margin,margin+line-2); ::SelectObject(dc,old_pen); ::DeleteObject(separator);
    if (document_) for (int visual = 0; margin + (visual + 1) * line < area.bottom; ++visual) {
        const std::size_t base = (first_row_ + static_cast<std::size_t>(visual)) * bytes_per_row; if (base >= document_->Size()) break;
        const int y=margin+(visual+1)*line; wchar_t offset[20]{}; swprintf_s(offset, L"%08llX", static_cast<unsigned long long>(base)); ::SetTextColor(dc, dark ? RGB(126,160,202) : RGB(72,91,130)); ::TextOutW(dc, margin, y, offset, 8);
        for (std::size_t column = 0; column < bytes_per_row && base + column < document_->Size(); ++column) {
            const std::size_t index = base + column; const int x = margin + offset_width + static_cast<int>(column) * byte_width; RECT cell{x - 2, y, x + byte_width - 3, y + line};
            if (index == selection_) { HBRUSH selected = ::CreateSolidBrush(dark ? RGB(48,86,125) : RGB(200,226,255)); ::FillRect(dc, &cell, selected); ::DeleteObject(selected); }
            const unsigned value = std::to_integer<unsigned>(document_->ByteAt(index)); wchar_t hex[3]{}; swprintf_s(hex, L"%02X", value);
            const COLORREF byte_color=document_->IsChanged(index)?(dark?RGB(255,190,92):RGB(190,70,20)):value==0?(dark?RGB(92,100,112):RGB(145,151,162)):(value>=32&&value<127?(dark?RGB(105,210,190):RGB(0,115,102)):text); ::SetTextColor(dc,byte_color); ::TextOutW(dc,x,y,hex,2);
            wchar_t ascii = value >= 32 && value < 127 ? static_cast<wchar_t>(value) : L'.'; const int ax = margin + offset_width + bytes_per_row * byte_width + ascii_gap + static_cast<int>(column) * m.tmAveCharWidth; ::TextOutW(dc, ax, y, &ascii, 1);
        }
    }
    ::SelectObject(dc, old_font); ::DeleteObject(font); ::EndPaint(window, &paint);
}
} // namespace hex_editor
