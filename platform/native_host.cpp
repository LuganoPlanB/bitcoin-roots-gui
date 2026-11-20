// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
// Stock backend sequencing follows the bundled MIT-licensed ImGui examples.
#include "platform/native_host.h"
#include "platform/linux/appearance.h"
#include "ui/components/semantic_packet.h"
#include "ui/theme/appearance.h"
#include "ui/theme/theme.h"
#include <imkit/imkit.h>
#ifdef ROOTS_GUI_SDL3
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#else
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#undef Status
#endif
#if !defined(__APPLE__)
#include <imgui_impl_opengl3.h>
#endif
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>
#include <cstring>
#include <utility>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace roots::gui::platform {
namespace {
std::atomic_bool active_host{false};
float ValidScale(float value) { return std::isfinite(value) && value > 0 ? value : 1; }
#ifdef ROOTS_GUI_SDL3
void RequireSdl(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}
#endif
}
std::filesystem::path ExecutableFontDirectory()
{
#if defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe").parent_path() / "roots-gui-assets/fonts";
#elif defined(_WIN32)
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) throw std::runtime_error("could not locate the Windows executable");
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / "roots-gui-assets/fonts";
#else
    throw std::runtime_error("native asset location is not yet qualified on this platform");
#endif
}
struct NativeHost::Impl {
    NativeOptions options;
    std::optional<DesktopRuntime> desktop;
    NativeObserver* observer{};
    NativeHost::Draw content;
    NativeHost* host{};
    std::thread::id owner{std::this_thread::get_id()};
    NativeWindow* window{};
    bool acquired{}, library{}, context{}, platform{}, renderer{}, dirty{true};
#ifdef ROOTS_GUI_SDL3
    SDL_Renderer* sdl_renderer{};
    bool close_requested{}, recreate_resources{}, vsync{};
    std::optional<DroppedPaths> pending_drop;
#else
    std::vector<unsigned int> texture_ids;
#endif
    int iteration{}, rendered{};
    ui::BootstrapState draw_state;
    ui::BootstrapAppearance appearance;
    imkit::Theme theme;
    SystemAppearance system_appearance;
    std::unique_ptr<LinuxAccessibility> accessibility;
    std::unique_ptr<NativeInput> input;
    const char* (*old_clipboard_get)(ImGuiContext*){};
    void (*old_clipboard_set)(ImGuiContext*,const char*){};
    void* old_clipboard_data{};bool clipboard_hooks{};
    ui::theme::Appearance choice{ui::theme::Appearance::Light};
    bool invalid_saved{},save_failed{};
    std::optional<bool> last_system_dark;
    ImFont* regular{};
    ImFont* emphasis{};
    ImFont* monospace{};
    std::string renderer_name;
    std::vector<unsigned char> pixels;
    std::vector<DroppedPaths> drops;
    int frame_left{}, frame_top{}, frame_right{}, frame_bottom{}, frame_queries{};
    float frame_dpi_x{}, frame_dpi_y{};
    bool frame_extents_known{};

