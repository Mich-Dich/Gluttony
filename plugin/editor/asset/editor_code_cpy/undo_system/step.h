
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #define CREATE_UNDO_ID(name, object)        mix_undo_id(hash_undo_id(name), (u64)(uintptr_t)object);

    #define UNDO_ID(name)                       ::GLT::editor::hash_undo_id(__FILE__ ":" __LINE__ ":" name)

    // TYPES ===========================================================================================================

    using undo_id = u64;


    struct step {

        using setter_fn = void(*)(void* object, const void* value);

        undo_id                                 id = 0;
        bool                                    can_combine = false;
        f32                                     timestamp = 0.0f;

        // --- Compact path ---------------------------------------------------
        void*                                   object = nullptr;
        setter_fn                               setter = nullptr;
        u8                                      size = 0;
        alignas(8)                              std::byte before[16]{};
        alignas(8)                              std::byte after [16]{};

        // --- Callback path (fallback for structural edits) ------------------
        std::function<void()>                   do_fn;
        std::function<void()>                   undo_fn;

        bool is_compact() const noexcept;

        void apply()  const;

        void revert() const;

        // Only called when ids match, both are compact, and both are combinable.
        void merge(const step& newer);
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // FNV-1a. Stable across runs, unlike pointers or std::hash.
    constexpr undo_id hash_undo_id(const char* s) noexcept;


    // Mix in a runtime discriminator (object pointer, name, index, ...).
    constexpr undo_id mix_undo_id(undo_id h, u64 discriminator) noexcept;

    // TEMPLATE DECLARATION ============================================================================================

    template <typename T>
    FORCE_INLINE_R step make_value_step(undo_id id, void* obj, step::setter_fn setter, const T& before, const T& after, 
        bool can_combine, f32 now);

    template <typename Do, typename Undo>
    FORCE_INLINE_R step make_callback_step(undo_id id, Do&& d, Undo&& u, f32 now);

    // CLASS DECLARATION ===============================================================================================

}

#include "step.inl"
