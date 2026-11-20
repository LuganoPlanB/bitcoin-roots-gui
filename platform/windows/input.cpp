// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/input.h"

#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

namespace roots::gui::platform {
namespace c = ui::components;
namespace {
constexpr std::size_t TextLimit = 65536;

std::wstring Wide(std::string_view text)
{
    if (text.empty() || text.size() > TextLimit || text.find('\0') != text.npos) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!size) return {};
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size))
        return {};
    return result;
}

std::string Utf8(std::wstring_view text)
{
    if (text.empty() || text.size() > TextLimit) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0 || size > int(TextLimit)) return {};
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), size, nullptr, nullptr))
        return {};
    return result;
}

struct Clipboard {
    bool opened;
    explicit Clipboard(HWND owner) : opened(OpenClipboard(owner) != FALSE) {}
    ~Clipboard() { if (opened) CloseClipboard(); }
};

bool PublishClipboard(UINT format, const void* data, std::size_t bytes)
{
    const auto handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) return false;
    auto* target = GlobalLock(handle);
    if (target) {
        std::copy_n(static_cast<const char*>(data), bytes, static_cast<char*>(target));
        GlobalUnlock(handle);
        if (SetClipboardData(format, handle)) return true; // Windows owns the allocation now.
    }
    GlobalFree(handle);
    return false;
}

std::string ReadClipboard(HWND owner)
{
    const Clipboard clipboard(owner);
    if (!clipboard.opened) return {};
    const auto handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) return {};
    const auto count = std::min(GlobalSize(handle) / sizeof(wchar_t), TextLimit + 1);
    const auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
    if (!text) return {};
    struct Unlock { HGLOBAL handle; ~Unlock() { GlobalUnlock(handle); } } unlock{handle};
    const auto end = std::find(text, text + count, L'\0');
    return end == text + count ? std::string{} : Utf8({text, std::size_t(end - text)});
}
}

struct NativeInput::Impl {
    HWND window;
    InputStatus status;
    bool hidden{}, editor_sensitive{}, editor_protected{}, copy_sensitive{};
    DWORD copy_sequence{};
    bool owns_copy{};
    std::string view;
    uint32_t editor{}, paste_target{};
    std::string paste;
    bool paste_seen{}, paste_sensitive{}, paste_protected{};
    uint64_t file_id{};
    bool save{};
    const wchar_t* title{};
    std::wstring initial;
    c::FileChoice choice;

    explicit Impl(GLFWwindow* native) : window(glfwGetWin32Window(native))
    {
        status.clipboard = status.file_dialog = window != nullptr;
    }

    void ClearPaste()
    {
        if (!paste.empty()) SecureZeroMemory(paste.data(), paste.size());
        paste.clear();
        paste_target = 0;
    }

    void ClearOwnCopy()
    {
        if (!owns_copy) return;
        const Clipboard clipboard(window);
        if (!clipboard.opened) return;
        if (GetClipboardOwner() == window && GetClipboardSequenceNumber() == copy_sequence && !EmptyClipboard()) return;
        owns_copy = copy_sensitive = false;
    }

    bool ReadFor(uint32_t target, bool sensitive, bool protected_editor)
    {
        if (!target || !status.clipboard || (hidden && sensitive && !protected_editor)) return false;
        ClearPaste();
        paste = ReadClipboard(window);
        ++status.clipboard_reads;
        if (paste.empty()) return false;
        paste_target = target;
        paste_seen = true;
        paste_sensitive = sensitive;
        paste_protected = protected_editor;
        return true;
    }

    uint64_t Choose(std::string_view path, bool saving, const wchar_t* prompt)
    {
        if (!status.file_dialog || choice.outcome == c::NativeOutcome::Pending || path.size() > 4096) return 0;
        initial = Wide(path);
        if (!path.empty() && initial.empty()) return 0;
        save = saving;
        title = prompt;
        choice = {c::NativeOutcome::Pending, {}, {}};
        return ++file_id;
    }
};

NativeInput::NativeInput(GLFWwindow* window) : impl(std::make_shared<Impl>(window)) {}
NativeInput::~NativeInput() { Close(); }

void NativeInput::Pump()
{
    if (!NativeDialogActive()) return;
    // The owner opens the modal native dialog after ImGui has returned from
    // the control callback. Only the selected path crosses this boundary.
    std::array<wchar_t, 32768> path{};
    if (impl->initial.size() < path.size()) std::ranges::copy(impl->initial, path.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = impl->window;
    dialog.lpstrFilter = L"All files\0*.*\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = impl->title;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT | OFN_NODEREFERENCELINKS;
    if (!impl->save) dialog.Flags |= OFN_FILEMUSTEXIST;
    const bool selected = impl->save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!selected) {
        impl->choice = CommDlgExtendedError()
            ? c::FileChoice{c::NativeOutcome::Failed, {}, "Windows file chooser failed."}
            : c::FileChoice{c::NativeOutcome::Cancelled, {}, "File selection cancelled."};
        return;
    }
    auto name = Utf8(path.data());
    if (name.empty() || name.size() > 4096 || !std::filesystem::path(path.data()).is_absolute())
        impl->choice = {c::NativeOutcome::Failed, {}, "Windows file chooser returned an invalid path."};
    else
        impl->choice = {c::NativeOutcome::Chosen, std::move(name), {}};
}

