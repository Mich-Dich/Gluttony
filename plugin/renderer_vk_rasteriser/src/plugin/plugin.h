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

    // CLASS DECLARATION ===============================================================================================

    // The plugin:
    //   - owns the Vulkan device, queues, deletion queue, and the shared resource manager
    //   - is the entry point for the shader compiler and the settings
    //   - constructs scene renderers through create_scene_renderer()
    //   - ticks the shared manager once per frame in begin_frame()
    //
    // It does NOT own, update, or render scene renderers. It does NOT own any swapchain. It does NOT expose debug-draw or preview interfaces - those live per-scene
    class renderer : public GLT::render::i_renderer_plugin,
        public virtual GLT::render::i_shader_compiler {
    public:

        // i_plugin ----------------------------------------------------------------------------------------------------

        void on_load();
        void on_unload();

        // i_renderer_plugin -------------------------------------------------------------------------------------------

        bool create() override;
        void destroy() override;
        void begin_frame(u64 frame_number) override;
        std::expected<std::unique_ptr<GLT::render::i_scene_renderer>, GLT::render::scene_renderer_error> create_scene_renderer(
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

        // accessors used by scene renderers --------------------------------------------------------------------------

        [[nodiscard]] util::instance_wrapper& instance_wrapper() { return m_instance; }
        [[nodiscard]] const util::instance_wrapper& instance_wrapper() const { return m_instance; }

        GETTER_C(vk::Instance,                                          instance, m_instance.instance_handle)
        GETTER_C(vk::Device,                                            vk_device, m_device)
        GETTER_REF(vk::Device,                                          vk_device, m_device)
        DEFAULT_GETTER(util::device*,                                   dev)
        DEFAULT_GETTER_C(vk::PhysicalDevice,                            physical_device)
        DEFAULT_GETTER_REF(vk::PhysicalDevice,                          physical_device)
        DEFAULT_GETTER_C(vk::SurfaceKHR,                                surface)
        DEFAULT_GETTER_REF(util::command_queues,                        queues)
        DEFAULT_GETTER_REF(util::shader_compiler,                       shader_compiler)
        GETTER_C(ref<GLT::platform::i_window_plugin>,                   window, mp_window)
        GETTER_C(shared_resource_manager*,                              shared, m_shared.get())

    private:

        void init_vulkan();
        void destroy_vulkan();


        util::instance_wrapper                                          m_instance{};
        util::command_queues                                            m_queues{};
        vk::Device                                                      m_device = nullptr;
        vk::SurfaceKHR                                                  m_surface = nullptr;
        vk::PhysicalDevice                                              m_physical_device = nullptr;
        util::deletion_queue                                            m_deletion_queue{};
        util::device*                                                   m_dev = nullptr;
        ref<GLT::platform::i_window_plugin>                             mp_window{};
        handle                                                          m_framebuffer_resize_sub{};
        GLT::unique_ref<shared_resource_manager>                        m_shared{};
        GLT::render::renderer_feature                                   m_features{};
        settings::visual                                                m_visual_settings{};
        util::shader_compiler                                           m_shader_compiler{};

    };

}
