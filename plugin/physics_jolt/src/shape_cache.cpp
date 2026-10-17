
#include "util/pch.h"
#include "util/jolt_pch.h"
#include "shape_cache.h"
#include "shape_builder.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

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

    std::expected<shape_handle, physics_error> shape_cache::create(const shape_desc& desc) {

        auto built = build_shape(desc);
        if (!built)
            return std::unexpected(built.error());

        shape_entry entry{
            .shape = std::move(built->shape),
            .material = desc.material,
            .motion_mask = built->motion_mask,
        };
        return m_table.insert(std::move(entry));
    }


    void shape_cache::release(shape_handle h) noexcept {

        auto* entry = m_table.get(h);
        if (!entry)
            return;      // invalid or already released — no-op

        #if !defined(NDEBUG)
            assert(entry->debug_refcount == 0 && "release_shape on a shape still referenced by a live body");
        #endif

        m_table.remove(h);
    }


    const shape_entry* shape_cache::get(shape_handle h) const noexcept { return m_table.get(h); }


    shape_entry* shape_cache::get(shape_handle h) noexcept { return m_table.get(h); }


    void shape_cache::add_ref(shape_handle h) noexcept {

        #if !defined(NDEBUG)
            if (auto* entry = m_table.get(h)) 
                ++entry->debug_refcount;
        #endif
    }


    void shape_cache::dec_ref(shape_handle h) noexcept {

        #if !defined(NDEBUG)
            if (auto* entry = m_table.get(h)) {
                assert(entry->debug_refcount > 0);
                --entry->debug_refcount;
            }
        #endif
    }


    void shape_cache::clear() noexcept {
        // Reassign to a fresh table. The old table's slots are destroyed as the
        // move-assign replaces the vector, releasing every ShapeRefC correctly.
        m_table = handle_table<shape_handle, shape_entry>{};
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