void NativeInput::BeginFrame() { impl->paste_seen = false; }
void NativeInput::EndFrame() { if (!impl->paste_seen) impl->ClearPaste(); }
void NativeInput::BeginView(std::string_view scope)
{
    if (impl->view != scope) impl->ClearPaste();
    impl->view = scope;
}
void NativeInput::Privacy(bool hidden)
{
    impl->hidden = hidden;
    if (hidden && impl->copy_sensitive) impl->ClearOwnCopy();
    if (hidden && impl->paste_sensitive && !impl->paste_protected) impl->ClearPaste();
}
void NativeInput::PrivacyLock()
{
    Privacy(true);
    impl->ClearOwnCopy();
    impl->ClearPaste();
    impl->choice = {c::NativeOutcome::Cancelled, {}, "File choice cancelled by privacy lock."};
    impl->status.notice.clear();
}
bool NativeInput::Copy(std::string_view text, bool sensitive)
{
    sensitive |= impl->editor_sensitive;
    if (!impl->status.clipboard || impl->editor_protected || (impl->hidden && sensitive)) return false;
    auto value = Wide(text);
    if (value.empty()) return false;
    bool accepted;
    {
        const Clipboard clipboard(impl->window);
        accepted = clipboard.opened && EmptyClipboard();
        if (accepted && sensitive) {
            // Keep sensitive writes out of Windows clipboard history and sync.
            const auto exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
            const DWORD marker = 0;
            accepted = exclude && PublishClipboard(exclude, &marker, sizeof(marker));
        }
        if (accepted) accepted = PublishClipboard(CF_UNICODETEXT, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    }
    if (sensitive) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    if (!accepted) {
        impl->status.notice = "Copy could not acquire the Windows clipboard.";
        return false;
    }
    impl->owns_copy = true;
    impl->copy_sensitive = sensitive;
    impl->copy_sequence = GetClipboardSequenceNumber();
    ++impl->status.clipboard_writes;
    impl->status.notice.clear();
    return true;
}
const char* NativeInput::ClipboardForPaste()
{
    impl->ReadFor(impl->editor, impl->editor_sensitive, impl->editor_protected);
    return ""; // The bounded editor callback consumes the pending value.
}
void NativeInput::EnterEditor(uint32_t id, bool sensitive)
{
    impl->editor = id;
    impl->editor_sensitive = sensitive;
    impl->editor_protected = false;
}
void NativeInput::EnterProtectedEditor(uint32_t id)
{
    EnterEditor(id, true);
    impl->editor_protected = true;
}
void NativeInput::LeaveEditor() { EnterEditor(0, false); }
std::optional<std::string> NativeInput::Paste(uint32_t id, std::string_view value, int, int start, int end, size_t capacity)
{
    if (!id || impl->paste_target != id) return {};
    if (start < 0 || end < start || size_t(end) > value.size() ||
        value.size() - size_t(end - start) + impl->paste.size() >= capacity) {
        impl->ClearPaste();
        impl->status.notice = "Clipboard text does not fit this field. Draft retained.";
        return {};
    }
    impl->paste_target = 0;
    ++impl->status.pastes;
    impl->status.notice.clear();
    return std::exchange(impl->paste, {});
}
bool NativeInput::RequestInspection(uint32_t id) { return !impl->paste_target && impl->ReadFor(id, false, false); }
bool NativeInput::InspectionPending(uint32_t id) const { return id && id == impl->paste_target; }
bool NativeInput::FileChooserAvailable() const { return impl->status.file_dialog && !NativeDialogActive(); }
bool NativeInput::NativeDialogActive() const { return impl->choice.outcome == c::NativeOutcome::Pending; }
uint64_t NativeInput::ChooseFile(std::string_view path, bool save)
{
    return impl->Choose(path, save, save ? L"Choose a file to save" : L"Choose a local file");
}
uint64_t NativeInput::ChooseRpcCookie() { return impl->Choose({}, false, L"Choose the RPC cookie file"); }
c::FileChoice NativeInput::PollFile(uint64_t id)
{
    if (!id || id != impl->file_id) return {c::NativeOutcome::Stale, {}, "File selection is no longer current."};
    return impl->choice;
}
InputStatus NativeInput::Status() const { return impl->status; }
bool NativeInput::Close()
{
    if (impl->status.closed) return true;
    PrivacyLock();
    impl->status.clipboard = impl->status.file_dialog = false;
    impl->status.closed = true;
    return true;
}
}
