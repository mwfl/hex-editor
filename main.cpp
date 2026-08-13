#include <mwfl/mwfl.h>
#include "hex_document.h"
#include "hex_view.h"
#include "resource.h"
#include <shellapi.h>
#include <memory>

using mwfl::operator""_dip;
namespace {
constexpr mwfl::ControlId kOpen{100}, kEdit{101}, kSave{102}, kSaveAs{103}, kUndo{104}, kFind{105}, kSearch{106}, kGoto{107}, kOffset{108}, kView{109};
constexpr UINT kRunSelfTest = WM_APP + 0x381;

class MainWindow final : public mwfl::WindowBase {
public:
    void BuildUI() override {
        SetTitle(L"MWFL Hex Editor — read-only");
        mwfl::ControlHost ui{*this};
        ui.Add(open_, kOpen, L"Open…"); ui.Add(edit_, kEdit, L"Enable editing"); ui.Add(save_, kSave, L"Save + backup"); ui.Add(save_as_, kSaveAs, L"Save As…"); ui.Add(undo_, kUndo, L"Undo");
        ui.Add(find_, kFind, L""); find_.SetCueBanner(L"Hex bytes: 89 50 4E 47"); ui.Add(search_, kSearch, L"Find next");
        ui.Add(offset_, kOffset, L""); offset_.SetCueBanner(L"Offset (hex)"); ui.Add(go_, kGoto, L"Go"); ui.AddNative(view_, kView, mwfl::RectDip{});
        ui.Add(status_, L"Open any file. Viewing is read-only by default."); ui.Add(inspector_, L"Selection: —");
        view_.SetDocument(&document_); view_.SetAppearance(GetAppearanceState()); view_.changed = [this] { RefreshState(); };
        SetLayout(mwfl::Column().Gap(7.0_dip).Margin(10.0_dip)
            .Add(mwfl::Row().Gap(6.0_dip).Add(open_,mwfl::Fixed(84.0_dip)).Add(edit_,mwfl::Fixed(128.0_dip)).Add(save_,mwfl::Fixed(124.0_dip)).Add(save_as_,mwfl::Fixed(94.0_dip)).Add(undo_,mwfl::Fixed(72.0_dip)).Add(mwfl::Column(),mwfl::Stretch()),mwfl::Fixed(34.0_dip))
            .Add(mwfl::Row().Gap(6.0_dip).Add(find_,mwfl::Stretch()).Add(search_,mwfl::Fixed(90.0_dip)).Add(offset_,mwfl::Fixed(130.0_dip)).Add(go_,mwfl::Fixed(58.0_dip)),mwfl::Fixed(34.0_dip))
            .Add(view_,mwfl::Stretch())
            .Add(mwfl::Row().Gap(8.0_dip).Add(status_,mwfl::Stretch()).Add(inspector_,mwfl::Fixed(350.0_dip)),mwfl::Fixed(28.0_dip)));
        save_.SetEnabled(false); save_as_.SetEnabled(false); undo_.SetEnabled(false);
        mwfl::EnableFileDrop(GetHwnd());
        const auto initial = InitialPath(); if (!initial.empty()) Open(initial);
        if (IsSelfTest()) ::PostMessageW(GetHwnd(),kRunSelfTest,0,0);
    }
    mwfl::EventResult OnCommand(const mwfl::CommandEvent& event) override {
        if (event.IsClicked(open_)) { auto r=mwfl::ShowOpenFileDialog({.owner=GetHwnd(),.title=L"Open any file",.filters={{L"All files",L"*.*"}}}); if(r.accepted) Open(r.path); return mwfl::EventResult::Handled(); }
        if (event.IsClicked(edit_)) { ToggleEditing(); return mwfl::EventResult::Handled(); }
        if (event.IsClicked(save_)) { SaveOriginal(); return mwfl::EventResult::Handled(); }
        if (event.IsClicked(save_as_)) { SaveAs(); return mwfl::EventResult::Handled(); }
        if (event.IsClicked(undo_)) { document_.Undo(); RefreshState(); return mwfl::EventResult::Handled(); }
        if (event.IsClicked(search_)) { FindNext(); return mwfl::EventResult::Handled(); }
        if (event.IsClicked(go_)) { GoTo(); return mwfl::EventResult::Handled(); }
        return mwfl::EventResult::Propagate();
    }
    mwfl::EventResult OnMessage(const mwfl::WindowMessage& event) override { if(event.id==kRunSelfTest){RunSelfTest();return mwfl::EventResult::Handled();} if(event.id!=WM_DROPFILES)return mwfl::EventResult::Propagate(); const auto files=mwfl::ReadDroppedFiles(reinterpret_cast<HDROP>(event.wparam)); if(!files.empty())Open(files.front()); return mwfl::EventResult::Handled(); }
    mwfl::EventResult OnAppearanceChanged(const mwfl::AppearanceState& state) override { view_.SetAppearance(state); return mwfl::EventResult::Propagate(); }
    mwfl::EventResult OnClose() override {
        if (!document_.IsDirty()) return mwfl::EventResult::Propagate();
        const int answer=::MessageBoxW(GetHwnd(),L"Discard the unsaved byte changes and close?",L"MWFL Hex Editor",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);
        return answer==IDYES ? mwfl::EventResult::Propagate() : mwfl::EventResult::Handled();
    }
private:
    static std::filesystem::path InitialPath() { int count=0; auto args=std::unique_ptr<wchar_t*,decltype(&::LocalFree)>(::CommandLineToArgvW(::GetCommandLineW(),&count),&::LocalFree); if(!args)return {}; for(int i=1;i<count;++i){std::wstring_view value=args.get()[i];if(!value.starts_with(L"--"))return std::filesystem::path(value);} return {}; }
    static bool IsSelfTest(){return std::wstring_view(::GetCommandLineW()).find(L"--self-test")!=std::wstring_view::npos;}
    void Open(const std::filesystem::path& path) { if(document_.IsDirty()&&::MessageBoxW(GetHwnd(),L"Discard current byte changes?",L"Open",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return; std::wstring error; if(!document_.Open(path,error)){ShowError(error);return;} editing_=false; view_.SetEditable(false); view_.SetDocument(&document_); RefreshState(); }
    void ToggleEditing() { if(!editing_){ if(::MessageBoxW(GetHwnd(),L"Editing binary data can make a file unusable. Hex Editor uses overwrite mode and keeps the file unchanged until Save. Enable editing?",L"Enable binary editing",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return; editing_=true;}else editing_=false; view_.SetEditable(editing_); RefreshState(); }
    void RefreshState() { const bool loaded=document_.Size()>0||!document_.Path().empty(); save_.SetEnabled(document_.IsDirty()&&!document_.Path().empty()); save_as_.SetEnabled(loaded); undo_.SetEnabled(document_.IsDirty()); edit_.SetEnabled(loaded); edit_.SetText(editing_?L"Return to read-only":L"Enable editing"); SetTitle(L"MWFL Hex Editor — "+std::wstring(editing_?L"EDIT MODE":L"read-only")+(document_.IsDirty()?L" *":L"")); status_.SetText(document_.Path().empty()?L"No file open":document_.Path().wstring()+L"  •  "+std::to_wstring(document_.Size())+L" bytes"+(document_.IsDirty()?L"  •  modified":L"")); const auto selected=view_.Selection(); if(selected<document_.Size()){ const auto remaining=document_.Bytes().subspan(selected,(std::min<std::size_t>)(8,document_.Size()-selected)); inspector_.SetText(L"0x"+Hex(selected)+L"  ULE: "+hex_editor::FormatUnsigned(remaining,true)+L"  SLE: "+hex_editor::FormatSigned(remaining,true)); } ::InvalidateRect(view_.GetHwnd(),nullptr,FALSE); }
    static std::wstring Hex(std::size_t value){ wchar_t text[24]{}; swprintf_s(text,L"%llX",static_cast<unsigned long long>(value)); return text; }
    void FindNext(){ std::wstring error; auto pattern=hex_editor::ParseHexPattern(find_.GetText(),error); if(!pattern){ShowError(error);return;} auto found=document_.Find(*pattern,view_.Selection()+1); if(!found)found=document_.Find(*pattern); if(found)view_.Select(*found);else status_.SetText(L"Pattern not found"); RefreshState(); }
    void GoTo(){ try { std::size_t used=0; const auto value=std::stoull(offset_.GetText(),&used,16); if(used!=offset_.GetText().size()||value>=document_.Size())throw std::out_of_range("offset"); view_.Select(static_cast<std::size_t>(value)); RefreshState(); }catch(...){ShowError(L"Enter a hexadecimal offset inside the current file.");} }
    void SaveAs(){ mwfl::FileDialogOptions options{.owner=GetHwnd(),.title=L"Save edited bytes as",.filters={{L"All files",L"*.*"}},.initial_path=document_.Path().filename()}; auto r=mwfl::ShowSaveFileDialog(options); if(!r.accepted)return; auto saved=document_.SaveAs(r.path); if(!saved.succeeded)ShowError(saved.error); else {editing_=false;view_.SetEditable(false);RefreshState();} }
    void SaveOriginal(){ if(document_.HasExternalChange()){ShowError(L"The original file changed outside Hex Editor. Reopen it or use Save As.");return;} if(::MessageBoxW(GetHwnd(),L"Replace the original file with the edited bytes? A .bak copy of the original will be created beside it.",L"Save binary changes",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return; auto saved=document_.SaveWithBackup(); if(!saved.succeeded)ShowError(saved.error); else {editing_=false;view_.SetEditable(false);RefreshState();status_.SetText(L"Saved safely. Backup: "+saved.backup_path.wstring());} }
    void ShowError(const std::wstring& text){::MessageBoxW(GetHwnd(),text.c_str(),L"MWFL Hex Editor",MB_OK|MB_ICONERROR);}
    void RunSelfTest() noexcept { int result=0; try { document_.New({std::byte{0x4d},std::byte{0x57},std::byte{0x46},std::byte{0x4c}}); view_.SetDocument(&document_); view_.SetEditable(true); ::SetFocus(view_.GetHwnd()); ::SendMessageW(view_.GetHwnd(),WM_CHAR,'A',0); ::SendMessageW(view_.GetHwnd(),WM_CHAR,'5',0); if(document_.ByteAt(0)!=std::byte{0xa5}||!document_.IsChanged(0))result=1; if(result==0&&!document_.Undo())result=2; view_.SetAppearance(mwfl::ResolveAppearance({mwfl::ColorMode::dark})); ::InvalidateRect(view_.GetHwnd(),nullptr,TRUE); ::UpdateWindow(view_.GetHwnd()); if(result==0&&!view_.IsWindow())result=3; }catch(...){result=4;} ::PostQuitMessage(result); }
    hex_editor::HexDocument document_; bool editing_=false; hex_editor::HexView view_; mwfl::Button open_,edit_,save_,save_as_,undo_,search_,go_; mwfl::TextBox find_,offset_; mwfl::Label status_,inspector_;
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){ return mwfl::RunApplication<MainWindow>(instance,show,{.title=L"MWFL Hex Editor",.initial_bounds={{},{1120.0_dip,760.0_dip}},.use_default_bounds=false,.icon=::LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP)),.small_icon=::LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP))}); }
