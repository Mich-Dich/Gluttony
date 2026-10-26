#pragma once

#include "util/pch.h"

#include "plugin_system/i_plugin.h"
#include "render/i_renderer.h"
#include "render/i_shader_compiler.h"
#include "render/i_scene_renderer.h"

#include "util/builders.h"
#include "util/device.h"
#include "shared/shared_resource_manager.h"

#include "settings.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {
    class scene_renderer;
}

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The plugin:
    //   - owns the Vulkan device, queues, deletion queue, and the shared resource manager
    //   - owns every per-window [vk::SurfaceKHR], refcounted by scene renderers that use them
    //   - is the entry point for the shader compiler and the settings
    //   - constructs scene renderers through create_scene_renderer()
    //   - ticks the shared manager once per frame in begin_frame()
    //
    // It does NOT own the OS windows; those live in the platform plugin. It does NOT own, update,
    // or render scene renderers. It does NOT own any swapchain; each surface scene renderer owns
    // the swapchain of *its* window
    class renderer : public GLT::render::i_renderer_plugin,
        public virtual GLT::render::i_shader_compiler {
    public:

        // i_plugin ----------------------------------------------------------------------------------------------------

        void on_load();
        void on_unload();

        // i_renderer_plugin -------------------------------------------------------------------------------------------

        bool create(GLT::platform::window_handle primary_window) override;
        void destroy() override;
        void begin_frame(u64 frame_number) override;
        std::expected<GLT::unique_ref<GLT::render::i_scene_renderer>, GLT::render::scene_renderer_error> create_scene_renderer(
            const GLT::render::scene_renderer_desc& desc) override;
        [[nodiscard]] GLT::render::renderer_feature get_supported_features() const override { return m_features; }
        [[nodiscard]] GLT::render::backend_api get_backend_api() const override { return GLT::render::backend_api::vulkan; }
        [[nodiscard]] void* get_native_device_handle() const override { return { m_physical_device }; }
        [[nodiscard]] void* get_native_context_handle() const override { return {}; }
        [[nodiscard]] GLT::debug::render_stats get_render_stats() const override;
        [[nodiscard]] std::span<const GLT::render::render_mode_info> supported_modes() const override;
        [[nodiscard]] const GLT::reflect::type_descriptor* settings_descriptor() const override;
        [[nodiscard]] void* settings_data() override;
        void on_settings_changed() override;

        // i_shader_compiler -------------------------------------------------------------------------------------------

        [[nodiscard]] std::string_view name() const override;
        [[nodiscard]] u64 implementation_hash() const override;
        [[nodiscard]] u32 contract_version() const override;
        [[nodiscard]] std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error>
            compile(std::string_view slang_source, GLT::asset::material::blend_mode blend) override;

        // window <-> surface registry ---------------------------------------------------------------------------------

        // @brief Returns (and lazily creates) the Vulkan surface for [handle]. Increments the surface's refcount
        //
        // Returns a null handle if the window is invalid, its native handle is null, or surface creation fails
        // Safe to call for the same [handle] many times; the underlying surface is shared
        //
        // @param handle  A window handle from the platform plugin
        // @return The surface (null on failure). The caller MUST call [release_surface(handle)] when done
        [[nodiscard]] vk::SurfaceKHR get_or_create_surface(GLT::platform::window_handle handle);


        // @brief Decrements the surface's refcount. When it hits zero the underlying [vk::SurfaceKHR] is destroyed
        //
        // Called by scene renderers in their destructor. Idempotent for handles that have no surface
        void release_surface(GLT::platform::window_handle handle);

        // accessors used by scene renderers --------------------------------------------------------------------------

        [[nodiscard]] util::instance_wrapper& instance_wrapper() { return m_instance; }
        [[nodiscard]] const util::instance_wrapper& instance_wrapper() const { return m_instance; }

        GETTER_C(vk::Instance,                                                  instance, m_instance.instance_handle)
        GETTER_C(vk::Device,                                                    vk_device, m_device)
        GETTER_REF(vk::Device,                                                  vk_device, m_device)
        DEFAULT_GETTER(util::device*,                                           dev)
        DEFAULT_GETTER_C(vk::PhysicalDevice,                                    physical_device)
        DEFAULT_GETTER_REF(vk::PhysicalDevice,                                  physical_device)
        DEFAULT_GETTER_REF(util::command_queues,                                queues)
        DEFAULT_GETTER_REF(util::shader_compiler,                               shader_compiler)
        GETTER_C(GLT::platform::i_window_plugin*,                               window, mp_window)
        GETTER_C(shared_resource_manager*,                                      shared, m_shared.get())
        GETTER_C(GLT::platform::window_handle,                                  primary_window, m_primary_window)

    private:

        // A surface plus its current refcount. Owned by the renderer, released when refcount hits zero
        struct surface_entry {

            vk::SurfaceKHR                                                      surface{};
            u32                                                                 refcount = 0;
        };

        void init_vulkan();
        void destroy_vulkan();


        util::instance_wrapper                                                  m_instance{};
        util::command_queues                                                    m_queues{};
        vk::Device                                                              m_device = nullptr;
        vk::PhysicalDevice                                                      m_physical_device = nullptr;
        util::deletion_queue                                                    m_deletion_queue{};
        util::device*                                                           m_dev = nullptr;
        GLT::platform::i_window_plugin*                                         mp_window{};
        GLT::platform::window_handle                                            m_primary_window{};
        std::unordered_map<GLT::platform::window_handle, surface_entry>         m_surfaces{};
        handle                                                                  m_framebuffer_resize_sub{};
        GLT::unique_ref<shared_resource_manager>                                m_shared{};
        GLT::render::renderer_feature                                           m_features{};
        settings::visual                                                        m_visual_settings{};
        util::shader_compiler                                                   m_shader_compiler{};

    };

}
