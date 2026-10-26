#include "util/pch.h"
#include "util/util_pch.h"
#include "plugin/plugin.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "platform/i_window.h"
#include "reflection/registry.h"
#include "render/image.h"
#include "image.h"

// GLFW is the only backend we implement surface creation for. The window plugin gives us a native
// window handle as void*; we cast it to the concrete type based on the backend it reports
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static vk::SurfaceKHR create_surface_from_native(vk::Instance instance, void* native,
        GLT::platform::backend_api backend);

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static vk::SurfaceKHR create_surface_from_native(vk::Instance instance, void* native,
        GLT::platform::backend_api backend) {

        if (!instance || !native)
            return nullptr;

        switch (backend) {
            case GLT::platform::backend_api::glfw: {

                VkSurfaceKHR raw = VK_NULL_HANDLE;
                const VkResult r = glfwCreateWindowSurface(
                    static_cast<VkInstance>(instance),
                    static_cast<GLFWwindow*>(native),
                    nullptr,
                    &raw);

                VALIDATE(r == VK_SUCCESS, return nullptr, "",
                    "glfwCreateWindowSurface failed with error {}", (static_cast<int>(r)))

                return vk::SurfaceKHR(raw);
            }
            case GLT::platform::backend_api::sdl:
                LOG(error, "SDL surface creation is not implemented for the Vulkan rasterizer")
                return nullptr;
        }
        return nullptr;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    void renderer::on_load() {

        // Register the image factory (used by external consumers of `render::image`)
        GLT::render::image::factory_table table{};
        table.default_fn = []() -> GLT::unique_ref<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>();
        };
        table.size_fn = [](const glm::uvec3& size) -> GLT::unique_ref<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(size);
        };
        table.data_fn = [](const void* data, const u32 width, const u32 height, const bool mipmapped = false)
            -> GLT::unique_ref<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(data, width, height, mipmapped);
        };
        table.path_fn = [](const std::filesystem::path& path, bool mipmapped)
            -> GLT::unique_ref<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(path, mipmapped);
        };
        GLT::render::image::register_factory(table);

        // Register the visual settings type so the editor can walk it
        GLT::reflect::register_type<settings::visual>();
        GLT::reflect::finalize();
    }


    void renderer::on_unload() { GLT::render::image::register_factory({}); }


    bool renderer::create(GLT::platform::window_handle primary_window) {

        mp_window = GLT::platform::window::get_ptr();
        ASSERT(mp_window, "", "Failed to get window plugin")
        ASSERT(primary_window.is_valid() && mp_window->is_valid(primary_window), "",
            "Renderer::create requires a valid primary window handle")

        m_primary_window = primary_window;

        init_vulkan();

        m_shared = GLT::create_unique_ref<shared_resource_manager>(m_device, m_physical_device, m_dev,
            m_queues.graphics_queue, m_queues.graphics_index);
        m_shared->create();

        m_framebuffer_resize_sub = GLT::event_bus::subscribe<GLT::window_framebuffer_resize_event>(
            [](const GLT::window_framebuffer_resize_event& /*event*/) {
                // No-op observer. Each surface scene renderer detects its own window's resize via
                // render() returning target_out_of_date, then the app calls scene_renderer::resize()
                // Left here as a hook for future diagnostics / dirty tracking
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


    std::expected<GLT::unique_ref<GLT::render::i_scene_renderer>, GLT::render::scene_renderer_error> renderer::create_scene_renderer(
        const GLT::render::scene_renderer_desc& desc) {

        // Validate surface targets up front — cheaper than doing it inside scene_renderer::create()
        if (std::holds_alternative<GLT::render::surface_target_desc>(desc.target)) {

            const auto& surface = std::get<GLT::render::surface_target_desc>(desc.target);
            if (!surface.window.is_valid() || !mp_window || !mp_window->is_valid(surface.window)) {
                return std::unexpected(GLT::render::scene_renderer_error{
                    GLT::render::scene_renderer_error::code::target_invalid,
                    "surface target references an invalid or destroyed window handle"
                });
            }
        }

        auto result = scene_renderer::create(*this, desc);
        if (!result)
            return std::unexpected(result.error());

        GLT::unique_ref<GLT::render::i_scene_renderer> base = std::move(*result);
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

    // window <-> surface registry -------------------------------------------------------------------------------------

    vk::SurfaceKHR renderer::get_or_create_surface(GLT::platform::window_handle handle) {

        if (!handle.is_valid() || !m_instance.instance_handle)
            return nullptr;

        // Window may have been destroyed since the last call. Clean up any stale surface in that case
        if (!mp_window || !mp_window->is_valid(handle)) {

            if (auto it = m_surfaces.find(handle); it != m_surfaces.end()) {
                if (it->second.surface)
                    m_instance.instance_handle.destroySurfaceKHR(it->second.surface);
                m_surfaces.erase(it);
            }
            LOG(warn, "get_or_create_surface called with invalid window [{}, {}]", handle.index, handle.generation)
            return nullptr;
        }

        if (auto it = m_surfaces.find(handle); it != m_surfaces.end()) {
            it->second.refcount++;
            return it->second.surface;
        }

        void* native = mp_window->get_native_window_handle(handle);
        if (!native) {
            LOG(error, "Cannot create surface: null native handle for window [{}, {}]",
                handle.index, handle.generation)
            return nullptr;
        }

        const vk::SurfaceKHR surface = create_surface_from_native(m_instance.instance_handle, native,
            mp_window->get_backend_api());

        if (!surface) {
            LOG(error, "Surface creation failed for window [{}, {}]", handle.index, handle.generation)
            return nullptr;
        }

        m_surfaces[handle] = surface_entry{ surface, 1 };
        LOG(trace, "created surface for window [{}, {}]", handle.index, handle.generation)
        return surface;
    }


    void renderer::release_surface(GLT::platform::window_handle handle) {

        auto it = m_surfaces.find(handle);
        if (it == m_surfaces.end())
            return;

        if (--it->second.refcount > 0)
            return;

        if (it->second.surface && m_instance.instance_handle)
            m_instance.instance_handle.destroySurfaceKHR(it->second.surface);

        LOG(trace, "destroyed surface for window [{}, {}]", handle.index, handle.generation)
        m_surfaces.erase(it);
    }

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

        // We need one surface for device selection (present support query). Use the primary window
        // The surface stays alive until renderer destruction so the picked device/queue stays valid
        const vk::SurfaceKHR primary_surface = get_or_create_surface(m_primary_window);
        ASSERT(primary_surface, "", "Failed to create the primary surface for device selection")

        m_physical_device = builder.pick_physical_device(primary_surface);
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

            // char* stats = nullptr;
            // vmaBuildStatsString(m_dev->get_allocator(), &stats, VK_TRUE);
            // LOG(warn, "VMA stats before device teardown:\n{}", stats);
            // vmaFreeStatsString(m_dev->get_allocator(), stats);
            delete m_dev;
            m_dev = nullptr;
        }

        // Destroy every remaining surface. By the time we get here all scene renderers have been destroyed and released their refs,
        // so this covers the renderer's own primary surface and any stragglers (should be zero in normal flow)
        for (auto& [handle, entry] : m_surfaces) {
            if (entry.surface && m_instance.instance_handle)
                m_instance.instance_handle.destroySurfaceKHR(entry.surface);
        }
        m_surfaces.clear();

        // device / instance destruction is handled by util::deletion_queue + instance_wrapper dtor. Verify against your
        // util::vulkan_builder implementation — if it doesn't own the device, add explicit m_device.destroy() here

        mp_window = nullptr;
    }

}
