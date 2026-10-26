
#pragma once

#include "util/pch.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/vec4.hpp>

#include "plugin_system/plugin_manager.h"
#include "plugin_system/i_plugin.h"
#include "render/image.h"
#include "render/render_mode.h"
#include "render/i_shader_compiler.h"
#include "render/i_scene_renderer.h"
#include "debug/profiler.h"
#include "asset/mesh.h"
#include "asset/material.h"
#include "world/camera_snapshot.h" 
#include "world/light_snapshot.h"
#include "reflection/registry.h"
#include "render/type.h"
#include "platform/type.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class camera;
}

namespace GLT::render {
    class i_renderer_plugin;
}

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace renderer {

        FORCE_INLINE_R ref<GLT::render::i_renderer_plugin> get_ref() {

            return plugin_manager::get_plugin_ref_unsafe<render::i_renderer_plugin>(plugin_manager::interface::renderer);
        }


        FORCE_INLINE_R GLT::render::i_renderer_plugin* get_ptr() {

            return plugin_manager::get_plugin_raw<render::i_renderer_plugin>(plugin_manager::interface::renderer);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Core renderer plugin interface
    //
    // This interface abstracts the low‑level graphics device, swapchain, and frame lifecycle. Concrete rendering backends
    // (e.g., Direct3D 12, Metal) implement this interface so that the engine core can drive rendering without depending on
    // a specific graphics API
    //
    // The renderer typically depends on a window plugin to obtain the native window handle and to handle resizing
    // It can retrieve the window plugin via plugin_manager::get_plugin<platform::i_window_plugin> (interface::window) during create()
    //
    // Typical lifecycle:
    //   1. create()
    //   2. begin_frame() / draw_frame() per frame
    //   3. wait_for_gpu() when needed
    //   4. destroy()
    //
    // @see platform::i_window_plugin
    // @see interface::render_device
    class i_renderer_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_renderer_plugin() = default;

        // @brief Creates and initialises all renderer resources
        //
        // [primary_window] is used for device + queue family selection (Vulkan requires *one* surface to
        // query present support). Subsequent windows are handled lazily via
        // [create_scene_renderer] with a [surface_target_desc] holding their own [window_handle]
        //
        // @param primary_window  A valid window handle; the plugin may create additional surfaces later
        // @return true on success, false on failure
        virtual bool create(GLT::platform::window_handle primary_window) = 0;


        // @brief Destroys all renderer resources and shuts down the device
        //
        // The implementation must wait for all pending GPU work to finish before releasing resources. It should unsubscribe
        // from any events, destroy the swapchain, render targets, pipelines, buffers, and any UI/overlay resources, then release
        // the native device and surface handles
        virtual void destroy() = 0;

        // --- feature queries -------------------------------------------------------

        virtual void begin_frame(const u64 frame_number) = 0;


        [[nodiscard]] virtual std::expected<GLT::unique_ref<i_scene_renderer>, scene_renderer_error> create_scene_renderer(
            const GLT::render::scene_renderer_desc& desc) = 0;


        // @brief Returns a bitmask of renderer capabilities supported by this backend
        //
        // The engine can query this value to determine which optional features are available (e.g., ray tracing, mesh shaders,
        // bindless resources)
        //
        // @return Bitmask of supported renderer features
        [[nodiscard]] virtual renderer_feature get_supported_features() const = 0;


        // @brief Returns the graphics API backend implemented by this renderer
        //
        // This allows the engine to identify the concrete backend without exposing API-specific types
        //
        // @return The backend API identifier
        [[nodiscard]] virtual backend_api get_backend_api() const = 0;

        // --- native access ---------------------------------------------------------

        // @brief Returns an opaque handle to the native graphics device
        //
        // This is intended for advanced interop scenarios where the engine or plugins need direct access to the graphics API device
        // The actual type is backend-specific
        //
        // @return Opaque pointer to the native device, or nullptr if unavailable
        [[nodiscard]] virtual void* get_native_device_handle() const = 0;


        // @brief Returns an opaque handle to the native graphics context
        //
        // The context may represent the API instance, command queue, or other backend-specific object used to submit work
        // The exact type depends on the backend implementation
        //
        // @return Opaque pointer to the native context, or nullptr if unavailable
        [[nodiscard]] virtual void* get_native_context_handle() const = 0;


        [[nodiscard]] virtual debug::render_stats get_render_stats() const = 0;

        // render output modes -----------------------------------------------------------------------------------------

        // @brief Returns every render output mode this renderer supports
        //
        // The editor queries this to build the mode selector; the renderer is free to return whatever modes it wants, in
        // whatever order. Ids are stable per renderer but not required to be contiguous
        //
        // @return Span over the renderer's static mode table. Valid for the plugin's lifetime
        [[nodiscard]] virtual std::span<const render_mode_info> supported_modes() const = 0;

        // settings ----------------------------------------------------------------------------------------------------

        // @brief Returns the reflection descriptor of the renderer's tunable settings
        //
        // Returns nullptr if the renderer exposes no settings. The editor pairs this with [settings_data()] to render
        // a generic panel: it walks the descriptor's members, dispatches on each member's [type_kind], and writes through
        // the pointer returned by [settings_data()]. [on_settings_changed()] is called once per frame after any edit
        [[nodiscard]] virtual const reflect::type_descriptor* settings_descriptor() const = 0;


        // @brief Returns a mutable pointer to the renderer's live settings instance
        //
        // The pointer is stable for the plugin's lifetime. Writes take effect on the next frame; the editor calls
        // [on_settings_changed()] after each batch of edits
        [[nodiscard]] virtual void* settings_data() = 0;


        // @brief Notifies the renderer that one or more settings changed
        //
        // The renderer uses this to invalidate any state derived from the changed values (typically: reset temporal
        // accumulation, re-upload constants, invalidate caches)
        virtual void on_settings_changed() = 0;

    };

}
