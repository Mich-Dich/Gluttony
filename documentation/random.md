

sudo chown -R mich:mich ~/workspace/Gluttony

validate SSH:
```bash
ssh -T git@github.com
```



# TODO
[ ] ensure asset registry is thread save
[ ] renderer: shift mesh loading to thread pool (should also load needed material from registry)
[x] bring already open asset editor to front instead of opening anew one for the asset!
[ ] update the asset path reference of an editor if the asset is moved
[x] let CMake compile even without internet connection (currently it fails)
[ ] Add a function to the plugins (new interface) that lets the plugin manager check if that plugin is even supported for the current users architecture (like my main renderer needs WR-TR)




Next steps for the renderer:
[x] **Static accumulation** No motion vectors. Accumulate when the camera and scene are static, reset when they aren't. This alone gets your emissive cube demo looking clean and gives you the ping-pong history buffer you'll need later. It will be a visible, dramatic improvement on the demo, and it's a stepping stone, not a throwaway.

[x] **Bump samples per bounce to 2–4** This is the cheapest way to make the noise floor low enough that a later spatial pass will actually work. Traces more rays per frame, but it's trivial to try.

[x] **Full temporal with reconstructed motion vectors** Reconstruct reprojection from the RT hit data. Add disocclusion rejection. This is the point where the technique works in a moving game, not just a static demo.

[ ] **Spatial denoise pass on top** Joint bilateral or wavelet filter guided by the G-buffer you added in step 3. Cleans up disocclusion artefacts and any residual noise temporal couldn't reach. This is where you'd integrate OptiX if you're NVIDIA-only, or roll your own if you need portability.
















    // driven by the render settings UI
    struct visual_settings {

        u32                                                     ao_samples = 4u;
        f32                                                     ao_radius = 30.0f;
        f32                                                     ao_ray_bias = 0.005f;
        u32                                                     indirect_samples_base = 3u;
        u32                                                     temporal_max_history = 64u;
        f32                                                     temporal_clip_k = 2.0f;
    };
    





# Existing Editors

- Image viewer
- Console / Log viewer
- Statistics / Profiler HUD


# Planned Editors

- Settings / Preferences Editor
- Plugin manager UI
- Dependency graph viewer
- Keybinding editor
- Theme / style editor










    ## Roadmap rasterizer

    ### Step 1 — Light data infrastructure
    **Goal:** Represent lights on the CPU and upload them to a GPU buffer.

    - Define a `gpu_light` struct: position, radius, color, intensity, type (point/spot), spot direction, etc.
    - Add `std::vector<gpu_light> m_lights` and `u32 m_light_count` to `renderer`.
    - Create a persistent host-visible `util::allocated_buffer m_light_buffer`.
    - Add a method `update_lights()` that maps the buffer, copies the light array, and unmaps.
    - Call it from `begin_frame()` before building the render graph.
    - Add a simple ImGui panel to add/remove lights for testing.

    **Outcome:** Lights exist on the GPU and can be inspected.

    ---

    ### Step 2 — Cluster grid buffers + clear pass
    **Goal:** Allocate the data structures that light culling will write.

    - Choose a cluster grid: e.g., `16x16` tiles, `24` depth slices.
    - Compute cluster count from render size: `clusters_x = ceil(width / 16)`, `clusters_y = ceil(height / 16)`, `clusters = clusters_x * clusters_y * 24`.
    - Create two **persistent graph buffers**:
    - `m_light_grid_buffer`: `u32` per cluster (offset into light index list) + `u32` per cluster (count). Or two separate buffers.
    - `m_light_index_list_buffer`: `u32` array of light indices, sized `MAX_LIGHTS_PER_CLUSTER * cluster_count`.
    - Add a render graph pass `"clear_light_grid"` that uses `vkCmdFillBuffer` to zero the grid and the global light index counter.  
    This pass should run **before** light culling.
    - Use `builder.write(...)` for the buffers so the graph inserts barriers.

    **Outcome:** Light grid and index list are allocated and cleared each frame.

    ---

    ### Step 3 — Light culling compute pipeline
    **Goal:** Create the compute shader and pipeline that builds the light lists.

    - Write `light_culling.comp.glsl`:
    - Inputs: depth texture (sampled), camera UBO, lights SSBO, light grid SSBO, light index list SSBO.
    - For each cluster, compute its screen-space AABB and depth range.
    - Test each light against the cluster. For point lights, use sphere-vs-AABB. For spot lights, add cone test.
    - Use `atomicAdd` on a global counter and per-cluster count to append light indices.
    - Create a new descriptor set layout for light culling:
    - binding 0: depth sampler
    - binding 1: camera UBO
    - binding 2: lights SSBO
    - binding 3: light grid SSBO
    - binding 4: light index list SSBO
    - Create the pipeline layout and compute pipeline in `create_mesh_resources()` or a new `create_light_culling_pipeline()`.
    - Create per-frame descriptor buffers for the light culling pipeline.

    **Outcome:** A compute pipeline that can fill the light grid from depth.

    ---

    ### Step 4 — Integrate light culling into the render graph
    **Goal:** Run light culling after the depth prepass.

    - In `begin_frame()`, after the `"depth_prepass"` pass and before `"scene"`, add:
    ```cpp
    m_graph->add_pass("light_culling",
        [&](graph::pass_builder& builder) {
            builder.read(m_depth_target, graph::texture_usage::depth_sampled);
            builder.write(m_light_grid_buffer, graph::buffer_usage::storage_readwrite);
            builder.write(m_light_index_list_buffer, graph::buffer_usage::storage_readwrite);
        },
        [this](graph::pass_context& ctx) {
            // bind pipeline, descriptor buffer, dispatch
            const u32 cluster_count = ...;
            ctx.cmd.dispatch((cluster_count + 63) / 64, 1, 1);
        });
    ```
    - Ensure the `"clear_light_grid"` pass runs before this.
    - The render graph will insert the correct barrier from `eDepthStencilAttachmentOptimal` to `eDepthStencilReadOnlyOptimal`.

    **Outcome:** Light culling runs every frame using the depth prepass result.