    Impl(NativeOptions value, NativeObserver* inspect, NativeHost::Draw draw) : options(std::move(value)), observer(inspect), content(std::move(draw))
    {
        if (active_host.exchange(true)) throw std::logic_error("one native host may own ImGui at a time");
        acquired = true;
    }
    void Owner() const
    {
        if (owner != std::this_thread::get_id()) throw std::logic_error("native operations require the host thread");
    }
    void Initialize()
    {
        Owner();
        if (!options.appearance_file.empty()) {
            const auto saved=ui::theme::LoadAppearance(options.appearance_file);
            choice=saved.choice;
            draw_state.show_appearance=true;
            draw_state.appearance_choice=static_cast<int>(choice);
            invalid_saved=!saved.valid;
        }
#if defined(__APPLE__)
        throw std::runtime_error("macOS Metal production lifecycle remains follow-on work");
#else
        if (options.width < 960 || options.height < 640 || !std::isfinite(options.font_scale) ||
            options.font_scale < .5F || options.font_scale > 2.F) throw std::invalid_argument("invalid native window/font settings");
        if (!std::isfinite(options.fixed_delta_seconds) || options.fixed_delta_seconds<0 || options.fixed_delta_seconds>1)
            throw std::invalid_argument("invalid presentation clock");
        unsigned long x11_window{};
#ifdef ROOTS_GUI_SDL3
        RequireSdl(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE), "X11 driver selection failed");
        RequireSdl(SDL_Init(SDL_INIT_VIDEO), "SDL3 initialization failed");
        library = true;
        window = SDL_CreateWindow("Bitcoin Roots", options.width, options.height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        RequireSdl(window != nullptr, "SDL3 window creation failed");
        RequireSdl(SDL_SetWindowMinimumSize(window, 960, 640), "SDL3 window limits failed");
        sdl_renderer = SDL_CreateRenderer(window, "software");
        RequireSdl(sdl_renderer != nullptr, "SDL3 software renderer creation failed");
        vsync = SDL_SetRenderVSync(sdl_renderer, 1);
        renderer_name = SDL_GetRendererName(sdl_renderer);
        x11_window = SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
#else
        glfwSetErrorCallback([](int code, const char* message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        library = true;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        window = glfwCreateWindow(options.width, options.height, "Bitcoin Roots", nullptr, nullptr);
        if (!window) throw std::runtime_error("native window/OpenGL context creation failed");
        glfwSetWindowSizeLimits(window, 960, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowUserPointer(window,this);
        glfwSetDropCallback(window,[](GLFWwindow* window,int count,const char** paths) {
            auto* self=static_cast<Impl*>(glfwGetWindowUserPointer(window));self->Owner();
            if(count<=0 || count>16 || self->drops.size()>=16)return;
            DroppedPaths drop;glfwGetCursorPos(window,&drop.x,&drop.y);
            for(int i=0;i<count;++i){if(!paths[i])return;const auto length=strnlen(paths[i],4097);if(!length || length>4096)return;drop.paths.emplace_back(paths[i],length);}
            self->drops.push_back(std::move(drop));
        });
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        const auto* name = glGetString(GL_RENDERER);
        if (!name) throw std::runtime_error("OpenGL context could not be made current");
        renderer_name = reinterpret_cast<const char*>(name);
#if defined(__linux__)
        x11_window = glfwGetX11Window(window);
#endif
#endif
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        context = true;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigDpiScaleFonts = false; // Host derives typography/layout once from native scale.
        io.ConfigDpiScaleViewports = false;
        if (options.font_directory.empty()) options.font_directory = ExecutableFontDirectory();
        for (const auto* file : {"Inter-Regular.ttf", "Inter-SemiBold.ttf"}) {
            if (!std::filesystem::is_regular_file(options.font_directory/file)) throw std::runtime_error("required Inter font asset is missing");
        }
        const auto font_path = [this](const char* name) {
            const auto utf8 = (options.font_directory / name).u8string();
            return std::string(utf8.begin(), utf8.end());
        };
        regular = io.Fonts->AddFontFromFileTTF(font_path("Inter-Regular.ttf").c_str(), 14);
        emphasis = io.Fonts->AddFontFromFileTTF(font_path("Inter-SemiBold.ttf").c_str(), 20);
        if (!regular || !emphasis) throw std::runtime_error("required licensed Inter fonts could not be loaded");
        monospace=io.Fonts->AddFontDefaultVector(); // Embedded MIT ProggyForever; atlas-owned technical face.
        if (!monospace) throw std::runtime_error("required embedded monospace font could not be loaded");
        io.FontDefault = regular; // Atlas owns file data; pointers survive all submitted frames.
#ifdef ROOTS_GUI_SDL3
        RequireSdl(ImGui_ImplSDL3_InitForSDLRenderer(window, sdl_renderer), "SDL3 ImGui backend initialization failed");
        platform = true;
        RequireSdl(ImGui_ImplSDLRenderer3_Init(sdl_renderer), "SDL3 ImGui renderer initialization failed");
#else
        if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) throw std::runtime_error("GLFW ImGui backend initialization failed");
        platform = true; // Stock callbacks always forward focus, mouse, key and character events.
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("OpenGL3 ImGui backend initialization failed");
#endif
        renderer = true;
        if(options.accessibility)accessibility=std::make_unique<LinuxAccessibility>(*options.accessibility);
        input=std::make_unique<NativeInput>(x11_window);
        if(input->Status().clipboard){auto& platform_io=ImGui::GetPlatformIO();old_clipboard_get=platform_io.Platform_GetClipboardTextFn;old_clipboard_set=platform_io.Platform_SetClipboardTextFn;old_clipboard_data=platform_io.Platform_ClipboardUserData;platform_io.Platform_ClipboardUserData=input.get();clipboard_hooks=true;
            platform_io.Platform_GetClipboardTextFn=[](ImGuiContext*){return static_cast<NativeInput*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->ClipboardForPaste();};
            platform_io.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){if(text)static_cast<NativeInput*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->Copy(text);};
        }
#endif
    }
    bool Visible() const
    {
#ifdef ROOTS_GUI_SDL3
        return !(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN);
#else
        return glfwGetWindowAttrib(window, GLFW_VISIBLE);
#endif
    }
    bool ShouldClose() const
    {
#ifdef ROOTS_GUI_SDL3
        return close_requested;
#else
        return glfwWindowShouldClose(window);
#endif
    }
    void SetShouldClose(bool close)
    {
#ifdef ROOTS_GUI_SDL3
        close_requested = close;
#else
        glfwSetWindowShouldClose(window, close);
#endif
    }
    void PollEvents()
    {
#ifdef ROOTS_GUI_SDL3
        const auto id = SDL_GetWindowID(window);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
            case SDL_EVENT_QUIT:
                close_requested = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == id) close_requested = true;
                break;
            case SDL_EVENT_RENDER_DEVICE_RESET:
                if (event.render.windowID == id) recreate_resources = true;
                break;
            case SDL_EVENT_DROP_BEGIN:
                if (event.drop.windowID == id) pending_drop.emplace();
                break;
            case SDL_EVENT_DROP_FILE:
                if (event.drop.windowID == id && pending_drop) {
                    const auto size = event.drop.data ? strnlen(event.drop.data, 4097) : 0;
                    if (!size || size > 4096 || pending_drop->paths.size() == 16) {
                        pending_drop.reset();
                        break;
                    }
                    pending_drop->x = event.drop.x;
                    pending_drop->y = event.drop.y;
                    pending_drop->paths.emplace_back(event.drop.data, size);
                }
                break;
            case SDL_EVENT_DROP_COMPLETE:
                if (event.drop.windowID == id) {
                    if (pending_drop && !pending_drop->paths.empty() && drops.size() < 16)
                        drops.push_back(std::move(*pending_drop));
                    pending_drop.reset();
                }
                break;
            default:
                break;
            }
        }
