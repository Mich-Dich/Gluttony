
#pragma once

#include <physics/type.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Implements the two Jolt filter interfaces. Each unique (object_layer, collide_mask) pair is interned to a small u16 
    // ObjectLayer id; the pair filter then applies the contract's symmetric check:
    //
    //   (a.collide_mask & b.object_layer) != 0
    //   (b.collide_mask & a.object_layer) != 0
    //
    // All calls happen on the physics thread. Insertions only happen from create_body(), which is also physics-thread-only,
    // so no locking is needed
    class layer_registry final : public JPH::BroadPhaseLayerInterface, public JPH::ObjectLayerPairFilter {
    public:

        layer_registry();


        // BroadPhaseLayerInterface
        [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override;


        [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override;


        #if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "default"; }
        #endif


        // ObjectLayerPairFilter
        [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override;


        // Intern a profile and return its Jolt ObjectLayer. Idempotent.
        [[nodiscard]] JPH::ObjectLayer intern(const collision_profile& profile);


        [[nodiscard]] u32 profile_count() const noexcept;


        [[nodiscard]] u32 object_layer_of(JPH::ObjectLayer l) const noexcept;

    private:

        struct entry { u32 object_layer; u32 collide_mask; };

        // The key packs both fields of the profile into a single u64.
        static u64 key_of(const collision_profile& p) noexcept { return (static_cast<u64>(p.collide_mask) << 32) | p.object_layer; }

        std::vector<entry>                                  m_profiles;   // index == ObjectLayer
        std::unordered_map<u64, JPH::ObjectLayer>           m_lookup;

    };


    class layer_vs_broadphase final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:

        [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override;

    };

}
