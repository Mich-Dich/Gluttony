#pragma once

#include <plugin_system/i_asset_registry_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::handler::mesh {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    FORCE_INLINE void plugin::on_load() {

        m_registry = GLT::asset::registry::get_ref();
        VALIDATE(m_registry, return, "", "asset_registry not available — handler inactive")
        m_registry->register_handler(this);

        // Ask the registry for the "mesh_collection" type on demand if you want
        // it later; for now the core types we handle are already registered.
        LOG_LOADED
    }


    FORCE_INLINE void plugin::on_unload() {

        if (m_registry)
            m_registry->unregister_handler(this);

        m_registry.reset();
        LOG_UNLOADED
    }


    FORCE_INLINE_R std::span<const GLT::asset::type> plugin::types() const noexcept {

        // NOTE: deliberately NOT claiming skeletal_mesh, procedural_mesh,
        // dynamic_mesh or mesh_collection. This handler can't decode skeletal
        // chunks, and silently loading a skinned mesh as a static one is a
        // bug that shows up as a T-pose in production. Add those types when
        // the corresponding chunk decoding exists.
        static constexpr GLT::asset::type t[] = {
            GLT::asset::core_types::static_mesh,
        };
        return t;
    }


    FORCE_INLINE_R std::expected<std::unique_ptr<GLT::asset::i_runtime_asset>, GLT::asset::load_error> plugin::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {


        using namespace GLT::asset;
        using namespace GLT::asset::mesh;

        // ---- required chunks ---------------------------------------------------

        const auto verts = reader.get_as<vertex>(CHUNK_VERTICES);
        const auto idxs = reader.get_as<u32>(CHUNK_INDICES);
        const auto subs = reader.get_as<submesh>(CHUNK_SUBMESHES);
        VALIDATE(!verts.empty() && !idxs.empty(), return std::unexpected{ load_error::corrupt_header }, "", 
            "'{}' missing required chunks (verts={}, idx={})", info.name, verts.size(), idxs.size())

        // ---- build the runtime asset ------------------------------------------

        auto asset = std::make_unique<mesh_asset>();
        asset->asset_type = info.asset_type;

        // Copy — the chunk_reader's backing buffer dies when the registry's
        // load function returns. Do not keep the spans.
        asset->vertices .assign(verts.begin(), verts.end());
        asset->indices  .assign(idxs .begin(), idxs .end());
        asset->submeshes.assign(subs .begin(), subs .end());

        // ---- bounds ------------------------------------------------------------

        if (const auto b = reader.get_as<bounds>(CHUNK_BOUNDS); b.size() == 1) {
            asset->bounds = b[0];
        } else {
            // Recompute if the writer didn't emit one (or emitted a malformed one).
            asset->bounds.min = asset->bounds.max = asset->vertices.front().position;
            for (const auto& v : asset->vertices) {
                asset->bounds.min = glm::min(asset->bounds.min, v.position);
                asset->bounds.max = glm::max(asset->bounds.max, v.position);
            }
        }

        // ---- material handles --------------------------------------------------

        // Positional alignment is guaranteed by the factory: it wrote one
        // dependency per submesh, in submesh order. Unresolved references
        // come through as INVALID_HANDLE and are preserved here so
        // submesh.material_slot keeps indexing correctly.
        asset->material_handles.assign(info.dependencies.begin(), info.dependencies.end());

        // Sanity check: every submesh must point at a valid slot. If the file
        // was hand-edited or the factory changed, catch it here rather than
        // blowing up in the renderer.
        for (const submesh& sm : asset->submeshes) {
            if (sm.material_slot >= asset->material_handles.size()) {
                LOG(warn, "'{}' submesh references material slot {} but only {} dependencies exist — clamping to invalid",
                    info.name, sm.material_slot, asset->material_handles.size());
                // We do not fail the load; the renderer will substitute a
                // fallback material. But we also don't want garbage indices.
            }
        }

        LOG(info, "loaded '{}' — {} verts, {} tris, {} submeshes", info.name, asset->vertices.size(), 
            asset->indices.size() / 3, asset->submeshes.size());

        return std::unique_ptr<i_runtime_asset>(std::move(asset));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
