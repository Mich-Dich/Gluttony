
#pragma once

#include <physics/shape.h>
#include "util/handle_table.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // What the plugin stores per shape_handle. Owns the compiled Jolt shape and the material + motion mask that create_body() reads back
    struct shape_entry {
        JPH::ShapeRefC                      shape;
        physics_material                    material;
        u8                                  motion_mask{ 0 };
        u8                                  _pad[3]{};

        #if !defined(NDEBUG)
            // Debug-only reference count; asserted on release_shape(). Contract: §Shape lifetime. Release builds pay zero cost for this
            u32                             debug_refcount{ 0 };
        #endif
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class shape_cache {
    public:

        [[nodiscard]] std::expected<shape_handle, physics_error> create(const shape_desc& desc);


        void release(shape_handle h) noexcept;


        [[nodiscard]] const shape_entry* get(shape_handle h) const noexcept;


        [[nodiscard]] shape_entry* get(shape_handle h) noexcept;


        [[nodiscard]] u32 size() const noexcept { return m_table.live_count(); }


        // Reference counting, only meaningful in debug builds. create_body() will call add_ref(); destroy_body() will call dec_ref()
        void add_ref(shape_handle h) noexcept;


        void dec_ref(shape_handle h) noexcept;


        void clear() noexcept;

    private:

        handle_table<shape_handle, shape_entry>             m_table;

    };

}
