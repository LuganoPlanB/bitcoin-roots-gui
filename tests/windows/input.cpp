// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace roots::gui;
using namespace std::chrono_literals;

namespace {
void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
    std::printf("PASS %s\n", message);
}

struct Dialog {
    HWND owner;
    HWND window{};
    static BOOL CALLBACK Find(HWND candidate, LPARAM data)
    {
        auto& self = *reinterpret_cast<Dialog*>(data);
        wchar_t name[32]{};
        if (GetWindow(candidate, GW_OWNER) == self.owner &&
            GetClassNameW(candidate, name, 32) && std::wstring_view(name) == L"#32770") {
            self.window = candidate;
            return FALSE;
        }
        return TRUE;
    }
};

struct Probe : platform::NativeObserver {
    int stage{};
    uint64_t request{};
    std::jthread closer;
    std::atomic_bool cancelled{};

    bool CaptureFramebuffer() const override { return stage == 0; }

    void Frame(platform::NativeHost& host, const platform::NativeFrame& frame) override
    {
        auto& input = host.Input();
        if (stage == 0) {
            std::size_t different{};
            for (std::size_t at = 4; at + 3 < frame.rgba.size(); at += 4)
                different += frame.rgba[at] != frame.rgba[0] || frame.rgba[at+1] != frame.rgba[1] || frame.rgba[at+2] != frame.rgba[2];
            Check(frame.decorated && frame.font_texture && different > 100, "Win32 OpenGL draws visible content");
            constexpr std::string_view text = "Roots \xCE\x94 clipboard";
            input.BeginFrame();
            Check(input.Copy(text) && input.RequestInspection(11), "Windows Unicode clipboard write and read");
            input.EndFrame();
            input.BeginFrame();
            Check(input.Paste(11, "", 0, 0, 0, 256) == text, "Unicode clipboard content reaches the intended editor");
            input.EndFrame();
            input.EnterProtectedEditor(12);
            Check(!input.Copy("protected editor"), "protected editor cannot publish clipboard text");
            input.LeaveEditor();
            const auto desktop = platform::NativeDesktopPorts();
            Check(bool(desktop.browser), "Windows browser handoff is available");
            Check(!desktop.browser(app::HelpTarget{}, "file:///rejected"), "browser handoff rejects unapproved URLs");
            const auto owner = glfwGetWin32Window(host.Window());
            const auto thread_id = GetCurrentThreadId();
            closer = std::jthread([this, owner, thread_id](std::stop_token stop) {
                const auto deadline = std::chrono::steady_clock::now() + 6s;
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                    Dialog dialog{owner};
                    EnumThreadWindows(thread_id, Dialog::Find, reinterpret_cast<LPARAM>(&dialog));
                    if (dialog.window && IsWindowVisible(dialog.window)) {
                        cancelled = PostMessageW(dialog.window, WM_CLOSE, 0, 0) != FALSE;
                        return;
                    }
                    std::this_thread::sleep_for(25ms);
                }
            });
            request = input.ChooseRpcCookie();
            Check(request && input.NativeDialogActive(), "native RPC cookie picker queued");
            ++stage;
        } else if (stage == 1) {
            Check(cancelled && input.PollFile(request).outcome == ui::components::NativeOutcome::Cancelled,
                "native file dialog opens and reports cancellation");
            ++stage;
            host.RequestClose();
        }
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 2 || std::string_view(argv[1]) != "--owned-desktop") return 2;
    try {
        Probe probe;
        platform::NativeOptions options;
        platform::NativeHost host(options, &probe);
        return host.Run() == 0 && probe.stage == 2 ? 0 : 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
