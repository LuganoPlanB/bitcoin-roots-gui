// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/macos/metal_renderer.h"

#include "imgui.h"
#include "imgui_impl_metal.h"
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdexcept>

namespace roots::gui::platform {
struct MetalRenderer::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer* layer;
    MTLRenderPassDescriptor* pass;
    id<CAMetalDrawable> drawable;
};

MetalRenderer::MetalRenderer(GLFWwindow* window) : impl(std::make_unique<Impl>())
{
    @autoreleasepool {
        impl->device = MTLCreateSystemDefaultDevice();
        if (!impl->device) throw std::runtime_error("macOS requires an available Metal device");
        impl->queue = [impl->device newCommandQueue];
        if (!impl->queue) throw std::runtime_error("could not create the Metal command queue");
        impl->layer = [CAMetalLayer layer];
        impl->layer.device = impl->device;
        impl->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        impl->pass = [MTLRenderPassDescriptor renderPassDescriptor];
        impl->pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        impl->pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        NSWindow* cocoa_window = glfwGetCocoaWindow(window);
        cocoa_window.contentView.layer = impl->layer;
        cocoa_window.contentView.wantsLayer = YES;
        if (!ImGui_ImplMetal_Init(impl->device))
            throw std::runtime_error("Metal ImGui renderer initialization failed");
    }
}

MetalRenderer::~MetalRenderer()
{
    @autoreleasepool {
        // Queue order makes completion of this buffer a barrier for submitted frames.
        id<MTLCommandBuffer> barrier = [impl->queue commandBuffer];
        [barrier commit];
        [barrier waitUntilCompleted];
        ImGui_ImplMetal_Shutdown();
    }
}

bool MetalRenderer::BeginFrame(int width, int height, float backing_scale)
{
    @autoreleasepool {
        impl->layer.contentsScale = backing_scale;
        const CGSize size = CGSizeMake(width, height);
        if (!CGSizeEqualToSize(impl->layer.drawableSize, size)) impl->layer.drawableSize = size;
        impl->drawable = [impl->layer nextDrawable];
        if (!impl->drawable) return false;
        impl->pass.colorAttachments[0].texture = impl->drawable.texture;
        ImGui_ImplMetal_NewFrame(impl->pass);
        return true;
    }
}

void MetalRenderer::Present(float red, float green, float blue)
{
    @autoreleasepool {
        impl->pass.colorAttachments[0].clearColor = MTLClearColorMake(red, green, blue, 1);
        id<MTLCommandBuffer> commands = [impl->queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:impl->pass];
        if (!encoder) throw std::runtime_error("could not create the Metal render encoder");
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commands, encoder);
        [encoder endEncoding];
        [commands presentDrawable:impl->drawable];
        [commands commit];
        impl->pass.colorAttachments[0].texture = nil;
        impl->drawable = nil;
    }
}

void MetalRenderer::RecreateResources()
{
    ImGui_ImplMetal_DestroyDeviceObjects();
}

std::string MetalRenderer::Name() const
{
    @autoreleasepool {
        return impl->device.name.UTF8String;
    }
}

bool MetalRenderer::BackendLinked()
{
    auto* volatile entry = &ImGui_ImplMetal_Init;
    return entry != nullptr;
}
}
