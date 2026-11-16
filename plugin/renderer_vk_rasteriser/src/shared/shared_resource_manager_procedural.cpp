#include "util/pch.h"
#include <debug/trace.h>
#include "util/util_pch.h"
#include "shared_resource_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Geometry bytes a version uploads (what the per-frame budget counts)
    static u64 procedural_upload_bytes(const GLT::asset::mesh::mesh_asset& mesh) {

        const u64 vertices = mesh.vertices.size() + mesh.lod_vertices.size() + mesh.cluster_vertices.size();
        const u64 indices = mesh.indices.size() + mesh.lod_indices.size() + mesh.cluster_indices.size();
        return vertices * sizeof(GLT::asset::mesh::vertex) + indices * sizeof(u32)
            + mesh.clusters.size() * sizeof(GLT::asset::mesh::cluster);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // Procedural meshes on the GPU
    //
    // The store (core, render/procedural_mesh.h) holds the CPU truth; this mirrors it. A procedural mesh version is
    // uploaded with exactly the code a static mesh asset uses (upload_new_slot) into the same pools and tables, so every
    // pass draws it without knowing where it came from: GPU culling, LODs, virtual geometry, shadows, debug views.
    //
    // Every version gets a NEW slot, the previous slot is released (retired once the GPU is done with it). So a table
    // entry is never rewritten while frames in flight read it, and the shadow caches, which compare the mesh index of
    // each caster, redraw exactly the tiles around the instances whose mesh changed.

    // CLASS PUBLIC ====================================================================================================

    shared_mesh_id shared_resource_manager::procedural_mesh(GLT::render::procedural_mesh_id id) const {

        if (id == GLT::render::INVALID_PROCEDURAL_MESH)
            return INVALID_SHARED_MESH;
        const u32 index = GLT::render::procedural_mesh_index(id);
        if (index >= m_procedural_entries.size())
            return INVALID_SHARED_MESH;
        const procedural_entry& entry = m_procedural_entries[index];
        return entry.id == id ? entry.slot : INVALID_SHARED_MESH;
    }


    void shared_resource_manager::sync_procedural() {
        GLT_PROFILE_SCOPE("shared_resource_manager::sync_procedural");

        auto& store = GLT::render::procedural_meshes();
        if (!m_procedural_registered) {
            m_procedural_consumer = store.register_consumer();      // existing meshes arrive as changes
            m_procedural_registered = true;
        }

        // Newest change per mesh; an id that changed again since it was queued keeps its place in the queue
        m_procedural_drain.clear();
        store.drain_changes(m_procedural_consumer, m_procedural_drain);
        for (auto& change : m_procedural_drain) {
            const u32 index = GLT::render::procedural_mesh_index(change.id);
            auto [it, inserted] = m_procedural_pending.try_emplace(index);
            it->second = std::move(change);
            if (inserted)
                m_procedural_order.push_back(index);
        }
        m_procedural_drain.clear();

        u64 budget_used = 0;
        u32 uploaded = 0;
        while (!m_procedural_order.empty()) {

            const u32 index = m_procedural_order.front();
            auto it = m_procedural_pending.find(index);
            if (it == m_procedural_pending.end()) {
                m_procedural_order.pop_front();
                continue;
            }
            GLT::render::procedural_mesh_store::change& change = it->second;

            if (index >= m_procedural_entries.size())
                m_procedural_entries.resize(static_cast<size_t>(index) + 1);

            // Destroyed (or the slot was reused and its new mesh has no data yet): drop what we draw for it
            if (!change.mesh) {
                release_procedural_entry(index);
                m_procedural_entries[index].id = GLT::render::INVALID_PROCEDURAL_MESH;
                m_procedural_attempts.erase(index);
                m_procedural_pending.erase(it);
                m_procedural_order.pop_front();
                continue;
            }

            const u64 bytes = procedural_upload_bytes(*change.mesh);
            if (uploaded > 0 && budget_used + bytes > PROCEDURAL_UPLOAD_BUDGET)
                break;                                      // next frame; the previous version keeps drawing

            const shared_mesh_id slot = upload_new_slot(*change.mesh, INVALID_HANDLE);
            if (slot == INVALID_SHARED_MESH) {

                // The store slot now belongs to another id: what we draw for the old one must go regardless
                if (m_procedural_entries[index].id != change.id) {
                    release_procedural_entry(index);
                    m_procedural_entries[index].id = change.id;
                }

                // Retry a few frames later (memory may be freed by then), then give up until the generator updates again
                const u32 attempt = ++m_procedural_attempts[index];
                if (attempt < PROCEDURAL_UPLOAD_ATTEMPTS) {
                    m_procedural_order.pop_front();
                    m_procedural_order.push_back(index);        // stays pending
                    break;
                }
                LOG(error, "procedural mesh {:#x} version {}: upload failed {} times (out of GPU memory?), skipped until its next update",
                    change.id, change.version, attempt)
                m_procedural_attempts.erase(index);
            } else {
                m_procedural_attempts.erase(index);
                mesh_slot& new_slot = m_mesh_slots[slot];
                new_slot.procedural = true;
                new_slot.procedural_id = change.id;
                m_mesh_table[slot].flags |= MESH_FLAG_PROCEDURAL;
                mark_mesh_table_dirty(slot, false);         // fresh slot: nobody reads its entry yet

                release_procedural_entry(index);            // previous version (or the previous id of this store slot)
                procedural_entry& entry = m_procedural_entries[index];
                entry.id = change.id;
                entry.slot = slot;
                entry.version = change.version;
            }

            budget_used += bytes;
            ++uploaded;
            m_procedural_pending.erase(it);
            m_procedural_order.pop_front();
        }

        GLT_PROFILE_PLOT("Procedural uploads", static_cast<int64_t>(uploaded));
        GLT_PROFILE_PLOT("Procedural queue", static_cast<int64_t>(m_procedural_order.size()));
    }

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::release_procedural_entry(u32 index) {

        if (index >= m_procedural_entries.size())
            return;
        procedural_entry& entry = m_procedural_entries[index];
        if (entry.slot != INVALID_SHARED_MESH)
            release_mesh(entry.slot);                       // ref count 1: retired once frames in flight are done with it
        entry.slot = INVALID_SHARED_MESH;
        entry.version = 0;
    }

}
