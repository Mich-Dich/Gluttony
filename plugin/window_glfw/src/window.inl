#pragma once

#include <glm/glm.hpp>
#include <backends/imgui_impl_glfw.h>

#include <util/pch.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <event/input_event.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform::glfw_backend {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    bool plugin::s_GLFWinitialized = false;

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static void glfw_error_callback(int errorCode, const char* description);


    static void get_max_workarea_size(int& max_width, int& max_height);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static void glfw_error_callback(int errorCode, const char* description) { LOG(error, "GLFW Error: [{}] [{}]", errorCode, description); }


    static void get_max_workarea_size(int& max_width, int& max_height) {

        int monitor_count;
        auto monitors = glfwGetMonitors(&monitor_count);
        max_width = max_height = 0;
        for (int i = 0; i < monitor_count; ++i) {
            int x, y, w, h;
            glfwGetMonitorWorkarea(monitors[i], &x, &y, &w, &h);
            if (w > max_width)  max_width = w;
            if (h > max_height) max_height = h;
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // slot lookup -----------------------------------------------------------------------------------------------------

    plugin::window_slot* plugin::lookup(GLT::platform::window_handle h) noexcept {

        if (h.index >= m_slots.size())
            return nullptr;
        window_slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation)
            return nullptr;
        return &s;
    }


    const plugin::window_slot* plugin::lookup(GLT::platform::window_handle h) const noexcept {

        if (h.index >= m_slots.size())
            return nullptr;
        const window_slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation)
            return nullptr;
        return &s;
    }


    plugin::window_slot* plugin::slot_from_window(GLFWwindow* window) noexcept {

        return window ? static_cast<window_slot*>(glfwGetWindowUserPointer(window)) : nullptr;
    }

    // lifecycle -------------------------------------------------------------------------------------------------------

    void plugin::on_load() { LOG_LOADED }


    void plugin::on_unload() { LOG_UNLOADED }


    GLT::platform::window_handle plugin::create(const GLT::platform::window_attributes& attrs) {

        if (!s_GLFWinitialized) {
            glfwSetErrorCallback(glfw_error_callback);
            VALIDATE(glfwInit(), return GLT::platform::INVALID_WINDOW_HANDLE, "", "Failed to initialize GLFW")
            s_GLFWinitialized = true;
        }

        // Acquire a slot: pop a recycled index, or append a fresh one
        u32 index;
        if (!m_free_list.empty()) {

            index = m_free_list.back();
            m_free_list.pop_back();

        } else {

            index = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
        }

        window_slot& slot = m_slots[index];
        slot.data = attrs;
        slot.imgui_context = nullptr;
        slot.cursor_first_sample = true;

        // Clamp / decorate the requested size
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwWindowHint(GLFW_MAXIMIZED,
            (slot.data.size_state == GLT::platform::window_size_state::fullscreen ||
             slot.data.size_state == GLT::platform::window_size_state::fullscreen_windowed)
                ? GLFW_TRUE : GLFW_FALSE);

        int max_width = 0, max_height = 0;
        get_max_workarea_size(max_width, max_height);

        slot.data.width = glm::clamp(slot.data.width,  300u, static_cast<u32>(max_width));
        slot.data.height = glm::clamp(slot.data.height, 200u, static_cast<u32>(max_height));
        slot.data.width -= 8;
        slot.data.height -= 35;

        slot.native = glfwCreateWindow(static_cast<int>(slot.data.width), static_cast<int>(slot.data.height), 
            slot.data.title.c_str(), nullptr, nullptr);

        if (!slot.native) {

            LOG(error, "Failed to create GLFW window");
            m_free_list.push_back(index);
            slot.alive = false;
            return GLT::platform::INVALID_WINDOW_HANDLE;
        }

        // Success — commit the slot
        slot.generation++;
        slot.alive = true;
        slot.handle = GLT::platform::window_handle{ index, slot.generation };
        m_active_count++;

        glfwSetWindowPos(slot.native, static_cast<int>(slot.data.pos_x), static_cast<int>(slot.data.pos_y));

        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        glfwSetWindowMonitor(slot.native, nullptr,
            static_cast<int>(slot.data.pos_x), static_cast<int>(slot.data.pos_y),
            static_cast<int>(slot.data.width), static_cast<int>(slot.data.height),
            mode->refreshRate);

        if (slot.data.size_state == GLT::platform::window_size_state::fullscreen ||
            slot.data.size_state == GLT::platform::window_size_state::fullscreen_windowed) {
            glfwMaximizeWindow(slot.native);
        }

        glfwSetWindowUserPointer(slot.native, &slot);
        bind_event_callbacks(slot);
        return slot.handle;
    }


    void plugin::destroy(GLT::platform::window_handle handle) {

        window_slot* slot = lookup(handle);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (handle.index, handle.generation))

        int pos_x, pos_y;
        glfwGetWindowPos(slot->native, &pos_x, &pos_y);
        slot->data.pos_x = static_cast<u32>(pos_x);
        slot->data.pos_y = static_cast<u32>(pos_y);

        if (slot->imgui_context)
            imgui_shutdown(handle);

        glfwDestroyWindow(slot->native);

        slot->native = nullptr;
        slot->imgui_context = nullptr;
        slot->alive = false;
        slot->handle = GLT::platform::INVALID_WINDOW_HANDLE;
        slot->cursor_first_sample = true;
        m_active_count--;
        m_free_list.push_back(handle.index);
    }


    void plugin::destroy_all() {

        for (auto& s : m_slots) {
            if (s.alive)
                destroy(s.handle);
        }
    }

    // queries ---------------------------------------------------------------------------------------------------------

    bool plugin::is_valid(GLT::platform::window_handle h) const noexcept { return lookup(h) != nullptr; }


    bool plugin::should_close(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        VALIDATE(slot, return true, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        return glfwWindowShouldClose(slot->native);
    }


    glm::ivec2 plugin::get_window_size(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        if (!slot)
            return {0, 0};
        int w, hh;
        glfwGetWindowSize(slot->native, &w, &hh);
        return {w, hh};
    }


    glm::ivec2 plugin::get_framebuffer_size(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        if (!slot)
            return {0, 0};
        int w, hh;
        glfwGetFramebufferSize(slot->native, &w, &hh);
        return {w, hh};
    }


    glm::ivec2 plugin::get_position(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        if (!slot)
            return {0, 0};
        int x, y;
        glfwGetWindowPos(slot->native, &x, &y);
        return {x, y};
    }


    GLT::platform::window_size_state plugin::get_state(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        if (!slot)
            return GLT::platform::window_size_state::windowed;

        if (glfwGetWindowAttrib(slot->native, GLFW_ICONIFIED))
            return GLT::platform::window_size_state::minimized;

        if (glfwGetWindowAttrib(slot->native, GLFW_MAXIMIZED))
            return GLT::platform::window_size_state::fullscreen_windowed;

        return GLT::platform::window_size_state::windowed;
    }


    bool plugin::get_vsync(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        if (!slot)
            return false;
        return slot->data.vsync;
    }


    GLT::platform::window_attributes plugin::get_window_attributes(GLT::platform::window_handle h) const {

        const window_slot* slot = lookup(h);
        if (!slot)
            return GLT::platform::window_attributes{};

        GLT::platform::window_attributes snap = slot->data;
        const auto size = get_window_size(h);
        const auto pos = get_position(h);
        snap.width = static_cast<u32>(size.x);
        snap.height = static_cast<u32>(size.y);
        snap.pos_x = static_cast<u32>(pos.x);
        snap.pos_y = static_cast<u32>(pos.y);
        snap.size_state = get_state(h);
        return snap;
    }


    u32 plugin::get_window_count() const noexcept { return m_active_count; }

    // modifiers -------------------------------------------------------------------------------------------------------

    void plugin::show(GLT::platform::window_handle h, bool visible) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        visible ? glfwShowWindow(slot->native) : glfwHideWindow(slot->native);
    }


    void plugin::set_state(GLT::platform::window_handle h, GLT::platform::window_size_state new_state) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))

        switch (new_state) {
            case GLT::platform::window_size_state::minimized:
                glfwIconifyWindow(slot->native);
                break;
            case GLT::platform::window_size_state::fullscreen:              [[fallthrough]];
            case GLT::platform::window_size_state::fullscreen_windowed:
                glfwMaximizeWindow(slot->native);
                break;
            default:
                glfwRestoreWindow(slot->native);
                break;
        }
        slot->data.size_state = new_state;
    }


    void plugin::set_title(GLT::platform::window_handle h, const std::string& title) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        glfwSetWindowTitle(slot->native, title.c_str());
        slot->data.title = title;
    }


    void plugin::set_size(GLT::platform::window_handle h, u32 width, u32 height) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        glfwSetWindowSize(slot->native, static_cast<int>(width), static_cast<int>(height));
        slot->data.width = width;
        slot->data.height = height;
    }


    void plugin::set_vsync(GLT::platform::window_handle h, bool vsync) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))

        slot->data.vsync = vsync;
        // GLFW vsync is a swapchain property; the renderer applies it on the next swapchain rebuild
    }


    void plugin::set_cursor_mode(GLT::platform::window_handle h, GLT::platform::cursor_mode mode) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))

        switch (mode) {
            case GLT::platform::cursor_mode::cursor_normal:
                glfwSetInputMode(slot->native, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                break;
            case GLT::platform::cursor_mode::cursor_hidden:
                glfwSetInputMode(slot->native, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                break;
            case GLT::platform::cursor_mode::cursor_disabled:
            case GLT::platform::cursor_mode::cursor_captured:
                glfwSetInputMode(slot->native, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                break;
        }
    }

    // event loop / backend info ---------------------------------------------------------------------------------------

    void plugin::poll_events() { glfwPollEvents(); }


    GLT::platform::backend_api plugin::get_backend_api() const { return GLT::platform::backend_api::glfw; }


    const char** plugin::get_required_render_extensions(u32* count) const { return glfwGetRequiredInstanceExtensions(count); }

    // renderer interop ------------------------------------------------------------------------------------------------

    void* plugin::get_native_window_handle(GLT::platform::window_handle h) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return nullptr, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        return static_cast<void*>(slot->native);
    }

    // ImGui -----------------------------------------------------------------------------------------------------------

    void plugin::imgui_init(GLT::platform::window_handle h, GLT::render::backend_api used_render_api) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        VALIDATE(!slot->imgui_context, return, "", "ImGui already initialized for this window")

        if (used_render_api != GLT::render::backend_api::vulkan) {
            LOG(warn, "ImGui GLFW initialization for this render API is not implemented");
            return;
        }

        // The caller owns the context; we register our GLFW backend against whatever context is
        // currently active. Record it so callback forwarding and begin_imgui_frame() can re-activate
        // it from any thread / call site
        slot->imgui_context = ImGui::GetCurrentContext();
        VALIDATE(slot->imgui_context, return, "",
            "imgui_init called with no current ImGui context — caller must create one first")

        // install_callbacks = false: the plugin forwards GLFW callbacks to ImGui from bind_event_callbacks,
        // so we control which context is current when the backend processes them
        ImGui_ImplGlfw_InitForVulkan(slot->native, false);
    }


    void plugin::imgui_shutdown(GLT::platform::window_handle h) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        if (!slot->imgui_context) return;

        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(slot->imgui_context);
        ImGui_ImplGlfw_Shutdown();
        ImGui::SetCurrentContext(prev);

        // Do NOT destroy the context — we didn't create it. The owner (scene_renderer / imgui_config) remains responsible for teardown
        slot->imgui_context = nullptr;
    }


    void plugin::begin_imgui_frame(GLT::platform::window_handle h) {

        window_slot* slot = lookup(h);
        VALIDATE(slot, return, "", "Invalid window handle [{}, {}]", (h.index, h.generation))
        if (!slot->imgui_context) return;

        ImGui::SetCurrentContext(slot->imgui_context);
        ImGui_ImplGlfw_NewFrame();
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void plugin::bind_event_callbacks(window_slot& slot) {

        // Every callback recovers the slot via the user pointer, so the outgoing event carries
        // the correct window_handle. (Requires events to have a `window` member — see file header note.)
        glfwSetWindowRefreshCallback(slot.native, [](GLFWwindow* w) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_refresh_event event(s->handle);
            GLT::event_bus::post(event);
        });


        glfwSetWindowSizeCallback(slot.native, [](GLFWwindow* w, int width, int height) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            if (s->data.size_state == GLT::platform::window_size_state::windowed) {
                s->data.width = static_cast<u32>(width);
                s->data.height = static_cast<u32>(height);
            }
            GLT::window_resize_event event(s->handle, static_cast<u32>(width), static_cast<u32>(height));
            GLT::event_bus::post(event);
        });


        glfwSetFramebufferSizeCallback(slot.native, [](GLFWwindow* w, int width, int height) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_framebuffer_resize_event event(s->handle, static_cast<u32>(width), static_cast<u32>(height));
            GLT::event_bus::post(event);
        });


        glfwSetWindowFocusCallback(slot.native, [](GLFWwindow* w, int focused) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_focus_event event(s->handle, focused == GLFW_TRUE);
            GLT::event_bus::post(event);
        });


        glfwSetWindowCloseCallback(slot.native, [](GLFWwindow* w) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_close_event event(s->handle);
            GLT::event_bus::post(event);
        });


        glfwSetWindowPosCallback(slot.native, [](GLFWwindow* w, int x, int y) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            s->data.pos_x = static_cast<u32>(x);
            s->data.pos_y = static_cast<u32>(y);
            GLT::window_move_event event(s->handle, x, y);
            GLT::event_bus::post(event);
        });


        glfwSetWindowIconifyCallback(slot.native, [](GLFWwindow* w, int iconified) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_iconify_event event(s->handle, iconified == GLFW_TRUE);
            GLT::event_bus::post(event);
            s->data.size_state = iconified
                ? GLT::platform::window_size_state::minimized
                : GLT::platform::window_size_state::windowed;
        });


        glfwSetWindowMaximizeCallback(slot.native, [](GLFWwindow* w, int maximized) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_maximize_event event(s->handle, maximized == GLFW_TRUE);
            GLT::event_bus::post(event);
            s->data.size_state = maximized
                ? GLT::platform::window_size_state::fullscreen_windowed
                : GLT::platform::window_size_state::windowed;
        });


        glfwSetWindowContentScaleCallback(slot.native, [](GLFWwindow* w, float xscale, float yscale) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::window_content_scale_event event(s->handle, xscale, yscale);
            GLT::event_bus::post(event);
        });


        glfwSetDropCallback(slot.native, [](GLFWwindow* w, int count, const char** paths) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            std::vector<std::string> list;
            list.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) list.emplace_back(paths[i]);
            GLT::window_drop_event event(s->handle, std::move(list));
            GLT::event_bus::post(event);
        });


        glfwSetKeyCallback(slot.native, [](GLFWwindow* w, int key, int /* scancode */, int action, int mods) {
            auto* s = slot_from_window(w);
            if (!s)
                return;

            dispatch_to_imgui(*s, [](GLFWwindow* ww) { ImGui_ImplGlfw_KeyCallback(ww, 0, 0, 0, 0); }, w);
            // ^ placeholder

            GLT::key_event event(s->handle, static_cast<GLT::key_code>(key), static_cast<GLT::key_state>(action), mods);
            GLT::event_bus::post(event);
        });


        glfwSetCharCallback(slot.native, [](GLFWwindow* w, unsigned int codepoint) {
            auto* s = slot_from_window(w);
            if (!s)
                return;

            if (s->imgui_context) {
                ImGuiContext* prev = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(s->imgui_context);
                ImGui_ImplGlfw_CharCallback(w, codepoint);
                ImGui::SetCurrentContext(prev);
            }

            GLT::char_event event(s->handle, codepoint);
            GLT::event_bus::post(event);
        });


        glfwSetCharModsCallback(slot.native, [](GLFWwindow* w, unsigned int codepoint, int mods) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            GLT::char_event event(s->handle, codepoint, mods);
            GLT::event_bus::post(event);
        });


        glfwSetMouseButtonCallback(slot.native, [](GLFWwindow* w, int button, int action, int mods) {
            auto* s = slot_from_window(w);
            if (!s)
                return;

            if (s->imgui_context) {
                ImGuiContext* prev = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(s->imgui_context);
                ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
                ImGui::SetCurrentContext(prev);
            }

            GLT::key_event event(s->handle, static_cast<GLT::key_code>(button), static_cast<GLT::key_state>(action), mods);
            GLT::event_bus::post(event);
        });


        glfwSetCursorPosCallback(slot.native, [](GLFWwindow* w, double x, double y) {
            auto* s = slot_from_window(w);
            if (!s)
                return;

            const glm::vec2 pos{ static_cast<f32>(x), static_cast<f32>(y) };
            if (s->cursor_first_sample) {
                s->last_cursor_pos = pos;
                s->cursor_first_sample = false;
                return;
            }
            const glm::vec2 delta = pos - s->last_cursor_pos;
            s->last_cursor_pos = pos;
            if (delta.x == 0.0f && delta.y == 0.0f)
                return;

            GLT::event_bus::post(GLT::mouse_event(s->handle, GLT::mouse_event::action_type::move, delta));
        });


        glfwSetCursorEnterCallback(slot.native, [](GLFWwindow* w, int entered) {
            auto* s = slot_from_window(w);
            if (!s)
                return;
            if (!entered)
                s->cursor_first_sample = true;   // suppress the next jump when the cursor re-enters
            GLT::mouse_event event(s->handle, entered == GLFW_TRUE);
            GLT::event_bus::post(event);
        });


        glfwSetScrollCallback(slot.native, [](GLFWwindow* w, double x_offset, double y_offset) {
            auto* s = slot_from_window(w);
            if (!s)
                return;

            if (s->imgui_context) {
                ImGuiContext* prev = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(s->imgui_context);
                ImGui_ImplGlfw_ScrollCallback(w, x_offset, y_offset);
                ImGui::SetCurrentContext(prev);
            }

            const glm::vec2 delta{ static_cast<f32>(y_offset), static_cast<f32>(x_offset) };
            if (delta.x == 0.0f && delta.y == 0.0f)
                return;
            GLT::event_bus::post(GLT::mouse_event(s->handle, GLT::mouse_event::action_type::scroll, delta));
        });
    }


    void plugin::dispatch_to_imgui(window_slot& slot, void(*fn)(GLFWwindow*), GLFWwindow* w) {

        // (kept as a trampoline for the exposed ImGui callbacks; see bind_event_callbacks)
        if (!slot.imgui_context)
            return;
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(slot.imgui_context);
        fn(w);
        ImGui::SetCurrentContext(prev);
    }

}
