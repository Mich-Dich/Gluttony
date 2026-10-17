
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Index + generation table. Index 0 is reserved as always-invalid; generation starts at 1 on first allocate and increments
    // on every subsequent allocate of the same slot, so a stale handle never matches a live one
    template<typename Handle, typename Payload>
    class handle_table {
    public:

        static constexpr u32 INVALID_INDEX = 0;

        handle_table();


        [[nodiscard]] Handle insert(Payload load);


        [[nodiscard]] Payload* get(Handle h);


        [[nodiscard]] const Payload* get(Handle h) const;


        [[nodiscard]] bool contains(Handle h) const;


        void remove(Handle h);


        [[nodiscard]] u32 live_count() const noexcept;


        template<typename F>
        void for_each(F&& f) const;

    private:

        struct slot {
            u32                             generation{ 0 };
            u32                             next_free{ INVALID_INDEX };
            bool                            live{ false };
            Payload                         payload{};
        };

        std::vector<slot>                   m_slots{};
        u32                                 m_free_head { INVALID_INDEX };
        u32                                 m_live_count{ 0 };

    };

}

#include "handle_table.inl"
