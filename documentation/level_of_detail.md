# Level of Detail (LOD)

Static meshes carry up to 8 levels of detail (`GLT::asset::mesh::MAX_LODS`), LOD 0 included. The levels are generated
automatically with [meshoptimizer](https://github.com/zeux/meshoptimizer). They are generated once on import and can be
tuned and regenerated in the mesh editor. The renderer picks a level per instance on the GPU.

Discrete LODs are one of the two detail modes of a mesh. The other one is virtual geometry, see
[virtual_geometry.md](virtual_geometry.md); the mesh editor's "Geometry Detail" card switches between them.

## How it fits together

| Piece | Where | What it does |
|---|---|---|
| Data types | `core/src/asset/mesh.h` | `lod_settings` (authored), `lod_level` / `lod_range` (generated), chunks `0x0008`–`0x000B`, `INSTANCE_FLAG_FORCE_LOD_MASK` |
| Generator | `core/src/asset/mesh_lod.{h,cpp}` | `build_lods`, `generate_lods`, `fit_screen_sizes`, `sanitize` (meshoptimizer is linked into the core only) |
| Import | `plugin/asset_factory_mesh_assimp` | Generates the levels with default settings. The import option "LOD levels" (default 4, 1 = none) sets the count |
| Asset I/O | `plugin/asset_handler_mesh` | Reads and writes the four optional chunks. Inconsistent data falls back to LOD 0 |
| GPU | `plugin/renderer_vk_rasteriser` | Submesh table laid out as `[lod][submesh]`. `cull.comp` / `shadow_cull.comp` select the level. The CPU twin `select_mesh_lod` handles translucents |
| Editor | `plugin/editor/src/window/mesh_editor.*` | "Level of Detail" inspector section, the LOD list in the outliner, the preview LOD selector and the wireframe overlay |

### Index layout

LOD 0 stays in `mesh_asset::indices` / `submeshes`, so colliders and other geometry consumers are unaffected. LOD 1..N
live in `lod_indices`. A `lod_range` addresses the concatenated stream `indices ++ lod_indices`, so LOD 0's ranges equal
the submeshes. The renderer uploads both arrays back to back and uses the ranges unchanged. All levels share the vertex
buffer: the simplifier only removes triangles and does not create vertices.

### Generation

- Each submesh is simplified on its own, so material boundaries survive, and each runs on a compacted local vertex set.
  The simplifier is attribute aware: normals and UV0 are weighted by `normal_weight` / `uv_weight`.
- Every level is simplified **from LOD 0** in parallel, so the reported error is exact, not accumulated.
- A level stops at `triangle_ratio × LOD 0` triangles **or** at `max_error`, whichever comes first. `max_error` is
  relative to the mesh extent (0.01 = 1 %). The error limit is what keeps the image intact.
- When a level keeps more than 95 % of the previous level's triangles, generation stops. The mesh can therefore end
  up with fewer levels than requested; the editor says so.
- Options:
  - `LOCK_BORDERS` (default on) prevents cracks between submeshes.
  - `PRUNE` (default on) may drop tiny disconnected parts within the error limit.
  - *Keep small parts* (default; `ALLOW_SUBMESH_REMOVAL` off): a submesh's error is also capped at 5 × `max_error` of
    **its own** size, small parts are never pruned, and a submesh the simplifier still removes keeps the previous
    level's geometry. This stops struts and landing feet from collapsing or vanishing on coarse levels.

### Selection metric

`screen size = bounding-sphere diameter / viewport height` (`radius × |P11| / distance`, 1.0 = the mesh spans the full
height).

- **Choosing the level:** level *i* is used once the screen size is below `levels[i].screen_size`.
- **Culling:** `cull_screen_size > 0` stops drawing the mesh entirely when it is smaller than that.
- **Auto screen sizes** (default): each level's switch size is computed from its measured error, so the change stays
  below `pixel_error` pixels at 1080p. The formula is `s = 2 r · px / (error_world · 1080)`.
- **Global multiplier:** the renderer setting *Level of Detail → LOD distance scale* multiplies every projected size.
- **Shadows:**
  - Shadow maps pick their level from the light's point of view, with the shadow tile as the "screen": its resolution
    relative to 1080, a perspective view for local lights and an orthographic one for the sun.
  - On top of that, shadows add the mesh's `shadow_lod_bias` (default +1).
  - Because the shadow LOD does not depend on the camera, the shadow cache stays valid while the camera moves.
- **Forcing a level:** `instance::flags & INSTANCE_FLAG_FORCE_LOD_MASK` = level + 1 forces a level. The mesh editor
  preview uses this.

### Editing at runtime

The editor changes the live `mesh_asset` and bumps `mesh_asset::revision`. `shared_resource_manager::refresh_meshes()`
checks this every 4 frames and re-uploads changed meshes into the same slot. Old ranges are retired, so in-flight frames
stay valid. Instances and scene renderers need no notification. `mesh_generation()` invalidates cached shadow maps.

Settings fall into two groups:

- **Live:** screen size, cull size, shadow bias and pixel error. They apply immediately.
- **Need regeneration:** count, ratio, max error, the flags and the weights. Use *Regenerate LODs*, which runs in the
  background.

*Save* writes the LOD chunks with the asset.

## Future upgrades

Ordered roughly by value for effort.

1. **Dedicated LOD models (artist authored).**
   - **Import:** import `name_LOD1.fbx` …, or FBX LOD groups (`LODGroup` nodes / glTF `MSFT_lod`), into the same chunks.
     A level would then carry its own vertex range: add `vertex_offset` / `vertex_count` per `lod_level` and keep LOD 0
     first. The GPU side already supports this, since `lod_range` is just an index range and `vertex_offset` is per
     mesh. Per-level vertex offsets would need either a per-level base vertex in `SubmeshData` (`_pad0` is free) or
     re-based indices.
   - **Editor:** "Replace level with file…" per row, plus a source column ("generated" / "imported").
   - **Mixing:** allow mixed sets, for example imported LOD 1 and generated LOD 2+ (simplified from the imported level).
2. **Smooth transitions.** Dithered cross-fade over a few frames, or screen-space dither by a fade factor written per
   draw, to hide popping. The GPU needs the previous level per instance (persistent per-instance state buffer) and a
   fade value in the instance data.
3. **Hysteresis.** A small dead band around each switch size, to avoid flicker when an object sits exactly at a
   threshold.
4. **Per-instance / per-component overrides.**
   - Expose `force_lod` / `lod_bias` on the mesh component, so gameplay can pin hero objects.
   - Add a world-level LOD bias, for example quality presets.
5. **Resolution-aware switching.** An option to scale the main view's metric by `viewport_height / 1080`, so the pixel
   error stays constant at 4K. This is already done for shadow tiles.
6. **LOD-aware stats and debug view.**
   - A render mode that tints instances by level.
   - Per-level draw and triangle counters in the stats window. `DrawStats` would need one slot per level.
7. **Vertex-buffer trimming for LODs.** Coarse levels touch few vertices but share the full buffer. Re-index each level
   into its own compact vertex range with `meshopt_optimizeVertexFetch` for better cache and fetch efficiency, at the
   cost of duplicated vertices. Pairs well with (1).
8. **Impostors / billboards as the last level.** Octahedral impostor atlas baked in the editor, for vegetation and
   distant props.
9. **Cluster LOD: done, see [virtual_geometry.md](virtual_geometry.md).** A mesh uses either the discrete levels
   described here or virtual geometry: a cluster hierarchy that the GPU selects from per cluster and per frame
   (continuous, crack-free detail plus frustum and backface culling per cluster). The mesh editor switches between
   the two ("Geometry Detail"). For a mesh in virtual geometry mode the discrete levels are only the fallback (shadows,
   translucent submeshes). Open ends (occlusion culling, cluster shadows, mesh shaders) are listed in that document.
10. **Skeletal meshes.** The same chunks apply. Simplification must respect bone weights (add them as attributes or lock
    vertices on influence boundaries), and animation LOD (bone count reduction) would come with it.
11. **Streaming.** Keep only coarse levels resident for far or streamed-out regions and fetch fine levels on demand.
    This needs per-level chunks to be independently loadable (they already are separate chunks) and GPU pool
    residency tracking.
