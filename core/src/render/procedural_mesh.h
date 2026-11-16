
#pragma once

#include "asset/mesh.h"
#include "asset/asset_ref.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // A procedural mesh id: low 20 bits = slot index + 1 (about a million live meshes), high 12 bits = generation of the
    // slot. A stale id (its mesh was destroyed and the slot reused) does not resolve to the new mesh until the slot has
    // been reused 4096 times. 0 is never a valid id
    using procedural_mesh_id = u32;
    inline constexpr procedural_mesh_id                 INVALID_PROCEDURAL_MESH = 0;
    inline constexpr u32                                PROCEDURAL_MESH_INDEX_BITS = 20;
    inline constexpr u32                                PROCEDURAL_MESH_GENERATION_MASK = (1u << (32 - PROCEDURAL_MESH_INDEX_BITS)) - 1u;
    inline constexpr u32                                PROCEDURAL_MESH_INDEX_MASK = (1u << PROCEDURAL_MESH_INDEX_BITS) - 1u;

    // Submeshes (materials) per procedural mesh. Same limit as the renderer's per-mesh material list
    inline constexpr u32                                PROCEDURAL_MAX_SUBMESHES = 32;

    // Automatic detail builds (procedural_detail::automatic / discrete_lods / virtual_geometry) --------------------------

    // An [occasional] mesh is built only after it went this long without an update (an editor slider being dragged never
    // starts a build). [immutable] meshes are built right away
    inline constexpr f32                                PROCEDURAL_DETAIL_SETTLE_SECONDS = 0.5f;

    // procedural_detail::automatic by LOD 0 triangle count: below the first nothing is built (LODs would cost more draws
    // than they save), below the second discrete LODs, above it virtual geometry (+ discrete LODs as its fallback)
    inline constexpr u32                                PROCEDURAL_LOD_MIN_TRIANGLES = 2'000;
    inline constexpr u32                                PROCEDURAL_VIRTUAL_GEOMETRY_MIN_TRIANGLES = 50'000;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // How often a procedural mesh changes. Decides the renderer's strategy and when detail levels are built
    //   immutable  — update() may be called exactly once. A second call returns already_finalized
    //   occasional — any number of updates (terrain chunks, editor tools). Each one is a new version
    //   streaming  — rewritten every frame. Not implemented yet: update() returns not_supported
    enum class mesh_update_class : u8 {

        immutable = 0,
        occasional,
        streaming,
    };


    // How the mesh reduces its detail with distance. Built in the background (GLT::thread_pool) once the mesh stops
    // changing; until the build lands the mesh is drawn with what it has (LOD 0, or the generator's own levels)
    //   automatic:        by triangle count (PROCEDURAL_LOD_MIN_TRIANGLES / PROCEDURAL_VIRTUAL_GEOMETRY_MIN_TRIANGLES)
    //   none:             never build anything
    //   discrete_lods:    build discrete LODs (skipped when the generator provided its own)
    //   virtual_geometry: build the cluster hierarchy, plus discrete LODs as its fallback unless the generator provided them
    // Generator-provided LODs (procedural_mesh_data::lods) are never replaced
    enum class procedural_detail : u8 {

        automatic = 0,
        none,
        discrete_lods,
        virtual_geometry,
    };


    // Where the background detail build of a mesh stands (procedural_mesh_store::info)
    enum class procedural_detail_state : u8 {

        none = 0,           // nothing to build (policy none, too small, or provided LODs cover it)
        waiting,            // waiting for the mesh to settle / a free build slot
        building,
        ready,              // the current version carries the built LODs / clusters
        failed,
    };


    struct procedural_mesh_desc {

        mesh_update_class                               update_class = mesh_update_class::occasional;
        procedural_detail                               detail = procedural_detail::automatic;
        std::string                                     debug_name{};
    };


    // One coarser level supplied by the generator
    //
    // [indices] are the triangles of every submesh, submesh by submesh in submesh order, [submesh_index_counts] how many
    // indices each submesh has at this level (0 = the submesh vanishes here). Indices address [LOD 0 vertices ++ this
    // level's own vertices]: an index >= vertices.size() of the mesh is vertex (index - vertices.size()) of [vertices]
    // below. Switch distance: [screen_size] (projected bounding-sphere diameter / viewport height, the asset LOD metric),
    // or, when it is 0, derived from [error] (geometric deviation in mesh units) and a 1 pixel tolerance at 1080p
    struct procedural_lod {

        std::vector<u32>                                indices;
        std::vector<u32>                                submesh_index_counts;
        std::vector<GLT::asset::mesh::vertex>           vertices;
        f32                                             screen_size = 0.0f;
        f32                                             error = 0.0f;
    };


    // What a generator hands over. Moved in, never copied
    struct procedural_mesh_data {

        std::vector<GLT::asset::mesh::vertex>           vertices;
        std::vector<u32>                                indices;
        std::vector<GLT::asset::mesh::submesh>          submeshes;          // empty = one submesh over all indices, default material
        std::vector<procedural_lod>                     lods;               // LOD 1..N, at most MAX_LODS - 1
        f32                                             cull_screen_size = 0.0f;    // below this the mesh is not drawn (0 = never)
        std::optional<GLT::AABB>                        bounds;             // computed from the vertices when absent
    };


    struct procedural_mesh_error {

        enum class code : u32 {
            none = 0,
            not_found,
            not_supported,
            already_finalized,
            invalid_input,
            out_of_memory,
            backend,
        }                                               kind = code::none;
        std::string                                     message{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    FORCE_INLINE_R u32 procedural_mesh_index(procedural_mesh_id id) noexcept { return (id & PROCEDURAL_MESH_INDEX_MASK) - 1u; }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief CPU truth of every procedural mesh. Generators write, renderers / physics / the editor read
    //
    // A generator and a renderer never talk to each other: the generator calls create() / update() / destroy() from any
    // thread, consumers (one per renderer plugin, ...) drain the ids that changed and read the current version. Every
    // version is an immutable mesh_asset (asset type procedural_mesh), so the renderer uploads it with the same code as a
    // static mesh, and baking one into a static mesh asset is a copy + save
    //
    // Threading: every method is thread-safe. Versions are shared_ptr<const>: a reader keeps one alive as long as it
    // needs it, while the generator already publishes the next
    class procedural_mesh_store {
    public:

        using consumer_id = u32;

        // One change notice per id since the consumer's last drain (create, update or destroy, coalesced)
        struct change {
            procedural_mesh_id                          id = INVALID_PROCEDURAL_MESH;
            std::shared_ptr<const GLT::asset::mesh::mesh_asset> mesh{};     // null: destroyed (or not updated yet)
            u64                                         version = 0;
        };

        procedural_mesh_store() = default;
        procedural_mesh_store(const procedural_mesh_store&) = delete;
        procedural_mesh_store& operator=(const procedural_mesh_store&) = delete;

        // generator side ----------------------------------------------------------------------------------------------

        [[nodiscard]] procedural_mesh_id create(const procedural_mesh_desc& desc);

        // Validates [data] on the calling thread, converts it into a new version and publishes it. Returns the version
        // number (1 for the first update)
        std::expected<u64, procedural_mesh_error> update(procedural_mesh_id id, procedural_mesh_data&& data);

        // The id becomes invalid immediately; consumers release their copies when they drain the change
        void destroy(procedural_mesh_id id);

        // consumer side -----------------------------------------------------------------------------------------------

        [[nodiscard]] consumer_id register_consumer();
        void unregister_consumer(consumer_id consumer);

        // Ids changed since this consumer's last drain, with their current version (appended to [out])
        void drain_changes(consumer_id consumer, std::vector<change>& out);

        // Current version, null if the id is invalid / destroyed / never updated
        [[nodiscard]] std::shared_ptr<const GLT::asset::mesh::mesh_asset> current(procedural_mesh_id id) const;

        [[nodiscard]] bool is_valid(procedural_mesh_id id) const;
        [[nodiscard]] std::optional<procedural_mesh_desc> desc(procedural_mesh_id id) const;
        [[nodiscard]] u32 live_count() const;

        // What tools show about a mesh (editor inspector)
        struct mesh_info {
            procedural_mesh_desc                        desc{};
            u64                                         version = 0;
            u32                                         vertices = 0;
            u32                                         triangles = 0;          // LOD 0
            u32                                         submeshes = 0;
            u32                                         lod_count = 1;
            u32                                         clusters = 0;
            bool                                        provided_lods = false;
            procedural_detail_state                     detail = procedural_detail_state::none;
        };
        [[nodiscard]] std::optional<mesh_info> info(procedural_mesh_id id) const;

        // detail builds -----------------------------------------------------------------------------------------------

        // Starts the background LOD / cluster builds of meshes that settled. Called once per frame (game_layer::update);
        // cheap when nothing waits. Finished builds publish themselves as a new version of their mesh (same version
        // number, new data) from the worker thread
        void tick();

        // baking (main thread: uses the asset registry) ----------------------------------------------------------------

        // Copies the current version into a new static mesh asset at [content_path] (content-dir relative, registry
        // rules) and saves it. LODs / clusters are kept. The procedural mesh itself is untouched
        [[nodiscard]] std::expected<GLT::asset::asset_ref, GLT::asset::load_error> bake(procedural_mesh_id id,
            const std::filesystem::path& content_path, std::string_view name = {});

        // conversion (exposed for tests and tools) -----------------------------------------------------------------------

        [[nodiscard]] static std::expected<GLT::asset::mesh::mesh_asset, procedural_mesh_error> build_asset(procedural_mesh_data&& data);

    private:

        struct record {
            procedural_mesh_desc                        desc{};
            std::shared_ptr<const GLT::asset::mesh::mesh_asset> mesh{};
            u64                                         version = 0;
            u32                                         generation = 0;     // PROCEDURAL_MESH_GENERATION_MASK wide
            bool                                        alive = false;
            bool                                        provided_lods = false;  // the current version came with the generator's LODs
            bool                                        waiting_listed = false; // in m_detail_waiting
            procedural_detail_state                     detail = procedural_detail_state::none;
            std::chrono::steady_clock::time_point       last_update{};
        };

        // Worker side of a detail build: copy of [base] with the requested parts built. Null on failure
        [[nodiscard]] static std::shared_ptr<const GLT::asset::mesh::mesh_asset> build_detail(const GLT::asset::mesh::mesh_asset& base,
            bool lods, bool clusters);
        void complete_detail(procedural_mesh_id id, u64 version, std::shared_ptr<const GLT::asset::mesh::mesh_asset> result);

        struct consumer {
            bool                                        alive = false;
            std::vector<u32>                            pending{};          // slot indices
            std::vector<u8>                             pending_flag{};     // [slot], dedups pending
        };

        [[nodiscard]] record* find(procedural_mesh_id id);
        [[nodiscard]] const record* find(procedural_mesh_id id) const;
        void notify(u32 index);                                         // m_mutex held

        mutable std::mutex                              m_mutex{};
        std::vector<record>                             m_records{};
        std::vector<u32>                                m_free{};
        std::vector<consumer>                           m_consumers{};
        u32                                             m_live = 0;
        std::vector<u32>                                m_detail_waiting{};     // slot indices with detail == waiting
        u32                                             m_builds_running = 0;
    };


    // The engine's store (lives in core; plugins reach it through this function)
    [[nodiscard]] procedural_mesh_store& procedural_meshes();

}