---

### Step 5 — Forward pass consumes light lists
**Goal:** Update the forward shading pass to use the light grid.

- Extend `m_mesh_set_layout` with new bindings:
  - binding 2: lights SSBO
  - binding 3: light grid SSBO
  - binding 4: light index list SSBO
  - binding 5: cluster params UBO (grid size, depth slices, near/far)
- Update `m_frame_desc_buffers` creation to include these new bindings.
- Modify `mesh.frag.glsl`:
  - Compute cluster index from `gl_FragCoord.xy` and linear depth.
  - Fetch light count and offset from the light grid.
  - Loop over light indices, fetch light data, and accumulate lighting.
- Keep the existing depth prepass pipeline using the same layout; it just won’t use the extra bindings.

**Outcome:** Forward pass shades with many point/spot lights efficiently.

---

### Step 6 — Debug visualization for light culling
**Goal:** Validate the cluster grid and light counts.

- Add a debug compute or fragment shader that outputs a heatmap of light count per cluster.
- Add a render graph pass that writes to the scene color when a toggle is enabled.

**Outcome:** You can visually verify that light culling is working correctly.

---

### Step 7 — Shadow mapping (directional light first)
**Goal:** Add shadows to the forward pass

- Create a shadow map texture (depth only) and a light-space matrix
- Add a render graph pass `"shadow_pass"` that renders the scene from the light’s point of view. Reuse the depth-only pipeline
- Update `mesh.frag.glsl` to sample the shadow map and attenuate the directional light

**Outcome:** Directional shadows work; Forward+ has basic shadowing

- Extend to point/spot shadows using cube maps or multiple shadow maps


---

### Step 8 — Material system and textures
**Goal:** Support per-material albedo, normal, roughness, metalness.

- Extend `mesh_asset` with material indices per submesh.
- Create a material buffer (SSBO) and a texture array or bindless descriptor set.
- Update `mesh.vert.glsl` to pass `material_index` to the fragment shader.
- Update `mesh.frag.glsl` to sample material textures and compute PBR lighting.
- Update descriptor set layout and buffers accordingly.

**Outcome:** Forward+ renders textured PBR materials.

---

### Step 9 — HiZ occlusion culling
**Goal:** Use the existing HiZ code in `cull.comp.glsl`.

- Build a HiZ pyramid after the depth prepass (compute pass).
- Keep the previous frame’s HiZ texture alive.
- In the cull compute shader, add binding 6 for the HiZ sampler.
- Use the previous frame’s HiZ to occlude instances before emitting draw commands.
- Handle the first frame (no HiZ) by disabling occlusion.

**Outcome:** GPU-driven culling also rejects occluded objects.

---

### Step 10 — Post-processing
**Goal:** Complete the render pipeline with tonemapping, bloom, etc.

- Add a post-processing pass that reads `m_scene_color` and writes to the backbuffer.
- Implement tonemapping (ACES, Reinhard) and gamma correction.
- Optionally add bloom, SSAO, or TAA.
- Use the render graph to manage intermediate textures.

**Outcome:** A polished Forward+ renderer ready for a game engine.

---

## Summary

Your existing render graph, depth prepass, and GPU-driven culling give you a great foundation. The critical path to Forward+ is:

1. Lights on GPU  
2. Cluster grid buffers  
3. Light culling compute  
4. Integrate into render graph  
5. Update forward shader to use light lists  

After that, shadow mapping, materials, HiZ, and post-processing are natural extensions. Each step is roughly the same size: one new shader, one new pipeline, or one new render graph pass. This keeps the work incremental and testable.
