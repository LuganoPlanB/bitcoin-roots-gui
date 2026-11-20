// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#import <Metal/Metal.h>

#include <cstdio>
#include <stdexcept>

using namespace roots::gui::platform;

namespace {
struct SmokeObserver : NativeObserver {
    int frames{};
    bool clean{};

    void Frame(NativeHost& host, const NativeFrame& frame) override
    {
        if (frame.framebuffer_width <= 0 || frame.framebuffer_height <= 0 || !frame.live_textures)
            throw std::runtime_error("Metal frame has no drawable or font texture");
        if (frame.draw.font_size != 16.F || frame.effective_scale != 1.F)
            throw std::runtime_error("Expected 16-point body text with independent Retina backing scale");
        if (++frames == 3) host.RequestClose();
    }

    void Teardown(const NativeTeardown& state) override
    {
        clean = state.renderer_cleared && state.platform_cleared && state.imgui_cleared;
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) {
            std::puts("Metal device unavailable on this runner");
            return 77;
        }
        SmokeObserver observer;
        try {
            {
                NativeOptions options;
                options.font_directory = argv[1];
                NativeHost host(options, &observer);
                if (host.Run() != 0) return 1;
            }
            if (observer.frames != 3 || !observer.clean) return 1;
            std::puts("Metal host submitted three frames and shut down cleanly");
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 1;
        }
    }
}
