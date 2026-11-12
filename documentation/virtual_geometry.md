# Virtual Geometry

Virtual geometry draws a static mesh as a set of small triangle clusters whose level of detail is chosen **per cluster
and per frame** on the GPU, instead of switching the whole mesh between a handful of LOD levels. A dense mesh then
costs roughly the triangles that are visible at their size on screen: the part next to the camera is drawn at full
detail, the far side of the same object coarsely, and clusters outside the view or facing away are not drawn at all.

A static mesh uses **one of two detail modes**: discrete LODs (see [level_of_detail.md](level_of_detail.md)) or virtual
geometry (`asset::mesh::detail_mode`, `mesh_asset::detail()`). The mode is chosen per mesh, on import or in the mesh
editor. In virtual geometry mode the discrete levels are no longer authored; they stay in the asset as a fallback for
shadows, translucent submeshes and frames or renderers without the cluster path. A mesh in discrete LOD mode carries
no cluster data.

## How it fits together

| Piece | Where | What it does |
|---|---|---|
| Data types | `core/src/asset/mesh.h` | `detail_mode`, `cluster_settings` (authored), `cluster` / `cluster_submesh` (generated), chunks `0x000D`–`0x0010`, `mesh_asset::detail()` / `uses_virtual_geometry()` |
| Builder | `core/src/asset/mesh_cluster.{h,cpp}` | `build_clusters`, `generate_clusters`, `set_detail_mode`, `validate_clusters`, the CPU twin of the selection rule (`cluster_in_cut`, `measure_cut`, `collect_cut`) and of the debug colours. Uses meshoptimizer's `clusterlod` |
| Import | `plugin/asset_factory_mesh_assimp` | Import option "Virtual geometry" (default off) puts the mesh in virtual geometry mode and builds the clusters |
| Asset I/O | `plugin/asset_handler_mesh` | Reads and writes the four optional chunks; cluster data only for meshes in virtual geometry mode. Data that fails `validate_clusters` is dropped: the mesh renders with its LODs |
| GPU data | `shared_resource_manager` | Cluster triangle lists behind the mesh's indices in the shared index buffer, cluster records in a growable pool, one table entry per mesh |
| GPU pass | `scene/scene_renderer_virtual_geometry.inl`, `virtual_geometry_cull.comp.glsl` | Selects and culls clusters, writes one indexed indirect draw per visible cluster (plus, per command, the index of its cluster for the debug views) |
| Debug views | `scene/scene_renderer_virtual_geometry_debug.inl`, `virtual_geometry_debug.vert/.frag`, `virtual_geometry_debug_triangles.frag` | Render modes of the category "Virtual Geometry": one pass that redraws the frame's commands coloured per cluster, level, triangle or error |
| Editor | `plugin/editor/src/window/mesh_editor.*` | "Geometry Detail" mode switch, then only the active mode's cards; for virtual geometry: build, statistics, settings, fallback, debug views |

## The cluster hierarchy

`build_clusters` works per submesh, so a cluster always has exactly one material.

1. The submesh is split into clusters of up to `cluster_triangles` triangles (default 128).
2. Neighbouring clusters are put into groups of about 16. Each group is simplified to half its triangles with the
   **outline of the group locked**, and the result is split into new clusters.
3. Step 2 repeats on the new clusters until nothing can be merged any more.

Because a group's outline never moves, the coarse clusters made from a group fit exactly against whatever is drawn
next to them, at any level. The result is a DAG: every cluster knows the error of the group it was made from
(`lod_error`, 0 for the source triangles) and the error of the group that replaces it with coarser clusters
(`parent_error`, `CLUSTER_ERROR_NONE` at the top). Errors are in mesh units and only grow towards the root.

Simplification only removes triangles, so clusters index the mesh's existing vertices. The hierarchy costs about twice
the source index data plus 80 bytes per cluster; no vertices are added.

`Lock material seams` (default on) keeps the vertices that several submeshes share in place. Submeshes are simplified
independently, so without the lock their common border moves differently on both sides and cracks open between
materials. The price is a higher triangle floor along those seams at a distance.

## Selection rule

For every cluster of every instance on the path, each frame:

```
projected(sphere, error) = error * scale * error_scale * (|P11| / 2) / max(distance(sphere centre, camera) - radius, near)

draw  <=>  projected(lod_bounds, lod_error) <= threshold   and   projected(parent_bounds, parent_error) > threshold
threshold = pixel error / viewport height
```

A cluster is drawn when it is detailed enough itself while its coarser replacement is not. The clusters made from a
group and the clusters that group replaces evaluate the same sphere and error, so for any camera exactly one version of
every piece of surface is drawn: no holes, no overlaps, no cracks between clusters of different levels. The shader marks
the expression `precise` so both sides get bit-identical results.

Selected clusters are then culled against the frustum (their own bounding sphere) and by their backface cone. Cone
culling is skipped for instances with non-uniform scale, shear or mirroring and for double-sided materials.

## Renderer integration

The path is separate from the instance cull on purpose: its own work list, command ranges, buffers and pass. A frame
without virtual geometry instances records exactly what it recorded before.

- **CPU, `build_instance_buffer`:** an instance goes on the path when the renderer setting is on, its mesh is in
  virtual geometry mode with built clusters, and no LOD is forced. It gets `INSTANCE_FLAG_VIRTUAL`, is listed as work items of 64 clusters, and each of
  its opaque submeshes reserves `cluster_submesh::max_visible` commands in its material's cluster command range.
  `max_visible` is an upper bound of clusters that can be selected at once, computed at build time.
