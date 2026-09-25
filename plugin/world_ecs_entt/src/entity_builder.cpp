
#include "util/pch.h"

#include "entity_builder.h"
#include "world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

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

    entity_builder::entity_builder(ecs_world_plugin& plugin, entity_id id) noexcept
        : m_plugin(&plugin), m_id(id) {}

    // CLASS PUBLIC ====================================================================================================

    entity_builder& entity_builder::named(std::string name) {

        name_component n{ std::move(name) };
        return add_or_replace<name_component>(std::move(n));
    }


    entity_builder& entity_builder::set_transform(const transform& t) { return add_or_replace<transform>(transform{ t }); }


    entity_builder& entity_builder::set_mesh(GLT::asset::handle mesh, bool visible) {

        mesh_renderer mesh_comp{};
        mesh_comp.mesh = mesh;
        mesh_comp.visible = visible;
        return add_or_replace<mesh_renderer>(std::move(mesh_comp));
    }


    entity_builder& entity_builder::set_audio(GLT::asset::handle clip, const GLT::asset::audio::source_config& cfg, bool autoplay) {

        audio_source audio_comp{};
        audio_comp.clip = clip;
        audio_comp.config = cfg;
        audio_comp.autoplay = autoplay;
        return add_or_replace<audio_source>(std::move(audio_comp));
    }


    entity_builder& entity_builder::parent(entity_id p) {

        m_plugin->set_parent(m_id, p);
        return *this;
    }


    entity_builder& entity_builder::detach() {

        m_plugin->set_parent(m_id, INVALID_ENTITY);
        return *this;
    }


    entity_builder& entity_builder::adopt(entity_id child) {

        m_plugin->set_parent(child, m_id);
        return *this;
    }


    entity_builder& entity_builder::disown(entity_id child) {

        // set_parent handles removing `child` from this entity's children list.
        m_plugin->set_parent(child, INVALID_ENTITY);
        return *this;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
