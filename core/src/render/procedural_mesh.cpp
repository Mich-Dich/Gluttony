
#include "util/pch.h"

#include "render/procedural_mesh.h"
#include "asset/mesh_lod.h"
#include "asset/mesh_cluster.h"
#include "asset/i_asset_registry.h"
#include "debug/trace.h"

#include <cmath>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace {

        using PME = procedural_mesh_error;

        std::unexpected<PME> invalid(std::string message) {
            return std::unexpected(PME{ PME::code::invalid_input, std::move(message) });
        }


        // Id of slot [index] with [generation]
        procedural_mesh_id make_id(u32 index, u32 generation) {
            return ((generation & PROCEDURAL_MESH_GENERATION_MASK) << PROCEDURAL_MESH_INDEX_BITS) | (index + 1u);
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    procedural_mesh_store& procedural_meshes() {

        static procedural_mesh_store store{};
        return store;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    procedural_mesh_id procedural_mesh_store::create(const procedural_mesh_desc& desc) {

        std::lock_guard lock(m_mutex);

        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            if (m_records.size() >= PROCEDURAL_MESH_INDEX_MASK) {
                LOG(error, "procedural mesh store full ({} meshes)", m_records.size())
                return INVALID_PROCEDURAL_MESH;
            }
            index = static_cast<u32>(m_records.size());
            m_records.emplace_back();
        }

        record& r = m_records[index];
        const u32 generation = r.generation;    // bumped on destroy
        const bool listed = r.waiting_listed;   // a stale entry may still sit in m_detail_waiting (tick() drops it)
        r = {};
        r.generation = generation;
        r.waiting_listed = listed;
        r.desc = desc;
        r.alive = true;
        ++m_live;

        // No change notice yet: consumers learn about the mesh with its first version
        return make_id(index, generation);
    }


    std::expected<u64, procedural_mesh_error> procedural_mesh_store::update(procedural_mesh_id id, procedural_mesh_data&& data) {
        GLT_PROFILE_SCOPE("procedural_mesh_store::update");

        mesh_update_class update_class{};
        {
            std::lock_guard lock(m_mutex);
            const record* r = find(id);
            if (!r)
                return std::unexpected(PME{ PME::code::not_found, "invalid or destroyed procedural mesh id" });
            update_class = r->desc.update_class;
            if (update_class == mesh_update_class::immutable && r->version > 0)
                return std::unexpected(PME{ PME::code::already_finalized, "immutable procedural mesh already has its data" });
        }
        if (update_class == mesh_update_class::streaming)
            return std::unexpected(PME{ PME::code::not_supported, "streaming procedural meshes are not implemented yet" });

        // Validation + conversion outside the lock: the expensive part, on the generator's thread
        const bool provided_lods = !data.lods.empty();
        auto built = build_asset(std::move(data));
        if (!built)
            return std::unexpected(std::move(built.error()));

        auto mesh = std::make_shared<GLT::asset::mesh::mesh_asset>(std::move(*built));

        std::lock_guard lock(m_mutex);
        record* r = find(id);
        if (!r)
            return std::unexpected(PME{ PME::code::not_found, "procedural mesh destroyed during update" });
        if (r->desc.update_class == mesh_update_class::immutable && r->version > 0)
            return std::unexpected(PME{ PME::code::already_finalized, "immutable procedural mesh already has its data" });

        r->version++;
        mesh->revision = static_cast<u32>(r->version);
        r->mesh = std::move(mesh);
        r->provided_lods = provided_lods;
        r->last_update = std::chrono::steady_clock::now();

        // Detail build: (re)starts the settle timer. A build still running for the previous version is discarded when it
        // lands (version check in complete_detail)
        if (r->desc.detail != procedural_detail::none) {
            r->detail = procedural_detail_state::waiting;
            if (!r->waiting_listed) {
                r->waiting_listed = true;
                m_detail_waiting.push_back(procedural_mesh_index(id));
            }
        } else
            r->detail = procedural_detail_state::none;

        notify(procedural_mesh_index(id));
        return r->version;
    }


    void procedural_mesh_store::destroy(procedural_mesh_id id) {

        std::lock_guard lock(m_mutex);
        record* r = find(id);
        if (!r)
            return;

        const u32 index = procedural_mesh_index(id);
        const bool had_data = r->version > 0;
        r->alive = false;
        r->mesh.reset();
        r->version = 0;
        r->detail = procedural_detail_state::none;
        r->generation = (r->generation + 1u) & PROCEDURAL_MESH_GENERATION_MASK;    // stale ids stop resolving
        m_free.push_back(index);
        --m_live;

        // Consumers that may hold a copy have to drop it. A consumer that drains the slot after it was reused sees the
        // new id (or nothing yet) and replaces its copy either way
        if (had_data)
            notify(index);
    }


    procedural_mesh_store::consumer_id procedural_mesh_store::register_consumer() {

        std::lock_guard lock(m_mutex);
        consumer_id id = 0;
        while (id < m_consumers.size() && m_consumers[id].alive)
            ++id;
        if (id == m_consumers.size())
            m_consumers.emplace_back();

        consumer& c = m_consumers[id];
        c = {};
        c.alive = true;

        // Everything that exists already is "changed" for a new consumer
        c.pending_flag.assign(m_records.size(), 0u);
        for (u32 index = 0; index < m_records.size(); ++index)
            if (m_records[index].alive && m_records[index].mesh) {
                c.pending.push_back(index);
                c.pending_flag[index] = 1u;
            }
        return id;
    }


    void procedural_mesh_store::unregister_consumer(consumer_id id) {

        std::lock_guard lock(m_mutex);
        if (id < m_consumers.size())
            m_consumers[id] = {};
    }


    void procedural_mesh_store::drain_changes(consumer_id id, std::vector<change>& out) {

        std::lock_guard lock(m_mutex);
        if (id >= m_consumers.size() || !m_consumers[id].alive)
            return;

        consumer& c = m_consumers[id];
        out.reserve(out.size() + c.pending.size());
        for (u32 index : c.pending) {
            c.pending_flag[index] = 0u;
            const record& r = m_records[index];
            change entry{};
            entry.id = make_id(index, r.generation);
            if (r.alive) {
                entry.mesh = r.mesh;
                entry.version = r.version;
            }
            out.push_back(std::move(entry));
        }
        c.pending.clear();
    }


    std::shared_ptr<const GLT::asset::mesh::mesh_asset> procedural_mesh_store::current(procedural_mesh_id id) const {

        std::lock_guard lock(m_mutex);
        const record* r = find(id);
        return r ? r->mesh : nullptr;
    }


    bool procedural_mesh_store::is_valid(procedural_mesh_id id) const {

        std::lock_guard lock(m_mutex);
        return find(id) != nullptr;
    }


    std::optional<procedural_mesh_desc> procedural_mesh_store::desc(procedural_mesh_id id) const {

        std::lock_guard lock(m_mutex);
        const record* r = find(id);
        if (!r)
            return std::nullopt;
        return r->desc;
    }


    u32 procedural_mesh_store::live_count() const {

        std::lock_guard lock(m_mutex);
        return m_live;
    }


    std::optional<procedural_mesh_store::mesh_info> procedural_mesh_store::info(procedural_mesh_id id) const {

        std::lock_guard lock(m_mutex);
        const record* r = find(id);
        if (!r)
            return std::nullopt;

        mesh_info out{};
        out.desc = r->desc;
        out.version = r->version;
        out.provided_lods = r->provided_lods;
        out.detail = r->detail;
        if (r->mesh) {
            out.vertices = static_cast<u32>(r->mesh->vertices.size());
            out.triangles = static_cast<u32>(r->mesh->indices.size() / 3);
            out.submeshes = r->mesh->submesh_count();
            out.lod_count = r->mesh->lod_count();
            out.clusters = r->mesh->uses_virtual_geometry() ? static_cast<u32>(r->mesh->clusters.size()) : 0u;
        }
        return out;
    }


    void procedural_mesh_store::tick() {

        struct job {
            procedural_mesh_id                                      id;
            u64                                                     version;
            std::shared_ptr<const GLT::asset::mesh::mesh_asset>     base;
            bool                                                    lods;
            bool                                                    clusters;
        };
        std::vector<job> jobs;

        {
            std::lock_guard lock(m_mutex);
            if (m_detail_waiting.empty())
                return;
            GLT_PROFILE_SCOPE("procedural_mesh_store::tick");

            // Leave most of the pool to the generators
            const u32 max_running = std::max(1u, std::thread::hardware_concurrency() / 4u);
            const auto now = std::chrono::steady_clock::now();
            const auto settle = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<f32>(PROCEDURAL_DETAIL_SETTLE_SECONDS));

            for (size_t i = 0; i < m_detail_waiting.size(); ) {

                const u32 index = m_detail_waiting[i];
                record& r = m_records[index];
                auto drop = [&]() {
                    r.waiting_listed = false;
                    m_detail_waiting[i] = m_detail_waiting.back();
                    m_detail_waiting.pop_back();
                };

                if (!r.alive || r.detail != procedural_detail_state::waiting || !r.mesh) {
                    drop();
                    continue;
                }
                if (r.desc.update_class != mesh_update_class::immutable && now - r.last_update < settle) {
                    ++i;
                    continue;
                }
                if (m_builds_running + jobs.size() >= max_running)
                    break;

                // What to build
                const u32 triangles = static_cast<u32>(r.mesh->indices.size() / 3);
                procedural_detail mode = r.desc.detail;
                if (mode == procedural_detail::automatic)
                    mode = triangles < PROCEDURAL_LOD_MIN_TRIANGLES ? procedural_detail::none
                         : triangles < PROCEDURAL_VIRTUAL_GEOMETRY_MIN_TRIANGLES ? procedural_detail::discrete_lods
                         : procedural_detail::virtual_geometry;
                const bool lods = (mode == procedural_detail::discrete_lods || mode == procedural_detail::virtual_geometry) && !r.provided_lods;
                const bool clusters = mode == procedural_detail::virtual_geometry;

                if (!lods && !clusters) {
                    r.detail = procedural_detail_state::none;
                    drop();
                    continue;
                }

                r.detail = procedural_detail_state::building;
                jobs.push_back({ make_id(index, r.generation), r.version, r.mesh, lods, clusters });
                drop();
            }
            m_builds_running += static_cast<u32>(jobs.size());
        }

        // Every started build must end in complete_detail(), or m_builds_running leaks and the record stays "building". A
        // task the pool drops without running it (shutdown) completes as failed from the guard's destructor
        struct build_task {
            procedural_mesh_store*                                  store = nullptr;
            job                                                     work;
            bool                                                    done = false;

            ~build_task() {
                if (!done && store)
                    store->complete_detail(work.id, work.version, nullptr);
            }
        };

        // Outside the lock: push() runs the task inline when the pool is not running, and the task takes the lock
        for (auto& j : jobs) {
            auto task = std::make_shared<build_task>();
            task->store = this;
            task->work = std::move(j);
            GLT::thread_pool::push([task]() {
                task->done = true;
                task->store->complete_detail(task->work.id, task->work.version,
                    build_detail(*task->work.base, task->work.lods, task->work.clusters));
            });
        }
    }


    std::expected<GLT::asset::asset_ref, GLT::asset::load_error> procedural_mesh_store::bake(procedural_mesh_id id,
        const std::filesystem::path& content_path, std::string_view name) {
        GLT_PROFILE_SCOPE("procedural_mesh_store::bake");

        const auto source = current(id);
        if (!source)
            return std::unexpected(GLT::asset::load_error::not_found);

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return std::unexpected(GLT::asset::load_error::no_handler);

        auto asset = std::make_unique<GLT::asset::mesh::mesh_asset>(*source);
        asset->asset_type = GLT::asset::core_types::static_mesh;
        asset->revision = 0;

        // A static mesh file: an asset extension the caller typed is replaced, anything else after a dot (a debug name
        // like "terrain.chunk_3") is part of the name
        std::filesystem::path path = content_path;
        if (path.has_extension()
            && GLT::asset::extension_to_type(path.extension().string()) != GLT::asset::core_types::invalid)
            path.replace_extension("");
        path += ".";
        path += GLT::asset::type_to_extension(GLT::asset::core_types::static_mesh);
        const std::string display_name = name.empty() ? path.stem().string() : std::string(name);

        auto ref = registry->register_runtime(std::move(asset), path, display_name);
        if (!ref)
            return std::unexpected(ref.error());

        if (auto saved = registry->save(ref->get()); !saved) {
            LOG(warn, "baked procedural mesh registered at {} but saving failed", content_path.generic_string())
            return std::unexpected(saved.error());
        }
        return std::move(*ref);
    }


    // Worker thread. The same builders the mesh editor uses (build_lods / build_clusters are pure), without their logging:
    // a streamed terrain builds thousands of these
    std::shared_ptr<const GLT::asset::mesh::mesh_asset> procedural_mesh_store::build_detail(const GLT::asset::mesh::mesh_asset& base,
        bool lods, bool clusters) {
        GLT_PROFILE_SCOPE("procedural_mesh_store::build_detail");

        namespace mesh_ns = GLT::asset::mesh;

        try {
            auto out = std::make_shared<mesh_ns::mesh_asset>(base);

            if (lods) {
                mesh_ns::sanitize(out->lod);
                mesh_ns::lod_build_output built = mesh_ns::build_lods(out->vertices, out->indices, out->submeshes, out->lod);
                out->lods = std::move(built.lods);
                out->lod_ranges = std::move(built.ranges);
                out->lod_indices = std::move(built.indices);
                out->lod_vertices = std::move(built.vertices);
                if (out->lod.flags & mesh_ns::LOD_FLAG_AUTO_SCREEN_SIZE)
                    mesh_ns::fit_screen_sizes(out->lod, out->lods, out->bounds);
            }

            if (clusters) {
                mesh_ns::sanitize(out->virtual_geometry);
                mesh_ns::cluster_build_output built = mesh_ns::build_clusters(out->vertices, out->indices, out->submeshes, out->virtual_geometry);
                if (!built.clusters.empty()) {
                    out->clusters = std::move(built.clusters);
                    out->cluster_indices = std::move(built.indices);
                    out->cluster_submeshes = std::move(built.submeshes);
                    out->cluster_vertices = std::move(built.vertices);
                    out->virtual_geometry.flags |= mesh_ns::CLUSTER_FLAG_ENABLED;
                }
            }
            return out;

        } catch (const std::exception& e) {
            LOG(error, "procedural mesh detail build failed: {}", e.what())
            return nullptr;
        } catch (...) {
            LOG(error, "procedural mesh detail build failed (unknown exception)")
            return nullptr;
        }
    }


    void procedural_mesh_store::complete_detail(procedural_mesh_id id, u64 version, std::shared_ptr<const GLT::asset::mesh::mesh_asset> result) {

        std::lock_guard lock(m_mutex);
        if (m_builds_running > 0)
            --m_builds_running;

        record* r = find(id);
        if (!r || r->version != version)
            return;                                     // destroyed or updated meanwhile: a build for the new version is queued

        if (!result) {
            r->detail = procedural_detail_state::failed;
            return;
        }

        // Same version number, richer data: consumers see a change and upload it like any update
        auto mesh = std::const_pointer_cast<GLT::asset::mesh::mesh_asset>(result);
        mesh->revision = static_cast<u32>(version);
        r->mesh = std::move(mesh);
        r->detail = procedural_detail_state::ready;
        notify(procedural_mesh_index(id));
    }


    // Validates a generator's data and turns it into the mesh_asset layout the renderer uploads:
    // [vertices ++ lod_vertices], [indices ++ lod_indices], lod_ranges[level * submesh_count + submesh]
    std::expected<GLT::asset::mesh::mesh_asset, procedural_mesh_error> procedural_mesh_store::build_asset(procedural_mesh_data&& data) {
        GLT_PROFILE_SCOPE("procedural_mesh_store::build_asset");

        using namespace GLT::asset::mesh;

        if (data.vertices.empty() || data.indices.empty())
            return invalid("empty vertices / indices");
        if (data.vertices.size() >= UINT32_MAX || data.indices.size() >= UINT32_MAX)
            return invalid("mesh too large (32 bit indices)");
        if (data.indices.size() % 3 != 0)
            return invalid(std::format("index count {} is not a multiple of 3", data.indices.size()));

        const u32 vertex_count = static_cast<u32>(data.vertices.size());
        const u32 index_count = static_cast<u32>(data.indices.size());
        for (u32 i = 0; i < index_count; ++i)
            if (data.indices[i] >= vertex_count)
                return invalid(std::format("index {} at {} >= vertex count {}", data.indices[i], i, vertex_count));

        mesh_asset mesh{};
        mesh.asset_type = GLT::asset::core_types::procedural_mesh;

        // Submeshes -----------------------------------------------------------------------------------------------------
        if (data.submeshes.empty())
            data.submeshes.push_back({ 0u, index_count, INVALID_HANDLE });
        if (data.submeshes.size() > PROCEDURAL_MAX_SUBMESHES)
            return invalid(std::format("{} submeshes, at most {}", data.submeshes.size(), PROCEDURAL_MAX_SUBMESHES));
        for (const submesh& sm : data.submeshes)
            if (static_cast<u64>(sm.first_index) + sm.index_count > index_count || sm.first_index % 3 != 0 || sm.index_count % 3 != 0)
                return invalid(std::format("submesh range [{}, +{}) outside the indices or not whole triangles", sm.first_index, sm.index_count));
        const u32 submesh_count = static_cast<u32>(data.submeshes.size());

        // Bounds --------------------------------------------------------------------------------------------------------
        if (data.bounds) {
            mesh.bounds = *data.bounds;
        } else {
            mesh.bounds.min = glm::vec3(std::numeric_limits<f32>::max());
            mesh.bounds.max = glm::vec3(std::numeric_limits<f32>::lowest());
            auto grow = [&mesh](const vertex& v) {
                mesh.bounds.min = glm::min(mesh.bounds.min, v.position);
                mesh.bounds.max = glm::max(mesh.bounds.max, v.position);
            };
            for (const vertex& v : data.vertices)
                grow(v);
            for (const procedural_lod& level : data.lods)       // a level's own vertices may reach outside LOD 0
                for (const vertex& v : level.vertices)
                    grow(v);
        }
        const auto finite = [](const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (!finite(mesh.bounds.min) || !finite(mesh.bounds.max))
            return invalid("non-finite bounds (NaN / inf vertex positions?)");

        // Generator-provided LODs -----------------------------------------------------------------------------------------
        if (data.lods.size() > MAX_LODS - 1)
            return invalid(std::format("{} LOD levels, at most {} besides LOD 0", data.lods.size(), MAX_LODS - 1));

        const u32 level_count = static_cast<u32>(data.lods.size()) + 1u;
        if (level_count > 1) {

            const glm::vec3 size = mesh.bounds.max - mesh.bounds.min;
            const f32 extent = std::max({ size.x, size.y, size.z, 1e-6f });

            mesh.lods.resize(level_count);
            mesh.lods[0] = { index_count, 0.0f, 0, 0 };
            mesh.lod_ranges.resize(static_cast<size_t>(level_count) * submesh_count);
            for (u32 s = 0; s < submesh_count; ++s)
                mesh.lod_ranges[s] = { data.submeshes[s].first_index, data.submeshes[s].index_count };

            u64 total_indices = index_count;
            u64 total_vertices = vertex_count;
            for (const procedural_lod& level : data.lods) {
                total_indices += level.indices.size();
                total_vertices += level.vertices.size();
            }
            if (total_indices >= UINT32_MAX || total_vertices >= UINT32_MAX)
                return invalid("mesh with LODs too large (32 bit indices)");
            mesh.lod_indices.reserve(static_cast<size_t>(total_indices - index_count));
            mesh.lod_vertices.reserve(static_cast<size_t>(total_vertices - vertex_count));

            bool derive_sizes = false;
            for (u32 n = 1; n < level_count; ++n) {

                procedural_lod& level = data.lods[n - 1];
                if (level.submesh_index_counts.size() != submesh_count)
                    return invalid(std::format("LOD {}: {} submesh index counts for {} submeshes", n, level.submesh_index_counts.size(), submesh_count));

                u64 sum = 0;
                for (u32 count : level.submesh_index_counts) {
                    if (count % 3 != 0)
                        return invalid(std::format("LOD {}: a submesh index count ({}) is not whole triangles", n, count));
                    sum += count;
                }
                if (sum != level.indices.size())
                    return invalid(std::format("LOD {}: submesh index counts add up to {}, the level has {} indices", n, sum, level.indices.size()));

                // Own vertices go behind everything so far in [vertices ++ lod_vertices]
                const u32 own_base = static_cast<u32>(mesh.lod_vertices.size());
                const u32 own_count = static_cast<u32>(level.vertices.size());
                const u32 limit = vertex_count + own_count;
                const u32 first_index = static_cast<u32>(index_count + mesh.lod_indices.size());

                for (u32 index : level.indices) {
                    if (index >= limit)
                        return invalid(std::format("LOD {}: index {} >= {} (LOD 0 vertices + the level's own)", n, index, limit));
                    mesh.lod_indices.push_back(index < vertex_count ? index : index + own_base);
                }
                mesh.lod_vertices.insert(mesh.lod_vertices.end(), level.vertices.begin(), level.vertices.end());

                u32 cursor = first_index;
                for (u32 s = 0; s < submesh_count; ++s) {
                    mesh.lod_ranges[static_cast<size_t>(n) * submesh_count + s] = { cursor, level.submesh_index_counts[s] };
                    cursor += level.submesh_index_counts[s];
                }

                const f32 error = std::isfinite(level.error) ? std::max(level.error, 0.0f) : 0.0f;
                mesh.lods[n] = { static_cast<u32>(level.indices.size()), error / extent, 0, 0 };

                const f32 screen = std::isfinite(level.screen_size) ? std::max(level.screen_size, 0.0f) : 0.0f;
                if (screen == 0.0f && error == 0.0f)
                    return invalid(std::format("LOD {}: needs a screen_size or an error to know when to switch", n));
                mesh.lod.levels[n].screen_size = screen;
                if (screen == 0.0f)
                    derive_sizes = true;
            }

            // Levels without a switch size: from their error, the same rule as generated LODs (1 px at 1080p)
            if (derive_sizes) {
                lod_settings fitted = mesh.lod;
                fitted.pixel_error = 1.0f;
                fit_screen_sizes(fitted, mesh.lods, mesh.bounds);
                for (u32 n = 1; n < level_count; ++n)
                    if (mesh.lod.levels[n].screen_size == 0.0f)
                        mesh.lod.levels[n].screen_size = fitted.levels[n].screen_size;
            }

            mesh.lod.lod_count = level_count;
            mesh.lod.flags = LOD_FLAG_LOCK_BORDERS;         // provided levels: no auto refit, no regeneration flags
        }

        mesh.lod.cull_screen_size = std::isfinite(data.cull_screen_size) ? data.cull_screen_size : 0.0f;
        sanitize(mesh.lod);                                 // monotonic switch sizes, clamped cull size

        mesh.vertices = std::move(data.vertices);
        mesh.indices = std::move(data.indices);
        mesh.submeshes = std::move(data.submeshes);
        return mesh;
    }

    // CLASS PRIVATE ===================================================================================================

    procedural_mesh_store::record* procedural_mesh_store::find(procedural_mesh_id id) {

        if (id == INVALID_PROCEDURAL_MESH || (id & PROCEDURAL_MESH_INDEX_MASK) == 0)
            return nullptr;
        const u32 index = procedural_mesh_index(id);
        if (index >= m_records.size())
            return nullptr;
        record& r = m_records[index];
        if (!r.alive || make_id(index, r.generation) != id)
            return nullptr;
        return &r;
    }


    const procedural_mesh_store::record* procedural_mesh_store::find(procedural_mesh_id id) const {

        return const_cast<procedural_mesh_store*>(this)->find(id);
    }


    void procedural_mesh_store::notify(u32 index) {

        for (consumer& c : m_consumers) {
            if (!c.alive)
                continue;
            if (c.pending_flag.size() <= index)
                c.pending_flag.resize(m_records.size(), 0u);
            if (!c.pending_flag[index]) {
                c.pending_flag[index] = 1u;
                c.pending.push_back(index);
            }
        }
    }

}
