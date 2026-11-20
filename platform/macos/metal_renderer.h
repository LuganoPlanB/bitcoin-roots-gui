// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include <memory>
#include <string>

struct GLFWwindow;

namespace roots::gui::platform {
class MetalRenderer {
public:
    explicit MetalRenderer(GLFWwindow* window);
    ~MetalRenderer();

    bool BeginFrame(int width, int height, float backing_scale);
    void Present(float red, float green, float blue);
    void RecreateResources();
    std::string Name() const;
    static bool BackendLinked();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
