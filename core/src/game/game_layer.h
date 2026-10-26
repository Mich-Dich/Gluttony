
#pragma once

#include <vector>

#include <layer/layer.h>
#include "game/game_session.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::game {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Owns every game_session. Inserted into the layer_stack once, right after the application boots. The primary session is created
    // eagerly and never destroyed by "Stop" — it is the editor's persistent world. Sim sessions come and go
    class game_layer final : public layer {
    public:

        game_layer();
        ~game_layer() override;

        // ---- sessions -------------------------------------------------------
        [[nodiscard]] game_session* create_session();
        void destroy_session(session_id id);

        [[nodiscard]] game_session* get_get_session(session_id id) const noexcept;
        [[nodiscard]] game_session* get_primary_session() const noexcept;

        // Materialized snapshot of every live session. Allocated on call, fine for the editor / debug UI, do not call per-frame in a hot path
        [[nodiscard]] std::vector<game_session*> get_sessions() const;

        // ---- layer ----------------------------------------------------------
        void update(f32 delta_time) override;
        void render_imgui(f32 /* delta_time */) override {}

    private:

        struct slot {
            unique_ref<game_session>                    session;
            u32                                         generation{ 0 };
        };

        [[nodiscard]] slot* find_slot(session_id id) noexcept;
        [[nodiscard]] const slot* find_slot(session_id id) const noexcept;

        std::vector<slot>                               m_slots{};
        std::vector<u32>                                m_free_slots{};
        session_id                                      m_primary_id{};
    };

}
