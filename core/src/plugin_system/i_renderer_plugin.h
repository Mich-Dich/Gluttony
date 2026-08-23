
#pragma once

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/vec4.hpp>

#include "i_plugin.h"

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
    // This interface abstracts the low‑level graphics device, swapchain, and
    // frame lifecycle. Concrete rendering backends (e.g., Direct3D 12, Metal)
    // implement this interface so that the engine core can drive rendering
    // without depending on a specific graphics API.
    //
    // The renderer typically depends on a window plugin to obtain the native
    // window handle and to handle resizing. It can retrieve the window plugin
    // via plugin_manager::get_plugin<platform::i_window_plugin>
    // (interface::window) during create().
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

        // @brief Creates and initialises all renderer resources.
        //
        // This is called after the plugin has been loaded and the window is
        // ready. The implementation should:
        // - Retrieve the window plugin and obtain the native surface/window handle.
        // - Create the graphics device, swapchain, and associated synchronisation
        //   objects.
        // - Create render targets, shader/pipeline state, and other GPU resources.
        // - Initialise any debug/UI overlays if used.
        // - Subscribe to window resize events if necessary.
        //
        // After this call returns true, the renderer is ready to process frames.
        //
        // @return true on success, false on failure.
        virtual bool create() = 0;


        // @brief Destroys all renderer resources and shuts down the device.
        //
        // The implementation must wait for all pending GPU work to finish before
        // releasing resources. It should unsubscribe from any events, destroy the
        // swapchain, render targets, pipelines, buffers, and any UI/overlay
        // resources, then release the native device and surface handles.
        virtual void destroy() = 0;

        // --- frame control ---------------------------------------------------------

        // @brief Begins recording a new frame.
        //
        // Acquires the next available swapchain image and prepares command buffers
        // for this frame. Typical responsibilities:
        // - Wait for the previous frame using the current frame index to finish.
        // - Acquire the next presentable image. If the swapchain is out of date,
        //   recreate it and return early.
        // - Begin the frame command buffer.
        // - Update per-frame uniform/constant data (e.g., camera matrices).
        // - Bind required resources and render targets.
        // - Clear the render target with the configured clear colour.
        // - Issue scene rendering commands into the offscreen render target.
        // - Prepare UI/overlay rendering for this frame.
        //
        // After this call, the frame is ready to be finished and presented by
        // draw_frame().
        virtual void begin_frame() = 0;


        // @brief Finishes and presents the current frame.
        //
        // Completes UI/overlay rendering, ends the frame command buffer, submits
        // all recorded commands to the graphics queue, and presents the swapchain
        // image. It should handle swapchain out-of-date conditions by recreating
        // the swapchain if necessary, then advance to the next frame index.
        virtual void draw_frame() = 0;


        // @brief Blocks until all pending GPU work has completed.
        //
        // This is useful when the application needs to safely destroy GPU
        // resources, read back data, or perform timing measurements without
        // interfering with in-flight work.
        virtual void wait_for_gpu() = 0;

        // --- swapchain & configuration ---------------------------------------------

        // @brief Returns the current size of the swapchain images in pixels.
        //
        // The returned size corresponds to the presentation surface size and may
        // differ from the internal render size set by set_render_size().
        //
        // @return Swapchain image dimensions in pixels.
        [[nodiscard]] virtual glm::ivec2 get_swapchain_size() const = 0;


        // @brief Resizes the swapchain and any size-dependent resources.
        //
        // Called when the window framebuffer size changes. The implementation
        // should recreate the swapchain at the new dimensions and rebuild
        // framebuffers or other resources that depend on the swapchain size.
        //
        // @param width  New width in pixels.
        // @param height New height in pixels.
        virtual void resize(const u32 width, const u32 height) = 0;


        // @brief Enables or disables vertical synchronisation.
        //
        // When enabled, presentation waits for the display refresh to avoid
        // tearing. When disabled, the implementation may use an immediate or
        // mailbox presentation mode. This may require recreating the swapchain
        // for the change to take effect.
        //
        // @param enabled True to enable vsync, false to disable.
        virtual void set_vsync(const bool enabled) = 0;


        // @brief Returns whether vertical synchronisation is currently enabled.
        //
        // @return True if vsync is enabled, false otherwise.
        [[nodiscard]] virtual bool get_vsync() const = 0;


        // @brief Sets the colour used to clear the render target at the start of
        // each frame.
        //
        // @param color RGBA colour with components in [0,1].
        virtual void set_clear_color(const glm::vec4& color) = 0;
        
        
        // @brief Sets the internal offscreen render resolution.
        //
        // This is the resolution at which the scene is rendered before it is
        // blitted or presented to the swapchain. It may be independent of the
        // swapchain size, allowing for render scaling.
        //
        // @param size Internal render target dimensions in pixels.
        virtual void set_render_size(const glm::ivec2& size) = 0;

        // --- feature queries -------------------------------------------------------

        // @brief Returns a bitmask of renderer capabilities supported by this
        // backend.
        //
        // The engine can query this value to determine which optional features are
        // available (e.g., ray tracing, mesh shaders, bindless resources).
        //
        // @return Bitmask of supported renderer features.
        [[nodiscard]] virtual renderer_feature get_supported_features() const = 0;


        // @brief Returns the graphics API backend implemented by this renderer.
        //
        // This allows the engine to identify the concrete backend without
        // exposing API-specific types.
        //
        // @return The backend API identifier.
        [[nodiscard]] virtual backend_api get_backend_api() const = 0;

        // --- native access ---------------------------------------------------------

        // @brief Returns a texture handle for the latest rendered image.
        //
        // The returned handle is intended for use with the engine's UI system to
        // display the rendered frame. The texture remains valid until the next
        // frame or until the renderer is destroyed, depending on the backend.
        //
        // @return Texture identifier for the current output image.
        [[nodiscard]] virtual ImTextureID get_rendered_image() = 0;


        // @brief Returns an opaque handle to the native graphics device.
        //
        // This is intended for advanced interop scenarios where the engine or
        // plugins need direct access to the graphics API device. The actual type
        // is backend-specific.
        //
        // @return Opaque pointer to the native device, or nullptr if unavailable.
        [[nodiscard]] virtual void* get_native_device_handle() const = 0;


        // @brief Returns an opaque handle to the native graphics context.
        //
        // The context may represent the API instance, command queue, or other
        // backend-specific object used to submit work. The exact type depends on
        // the backend implementation.
        //
        // @return Opaque pointer to the native context, or nullptr if unavailable.
        [[nodiscard]] virtual void* get_native_context_handle() const = 0;
        
    };

}