- **GPU, `virtual_geometry_cull`:** one workgroup per work item, one thread per cluster. Runs between
  `reset_draw_count` and `cull`. `cull.comp` skips flagged instances in the main view.
- **Draws:** the depth pre-pass, the scene pass and the triangle density pass issue one extra
  `drawIndexedIndirectCount` per material over the cluster commands, with the same pipelines, vertex and index buffer.
- **Shadows:** unchanged. Shadow views draw the discrete levels of every mesh, so the shadow cache is not affected.
- **Translucent submeshes** of a virtual geometry mesh keep going through the sorted translucent pass with the LODs.
- **Budget:** a frame reserves at most `MAX_VIRTUAL_DRAW_COMMANDS` (about one million, 20 bytes each per frame in
  flight) cluster commands. Instances beyond that stay on the LOD path for that frame.
- **Fail-safe:** if the shader or pipeline cannot be created, the path is off and every mesh uses its LODs.

Settings (renderer, "Virtual Geometry"): on/off, pixel error (default 1), backface cluster culling.

## Debug views

Four render modes in the category **Virtual Geometry**. They are ordinary render modes, so every viewport offers them
(render mode menu of the world viewport and of the mesh editor preview):

| Mode | Shows |
|---|---|
| *Geometry Clusters* | Every drawn cluster in its own colour (stable per cluster of a mesh) |
| *Cluster Levels* | The hierarchy level of each drawn cluster: green = source triangles, then yellow, orange, red, magenta, purple, blue, cyan (about half the triangles per level) |
| *Triangles* | Every drawn triangle in its own colour, for all meshes. Needs the `geometryShader` device feature (`gl_PrimitiveID`); without it the view shows cluster colours |
| *Screen Error* | The simplification error of each drawn cluster on screen as a share of the allowed pixel error: dark blue = source triangles, blue → green → yellow → red = 0 → 100 % |

Meshes drawn with discrete LODs are dark grey and procedural meshes light grey (except in *Triangles*).

How it works: while one of the modes is active the lit path still runs (the material shaders draw flat grey), and the
pass `virtual_geometry_debug` at the end replaces the image. It redraws exactly the commands the lit path drew
(culled LOD draws, cluster draws, translucents, procedural meshes) with one pipeline and clears and rewrites the
depth buffer, so debug lines drawn afterwards are tested against what the view shows. A cluster draw finds its
cluster through `gl_DrawID`: `virtual_geometry_cull.comp` writes, parallel to its commands, the index of the cluster
each command draws (binding 11), and the pass pushes the first command of each indirect call. What is not shown:
alpha-masked materials are drawn solid, and back faces of single-sided materials are visible where nothing is in
front of them.

Other modes: *Level of Detail* colours clusters by hierarchy level too (in the LOD palette). *Triangle Density*
includes the cluster draws. The GPU pass list shows the clusters and triangles drawn per target.

The mesh editor adds two overlays that it draws itself from the CPU twin of the selection (no frustum or backface
culling): *Cluster bounds* (the culling sphere of every selected cluster, in the colour it has in *Geometry Clusters*)
and *Wireframe* (the edges of the selected clusters' triangles).

## Using it

- **Import:** tick "Virtual geometry" in the import options, or
- **Mesh editor:** inspector → *Geometry Detail* → *Mode* → *Virtual Geometry*. The clusters are built in the background
  (roughly 0.2 s per 100k triangles in a release build). *Save* writes them with the asset. Switching back to
  *Discrete LODs* removes the clusters.
- The inspector then shows the virtual geometry cards instead of the LOD cards: *Virtual Geometry* (statistics,
  rebuild), *Cluster Detail* (detail scale, cull size: apply immediately), *Cluster Build* (cluster size, seam lock,
  simplifier weights: need *Rebuild clusters*), *Fallback Levels* (the discrete levels, read-only; generated with the
  mesh's LOD settings) and *Debug Views*.
- The preview shows what the camera selects ("Virtual geometry · N clusters · M tris"). Locking the preview to a
  fallback level (outliner or the pill in the preview) shows that discrete level instead.

It pays off for meshes with many triangles that are seen at varying distances or only partly. A mesh that fits into a
few clusters gains nothing over its LODs.

## Limitations and future upgrades

1. **Shadows from clusters.** Shadow maps still draw the discrete levels (LOD 0 unless forced). Running the cluster
   pass per shadow view, with the error measured from the light, would cut their triangle counts the same way.
2. **Occlusion culling.** Clusters hidden behind other geometry are still drawn. A hierarchical depth buffer from the
   previous frame, tested per cluster in the same pass, is the natural next step.
3. **Hierarchical selection.** Every cluster of every instance on the path is tested each frame. `clusterlod` can
   build a BVH over the groups (`clodBuildHierarchy`) so whole subtrees are skipped; worth it for very many instances.
   It would also give exact command counts and remove the fixed draw budget.
4. **Seams between materials.** Building one hierarchy across all submeshes and splitting clusters by material
   afterwards would simplify seams consistently on both sides instead of locking them.
5. **One draw per cluster.** Each visible cluster is one indirect draw. A mesh-shader path, or expanding the visible
   clusters into one index buffer per material, would lower the per-draw cost.
6. **Index memory.** The level 0 clusters repeat the triangles of LOD 0. Reordering LOD 0 into cluster order would let
   both share one index range.
7. **Normals of coarse clusters** are the source vertices' normals (as with the LOD mode "Keep source").
8. **Streaming.** All clusters of a resident mesh are resident. Loading coarse levels first and fine ones on demand
   needs per-level chunks and residency tracking on the GPU.
9. **Skeletal meshes** are not handled, as with the discrete LODs.
