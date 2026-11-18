#pragma once

#include "procedural/construct/evaluate.h"
#include "asset/mesh.h"
#include "asset/asset_ref.h"

// Procedural construction -> static mesh asset: "Export static mesh" of the construct editors, and code that wants a
// baked copy of a construct (no evaluation at runtime). The mesh is a normal .glt_static_mesh: LODs, colliders and
// materials are then authored in the mesh editor like for any imported mesh.



namespace GLT::construct {

    // TYPES ===========================================================================================================

    struct static_mesh_options {

        std::vector<std::string>                        outputs{};          // outputs to include (empty: every render output)
        std::vector<output_purpose>                     purposes{ output_purpose::render };
    };

    // FUNCTION DECLARATION ============================================================================================

    // The outputs of [result] as one mesh: one submesh per material (core.material: "bark", "leaf", ...), relative to
    // the construct origin. Null if there is no geometry
    [[nodiscard]] GLT::unique_ref<GLT::asset::mesh::mesh_asset> to_static_mesh(const evaluation_result& result,
        const static_mesh_options& options, std::vector<std::string>& warnings);

    // Writes [mesh] to [content_path] (content-dir relative, ".glt_static_mesh"). A mesh already there (loaded or on
    // disk) gets the new geometry and keeps its id, so references to it stay valid; its LODs / clusters are dropped
    // (regenerate them in the mesh editor), its colliders and materials are kept
    [[nodiscard]] std::expected<GLT::asset::asset_ref, std::string> save_static_mesh(GLT::unique_ref<GLT::asset::mesh::mesh_asset> mesh,
        const std::filesystem::path& content_path);

}
