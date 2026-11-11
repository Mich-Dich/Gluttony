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


    // Out-of-range indices and broken submesh ranges have been seen in saved files (stealth_ship: the last two indices
    // and the first submesh's {first_index, index_count} overwritten with allocator bookkeeping - the in-memory mesh was
    // read after its buffers had been freed somewhere). An index past the vertex range makes the GPU fetch another mesh's
    // or uninitialised vertices: a huge black (NaN shaded) triangle. Repair what can be repaired and say so; re-saving
    // the asset writes the repaired data
    static void repair_geometry(const GLT::asset::info& info, GLT::asset::mesh::mesh_asset& mesh) {

        auto& indices = mesh.indices;
        auto& submeshes = mesh.submeshes;
        const u32 vertex_count = static_cast<u32>(mesh.vertices.size());

        if (indices.size() % 3 != 0) {
            LOG(warn, "[{}] index count {} is not a multiple of 3 - truncating", info.name, indices.size());
            indices.resize(indices.size() - indices.size() % 3);
        }

        // Triangles referencing a vertex that does not exist -> degenerate (dropped by the rasterizer)
        u32 bad_triangles = 0;
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            u32* tri = &indices[t];
            if (tri[0] < vertex_count && tri[1] < vertex_count && tri[2] < vertex_count)
                continue;
            const u32 keep = tri[0] < vertex_count ? tri[0] : (tri[1] < vertex_count ? tri[1] : (tri[2] < vertex_count ? tri[2] : 0u));
            tri[0] = tri[1] = tri[2] = keep;
            ++bad_triangles;
        }

        // Submesh ranges: the factory writes them back to back in index order. A range that is out of bounds (or empty
        // while its slot in the chain has indices) is rebuilt from its neighbours: [end of previous, start of next)
        const u64 index_count = indices.size();
        auto valid = [&](const GLT::asset::mesh::submesh& sm) {
            return sm.index_count > 0 && static_cast<u64>(sm.first_index) + sm.index_count <= index_count;
        };
        u32 repaired_submeshes = 0;
        for (size_t i = 0; i < submeshes.size(); ++i) {

            const u64 start = (i == 0) ? 0 : (valid(submeshes[i - 1]) ? static_cast<u64>(submeshes[i - 1].first_index) + submeshes[i - 1].index_count : UINT64_MAX);
            const u64 end = (i + 1 == submeshes.size()) ? index_count : (valid(submeshes[i + 1]) ? submeshes[i + 1].first_index : UINT64_MAX);
            const bool chained = start != UINT64_MAX && end != UINT64_MAX && start < end && end <= index_count && (end - start) % 3 == 0;

            if (valid(submeshes[i])) {
                if (submeshes[i].index_count % 3 != 0)
                    submeshes[i].index_count -= submeshes[i].index_count % 3;
                continue;
            }
            if (!chained) {
                submeshes[i].first_index = 0;                   // unrecoverable: draw nothing rather than garbage
                submeshes[i].index_count = 0;
                continue;
            }
            submeshes[i].first_index = static_cast<u32>(start);
            submeshes[i].index_count = static_cast<u32>(end - start);
            ++repaired_submeshes;
        }

        if (bad_triangles > 0 || repaired_submeshes > 0)
            LOG(warn, "[{}] repaired corrupt geometry: {} triangle(s) with out-of-range indices removed, {} submesh range(s) "
                "rebuilt. Save the asset to write the repaired data", info.name, bad_triangles, repaired_submeshes);
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
        repair_geometry(info, *asset);

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

        // ---- level of detail (optional) ------------------------------------------------------------------------------
        //
        // All four chunks or none. Anything inconsistent (ranges not matching levels x submeshes, ranges pointing past the
        // index stream) drops the generated levels: the mesh still renders, at LOD 0
        if (const auto settings = reader.get_as<GLT::asset::mesh::lod_settings>(GLT::asset::mesh::CHUNK_LOD_SETTINGS); settings.size() == 1)
            asset->lod = settings[0];

        {
            const auto levels = reader.get_as<GLT::asset::mesh::lod_level>(GLT::asset::mesh::CHUNK_LOD_LEVELS);
            const auto ranges = reader.get_as<GLT::asset::mesh::lod_range>(GLT::asset::mesh::CHUNK_LOD_RANGES);
            const auto lod_idx = reader.get_as<u32>(GLT::asset::mesh::CHUNK_LOD_INDICES);
            const auto lod_verts = reader.get_as<GLT::asset::mesh::vertex>(GLT::asset::mesh::CHUNK_LOD_VERTICES);   // optional

            bool valid = levels.size() > 1 && levels.size() <= GLT::asset::mesh::MAX_LODS
                && ranges.size() == levels.size() * asset->submeshes.size();

            const u64 stream_size = asset->indices.size() + lod_idx.size();
            for (size_t i = 0; valid && i < ranges.size(); ++i)
                valid = static_cast<u64>(ranges[i].first_index) + ranges[i].index_count <= stream_size;
            const u64 vertex_stream = asset->vertices.size() + lod_verts.size();
            for (size_t i = 0; valid && i < lod_idx.size(); ++i)
                valid = lod_idx[i] < vertex_stream;

            if (valid) {
                asset->lods.assign(levels.begin(), levels.end());
                asset->lod_ranges.assign(ranges.begin(), ranges.end());
                asset->lod_indices.assign(lod_idx.begin(), lod_idx.end());
                asset->lod_vertices.assign(lod_verts.begin(), lod_verts.end());
            } else if (!levels.empty()) {
                LOG(warn, "[{}] LOD chunks are inconsistent ({} levels, {} ranges, {} indices) - using LOD 0 only",
                    info.name, levels.size(), ranges.size(), lod_idx.size());
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

        LOG(trace, "loaded [{}] - {} verts, {} tris, {} submeshes, {} colliders, {} LODs",
            info.name, asset->vertices.size(), asset->indices.size() / 3, asset->submeshes.size(),
            asset->colliders.size(), asset->lod_count());

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

        // ---- level of detail ------------------------------------------------------------------------------------------
        //
        // Settings are always written (they are the user's authoring choice even when no levels were generated). The
        // generated data only when consistent, otherwise deserialize() would drop it anyway
        out.write_chunk(GLT::asset::mesh::CHUNK_LOD_SETTINGS, std::as_bytes(std::span{ &m.lod, 1 }));
        if (m.lod_count() > 1) {

            out.write_chunk(GLT::asset::mesh::CHUNK_LOD_LEVELS,  std::as_bytes(std::span{ m.lods }));
            out.write_chunk(GLT::asset::mesh::CHUNK_LOD_RANGES,  std::as_bytes(std::span{ m.lod_ranges }));
            out.write_chunk(GLT::asset::mesh::CHUNK_LOD_INDICES, std::as_bytes(std::span{ m.lod_indices }));
            if (!m.lod_vertices.empty())
                out.write_chunk(GLT::asset::mesh::CHUNK_LOD_VERTICES, std::as_bytes(std::span{ m.lod_vertices }));
        }

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

        LOG(trace, "serialized [{}] - {} verts, {} tris, {} submeshes, {} colliders, {} LODs",
            info.name, m.vertices.size(), m.indices.size() / 3, m.submeshes.size(), m.colliders.size(), m.lod_count());

        return {};
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
