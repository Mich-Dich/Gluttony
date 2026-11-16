# Procedural Geometry

> **Status:** phases 1–7 are implemented, except the demo generator: a terrain generator will be its own plugin (see
> "Writing a generator plugin" below). Device-local mesh buffers with staging uploads and growable tables
> (`shared_resource_manager_{mesh,upload}.cpp`), the core store with generator-provided LODs, background detail builds
> and baking (`core/src/render/procedural_mesh.{h,cpp}`), its GPU mirror (`shared_resource_manager_procedural.cpp`), the
> unified scene renderer path, and the world / editor side (`i_world::set_procedural_mesh`, the mesh component's
> inspector with "Bake to static mesh"). Phase 8 (`streaming`) is still a proposal.
>
> Deviations from the text below: ids carry a 12-bit generation (`procedural_mesh_id` = 20-bit index + generation);
> consumers drain coalesced changes per consumer id instead of a cursor; `keep_cpu_copy` is not implemented (versions are
> always kept); a level with neither `screen_size` nor `error` is rejected. It describes how the Vulkan rasteriser
> should treat procedural meshes so that they are first-class geometry, not a side path. Read it together with
> [multi_scene_renderer.md](multi_scene_renderer.md) §10, which it replaces, [level_of_detail.md](level_of_detail.md)
> and [virtual_geometry.md](virtual_geometry.md).

A procedural mesh is geometry that a generator produces at runtime instead of loading it from an asset file. Some
are generated once (a rock, a building). Others are generated, used for a while and then regenerated or dropped
(terrain chunks, an editor tool while a slider is being dragged). A third kind would be rewritten every frame (water,
cloth). The renderer has to handle all three without slowing down static meshes, and a procedural mesh must be
convertible into a static mesh asset during game creation.

## Goals

1. **One geometry path on the GPU.** Once a procedural mesh is uploaded, the renderer can't tell it apart from a
   static mesh: same pools, same mesh table, same GPU culling, same shadows, same LODs / virtual geometry, same debug
   views. Its own logic only covers how the data gets there.
2. **Static meshes stay as fast as they are.** No new branches in `cull.comp.glsl`, `virtual_geometry_cull.comp.glsl`,
   `shadow_cull.comp.glsl` or the material shaders, and no extra per-frame CPU work for a scene without procedural
   meshes beyond one empty-queue check.
3. **Generator and renderer don't know each other** (see the README). A generator writes into a core service. The
   renderer plugin mirrors that service to the GPU. Swapping the renderer, or having no renderer at all (a headless
   server that only needs colliders), doesn't affect the generator.
4. **Cost follows the update rate.** A mesh that is generated once gets the same treatment as an imported mesh,
   including automatic LODs or virtual geometry. A mesh that changes often never pays for work that its next update
   would throw away.
5. **Converting to a static mesh is cheap.** The CPU data of a procedural mesh is already a `mesh_asset`, so baking it
   means registering it and saving it. No conversion step, no second upload.

## What exists today, and why it gets replaced

`i_scene_renderer` has `create_procedural_mesh` / `update_procedural_mesh` / `destroy_procedural_mesh`, implemented in
`scene/scene_renderer_procedural.inl`. Nothing in the engine calls them yet. The implementation is the afterthought
this document is meant to prevent:

| Problem | Where |
|---|---|
| Owned per scene renderer. One world feeds every scene renderer in `game_session::update` with the same instance list, so a procedural id can't be valid in all of them | `scene_renderer::m_procedural_meshes` |
| At most 64 meshes, each with its own `VkBuffer` pair, allocated on every update | `MAX_PROCEDURAL_MESHES`, `update_procedural_mesh` |
| One material per mesh, no submeshes | `procedural_mesh_slot::material_slot` |
| No GPU culling: every instance is drawn every frame, one `drawIndexed` and two buffer rebinds each | `scene_renderer_scene.inl` (depth pre-pass, scene pass) |
| No shadows | `update_shadow_casters` and both shadow passes only look at the asset prefix of the instance buffer |
| No LODs, no virtual geometry | — |
| Separate code paths in seven places | `scene_renderer_{scene,cull,util,tri_density,lod_debug,virtual_geometry_debug,shadow}.inl` |
| `streaming` returns `not_supported` | `update_procedural_mesh` |

Since there are no callers, the interface can be replaced without a migration.

## Should procedural and static meshes share storage? (validated)

Two questions are mixed up in "the same data structure", and the answers differ:

1. **The same logical layout**: mesh table, submesh table, cluster table, GPU culling, indirect draws.
2. **The same memory**: which `VkBuffer`, in which Vulkan memory type.

