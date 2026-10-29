
#include <imgui.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(PLATFORM_WINDOWS)
    #include <Windows.h>
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <platform/i_window.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform::glfw_backend {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };


    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::none,
    };


    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .display_name                                           = "GLFW Windows",
        .description                                            = "GLFW based Windowing library",
        .category                                               = "Core|Platform",
        .author                                                 = "Mich-Dich",
        .plugin_version                                         = {1, 0, 0},
        .api_version                                            = {1, 0, 0},
        .load_phase                                             = GLT::plugin_manager::phase::pre_application,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::window,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // GLFW window plugin — multi-window implementation of i_window_plugin.
    //
    // Slots live in a dense vector; handles are {index, generation}. Lookup is a single bounds check plus
    // one generation compare. No hashing, no locks, no allocations on the hot path.
    class plugin : public GLT::platform::i_window_plugin {
    public:

        // lifecycle
        void on_load() override;
        void on_unload() override;
        [[nodiscard]] GLT::platform::window_handle create(const GLT::platform::window_attributes& attrs) override;
        void destroy(GLT::platform::window_handle handle) override;
        void destroy_all() override;

        // queries
        [[nodiscard]] bool is_valid(GLT::platform::window_handle) const noexcept override;
        [[nodiscard]] bool should_close(GLT::platform::window_handle) const override;
        [[nodiscard]] glm::ivec2 get_window_size(GLT::platform::window_handle) const override;
        [[nodiscard]] glm::ivec2 get_framebuffer_size(GLT::platform::window_handle) const override;
        [[nodiscard]] glm::ivec2 get_position(GLT::platform::window_handle) const override;
        [[nodiscard]] GLT::platform::window_size_state get_state(GLT::platform::window_handle) const override;
        [[nodiscard]] bool get_vsync(GLT::platform::window_handle) const override;
        [[nodiscard]] GLT::platform::window_attributes get_window_attributes(GLT::platform::window_handle) const override;
        [[nodiscard]] u32 get_window_count() const noexcept override;

        // modifiers
        void show(GLT::platform::window_handle, bool) override;
        void set_state(GLT::platform::window_handle, GLT::platform::window_size_state) override;
        void set_title(GLT::platform::window_handle, const std::string&) override;
        void set_size(GLT::platform::window_handle, u32, u32) override;
        void set_vsync(GLT::platform::window_handle, bool) override;
        void set_cursor_mode(GLT::platform::window_handle, GLT::platform::cursor_mode) override;

        // event loop / backend info
        void poll_events() override;
        [[nodiscard]] GLT::platform::backend_api get_backend_api() const override;
        [[nodiscard]] const char** get_required_render_extensions(u32* count) const override;

        // renderer interop
        [[nodiscard]] void* get_native_window_handle(GLT::platform::window_handle) override;

        // ImGui
        void imgui_init(GLT::platform::window_handle, GLT::render::backend_api) override;
        void imgui_shutdown(GLT::platform::window_handle) override;
        void begin_imgui_frame(GLT::platform::window_handle) override;

    private:

        struct window_slot {

            GLFWwindow*                                     native = nullptr;
            GLT::platform::window_attributes                data{};
            GLT::platform::window_handle                    handle{};
            u32                                             generation = 0;
            bool                                            alive = false;
            ImGuiContext*                                   imgui_context = nullptr;

            // per-window mouse delta state (previously a function-local static in the single-window version)
            glm::vec2                                       last_cursor_pos{};
            bool                                            cursor_first_sample = true;
        };

        [[nodiscard]] window_slot* lookup(GLT::platform::window_handle h) noexcept;

        [[nodiscard]] const window_slot* lookup(GLT::platform::window_handle h) const noexcept;

        [[nodiscard]] static window_slot* slot_from_window(GLFWwindow* w) noexcept;

        void bind_event_callbacks(window_slot& slot);

        std::deque<window_slot>                             m_slots{};
        std::vector<u32>                                    m_free_list{};
        u32                                                 m_active_count = 0;
        static bool                                         s_GLFWinitialized;

    };
}

#include "window.inl"

EXPORT_PLUGIN_CLASS(GLT::platform::glfw_backend::plugin, GLT::platform::glfw_backend::descriptor)