#else
        glfwPollEvents();
#endif
    }
    NativeFrame Facts()
    {
        NativeFrame f;
        f.iteration = iteration; f.rendered_frames = rendered;
#ifdef ROOTS_GUI_SDL3
        SDL_GetWindowSize(window, &f.width, &f.height);
        SDL_GetRenderOutputSize(sdl_renderer, &f.framebuffer_width, &f.framebuffer_height);
        f.dpi_x = f.dpi_y = SDL_GetWindowDisplayScale(window);
        const auto flags = SDL_GetWindowFlags(window);
        f.iconified = flags & SDL_WINDOW_MINIMIZED;
        f.decorated = !(flags & SDL_WINDOW_BORDERLESS);
#else
        glfwGetWindowSize(window, &f.width, &f.height);
        glfwGetFramebufferSize(window, &f.framebuffer_width, &f.framebuffer_height);
        glfwGetWindowContentScale(window, &f.dpi_x, &f.dpi_y);
        f.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
        f.decorated = glfwGetWindowAttrib(window, GLFW_DECORATED);
#endif
        if(input)if(auto scale=input->Status().system_scale)f.dpi_x=f.dpi_y=*scale;
        f.dpi_x = ValidScale(f.dpi_x); f.dpi_y = ValidScale(f.dpi_y);
        f.effective_scale = options.font_scale * std::max(f.dpi_x, f.dpi_y);
        f.should_close = ShouldClose();
        // Some X11 WMs do not answer frame-extents requests for unmapped
        // windows. Decorations are observation metadata, not a rendering input:
        // cache mapped extents and never query them while minimized/hidden.
        if (!f.iconified && Visible() &&
            (!frame_extents_known || frame_dpi_x != f.dpi_x || frame_dpi_y != f.dpi_y)) {
#ifdef ROOTS_GUI_SDL3
            SDL_GetWindowBordersSize(window, &frame_top, &frame_left, &frame_bottom, &frame_right);
#else
            glfwGetWindowFrameSize(window, &frame_left, &frame_top, &frame_right, &frame_bottom);
#endif
            ++frame_queries;
            frame_extents_known = frame_left || frame_top || frame_right || frame_bottom;
            frame_dpi_x = f.dpi_x; frame_dpi_y = f.dpi_y;
        }
        f.frame_left = frame_left; f.frame_top = frame_top;
        f.frame_right = frame_right; f.frame_bottom = frame_bottom;
        f.frame_extents_queries = frame_queries;
        return f;
    }
    int Loop()
    {
        Owner();
#if defined(__APPLE__)
        return 1;
#else
        for (;;) {
            ++iteration;
            PollEvents();
            if(desktop)desktop->Poll();
            if(input)input->Pump();
            if(accessibility)accessibility->Pump();
            auto facts = Facts();
            if (observer) observer->Idle(*host, facts);
            if (ShouldClose()) {
                if (!options.before_close || options.before_close()) break;
                SetShouldClose(false);
            }
            facts = Facts();
            if (facts.iconified || !Visible()
                || facts.framebuffer_width <= 0 || facts.framebuffer_height <= 0) {
                if (options.on_draw_suspended) options.on_draw_suspended();
                if(desktop)desktop->Suspend();
                if(input){input->BeginFrame();input->EndFrame();}
                if(accessibility)accessibility->Update(0,0,0,0,false);
#ifdef ROOTS_GUI_SDL3
                SDL_Delay(10);
#else
                glfwWaitEventsTimeout(.01); // Continue pumping events; never render a zero drawable.
#endif
                continue;
            }
            const float dpi = std::max(facts.dpi_x, facts.dpi_y);
            if (!options.appearance_file.empty()) {
                last_system_dark=options.system_appearance_reader?options.system_appearance_reader():system_appearance.Dark();
                const bool dark=ui::theme::IsDark(choice, last_system_dark.value_or(false));
                if (options.dark != dark) { options.dark=dark; dirty=true; }
            }
            if (dirty || appearance.dpi_scale != dpi) {
#ifdef ROOTS_GUI_SDL3
                RequireSdl(SDL_SetWindowMinimumSize(window, int(std::ceil(960 * dpi)), int(std::ceil(640 * dpi))), "SDL3 window limits failed");
#else
                glfwSetWindowSizeLimits(window, int(std::ceil(960 * dpi)), int(std::ceil(640 * dpi)), GLFW_DONT_CARE, GLFW_DONT_CARE);
#endif
                appearance = {options.dark, options.font_scale, dpi};
                appearance.monospace=monospace;
                theme = ui::MakeBootstrapTheme(appearance);
                theme.fonts = {regular, emphasis, emphasis};
                ImGui::GetStyle().FontScaleDpi = 1;
                ui::theme::Apply(theme); // Fresh base tokens; no cumulative size/font scaling.
                dirty = false;
            }
            // Status can change while the projected color stays Light. Publish
            // it every frame, independently of theme/font resource rebuilds.
            appearance.status=host->AppearanceState();
            appearance.reduced_motion=input?input->Status().reduced_motion.value_or(true):true;
            theme.motion.reducedMotion=appearance.reduced_motion;
            draw_state.appearance_choice=appearance.status.choice;
            draw_state.appearance_error=appearance.status.invalid_saved || appearance.status.save_failed;
            draw_state.system_unknown=appearance.status.system_unavailable;
#ifdef ROOTS_GUI_SDL3
            if (std::exchange(recreate_resources, false)) ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
#else
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
#endif
            if (options.fixed_delta_seconds>0) ImGui::GetIO().DeltaTime=options.fixed_delta_seconds;
            ImGui::NewFrame();
            ui::components::NativeInputScope native_scope(input.get());if(input)input->BeginFrame();
            ImGui::PushFont(regular, theme.typography.body);
            if (content) content(theme, appearance);
            else facts.draw = ui::DrawBootstrap(theme, appearance, draw_state);
            if(input){input->EndFrame();input->Pump();}
            if (accessibility) {
                int x{}, y{};
#ifdef ROOTS_GUI_SDL3
                SDL_GetWindowPosition(window, &x, &y);
#else
                glfwGetWindowPos(window, &x, &y);
#endif
                accessibility->Update(x, y, facts.width, facts.height, host->Focused());
                accessibility->Pump();
            }
            if (facts.draw.appearance_requested >= 0 && !options.appearance_file.empty()) {
                host->SetAppearance(facts.draw.appearance_requested);
            }
            ImGui::PopFont();
            auto& io = ImGui::GetIO();
            facts.space_down = ImGui::IsKeyDown(ImGuiKey_Space);
            facts.mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            facts.capture_keyboard = io.WantCaptureKeyboard;
            facts.capture_mouse = io.WantCaptureMouse;
            ImGui::Render();
            for (auto* texture : ImGui::GetPlatformIO().Textures) {
                facts.texture_creates += texture->Status == ImTextureStatus_WantCreate;
                facts.texture_updates += texture->Status == ImTextureStatus_WantUpdates;
            }
#ifdef ROOTS_GUI_SDL3
            auto* draw_data = ImGui::GetDrawData();
            RequireSdl(SDL_SetRenderScale(sdl_renderer, draw_data->FramebufferScale.x, draw_data->FramebufferScale.y), "SDL3 render scale failed");
            RequireSdl(SDL_SetRenderDrawColorFloat(sdl_renderer, theme.semantic.canvas.x, theme.semantic.canvas.y, theme.semantic.canvas.z, 1), "SDL3 canvas color failed");
            RequireSdl(SDL_RenderClear(sdl_renderer), "SDL3 canvas clear failed");
            ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, sdl_renderer);
#else
            glViewport(0, 0, facts.framebuffer_width, facts.framebuffer_height);
            glClearColor(theme.semantic.canvas.x, theme.semantic.canvas.y, theme.semantic.canvas.z, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            facts.gl_error = glGetError();
            if (facts.gl_error) throw std::runtime_error("native renderer reported an OpenGL error");
#endif
            for (auto* texture : ImGui::GetPlatformIO().Textures) {
                if (texture->TexID != ImTextureID_Invalid) {
#ifndef ROOTS_GUI_SDL3
                    const auto id = static_cast<unsigned int>(texture->TexID);
                    if (std::find(texture_ids.begin(), texture_ids.end(), id) == texture_ids.end()) texture_ids.push_back(id);
#endif
                    ++facts.live_textures;
                }
            }
            facts.font_texture = io.Fonts->TexRef.GetTexID();
#ifdef ROOTS_GUI_SDL3
            RequireSdl(facts.font_texture != ImTextureID_Invalid, "SDL3 font texture upload failed");
#endif
            facts.rendered_frames = ++rendered;
            if (observer && observer->CaptureFramebuffer()) {
                pixels.resize(static_cast<std::size_t>(facts.framebuffer_width) * facts.framebuffer_height * 4);
#ifdef ROOTS_GUI_SDL3
                using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
                Surface captured(SDL_RenderReadPixels(sdl_renderer, nullptr), SDL_DestroySurface);
                RequireSdl(captured != nullptr, "SDL3 framebuffer readback failed");
                Surface rgba(SDL_ConvertSurface(captured.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
                RequireSdl(rgba != nullptr, "SDL3 framebuffer conversion failed");
                if (rgba->w != facts.framebuffer_width || rgba->h != facts.framebuffer_height)
                    throw std::runtime_error("SDL3 framebuffer dimensions changed during capture");
                const auto row_bytes = static_cast<std::size_t>(rgba->w) * 4;
                for (int y = 0; y < rgba->h; ++y)
                    std::memcpy(pixels.data() + (rgba->h - 1 - y) * row_bytes,
                        static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch, row_bytes);
#else
                glReadPixels(0, 0, facts.framebuffer_width, facts.framebuffer_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                if (glGetError()) throw std::runtime_error("framebuffer readback failed");
#endif
                facts.rgba = pixels;
            }
            if (facts.draw.close_requested) SetShouldClose(true);
            if (observer) observer->Frame(*host, facts);
#ifdef ROOTS_GUI_SDL3
            RequireSdl(SDL_RenderPresent(sdl_renderer), "SDL3 present failed");
            if (!vsync) SDL_Delay(16);
#else
            glfwSwapBuffers(window);
#endif
        }
        return 0;
#endif
    }
    ~Impl()
    {
        if (owner != std::this_thread::get_id()) std::terminate();
        NativeTeardown state;
        desktop.reset();
        if(context && clipboard_hooks){auto& platform_io=ImGui::GetPlatformIO();platform_io.Platform_GetClipboardTextFn=old_clipboard_get;platform_io.Platform_SetClipboardTextFn=old_clipboard_set;platform_io.Platform_ClipboardUserData=old_clipboard_data;}
        if(input){state.input_cleared=input->Close();input.reset();}
        if(accessibility){state.accessibility_cleared=accessibility->Close();accessibility.reset();}
        if(options.accessibility)state.accessibility_cleared=options.accessibility->Close() && state.accessibility_cleared;
#if defined(ROOTS_GUI_SDL3)
        if (renderer) ImGui_ImplSDLRenderer3_Shutdown();
        if (context) state.renderer_cleared = ImGui::GetIO().BackendRendererUserData == nullptr;
        if (platform) ImGui_ImplSDL3_Shutdown();
        if (context) state.platform_cleared = ImGui::GetIO().BackendPlatformUserData == nullptr;
        if (context) ImGui::DestroyContext();
        state.imgui_cleared = ImGui::GetCurrentContext() == nullptr;
        if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer); // SDL also destroys any remaining renderer textures.
        state.textures_released = true;
        if (window) SDL_DestroyWindow(window);
        if (library) SDL_Quit();
        state.context_cleared = !SDL_WasInit(SDL_INIT_VIDEO);
#elif !defined(__APPLE__)
        if (window) glfwMakeContextCurrent(window);
        if (renderer) ImGui_ImplOpenGL3_Shutdown();
        if (context) state.renderer_cleared = ImGui::GetIO().BackendRendererUserData == nullptr;
        state.textures_released = true;
        for (const auto id : texture_ids) state.textures_released &= glIsTexture(id) == GL_FALSE;
        if (platform) ImGui_ImplGlfw_Shutdown();
        if (context) state.platform_cleared = ImGui::GetIO().BackendPlatformUserData == nullptr;
        if (context) ImGui::DestroyContext();
        state.imgui_cleared = ImGui::GetCurrentContext() == nullptr;
        if (window) { glfwMakeContextCurrent(nullptr); glfwDestroyWindow(window); }
        state.context_cleared = !library || glfwGetCurrentContext() == nullptr;
        if (library) glfwTerminate();
#endif
        if (acquired) active_host = false;
        if (observer) { try { observer->Teardown(state); } catch (...) { std::fputs("native teardown observer failed\n", stderr); } }
    }
};
NativeHost::NativeHost(NativeOptions options, NativeObserver* observer, Draw draw) : m_impl(std::make_unique<Impl>(std::move(options), observer, std::move(draw)))
{
    m_impl->host = this;
    m_impl->Initialize();
    if(m_impl->options.desktop_ports)m_impl->desktop.emplace(*m_impl->options.desktop_ports);
}
NativeHost::~NativeHost() = default;
int NativeHost::Run() { return m_impl->Loop(); }
NativeWindow* NativeHost::Window() { m_impl->Owner(); return m_impl->window; }
bool NativeHost::Focused() const
{
    m_impl->Owner();
#ifdef ROOTS_GUI_SDL3
    return SDL_GetWindowFlags(m_impl->window) & SDL_WINDOW_INPUT_FOCUS;
#else
    return glfwGetWindowAttrib(m_impl->window, GLFW_FOCUSED);
#endif
}
void NativeHost::RequestClose() { m_impl->Owner(); m_impl->SetShouldClose(true); }
bool NativeHost::Minimize()
{
    m_impl->Owner();
    if (!m_impl->window || m_impl->ShouldClose()) return false;
#ifdef ROOTS_GUI_SDL3
    return SDL_MinimizeWindow(m_impl->window);
#else
    glfwIconifyWindow(m_impl->window);
    return true;
#endif
}
DesktopRuntime* NativeHost::Desktop()
{
    m_impl->Owner();
    return m_impl->desktop?&*m_impl->desktop:nullptr;
}
void NativeHost::SetFontScale(float scale)
{
    m_impl->Owner();
    if (!std::isfinite(scale) || scale < .5F || scale > 2) throw std::invalid_argument("font scale must be between 50% and 200%");
    m_impl->options.font_scale = scale;
    m_impl->dirty = true;
}
bool NativeHost::SetAppearance(int choice)
{
    m_impl->Owner();
    if(choice<0 || choice>2)return false;
    if(m_impl->options.appearance_file.empty()){m_impl->save_failed=true;return false;}
    const auto value=static_cast<ui::theme::Appearance>(choice);
    if(!ui::theme::SaveAppearance(m_impl->options.appearance_file,value)){m_impl->save_failed=true;return false;}
    m_impl->choice=value;m_impl->draw_state.appearance_choice=choice;m_impl->draw_state.appearance_error=false;
    m_impl->invalid_saved=false;m_impl->save_failed=false;
    return true;
}
ui::AppearanceStatus NativeHost::AppearanceState() const
{
    m_impl->Owner();
    return {m_impl->options.appearance_file.empty()?-1:static_cast<int>(m_impl->choice),m_impl->invalid_saved,m_impl->save_failed,
        !m_impl->options.appearance_file.empty() && m_impl->choice==ui::theme::Appearance::System && !m_impl->last_system_dark.has_value()};
}
NativeInput& NativeHost::Input(){m_impl->Owner();if(!m_impl->input)throw std::logic_error("native input is not initialized");return *m_impl->input;}
InputStatus NativeHost::InputState() const {m_impl->Owner();return m_impl->input?m_impl->input->Status():InputStatus{};}
AccessibilityStatus NativeHost::AccessibilityState() const {m_impl->Owner();return m_impl->accessibility?m_impl->accessibility->Status():AccessibilityStatus{false,false,false,true,0,0,0,"","No semantic publisher is associated with this native host."};}
void NativeHost::RecreateRendererResources()
{
    m_impl->Owner();
#if defined(ROOTS_GUI_SDL3)
    m_impl->recreate_resources = true;
#elif !defined(__APPLE__)
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
#endif
}
const std::string& NativeHost::Renderer() const { m_impl->Owner(); return m_impl->renderer_name; }
}

namespace roots::gui::platform {
std::vector<DroppedPaths> NativeHost::TakeDroppedPaths() {m_impl->Owner();return std::exchange(m_impl->drops,{});}
}
