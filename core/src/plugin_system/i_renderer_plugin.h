
#pragma once

#include <glm/glm.hpp>
#include <glm/vec4.hpp>

#include "plugin_interface.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Renderer capability flags (optional, can be used for feature queries)
    enum class renderer_feature : u8 {
        none                = 0,
        compute_shaders     = BIT(0),
        tessellation        = BIT(1),
        ray_tracing         = BIT(2),
        bindless_resources  = BIT(3),
    };


    enum class backend_api : u8 {
        vulkan = 0,
        open_gl,
        direct_x,
        metal,
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Core renderer plugin interface.
    //
    // This interface abstracts the low‑level graphics device, swapchain, and frame
    // lifecycle. It is intended to be implemented by concrete rendering backends
    // (e.g., Vulkan, Direct3D 12, Metal). The engine core uses this interface to
    // drive rendering without depending on a specific API.
    //
    // The renderer typically depends on a window plugin to obtain the native
    // window handle and to handle resizing. It can retrieve the window plugin
    // via plugin_manager::get_plugin<platform::i_window_plugin>
    // (targeted_interface::window) during on_load() or initialize().
    //
    // @see platform::i_window_plugin
    // @see targeted_interface::render_device
    class i_renderer_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_renderer_plugin() = default;

        // Initializes the rendering device and creates the swapchain.
        // Called after the plugin has been loaded (e.g., in on_load() or later when
        // the window is ready). The implementation should query the window plugin
        // for the native handle and framebuffer size.
        // @return true on success, false on failure.
        virtual bool create() = 0;


        // Destroys all rendering resources and shuts down the device.
        virtual void destroy() = 0;

        // --- frame control ---------------------------------------------------------

        // Begins a new frame.
        // Acquires the next swapchain image and records necessary commands to start
        // the frame (e.g., clearing the default framebuffer).
        virtual void begin_frame() = 0;


        // Ends the current frame.
        // render all 
        virtual void draw_frame() = 0;


        // Waits for the GPU to finish all submitted work.
        // Useful for safe resource destruction or benchmarking.
        virtual void wait_for_gpu() = 0;

        // --- swapchain & configuration ---------------------------------------------

        // Returns the current size of the swapchain images (in pixels).
        virtual glm::ivec2 get_swapchain_size() const = 0;


        // Resizes the swapchain.
        // Called automatically when the window is resized (the renderer should
        // observe the window resize events or the window plugin’s size).
        // @param width  New width in pixels.
        // @param height New height in pixels.
        virtual void resize(const u32 width, const u32 height) = 0;


        // Enables or disables vertical synchronisation.
        virtual void set_vsync(const bool enabled) = 0;


        // Returns the current VSync state.
        virtual bool get_vsync() const = 0;


        // Sets the clear colour used at the beginning of each frame.
        // @param color RGBA colour (each component in [0,1]).
        virtual void set_clear_color(const glm::vec4& color) = 0;

        // --- feature queries -------------------------------------------------------

        // Returns a bitmask of supported renderer features.
        virtual renderer_feature get_supported_features() const = 0;


        virtual backend_api get_backend_api() const = 0;

        // --- native access ---------------------------------------------------------

        // Returns a pointer to the underlying graphics API device.
        // Examples: VkDevice*, ID3D12Device*, MTLDevice*.
        // @return Opaque handle to the native device, or nullptr if not available.
        virtual void* get_native_device_handle() const = 0;


        // Returns a pointer to the native graphics context (e.g., VkInstance, ID3D12CommandQueue, MTLCommandQueue).
        virtual void* get_native_context_handle() const = 0;
        
    };

}
