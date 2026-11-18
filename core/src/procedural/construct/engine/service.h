#pragma once

#include "procedural/construct/evaluate.h"

#include <future>
#include "render/procedural_mesh.h"

// The engine side of procedural construction: one registry for the whole process, evaluation on the thread pool,
// publishing outputs to the procedural mesh store. Plugins (construct_plant, construct_spaceship, terrain, game code)
// use these functions; they never own the system, they add packs to it.
//
//     construct::register_pack<^^my_pack>(construct::packs(), GLT_MODULE_NAME, { .name = "my_pack" });   // on_load
//     construct::packs().remove_owner(GLT_MODULE_NAME);                                                  // on_unload
//
// A plugin that unloads must first let its evaluations finish (GLT::thread_pool::wait_for_all()): they run its code.



namespace GLT::construct {

    // CONSTANTS =======================================================================================================

    // Owner of everything the core registers (the "core" and "demo" packs)
    inline constexpr std::string_view                   CORE_OWNER = "core";

    // TYPES ===========================================================================================================

    struct publish_settings {

        GLT::render::mesh_update_class                  update_class = GLT::render::mesh_update_class::occasional;
        GLT::render::procedural_detail                  detail = GLT::render::procedural_detail::automatic;
        // Material of a group (core.material name of its faces). Unset / INVALID_HANDLE: the default material
        std::function<GLT::asset::handle(name_id)>      material_for{};
        // Only these purposes become procedural meshes (collision outputs are for physics, not drawing)
        std::vector<output_purpose>                     purposes{ output_purpose::render };
        std::string                                     debug_name{ "construct" };
    };


    // @brief One procedural mesh made from one output part
    struct published_mesh {

        std::string                                     output{};
        element_path                                    element{};          // the part's element (empty for merged parts)
        u32                                             occurrence = 0;     // parts of one element in one output (as_realized)
        GLT::render::procedural_mesh_id                 mesh = GLT::render::INVALID_PROCEDURAL_MESH;
    };

    // FUNCTION DECLARATION ============================================================================================

    // The process-wide registry. The core and demo packs are registered on first use
    [[nodiscard]] registry& packs();

    // Evaluate against the current registry snapshot: on the calling thread / on GLT::thread_pool
    [[nodiscard]] evaluation_result evaluate(const evaluation_request& request);
    [[nodiscard]] std::future<evaluation_result> evaluate_async(evaluation_request request);

    // Output parts -> procedural meshes (one per part, one submesh per material group). Any thread.
    // [reuse]: meshes of an earlier publish of the same construct, matched by (output, element, occurrence) and updated in
    // place; the rest are destroyed. Returns what exists afterwards
    [[nodiscard]] std::vector<published_mesh> publish(const evaluation_result& result, const publish_settings& settings,
                                                      std::span<const published_mesh> reuse = {});

    // Destroys published meshes (procedural mesh store)
    void unpublish(std::span<const published_mesh> meshes);

    // One output part as procedural mesh data: one submesh per material group, tangents from the UVs. Groups beyond
    // PROCEDURAL_MAX_SUBMESHES are folded into the last submesh (reported in [warnings])
    [[nodiscard]] GLT::render::procedural_mesh_data to_procedural_mesh_data(const output_part& part,
        const std::function<GLT::asset::handle(name_id)>& material_for, std::vector<std::string>& warnings);

}
