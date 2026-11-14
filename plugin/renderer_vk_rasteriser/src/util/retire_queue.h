#pragma once

#include "util/pch.h"
#include <vulkan/vulkan.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Deferred destruction of GPU objects that may still be referenced by in-flight command buffers
    //
    // Built on a single timeline semaphore that every frame submission on the graphics queue signals with a strictly
    // increasing value (see `next_signal_value()`). `retire()` tags the destroy callback with the last value handed out:
    // all work that could reference the object was submitted at or before that value. `collect()` runs every callback
    // whose value the GPU has reached. No waitIdle, no frame-count guessing, works with any number of scene renderers
    //
    // Threading: main/update thread only (same as the rest of the renderer)
    class retire_queue {
    public:

        retire_queue() = default;
        ~retire_queue();

        retire_queue(const retire_queue&) = delete;
        retire_queue& operator=(const retire_queue&) = delete;

        void init(vk::Device device);

        // Runs every pending callback. Caller must have waited for the device to be idle
        void shutdown();

        // Timeline semaphore to signal from every queue submission
        [[nodiscard]] vk::Semaphore timeline() const { return m_timeline; }

        // Reserve the value the next submission must signal on `timeline()`
        [[nodiscard]] u64 next_signal_value() { return ++m_last_submitted; }

        // Queue [fn] to run once the GPU has finished everything submitted so far
        void retire(std::function<void()>&& fn);

        // Run every callback whose submission has completed. Cheap when nothing is pending
        void collect();

        // Run everything now. Caller must have waited for the device to be idle
        void flush();

        [[nodiscard]] u64 completed_value() const;
        [[nodiscard]] size_t pending_count() const { return m_pending.size(); }

    private:

        struct entry {
            u64                                 value = 0;
            std::function<void()>               fn{};
        };

        vk::Device                              m_device = nullptr;
        vk::Semaphore                           m_timeline = nullptr;
        u64                                     m_last_submitted = 0;
        std::deque<entry>                       m_pending{};        // values are non-decreasing front to back
    };

}
