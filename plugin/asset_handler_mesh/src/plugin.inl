#pragma once

#include <asset/i_asset_registry.h>



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
        VALIDATE(m_registry, return, "", "asset_registry not available - handler inactive")
        m_registry->register_handler(this);
        LOG_LOADED
    }


    FORCE_INLINE void plugin::on_unload() {

        if (m_registry)
            m_registry->unregister_handler(this);

        m_registry.reset();
        LOG_UNLOADED
    }


    FORCE_INLINE_R std::span<const GLT::asset::type> plugin::types() const noexcept {

        // deliberately NOT claiming skeletal_mesh, procedural_mesh, dynamic_mesh or mesh_collection
        static constexpr GLT::asset::type t[] = { GLT::asset::core_types::static_mesh, };
        return t;
    }


    FORCE_INLINE_R std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> plugin::deserialize(
        const GLT::asset::info& info, GLT::asset::chunk_reader& reader) {

        // ---- required chunks ----------------------------------------------------------------------------------------
        const auto verts = reader.get_as<GLT::asset::mesh::vertex>(GLT::asset::mesh::CHUNK_VERTICES);
        const auto idxs = reader.get_as<u32>(GLT::asset::mesh::CHUNK_INDICES);
        const auto subs = reader.get_as<GLT::asset::mesh::submesh>(GLT::asset::mesh::CHUNK_SUBMESHES);
        VALIDATE(!verts.empty() && !idxs.empty(), return std::unexpected{ load_error::corrupt_header }, 
            "", "[{}] missing required chunks (verts={}, idx={})", (info.name, verts.size(), idxs.size()))

        // ---- build the runtime asset --------------------------------------------------------------------------------
        auto asset = std::make_unique<GLT::asset::mesh::mesh_asset>();
        asset->asset_type = info.asset_type;

        // Copy - the chunk_reader's backing buffer dies when the registry's load function returns. Do not keep the spans.
        asset->vertices.assign(verts.begin(), verts.end());
        asset->indices.assign(idxs.begin(), idxs.end());
        asset->submeshes.assign(subs.begin(), subs.end());

        // ---- colliders (optional) -----------------------------------------------------------------------------------
        //
        // Absent when the asset was authored/imported without physics data, or written by a pre-collider factory.
        // Empty colliders is valid - it just means the mesh has no physics representation, and whoever builds the
        // physics body later will simply not create one.
        if (const auto cols = reader.get_as<GLT::asset::mesh::mesh_collider>(GLT::asset::mesh::CHUNK_COLLIDERS); !cols.empty()) {

            asset->colliders.assign(cols.begin(), cols.end());

            // Soft diagnostics - the runtime doesn't consume colliders yet, so a malformed entry would otherwise
            // silently turn into a "missing" collider once the physics bridge is wired up.
            for (u32 i = 0; i < asset->colliders.size(); ++i) {

                if (asset->colliders[i].shape == GLT::asset::mesh::collider_shape::triangle_mesh
                    && (asset->indices.empty() || asset->vertices.empty())) {

                    LOG(warn, "[{}] collider[{}] is triangle_mesh but the mesh has no geometry", info.name, i);
                }
            }
        }

        // ---- bounds -------------------------------------------------------------------------------------------------
        if (const auto bound = reader.get_as<GLT::AABB>(GLT::asset::mesh::CHUNK_BOUNDS); bound.size() == 1) {

            asset->bounds = bound[0];

        } else {

            // Recompute if the writer didn't emit one (or emitted a malformed one).
            asset->bounds.min = asset->bounds.max = asset->vertices.front().position;
            for (const auto& v : asset->vertices) {
                asset->bounds.min = glm::min(asset->bounds.min, v.position);
                asset->bounds.max = glm::max(asset->bounds.max, v.position);
            }
        }

        // ---- material handles ---------------------------------------------------------------------------------------

        // Positional: dependency [i] belongs to submesh [i]. The factory wrote one dependency per submesh, in submesh order
        // Unresolved references come through as INVALID_HANDLE and are preserved here so submesh.material still indexes correctly
        const size_t dep_count = info.dependencies.size();
        if (asset->submeshes.size() > dep_count) {
            LOG(warn, "[{}] {} submeshes but only {} dependency slots - remaining submeshes get the default material",
                info.name, asset->submeshes.size(), dep_count);
        }

        for (size_t i = 0; i < asset->submeshes.size(); ++i)
            asset->submeshes[i].material = (i < dep_count) ? info.dependencies[i] : INVALID_HANDLE;

        LOG(trace, "loaded [{}] - {} verts, {} tris, {} submeshes, {} colliders",
            info.name, asset->vertices.size(), asset->indices.size() / 3, asset->submeshes.size(),
            asset->colliders.size());

        return GLT::unique_ref<GLT::asset::i_runtime_asset>(std::move(asset));
    }


    FORCE_INLINE_R std::expected<void, GLT::asset::load_error> plugin::serialize(const GLT::asset::info& info,
        const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const {

        // ---- type check ----------------------------------------------------------------------------------------------
        //
        // The handler only claims static_mesh, so info.asset_type is authoritative and cheaper than a dynamic_cast.
        // If it doesn't match, the caller has handed us the wrong asset - bail rather than reinterpret bytes.
        if (info.asset_type != GLT::asset::core_types::static_mesh)
            return std::unexpected{ GLT::asset::load_error::corrupt_header };

        const auto& m = static_cast<const GLT::asset::mesh::mesh_asset&>(asset);

        // ---- validate required geometry ------------------------------------------------------------------------------
        //
        // deserialize() refuses to build an asset from empty verts/idxs, so emitting a file that can't be loaded is
        // strictly worse than failing the save. The index-range check catches the (rare but fatal) case of an
        // editor bug having produced an out-of-bounds triangle.
        if (m.vertices.empty() || m.indices.empty() || m.indices.size() % 3 != 0)
            return std::unexpected{ GLT::asset::load_error::corrupt_header };

        for (const u32 idx : m.indices) {
            if (idx >= m.vertices.size())
                return std::unexpected{ GLT::asset::load_error::corrupt_header };
        }

        // ---- geometry chunks ------------------------------------------------------------------------------------------
        //
        // Chunk layout mirrors deserialize() exactly: vertices, indices, submeshes, bounds, colliders. Byte-for-byte
        // trivially-copyable payloads -> std::as_bytes, so no per-field serialization here.
        out.write_chunk(GLT::asset::mesh::CHUNK_VERTICES, std::as_bytes(std::span{ m.vertices }));
        out.write_chunk(GLT::asset::mesh::CHUNK_INDICES,  std::as_bytes(std::span{ m.indices }));

        if (!m.submeshes.empty())
            out.write_chunk(GLT::asset::mesh::CHUNK_SUBMESHES, std::as_bytes(std::span{ m.submeshes }));

        // ---- bounds (single AABB, matches deserialize's size==1 check) ------------------------------------------------
        out.write_chunk(GLT::asset::mesh::CHUNK_BOUNDS, std::as_bytes(std::span{ &m.bounds, 1 }));

        // ---- colliders (optional) -------------------------------------------------------------------------------------
        //
        // Absent when the user authored no physics data. Emitting an empty chunk vs. omitting it are equivalent on the
        // read side (get_as returns an empty span either way), so skip the write - keeps the file smaller and makes
        // "no physics" visually obvious in a file dump.
        if (!m.colliders.empty())
            out.write_chunk(GLT::asset::mesh::CHUNK_COLLIDERS, std::as_bytes(std::span{ m.colliders }));

        // ---- material dependencies (one per submesh, positionally) ----------------------------------------------------
        //
        // deserialize() reads info.dependencies[i] for submesh[i].material, so save() must re-emit the exact same slot
        // layout: one UUID per submesh, in submesh order, INCLUDING slots whose handle never resolved. The runtime asset
        // only tracks handles, not UUIDs, so we recover the UUID from info.dependency_ids at the same position. This is
        // safe as long as material assignments weren't re-wired outside the registry (which would have invalidated the
        // graph anyway), so we warn rather than silently emit the wrong UUID.
        const size_t dep_count = info.dependency_ids.size();
        if (m.submeshes.size() > dep_count) {
            LOG(warn, "[{}] {} submeshes but only {} dependency ids - cold-start material resolution may fail for the tail",
                info.name, m.submeshes.size(), dep_count);
        }

        const bool handles_resolvable = info.dependencies.size() == info.dependency_ids.size();

        for (size_t i = 0; i < m.submeshes.size() && i < dep_count; ++i) {

            if (handles_resolvable && info.dependencies[i] != m.submeshes[i].material) {
                LOG(warn, "[{}] submesh[{}] material handle diverged from the registry snapshot - "
                    "emitting the original UUID; the reference may be stale", info.name, i);
            }

            out.declare_dependency(info.dependency_ids[i]);
        }

        // ---- name ------------------------------------------------------------------------------------------------------
        if (!info.name.empty())
            out.set_name(info.name);

        LOG(trace, "serialized [{}] - {} verts, {} tris, {} submeshes, {} colliders",
            info.name, m.vertices.size(), m.indices.size() / 3, m.submeshes.size(), m.colliders.size());

        return {};
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
