#include "util/pch.h"
#include "util/util_pch.h"
#include "plugin/plugin.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "platform/i_window.h"
#include "reflection/registry.h"
#include "render/image.h"
#include "image.h"

#include "scene/scene_renderer.h"
#include "modes.h"



namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        constexpr bool                              USE_VULKAN_VALIDATION = true;
    #else
        constexpr bool                              USE_VULKAN_VALIDATION = false;
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    void renderer::on_load() {

        // Register the image factory (used by external consumers of `render::image`).
        GLT::render::image::factory_table table{};
        table.default_fn = []() -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>();
        };
        table.size_fn = [](const glm::uvec3& size) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(size);
        };
        table.data_fn = [](const void* data, const u32 width, const u32 height, const bool mipmapped = false)
            -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(data, width, height, mipmapped);
        };
        table.path_fn = [](const std::filesystem::path& path, bool mipmapped)
            -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(path, mipmapped);
        };
        GLT::render::image::register_factory(table);

        // Register the visual settings type so the editor can walk it.
        GLT::reflect::register_type<settings::visual>();
        GLT::reflect::finalize();
    }


    void renderer::on_unload() { GLT::render::image::register_factory({}); }


    bool renderer::create() {

        mp_window = GLT::platform::window::get_ref();
        ASSERT(mp_window, "", "Failed to get window plugin")

        init_vulkan();

        m_shared = GLT::create_unique_ref<shared_resource_manager>(m_device, m_physical_device, m_dev, m_queues.graphics_queue,
            m_queues.graphics_index);
        m_shared->create();

        m_framebuffer_resize_sub = GLT::event_bus::subscribe<GLT::window_framebuffer_resize_event>(
            [this](const GLT::window_framebuffer_resize_event& /*event*/) {
                // Handled by the scene renderer's `render()` returning target_out_of_date.
                // The application layer calls scene_renderer::resize() with the new extent.
            });

        LOG_INIT
        return true;
    }


    void renderer::destroy() {

        GLT::event_bus::unsubscribe(m_framebuffer_resize_sub);

        if (m_shared) {
            m_shared->destroy();
            m_shared.reset();
        }

        destroy_vulkan();
        LOG_SHUTDOWN
    }


    void renderer::begin_frame(u64 frame_number) { m_shared->begin_frame(frame_number); }


    std::expected<std::unique_ptr<GLT::render::i_scene_renderer>, GLT::render::scene_renderer_error> renderer::create_scene_renderer(
        const GLT::render::scene_renderer_desc& desc) {

        auto result = scene_renderer::create(*this, desc);
        if (!result)
            return std::unexpected(result.error());

        std::unique_ptr<GLT::render::i_scene_renderer> base = std::move(*result);
        return base;
    }


    GLT::debug::render_stats renderer::get_render_stats() const {

        return GLT::debug::render_stats{
            .gpu_time_ms = 0.0f,
            .draw_calls = 0,
            .triangles = 0,
            .vertices = 0,
            .render_passes = 0,
            #if defined(DEBUG)
                .vram_bytes = GLT::render::image::get_live_vram_bytes(),
            #endif
            .ram_bytes = 0,
            #if defined(DEBUG)
                .texture_count = GLT::render::image::get_live_count(),
            #endif
            .buffer_count = m_shared ? m_shared->live_buffer_count() : 0u,
            .descriptor_set_count = m_shared ? m_shared->live_descriptor_set_count() : 0u,
            .pipeline_count = m_shared ? m_shared->live_pipeline_count() : 0u,
        };
    }


    std::span<const GLT::render::render_mode_info> renderer::supported_modes() const { return modes::mode_table; }


    const GLT::reflect::type_descriptor* renderer::settings_descriptor() const { return GLT::reflect::type_of<settings::visual>(); }


    void* renderer::settings_data() { return &m_visual_settings; }


    void  renderer::on_settings_changed() { /* no derived state */ }


    std::string_view renderer::name() const { return m_shared->compiler_name(); }


    u64 renderer::implementation_hash() const { return m_shared->implementation_hash(); }


    u32 renderer::contract_version() const { return m_shared->contract_version(); }


    std::expected<GLT::render::compiled_shader, GLT::render::shader_compile_error>


    renderer::compile(std::string_view slang_source, GLT::asset::material::blend_mode blend) {
        return m_shared->compile(slang_source, blend);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void renderer::init_vulkan() {

        u32 count = 0;
        const char** ext = mp_window->get_required_render_extensions(&count);

        util::vulkan_builder builder;
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = util::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;
        builder.physical_device_features12.shaderSampledImageArrayNonUniformIndexing = true;
        for (u32 i = 0; i < count; i++)
            builder.instance_extensions.push_back(ext[i]);

        m_instance = builder.create_instance();
        m_surface = mp_window->create_vulkan_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);
        m_device = builder.create_device();
        m_queues = builder.get_queues();
        ASSERT(m_queues.graphics_queue, "", "Failed to select graphics queue");

        m_deletion_queue.setup(m_device);
        m_dev = new util::device(m_instance.instance_handle, m_device, m_physical_device);
    }


    void renderer::destroy_vulkan() {

        if (m_device)
            m_device.waitIdle();

        m_deletion_queue.shutdown();

        if (m_dev) {
            delete m_dev;
            m_dev = nullptr;
        }

        if (m_surface) {
            m_instance.instance_handle.destroySurfaceKHR(m_surface);
            m_surface = nullptr;
        }

        // device / instance destruction is handled by util::deletion_queue + instance_wrapper
        // dtor. Verify against your util::vulkan_builder implementation — if it doesn't own the
        // device, add explicit m_device.destroy() here.

        mp_window.reset();
    }

}