### Vulkan memory types

On a PC with a discrete GPU there are three relevant kinds of memory
([VMA: recommended usage patterns](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html)):

| Memory | Flags | GPU reads (drawing) | CPU writes (updating) | Size |
|---|---|---|---|---|
| VRAM | `DEVICE_LOCAL` | full VRAM bandwidth (hundreds of GB/s) | not mappable: write a staging buffer, then `vkCmdCopyBuffer` | all of VRAM |
| BAR / ReBAR | `DEVICE_LOCAL \| HOST_VISIBLE` | full VRAM bandwidth | mapped, but writes go over PCIe and must be sequential, write-only (write-combined) | **256 MB** without Resizable BAR, all of VRAM with it |
| System RAM | `HOST_VISIBLE` (not device-local) | over PCIe (roughly 16–32 GB/s, an order of magnitude below VRAM), on every frame and every pass | plain `memcpy` | large |

So what you heard is right, with one correction: the "fast to update" kind isn't "a bit slower" to draw. If geometry
sits in system RAM, the GPU fetches it over PCIe in every pass that draws it. That's the depth pre-pass, the scene
pass and every shadow view, every frame. For per-frame data that is written once and read once, that's the right
trade-off. For geometry that stays the same over many frames it's the wrong one, whether the geometry is static or
procedural.

### What the engine does today

All pools (`pool_resize` in `shared_resource_manager_mesh.cpp`) are created with `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE`
and `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT` (`util/buffer.cpp`), then mapped and written with
`memcpy`. In the VMA docs this is the pattern for "a buffer that you want to map and write from the CPU, directly
read from the GPU". The memory has to be `HOST_VISIBLE`. VMA prefers the BAR heap, and when that is missing or full
it falls back to system RAM. Whether static meshes are drawn from VRAM today therefore depends on the machine:

