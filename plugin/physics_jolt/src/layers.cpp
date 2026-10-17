
#include "util/pch.h"
#include "util/jolt_pch.h"
#include "layers.h"



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

    layer_registry::layer_registry() {

        m_profiles.reserve(256);
        m_lookup.reserve(256);
    }

    // CLASS PUBLIC ====================================================================================================

    JPH::uint layer_registry::GetNumBroadPhaseLayers() const { return 1; }


    JPH::BroadPhaseLayer layer_registry::GetBroadPhaseLayer(JPH::ObjectLayer) const { return JPH::BroadPhaseLayer(0); }


    bool layer_registry::ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const {

        if (a == 0 || b == 0)
            return false;

        if (a > m_profiles.size() || b > m_profiles.size())
            return false;

        const auto& pa = m_profiles[a - 1];
        const auto& pb = m_profiles[b - 1];
        return (pa.collide_mask & pb.object_layer) != 0 && (pb.collide_mask & pa.object_layer) != 0;
    }


    JPH::ObjectLayer layer_registry::intern(const collision_profile& p) {

        const u64 k = key_of(p);
        if (auto it = m_lookup.find(k); it != m_lookup.end())
            return it->second;

        // ObjectLayer 0 is reserved for "no layer" / invalid bodies in our own
        // bookkeeping. Jolt is fine with 0 being used, but reserving it lets us
        // distinguish "unset" from "profile #0" in the body table.
        const auto id = static_cast<JPH::ObjectLayer>(m_profiles.size() + 1);
        m_profiles.push_back({ p.object_layer, p.collide_mask });
        m_lookup.emplace(k, id);
        return id;
    }


    u32 layer_registry::profile_count() const noexcept { return static_cast<u32>(m_profiles.size()); }


    u32 layer_registry::object_layer_of(JPH::ObjectLayer layer) const noexcept {

        if (layer == 0 || layer > m_profiles.size())
            return 0;
        return m_profiles[layer - 1].object_layer;
    }

    // layer_vs_broadphase ---------------------------------------------------------------------------------------------

    [[nodiscard]] bool layer_vs_broadphase::ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const { return true; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