- **With Resizable BAR** (most GPUs and boards since about 2020, if it's enabled in the BIOS): VRAM. Fine.
- **Without it**: the vertex, index, cluster, mesh-table and submesh pools share the 256 MB BAR heap with every
  other mapped buffer (instance buffers, UBOs, the procedural buffers). Once that heap is full, new and grown pools
  land in system RAM, and from then on every draw fetches geometry over PCIe. Pools grow by 1.5× and the old buffer
  stays alive until it's retired, so a few hundred MB of meshes are enough to get there.

The code reads from this memory in two places: `pool_resize` copies the old pool with `memcpy` from a mapped
pointer, and `try_compact` uses `memmove` inside the mapped pool. VMA warns that this memory is uncached and
write-combined ("only sequential writes and no reads"), so both are very slow, on top of the `waitIdle`.

The renderer logs nothing about which memory type the pools get. Logging `vmaGetAllocationMemoryProperties` once
after each `pool_resize` would show how your machines behave.

### Verdict

| Option | Draw speed | Update cost | Code |
|---|---|---|---|
| A. Separate buffers per procedural mesh (today) | VRAM or RAM, VMA decides | allocation per update | a second draw path through every pass; no GPU culling, no shadows |
| B. Separate procedural pool, same layout | as A | low | every draw loop runs once per pool (rebinding buffers), the cull shader needs a pool index |
| C. **Shared layout, shared device-local pools, staging uploads** | **VRAM for static and procedural** | CPU `memcpy` into staging plus a GPU copy (a few µs for a terrain chunk) | one path; pools need a transfer pass |
| D. C, plus a separate host-visible ring pool for `streaming` only | C, plus PCIe reads for per-frame meshes only | `memcpy` only | small: one extra draw batch per material, only when streaming meshes exist |

**Decision: C now, D in phase 8.** The shared logical layout is confirmed. It's the only option where procedural
meshes get culling, shadows and LODs without a second path. Staging is the same `memcpy` the CPU does today, into a
different buffer. The GPU copy is tiny next to a frame: a terrain chunk of ~8k triangles is ~400 KB, about 30 µs at
PCIe speed. Changing the pool memory isn't a cost of the procedural work. It also makes static meshes draw from VRAM
on machines without ReBAR, and it removes the slow reads and the `waitIdle` from growing and compacting the pools,
because both become GPU copies.

How the transfer works (phase 2):

- **Pools:** `DEVICE_LOCAL` with no host access, plus `eTransferDst | eTransferSrc` usage (needed for growth and
  compaction copies).
- **Staging:** one persistently mapped ring buffer per plugin (host-visible, sequential write), e.g. 64 MB,
  allocated in chunks per frame in flight. The per-frame upload budget can't exceed it.
- **Upload submission:** `begin_frame()` records every copy of the frame into one plugin-level command buffer. The
  same command buffer does pool growth (old to new buffer) and compaction. It is submitted on the graphics queue
  before any scene renderer submits, and signals a timeline value. Each scene renderer's submit waits for that value
  at `DRAW_INDIRECT | VERTEX_INPUT | COMPUTE_SHADER`. The existing retire timeline already knows when the staging
  chunk is free again.
- **Textures:** `create_gpu_texture` still uses `immediate_submit`, which waits for a fence. It can move onto the
  same path later.
- **Fallback:** on integrated GPUs with unified memory, `DEVICE_LOCAL | HOST_VISIBLE` covers all memory. There, VMA's
  `HOST_ACCESS_ALLOW_TRANSFER_INSTEAD` lets the pools stay mapped and skip staging. Treat that as an optimisation, not
  a second code path to maintain from day one.

## Architecture

```
generator (any thread)                core                                    renderer plugin (update thread)
──────────────────────                ────                                    ───────────────────────────────
procedural_mesh_data  ──update()──►  procedural_mesh_store                    shared_resource_manager
                                      id → record {                           ┌──────────────────────────────┐
                                        desc, current version,                │ begin_frame():               │
                                        shared_ptr<const mesh_asset>,  ◄──────│   drain store changes        │
                                        detail build state }                  │   upload within byte budget  │
                                      change log (id, version)                │   publish mesh-table entries │
                                            │                                 │ same pools / tables as       │
                                            ▼                                 │ static meshes                │
                                      thread_pool: async LOD / cluster ──────►│                              │
                                      builds → new version of the same id     └──────────────────────────────┘
                                                                                         ▲
mesh::instance { procedural_mesh = id }  ── submit_scene ──►  scene_renderer::build_instance_buffer: id → mesh index
```

### Core: `procedural_mesh_store`

A new core service (`core/src/render/procedural_mesh.{h,cpp}`; it could also go in `core/src/geometry/` if you'd rather
keep it out of `render`). It is thread-safe and owns the CPU truth of every procedural mesh.

```cpp
namespace GLT::render {

    using procedural_mesh_id = u32;                         // existing type, now global instead of per scene
    inline constexpr procedural_mesh_id INVALID_PROCEDURAL_MESH = 0;

    enum class mesh_update_class : u8 { immutable = 0, occasional, streaming };     // existing

    // How the renderer reduces detail with distance (asset::mesh::detail_mode plus "decide for me")
    enum class procedural_detail : u8 {
        automatic = 0,      // by update class and triangle count (see "Detail builds")
        none,               // LOD 0 only
        discrete_lods,
        virtual_geometry,
    };

    struct procedural_mesh_desc {
        mesh_update_class       update_class = mesh_update_class::occasional;
        procedural_detail       detail = procedural_detail::automatic;
        bool                    keep_cpu_copy = true;       // false: data is dropped after upload (no bake, no physics)
        std::string             debug_name{};
    };

    // One coarser level supplied by the generator (see "Generator-provided LODs")
    struct procedural_lod {
        std::vector<u32>                    indices;        // triangles of every submesh, submesh by submesh, in submesh order
        std::vector<u32>                    submesh_index_counts;   // one per submesh, sum == indices.size()
        std::vector<asset::mesh::vertex>    vertices;       // optional own vertices; empty = the level reuses LOD 0's
        f32                                 screen_size = 0.0f;     // switch below this projected size; 0 = derive from [error]
        f32                                 error = 0.0f;           // geometric deviation in mesh units (for the 0 case)
    };

    // What a generator hands over. Moved in, never copied
    struct procedural_mesh_data {
        std::vector<asset::mesh::vertex>    vertices;
        std::vector<u32>                    indices;
        std::vector<asset::mesh::submesh>   submeshes;      // empty = one submesh over all indices, default material
        std::vector<procedural_lod>         lods;           // LOD 1..N (at most MAX_LODS - 1). Non-empty: no automatic LOD build
        f32                                 cull_screen_size = 0.0f;   // below this the mesh isn't drawn (0 = never)
        std::optional<AABB>                 bounds;         // computed when absent
    };

    class procedural_mesh_store {
    public:
        // generator side (any thread) ---------------------------------------------------------------------------
        [[nodiscard]] procedural_mesh_id create(const procedural_mesh_desc& desc);
        std::expected<u64, procedural_mesh_error> update(procedural_mesh_id id, procedural_mesh_data&& data);   // -> version
        void destroy(procedural_mesh_id id);

        // consumer side (renderer, physics, editor) -------------------------------------------------------------
        [[nodiscard]] std::shared_ptr<const asset::mesh::mesh_asset> current(procedural_mesh_id id) const;
        // Changes since [cursor] (create / new version / detail ready / destroy), coalesced per id. O(changes)
        void drain_changes(u64& cursor, std::vector<procedural_mesh_change>& out) const;

        // baking (main thread) -----------------------------------------------------------------------------------
        [[nodiscard]] std::expected<asset::asset_ref, asset::load_error> bake(procedural_mesh_id id,
            const std::filesystem::path& content_path);
    };
}
```

A **version is an immutable `std::shared_ptr<const mesh_asset>`** with `asset_type = core_types::procedural_mesh`
(this type already exists in `asset/type.h`). That choice does most of the work:

- `shared_resource_manager::mesh_layout_of`, `upload_mesh_slice` and `pack_mesh_into_ssbo` already take a
  `mesh_asset`. Procedural meshes reuse them unchanged, including the LOD and cluster layouts.
- `asset::mesh::generate_lods` / `generate_clusters` run on a worker's copy and produce the next version.
- Readers keep their `shared_ptr` for as long as they need it. A generator can publish version N+1 while the renderer
  is still uploading N. No locks are held during uploads or builds.
- `bake()` clones the current version, sets `asset_type = static_mesh`, keeps the generated LODs / clusters and the
  authored settings, then calls `i_asset_registry::register_runtime` and `save`. The caller swaps its entity from the
  procedural id to the returned asset. In the renderer, `ensure_mesh` for the new handle finds the asset already
  loaded and uploads it into a fresh slot. Re-keying the existing GPU slot instead of uploading it again is possible
  later but not worth it for an editor action.

`update()` validates on the calling thread: non-empty data, `indices.size() % 3 == 0`, indices in range, at most
`MAX_SUBMESHES_PER_MESH` submeshes, finite bounds, and the LOD rules below. Today this check runs on the render
thread. Moving it to the generator's thread takes it off the frame.

### Generator-provided LODs

A generator often knows its coarser levels better than a simplifier does: a heightfield can be sampled on a coarser
grid, an L-system can stop recursing earlier, a building can drop its window frames. `procedural_mesh_data::lods`
takes those levels as they are, and `update()` converts them into the `mesh_asset` LOD fields the renderer already
draws. The GPU side doesn't change at all.

| `procedural_lod` | becomes in `mesh_asset` |
|---|---|
| `indices` of level *n* | appended to `lod_indices`. Indices ≥ `vertices.size()` (the level's own vertices) are shifted by where those vertices start in `lod_vertices` |
| `vertices` | appended to `lod_vertices`. The address space is the existing `[vertices ++ lod_vertices]` |
| `submesh_index_counts` | `lod_ranges[n * submesh_count + s]`, `first_index` into `[indices ++ lod_indices]` as for generated levels |
| `screen_size` | `lod.levels[n].screen_size` |
| `error` (when `screen_size == 0`) | `lods[n].error = error / mesh extent`, then the existing `fit_screen_sizes` with `lod.pixel_error`, the same metric as generated LODs |
| `cull_screen_size` | `lod.cull_screen_size` |

Rules checked in `update()`:

- At most `MAX_LODS - 1` levels (the mesh table holds 8 switch sizes including LOD 0).
- Every level has one count per submesh, and a submesh may have 0 triangles at a level (it vanishes there). Indices
  are in range of `[LOD 0 vertices ++ own vertices]`.
- Switch sizes must decrease from level to level; `sanitize` clamps them, as it does for authored LODs.
- **Crack-free borders are the generator's job.** The renderer picks a level per instance, so neighbouring terrain
  chunks can be at different levels. The generator either keeps chunk-border vertices identical at every level
  (locked border) or adds skirts.

With provided LODs, the automatic LOD build is skipped. Virtual geometry can still be built on top
(`procedural_detail::virtual_geometry`). In that case the provided levels become the fallback for shadows and
translucents, exactly like generated levels on an asset. Baking keeps provided levels unchanged. Afterwards they are
indistinguishable from authored ones, and the mesh editor can regenerate them if you want.

There are two ways for a generator to do LOD, and the store supports both:

1. **Per-mesh levels** (this section): every level is resident, and the GPU picks one per instance per frame. Good for
   props, buildings and fixed-size terrain chunks.
2. **Generator-side LOD** (quadtree / clipmap terrain): the generator replaces a chunk with a coarser or finer chunk,
   each its own procedural mesh with `procedural_detail::none`. The renderer needs nothing for this. Generating
   distant chunks is cheaper because the fine version never exists.

Update-class contract (unchanged from multi_scene_renderer.md §10.1, with `streaming` now defined):

| Class | `update()` may be called | Renderer strategy |
|---|---|---|
| `immutable` | once | Uploaded like an imported mesh. Detail build starts right away |
| `occasional` | any number of times | New ranges per version, old ranges retired. Detail build after the mesh has stopped changing (settle time) |
| `streaming` | every frame | Phase 8: separate host-visible ring pool, never a detail build |

Where the store lives: one instance owned by `application`, reachable like the other core services (a `get_ref()`
helper next to `render::renderer::get_ref()`). It isn't a plugin. Every renderer, physics and the editor share it.

### Renderer plugin: procedural slots in the shared resource manager

Procedural meshes become ordinary `mesh_slot`s in `shared_resource_manager::m_mesh_slots`. The only differences are
the key (`procedural_mesh_id` instead of `asset::handle`) and the source (the store instead of the registry).

```cpp
// shared_resource_manager.h (new members)
void sync_procedural(const GLT::render::procedural_mesh_store& store);      // from begin_frame()
[[nodiscard]] shared_mesh_id procedural_mesh(GLT::render::procedural_mesh_id id) const;    // INVALID_SHARED_MESH: not resident (yet)

std::vector<shared_mesh_id>     m_procedural_slot_by_id{};      // dense: store ids are small and recycled
std::deque<pending_upload>      m_procedural_pending{};         // (id, shared_ptr<const mesh_asset>), latest version only
u64                             m_procedural_cursor = 0;
```

**Refactor first.** Split `load_mesh_internal(handle)` into the registry lookup and a source-agnostic
`upload_new_slot(const mesh_asset&) -> shared_mesh_id`. Do the same for `refresh_mesh`
(`reupload_slot(id, const mesh_asset&)`). Static and procedural meshes then go through the same upload code. This is a
pure refactor of the static path, so verify it on its own before adding anything (see Verification).

**`sync_procedural`**, once per plugin frame (it is plugin-level, so N scene renderers don't do it N times):

1. `drain_changes` from the store. For each changed id, replace its pending entry with the latest version (coalescing:
   ten updates between two frames cost one upload).
2. Upload pending entries in FIFO order (`memcpy` into the staging ring, one recorded copy per range into the
   upload command buffer) until a **per-frame byte budget** is used up (setting
   `procedural_upload_budget_mb`, default 32). A large world load then spreads over several frames instead of one
   long hitch. Entries that don't fit stay queued, and the previous version keeps drawing in the meantime.
3. Destroys: retire the slot exactly like `release_mesh` (ranges freed once the GPU is done).

**Mesh-table entries are immutable once published.** Today `refresh_mesh` rewrites `gpu_mesh_data[id]` in place while
frames in flight may read it. That's acceptable for the occasional editor edit, but a terrain update would rewrite
`vertex_offset` and `index_offset` while a frame reads them, and the two fields can tear. For procedural meshes an
update instead:

1. allocates new pool ranges (`allocate_mesh_ranges`) **and a new mesh-table index**,
2. writes geometry and the new table entry,
3. points `m_procedural_slot_by_id[id]` at the new index,
4. retires the old index and its ranges together.

A frame then sees either the complete old mesh or the complete new one. There is a second benefit:
`update_shadow_casters` compares `gpu_instance_data::mesh_index` (`same_caster`), so a changed index automatically
yields **precise dirty spheres** (old and new bounds of the instances using that mesh). Only the shadow tiles around
the regenerated chunk are redrawn. Today any re-upload bumps `m_mesh_generation` and invalidates every shadow map,
which would be fatal for terrain streaming. `refresh_mesh` for static meshes can adopt the same rule later. Doing so
fixes the same tearing and the same over-invalidation in the mesh editor, but it isn't required.

**Growable mesh tables (decided).** Mesh-table indices churn, so the fixed `MAX_MESHES = 4096` would run out with
thousands of terrain chunks plus retired indices waiting for the GPU. All four fixed tables become growable, the same
way the vertex, index and cluster pools already are:

| Table | Today | New |
|---|---|---|
| `m_mesh_data_buffer` (`gpu_mesh_data`) | `MAX_MESHES` entries, fixed | starts at 4096 entries, doubles on demand |
| `m_mesh_cluster_buffer` (`gpu_mesh_cluster_data`) | `MAX_MESHES`, fixed | grows together with the mesh table (same index) |
| `m_submesh_data_buffer` (`gpu_submesh_data`) | `MAX_SUBMESHES` = 131072, fixed, `growable = false` | `linear_pool` with `growable = true` |
| `m_mesh_slots` (CPU) | `MAX_MESHES` cap in `load_mesh_internal` | cap removed |

Growth is the `pool_resize` pattern: new buffer, GPU copy of the live part (in the upload command buffer, phase 2),
then retire the old buffer. Nothing else is needed:

- Every descriptor that points at these buffers is rewritten each frame (`update_cull_descriptors`,
  `update_virtual_geometry_descriptors`).
- No shader sizes an array by `MAX_MESHES`. It is injected as a define in `util/shader_compiler.cpp` but never
  used, so it can be removed.

A hard sanity limit (e.g. 2²⁰ meshes, matching `MAX_INSTANCES`) stays as a `VALIDATE`, not as an allocation size.

**Allocation and fragmentation.** Terrain churn frees and allocates ranges of similar but not identical sizes
constantly. With first-fit on the shared pools this fragments, and `try_compact()` calls `m_device.waitIdle()`, a
visible stall. Two measures:

- Round procedural allocations up to size classes (8 steps per power of two, at most 12.5 % waste) so a freed chunk
  range fits the next chunk.
- With device-local pools (phase 2), compaction becomes a GPU copy into a fresh buffer plus retire, the same as
  growth. No `waitIdle`, no reads from mapped memory, so it can run whenever fragmentation passes a threshold. It
  moves ranges, so it also has to write the mesh table (and the cluster table) into new buffers. Frames in flight then
  keep reading the old table, which points into the old pool.

### Scene renderer: one instance loop

`build_instance_buffer` loses its second pass. The resolve step becomes:

```cpp
shared_mesh_id mesh_id = INVALID_SHARED_MESH;
if (scene_instance.procedural_mesh != GLT::render::INVALID_PROCEDURAL_MESH)
    mesh_id = m_shared->procedural_mesh(scene_instance.procedural_mesh);       // O(1) vector lookup
else if (auto it = m_resident_meshes.find(scene_instance.mesh); it != m_resident_meshes.end())
    mesh_id = it->second;
const mesh_slot* slot = m_shared->get_mesh(mesh_id);
if (!slot)
    continue;
// ... everything below unchanged: materials per submesh (overrides included), virtual geometry, translucents
```

`ensure_scene_meshes` skips procedural instances. They aren't ref-counted per scene, because their lifetime belongs
to the generator (`store.destroy`).

**Removed:** `scene_renderer_procedural.inl`, `procedural_mesh_slot`, `MAX_PROCEDURAL_MESHES`,
`m_procedural_meshes` / `m_free_procedural_slots` / `m_procedural_draw_slots`, `m_asset_instance_count` (the whole
instance buffer is culled), `translucent_draw::procedural`, the procedural loops in the depth pre-pass, scene pass,
translucent pass, triangle density, LOD debug and virtual geometry debug passes, the three methods on
`i_scene_renderer`, and `scene_renderer_desc::procedural_mesh_capacity_bytes`.

**Debug colouring** ("light grey = procedural" in the LOD and virtual geometry views) moves into the data: one bit in
`gpu_mesh_data::_pad1` (rename it `flags`), which `cull.comp` / `virtual_geometry_cull.comp` OR into `firstInstance`
bit 25 (`INSTANCE_PROCEDURAL`, unchanged). That's one OR per emitted draw, not a branch. Alternatively, drop the colour
and let procedural meshes show their real LOD / cluster colours, which arguably is more useful once they have LODs.

### Detail builds (LODs and virtual geometry)

Built asynchronously on `GLT::thread_pool`, in the store (not the renderer), so a baked mesh carries its detail data
and a headless build gets it too.

| Update class | When a build starts | Result |
|---|---|---|
| any class, with generator-provided `lods` | no LOD build. Virtual geometry only if requested explicitly | the provided levels, converted in `update()` |
| `immutable` | right after `update()` | new version of the same id: geometry unchanged, `lods` / `clusters` filled |
| `occasional` | when no `update()` arrived for `settle_seconds` (default 0.5) | same. An `update()` during the build discards its result (version check) |
| `streaming` | never | — |

`procedural_detail::automatic` chooses by triangle count of LOD 0. The thresholds are settings, and the figures below
are starting points to measure against:

| Triangles | Detail |
|---|---|
| < 2 000 | none (LODs would cost more in draws than they save) |
| 2 000 – 50 000 | discrete LODs (`generate_lods`, default `lod_settings`) |
| > 50 000 | virtual geometry (`generate_clusters`; discrete LODs as fallback for shadows and translucents, as for assets) |

Rules:

- **Borders stay locked.** Terrain chunks are simplified independently, so their shared edges must not move.
  `LOD_FLAG_LOCK_BORDERS` is on by default for discrete LODs. For clusters the group outlines are locked, and the
  mesh's open border is part of those outlines (check this in `mesh_cluster.cpp`, step 2 of the hierarchy). The
  generator's side of the contract: neighbouring chunks emit identical border vertex positions.
- **Bounded cost.** At most `max(1, hardware_concurrency / 4)` builds run at once, so generators keep the rest of the
  pool. Pending builds are processed in order of creation.
- **Cheap to throw away.** `generate_*` can't be cancelled, so a stale result is simply discarded when it arrives.
  The settle time is what keeps an editor slider from starting a build every frame.
- Until a build lands, the mesh draws at LOD 0, as an imported mesh without LODs does today. When it lands, the
  renderer sees a new version and re-uploads (new ranges, new table index, as above).

### World and editor integration

- **ECS:** a `component::procedural_mesh { render::procedural_mesh_id id; std::vector<asset_ref> material_overrides;
  bool visible; }` next to `component::mesh`. `emits_instance` / `write_instance` fill `instance::procedural_mesh`
  instead of `instance::mesh`. Generators create the store entry and attach the component. The world never touches
  geometry.
- **Bake to static mesh** (editor action on an entity with `procedural_mesh`): `store.bake(id, path)`, then replace the
  component with `component::mesh { baked asset, same overrides }` and optionally destroy the store entry. Undoable
  like any other component swap.
- **Physics** (later, outside this document): `store.current(id)` is a `mesh_asset`, so a `triangle_mesh` collider for
  terrain can be built from the same data without the renderer being involved.

### The ray-tracing renderer

`renderer_vk_ray` doesn't implement procedural meshes today and doesn't need to for this work. Because the CPU side
lives in core, it can later mirror the store the same way (one BLAS per version, built on upload; `streaming` as a
refit), without any change to generators.

## Writing a generator plugin

A generator (terrain, props, ...) is an ordinary plugin. It needs nothing from the renderer and nothing from the ECS
plugin: only core headers.

```cpp
#include <render/procedural_mesh.h>     // GLT::render::procedural_meshes()
#include <world/i_world.h>

auto& store = GLT::render::procedural_meshes();

// once per mesh (any thread)
const GLT::render::procedural_mesh_id mesh = store.create({
    .update_class = GLT::render::mesh_update_class::occasional,         // terrain chunk: regenerated now and then
    .detail       = GLT::render::procedural_detail::automatic,          // or none when the generator does its own LOD
    .debug_name   = "terrain_3_7",
});

// whenever the geometry changes (any thread, typically a worker of GLT::thread_pool)
GLT::render::procedural_mesh_data data;
data.vertices  = ...;                   // GLT::asset::mesh::vertex
data.indices   = ...;
data.submeshes = { { 0, index_count, material_handle } };     // optional; empty = one submesh, default material
data.lods      = ...;                   // optional generator-made levels (procedural_lod)
if (auto result = store.update(mesh, std::move(data)); !result)
    LOG(warn, "{}", result.error().message)

// placing it (main thread): an entity draws it like a mesh asset
GLT::world::i_world& world = ...;
const auto entity = world.spawn();
world.set_procedural_mesh(entity, mesh);   // several entities may share one mesh

// when the chunk is unloaded
world.despawn(entity);
store.destroy(mesh);
```

Things the plugin has to know:

- **It owns its meshes.** The world references them but does not save them: on load the generator recreates its meshes
  and calls `set_procedural_mesh` again. A mesh the user wants to keep is baked in the inspector ("Bake to static mesh"),
  which turns the reference into an ordinary mesh asset that is saved.
- **Updates are cheap to issue, uploads are paced.** Ten updates between two frames cost one upload. The renderer uploads
  at most `PROCEDURAL_UPLOAD_BUDGET` per frame and keeps drawing the previous version until the new one is in.
- **Detail is automatic unless provided.** Without `data.lods`, meshes above `PROCEDURAL_LOD_MIN_TRIANGLES` get LODs (and
  above `PROCEDURAL_VIRTUAL_GEOMETRY_MIN_TRIANGLES` virtual geometry) in the background, once the mesh did not change for
  `PROCEDURAL_DETAIL_SETTLE_SECONDS`. Chunked terrain should keep chunk borders identical on both sides: the builders lock
  open borders, so neighbouring chunks stay crack-free at any level.
- **Editor undo keeps the reference**, since `snapshot_entities` stores it in a runtime-only trailer of the blob.

## Implementation phases

Each phase compiles, runs and keeps every existing scene rendering identically. Phases 1–3 only touch the static path
and make it faster or more robust. Phases 1–7 are the target. Phase 8 comes once something actually deforms every
frame.

| # | Phase | Main files | Done when |
|---|---|---|---|
| 1 | Refactor upload into source-agnostic `upload_new_slot` / `reupload_slot`. Log the memory type of every pool | `shared_resource_manager_mesh.cpp/.h` | Static scenes produce identical frame stats and timings (see Verification) |
| 2 | Device-local pools, staging ring, plugin-level upload command buffer + timeline wait; growth and compaction as GPU copies | `shared_resource_manager*`, `util/buffer.cpp`, `plugin.cpp`, scene renderer submit | Pools report `DEVICE_LOCAL`. Draw counts identical, GPU time same or lower (lower on non-ReBAR machines). No `waitIdle` left in the mesh code |
| 3 | Growable mesh, cluster and submesh tables; remove `MAX_MESHES` | `shared_resource_manager_mesh.cpp`, `shared/type.h` | 10 000 distinct meshes load. Tables grow without hitches |
| 4 | Core `procedural_mesh_store` with versions, validation, provided-LOD conversion, change log, bake | `core/src/render/procedural_mesh.{h,cpp}`, `application` | Unit test: create / update from two threads / provided LODs map to the right `mesh_asset` fields / drain / destroy / bake round-trip |
| 5 | Procedural slots in the shared manager: `sync_procedural`, upload budget, immutable table entries, size classes | `shared_resource_manager*` | Store meshes become resident. Static path unchanged |
| 6 | Scene renderer unification; delete the old procedural path and API; debug bit | `scene_renderer*.inl`, `scene/type.h`, `i_scene_renderer.h`, `cull.comp.glsl`, `virtual_geometry_cull.comp.glsl` | Procedural meshes are culled, cast shadows, support several materials, translucency and their own LODs. Fewer lines than before |
| 7 | Async detail builds + settle time + `automatic` policy; ECS component, bake action, demo generator (chunked terrain with provided LODs) | `procedural_mesh.cpp`, `world_ecs_entt`, `editor` | A generated 200k-triangle rock gets virtual geometry within seconds, a dragged slider never builds, terrain chunks stream in and out, a baked chunk reloads as a static mesh |
| 8 | `streaming` class: separate host-visible ring pool (option D above), per-frame mesh index chosen in the per-frame instance buffer, one extra draw batch per material | `shared_resource_manager*`, scene passes | Per-frame deforming mesh at 60 fps without allocations. Scenes without streaming meshes record the same commands as before |

## Verification

The "static meshes stay as optimised" promise needs measuring, not just arguing:

- **Memory placement (phase 1, before anything changes):** log the memory type flags of every pool after creation and
  growth, on a machine with ReBAR and on one without (or with ReBAR turned off in the BIOS). Load a large scene. If a
  pool reports `HOST_VISIBLE` without `DEVICE_LOCAL`, static meshes are drawn over PCIe today, and phase 2 should show
  a clear GPU-time drop on that machine.

- **Before/after on static-only scenes** (your heaviest test world, and the virtual geometry test mesh): compare
  `frame_stats` (`draw_calls`, `triangles`, `cluster_draws`, `gpu_time_ms`) and the Tracy CPU zones
  `build_instance_buffer`, `ensure_scene_meshes` and `begin_frame` over a fixed camera path. Draw and triangle counts
  must match exactly. Times within noise.
- **Terrain stress:** 2 000 chunks of ~8k triangles, regenerate 20 per frame while flying. Watch the frame time
  ceiling, upload queue depth, pool fragmentation (`live / high_water`), shadow tiles redrawn per frame (should track
  the regenerated chunks, not all tiles), and that no `waitIdle` happens.
- **Editor tool:** drag a slider that regenerates a 50k-triangle mesh every frame. Exactly one upload per frame, zero
  detail builds while dragging, one build after release.
- **Bake round-trip:** bake a procedural mesh with virtual geometry, restart, load the asset, then compare the cluster
  count and the rendered image.
- Vulkan validation layers and synchronisation validation on all of the above (the immutable-entry rule exists for
  synchronisation correctness).

## Open questions

1. Store location: `core/src/render/` or a new `core/src/geometry/`? The latter fits better if physics becomes a
   consumer.
2. `keep_cpu_copy` default `true` doubles memory for big terrains. Is that acceptable given bake and physics need it,
   or should terrain opt out?
3. ~~Mesh tables: growable or fixed?~~ Decided: growable (phase 3).
4. Staging ring size: 64 MB with a 32 MB per-frame budget is a starting point. Should it follow a renderer setting?
5. Debug colouring: keep "light grey = procedural", or show real LOD / cluster colours for procedural meshes too?
