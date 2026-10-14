
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
[ ] render debug stuff in lines (light cones/radius, Colliders, AABBs, ...)
[ ] HiZ occlusion culling
[ ] Post-processing: Complete the render pipeline with tonemapping, bloom, etc.


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






























## Roadmap

### Phase 1 — Contract package + shader compiler service

**Goal:** Make it possible to compile Slang material source through a renderer-provided compiler.

Tasks:
- Add `material_contract/v0/` Slang module: `Globals`, `vertex_input/output`, `surface_input/output`, `material_textures`, helpers.
- Add `i_shader_compiler` interface and `compiled_shader` type.
- Implement Vulkan `i_shader_compiler`:
  - Slang → SPIR-V.
  - Produce reflection schema.
  - Produce packed default params.
  - Produce `implementation_hash()` covering Slang version, SPIR-V target, reflection schema, renderer codegen.
- Add `matc` skeleton:
  - Reads source + blend mode.
  - Calls registered renderer shader compiler.
  - Validates `MATERIAL_CONTRACT_VERSION`.
  - Writes derived chunks.

Exit criteria:
- A test Slang material compiles to SPIR-V + reflection + default params.
- `matc` runs headlessly on one test material.

---

### Phase 2 — Asset format and material handlers

**Goal:** Store the new contract data in material assets and add material instances.

Tasks:
- Update `asset/material.h`:
  - Add chunk IDs `0x0302`–`0x0306`.
  - Replace or deprecate fixed `material_params` C++ struct in the asset path.
  - Add `material_instance` type and chunks `0x0310`, `0x0311`.
- Update material handler:
  - Load Slang source, bytecode, reflection, compiler hash, blend mode, texture refs.
  - Validate blend mode against `alpha_test` presence.
  - Repack `CHUNK_MATERIAL_PARAMS` when reflection schema changes.
- Add material instance handler:
  - Base UUID + override list.
  - Validate overrides against base reflection schema.
- Keep legacy load path for old assets until migration is complete.

Exit criteria:
- A material asset round-trips all new chunks.
- A masked material without `alpha_test` fails import.
- A material instance overrides a scalar parameter.

---

### Phase 3 — `matc` import pipeline integration

**Goal:** Make material import/save automatically compile and cache.

Tasks:
- Hook `matc` into material import and save.
- Write compiler hash into `CHUNK_MATERIAL_COMPILER_HASH`.
- On compiler-hash mismatch:
  - User assets: recompile and update in place.
  - Engine-shipped assets: cache under per-renderer cache directory.
- Packaged builds: bake derived chunks for shipping renderer.
- Add error path for missing compiler / unsupported contract version.

Exit criteria:
- Editing Slang source regenerates bytecode, reflection, params, hash.
- Switching renderer triggers recompile or cache lookup.

---

### Phase 4 — Renderer material pipeline

**Goal:** Replace hardcoded PBR GLSL with contract-driven material pipelines.

Tasks:
- Add renderer material pipeline cache keyed by:
  - material asset / compiler hash
  - entry point set
  - blend mode
  - render pass variant
- Compile `CHUNK_MATERIAL_BYTECODE` into `vk::ShaderModule`.
- Implement binding for:
  - Contract `Globals` UBO.
  - Reflection-driven `material_params` buffer.
  - Six texture slots + sampler.
- Replace `mesh.vert.glsl` / `mesh.frag.glsl` material path with compiled material stages.
- Keep hardcoded PBR fallback for legacy materials until migration ends.

Exit criteria:
- A Slang `surface_evaluate` material renders correctly.
- Material params are packed per reflection offsets, not fixed C++ struct.

---

### Phase 5 — Entry points, alpha test, vertex transform

**Goal:** Implement the full v0 entry-point surface.

Tasks:
- `surface_evaluate`: mandatory fragment path.
- `alpha_test`: required for masked, forbidden otherwise.
- `vertex_transform`: optional vertex path.
- Integrate `alpha_test` into:
  - depth prepass
  - shadow passes
  - local shadow atlas passes
- Add pipeline variants for masked/opaque/translucent.
- Resolve culling with `vertex_transform`:
  - Conservative bounds, or
  - Disable GPU culling for displaced materials, or
  - Add a contract-level displacement bound.
  - This is an open design issue; do not silently ignore it.

Exit criteria:
- Masked material discards fragments in main and shadow passes.
- Vertex-displaced material does not get incorrectly culled.

---

### Phase 6 — Material instances and editor tooling

**Goal:** Make parameters editable and instance overrides usable.

Tasks:
- Expose reflection schema to editor.
- Editor reads `material_params` from reflection, not fixed C++ fields.
- Defaults ownership: `CHUNK_MATERIAL_PARAMS` is authoritative after first compile.
- Instance overrides:
  - Preserve values by `(parameter_name_hash, type)`.
  - Drop removed params.
  - Reset type-changed params.
- Add `pbr.mat_asset` as engine-shipped Slang material.

Exit criteria:
- Editor exposes sliders from reflection.
- Instance override survives base material schema evolution.

---

### Phase 7 — Renderer switching, caching, packaged builds

**Goal:** Make compiler hash and renderer portability robust.

Tasks:
- Store `implementation_hash()` in every material.
- On mismatch:
  - Recompile from `CHUNK_MATERIAL_SLANG_SRC`.
  - Update asset or external cache.
- Implement per-renderer cache path for engine-shipped read-only assets.
- Packaged builds bake only shipping renderer bytecode.
- Add fallback material when packaged build hits mismatch.

Exit criteria:
- Vulkan material asset does not attempt to use D3D12 bytecode.
- Compiler update transparently recompiles.

---

### Phase 8 — Migration and cleanup

**Goal:** Remove legacy path once all assets are reimported.

Tasks:
- Migrate existing materials through new importer.
- Rebuild `pbr.mat_asset` as Slang contract material.
- Remove legacy `material_params` C++ struct from runtime asset path.
- Remove hardcoded `mesh.frag.glsl` PBR path.
- Update tests and docs.

Exit criteria:
- No material asset uses old `0x0300`-only layout.
- No renderer path consumes fixed `material_asset::params`.

---

## Suggested PR order

1. Add contract Slang package + `i_shader_compiler`.
2. Add `matc` skeleton + reflection schema format.
3. Add new material chunks to `material.h` and material handler.
4. Add material instance handler.
5. Integrate `matc` into import/save.
6. Add renderer material pipeline cache and Globals UBO.
7. Add `surface_evaluate`.
8. Add `alpha_test` + blend mode validation.
9. Add `vertex_transform` and culling strategy.
10. Add editor reflection UI and instance overrides.
11. Add compiler-hash cache and packaged build path.
12. Migrate assets, remove legacy.

---

## Additional files I need to make this exact

I can keep the roadmap generic, but to turn it into concrete API-level tasks I need these files:

- `asset/i_asset_handler.h`
- `asset/i_asset_registry.h` and the registry plugin interface
- `asset/header.h`
- `chunk_reader` / `asset_writer` definitions
- `util/shader_compiler.h` / `.cpp`
- `plugin_system/plugin_manager.h`
- `plugin_system/i_project_manager.h`
- `render/i_renderer.h`
- `render_graph/render_graph.h`
- `util/descriptors.h`
- `util/device.h`
- `util/buffer.h`
- `util/shader.h`
- `util/builders.h`
- `renderer_lights.inl`
- `renderer_clusters.inl`
- `renderer_light_culling.inl`
- `renderer_shadow.inl`
- `renderer_debug_view.inl`
- `renderer_swapchain.inl`
- `settings.inl`
- Any existing material importer / editor material UI code
- Any existing Slang SDK integration or build configuration
- A sample `pbr.mat_asset` or current material source/import file

With those, I can turn this roadmap into a file-by-file migration plan with exact function signatures and chunk read/write changes.








































# Roadmap v2 — Textual Concatenation

Decision locked: `renderer::compile()` builds `prefix + material_source + suffix` per entry point, compiles each as one module, and returns the resulting SPIR-V. This affects Phase 1's struct shape, Phase 2's signature, Phase 3's chunks, and rewrites Phase 5 entirely. Phases 4, 6, 7, 8 are largely unchanged.

---

## Blast radius of the decision

| Component | Change |
|---|---|
| `GLT::render::compiled_shader` | One `bytecode` → three (`vertex`, `fragment`, `depth`) |
| `renderer::compile()` | One concatenation + one Slang compile → three |
| `material_compiler::run()` | Writes three bytecode chunks instead of one |
| Material handler `serialize`/`deserialize` | New chunk reads/writes |
| `renderer_material.inl` | Per-material pipeline + shader modules |
| `renderer_gpu_driven.inl` | Cull output must be bucketed by material |
| `renderer.inl` `"scene"` pass | Loops over material buckets |
| `renderer_shadow.inl` | Masked materials need a per-material depth pipeline |
| New artifact | `renderer_shading_prefix.slang` + three suffix files |

Unaffected: reflection schema layout, `material_asset`/`material_instance_asset` structs, `material.cpp` helpers, `Globals` UBO design, hot-reload flow, material instance merging, editor integration.

---

## Phase 1 — Reflection schema producer

**Still the critical path.** No shape change from the concat decision; only the `compiled_shader` struct grows.

Tasks:
1. Add `renderer_shading_prefix.slang` and the three suffix files (`renderer_vertex_suffix.slang`, `renderer_fragment_suffix.slang`, `renderer_depth_suffix.slang`) as embedded strings or as on-disk assets. Suggested location: `plugin/renderer_vk_rasteriser/asset/shader/vk_rasterizer/slang/`.
   - The prefix declares `renderer_*`-prefixed helpers only. **Naming convention required**: Slang doesn't offer C++-style namespaces cleanly inside a module, so any helper the prefix declares must be uniquely named (or wrapped in a `struct` with static methods).
   - The prefix does *not* declare `Globals` or `material_textures`; it `import material_contract_v0;` and uses them.
2. Extend `util::slang_compiler` so callers can reach the linked `IComponentType` for reflection. Two options: return the `ComPtr<IComponentType>` alongside `raw_slang_result`, or add a second entry point `compile_program(source, entry_name)` that returns both SPIR-V and the linked type. Prefer the second — `compile_surface_evaluate` becomes a special case of it.
3. Implement `build_reflection_schema(IComponentType*)`:
   - `linked->getLayout()->findTypeByName("material_params")`
   - Walk fields in declaration order; emit `reflection_param[]` with `{name_hash, name_offset, type, byte_offset, byte_size, array_count=1}`.
   - `param_type` mapping table is fixed: `float→f32`, `float2→f32x2`, `float3→f32x3`, `float4→f32x4`, `int→i32`, `uint→u32`, `bool→boolean`, `float2x2/3x3/4x4`.
   - `byte_offset` computed via `align_up(prev_offset + prev_size, param_type_alignment(type))`.
   - Reject unknown types with `unsupported_param_type`.
4. Implement `pack_defaults_from_linked(linked)` — walk the layout, read each field's default bytes, write at its `byte_offset`.
5. Populate `entry_flags` from declared entry points: `surface_evaluate`, `vertex_transform`, `alpha_test`. Bitmask matches `entry_bits` in `asset/material.h`.

**Compiled shader struct (new):**
```cpp
struct compiled_shader {
    std::vector<std::byte> vertex_bytecode;    // always present (passthrough if no vertex_transform)
    std::vector<std::byte> fragment_bytecode;  // always present
    std::vector<std::byte> depth_bytecode;     // empty unless blend == masked
    std::vector<std::byte> reflection;
    std::vector<std::byte> default_params;
};
```

**Deliverable:** feeding `ocean.slang` from Appendix B through `renderer::compile()` produces three valid SPIR-V modules and a valid reflection blob.

**Spike:** confirm Slang handles concatenation with two `import material_contract_v0;` lines (one from prefix, one from material source). If it complains, strip the material's import line before concat. Cheap to test early.

---

## Phase 2 — Material compiler completes

Signature change (already agreed): drop `chunk_reader*`, take two spans.

```cpp
std::expected<void, GLT::asset::import_error> run(
    GLT::asset::asset_writer& out,
    std::string_view slang_source,
    GLT::asset::material::blend_mode blend,
    std::span<const std::byte> old_reflection = {},
    std::span<const std::byte> old_params = {});
```

Tasks:
1. Implement `repack_params(old_reflection, old_params, new_reflection, new_defaults)`:
   - For each old `reflection_param`, find a new row with matching `(name_hash, type)`. Match → copy bytes. No match → drop.
   - New rows with no old match → copy from `new_defaults`.
2. Full validation chain (mostly written):
   - `magic == REFLECT_SCHEMA_MAGIC`, `schema_version == REFLECT_SCHEMA_VERSION`, `contract_version == MATERIAL_CONTRACT_VERSION`
   - `entry_flags & surface_evaluate`
   - `blend == masked` ⇔ `alpha_test` bit set
3. Emit three bytecode chunks:
   - `CHUNK_MATERIAL_VERTEX_BYTECODE` (new, `0x0307`)
   - `CHUNK_MATERIAL_BYTECODE` (existing, `0x0303`) — repurposed as fragment
   - `CHUNK_MATERIAL_DEPTH_BYTECODE` (new, `0x0308`) — only written if `blend == masked`

**Deliverable:** a material with `blend == masked` produces six chunks; a material with `blend == opaque` produces five (no depth bytecode).

---

## Phase 3 — Material asset handler rewrite

**`plugin/asset_handler_material/plugin.inl`:**

`deserialize()`:
- Required: `CHUNK_MATERIAL_REFLECT`, `CHUNK_MATERIAL_PARAMS`, `CHUNK_MATERIAL_VERTEX_BYTECODE`, `CHUNK_MATERIAL_BYTECODE`.
- Optional: `CHUNK_MATERIAL_SLANG_SRC`, `CHUNK_MATERIAL_COMPILER_HASH`, `CHUNK_MATERIAL_BLEND_MODE`, `CHUNK_TEXTURE_REFS`, `CHUNK_MATERIAL_DEPTH_BYTECODE`.
- If `blend == masked` and depth bytecode is missing, log a warning and set `compiler_hash_matches = false` (forces recompile).
- `compiler_hash_matches` set by querying the active renderer's `i_shader_compiler::implementation_hash()`; mismatch → flag for recompile (Phase 6).

`serialize()`:
- Emit `CHUNK_MATERIAL_SLANG_SRC`, `CHUNK_MATERIAL_BLEND_MODE`, `CHUNK_TEXTURE_REFS` (as today).
- Call `material_compiler::run()` with the live asset's `reflection` + `packed_params` as the "old" schema.
- Emit `CHUNK_MATERIAL_PARAMS`, `CHUNK_MATERIAL_REFLECT`, `CHUNK_MATERIAL_COMPILER_HASH`, and the three bytecode chunks from the compiler's output.

**`material.cpp::find_param()` and `param_bytes()`** — implement per the previous roadmap: linear scan of `decode_reflection().params`, slice `packed_params` at `byte_offset`.

**Material instance handler** — new plugin entry for `core_types::material_instance`:
- `CHUNK_INSTANCE_BASE` = base UUID; resolved via `info.dependency_ids`.
- `CHUNK_INSTANCE_OVERRIDES` — packed rows `{ u64 name_hash; u16 type; u16 _pad; u32 byte_size; }` followed by `byte_size` bytes inline.
- Validate each override against base's reflection; drop unknown hashes / type mismatches with a warning.

**Deliverable:** round-tripping a material through `register_runtime` → `save()` → `load()` preserves all six chunks.

---

## Phase 4 — Renderer material storage becomes reflection-driven

Unchanged from the previous roadmap. Summary:

1. **Add `Globals` UBO** at a new binding (suggest set 0 binding 9). 480 bytes, std140, matches the contract table exactly.
   - Fill in `update_camera_ubo()` or a new `update_globals_ubo()`.
   - Keep `camera_ubo_data` unchanged — the light-cull and shadow passes still read it.
2. **Replace `gpu_material`** with per-slot UBO. Two options:
   - **(a) Per-material UBO** sized to `reflection.packed_param_size`, round-up to 256 B. Descriptor set = `{ material_params_ubo, material_textures[6], sampler }`.
   - **(b) Big SSBO of packed params**, indexed by slot. Simpler wiring, more waste.
   - Go with (a); it maps cleanly to the concat suffix's `ConstantBuffer<material_params>`.
3. **`pack_material(slot)`** becomes: read `asset->reflection` and `asset->packed_params`, `memcpy` into the slot's UBO. No field access. Delete `gpu_material`.
4. **`material_textures`** binds six `SampledImage` descriptors + the existing `m_material_sampler`. Fallbacks stay as today.
5. Update `update_mesh_descriptors()` for the new set layout.

**Deliverable:** the renderer uploads correct per-material data from any reflection schema. The shader side still breaks until Phase 5 lands.

---

        ## Phase 5 — Textual concatenation + per-material pipelines

        This is the big one. Three sub-decisions bundled here:

        ### 5.1 — The renderer's shading sources

        Files under `plugin/renderer_vk_rasteriser/asset/shader/vk_rasterizer/slang/`:

        - **`prefix.slang`** — imports `material_contract_v0`, declares all helpers (`renderer_cluster_lookup`, `renderer_sample_shadow`, `renderer_pbr`, etc.) with a mandatory `renderer_` prefix. Does not declare entry points. Does not declare `material_params`.
        - **`vertex_suffix.slang`** — declares `vertex_main`, builds `vertex_input` from the raw vertex buffer, calls `vertex_transform` if the material declares one (else `passthrough`), projects.
          - "Declared one" is decided at concat time by scanning the material source for the signature (regex or Slang reflection). If absent, suffix uses `passthrough`.
        - **`fragment_suffix.slang`** — declares `fragment_main`, builds `surface_input`, loads `material_params` from the UBO, binds `material_textures`, calls `surface_evaluate`, runs the renderer's lighting loop on the returned `surface_output`, tonemaps, writes the pixel.
        - **`depth_suffix.slang`** — only used when `blend == masked`. Declares `depth_main`, calls `alpha_test`, `discard`s on false. The no-op depth fragment stays as the existing fixed `depth_only.frag.glsl` for non-masked materials.

        **Concern: `Globals` binding name.** The renderer's prefix and the material's `material_params` both need descriptors. Slang assigns descriptor set/binding numbers via `[[vk::binding(...)]]` attributes. Reserve a numbering scheme:
        - Set 0: `Globals`, `material_params` UBO (dynamic offset), `material_textures` (six samplers) — matches what the concat suffix expects.
        - Set 1: renderer-private (light grid, shadow views, etc.). The material cannot see these; the prefix references them via `[[vk::binding(N, 1)]]`.

        ### 5.2 — `renderer::compile()`

        ```cpp
        std::expected<compiled_shader, shader_compile_error>
        renderer::compile(std::string_view material_source) {

            // 1. Reflection walk (Phase 1)
            auto schema = build_reflection_schema_from(material_source);
            if (!schema) return unexpected;

            // 2. Concat + compile for each entry point
            auto vsrc = std::string(vertex_prefix) + std::string(material_source) + vertex_suffix;
            auto fsrc = std::string(frag_prefix)   + std::string(material_source) + frag_suffix;
            auto vs = m_slang.compile_program(vsrc, "vertex_main");
            auto fs = m_slang.compile_program(fsrc, "fragment_main");
            if (!vs || !fs) return unexpected;

            compiled_shader out;
            out.vertex_bytecode = std::move(vs->spv);
            out.fragment_bytecode = std::move(fs->spv);
            out.reflection = std::move(*schema);

            if (blend == blend_mode::masked) {
                auto dsrc = std::string(depth_prefix) + std::string(material_source) + depth_suffix;
                auto ds = m_slang.compile_program(dsrc, "depth_main");
                if (!ds) return unexpected;
                out.depth_bytecode = std::move(ds->spv);
            }

            out.default_params = pack_defaults_from_linked(/* the fragment's linked type */);
            return out;
        }
        ```

        Note the `blend` parameter must come from the caller (material_compiler) since the shader doesn't know it. Extend `i_shader_compiler::compile` to `compile(source, blend)`. The design doc's `i_shader_compiler` interface already has this shape; only the last parameter is new.

        ### 5.3 — Per-material pipeline cache

        In `renderer_material.inl`:

        ```cpp
        struct material_slot {
            GLT::asset::handle                 asset{};
            u32                                ref_count = 0;
            // reflection-driven UBO (Phase 4)
            util::allocated_buffer             params_ubo{};
            // compiled shaders (Phase 5)
            vk::ShaderModule                   vs = nullptr;
            vk::ShaderModule                   fs = nullptr;
            vk::ShaderModule                   depth_fs = nullptr;   // null unless masked
            vk::Pipeline                       pipeline = nullptr;   // main pass
            vk::Pipeline                       depth_pipeline = nullptr; // depth prepass + shadow
            // textures (existing)
            std::array<GLT::asset::handle, 6>  texture_assets{};
            std::array<u32, 6>                 texture_slots{};
            bool                               alive = false;
        };
        ```

        - `acquire_material`: create UBO, compile-cache shaders, create `pipeline` and `depth_pipeline`.
        - `release_material`: destroy pipelines, destroy shader modules, free UBO.
        - Pipelines share the same `m_mesh_pipeline_layout` — the descriptor set layouts don't change per material; only the shader code does.
        - Descriptor buffers stay shared (per-frame); the per-material `params_ubo` is bound via dynamic offset.

        ### 5.4 — Draw-call bucketing

        This is the honest cost of per-material shaders. The current single-indirect-draw-with-packed-material-slot approach cannot work — each material has its own pipeline.

        **Recommended approach for v0 — GPU bucketing with a bounded per-material command buffer:**

        - Add `MAX_ACTIVE_MATERIALS = 256` and `MAX_COMMANDS_PER_MATERIAL = 4096`.
        - Cull output: `commands[MAX_ACTIVE_MATERIALS][MAX_COMMANDS_PER_MATERIAL]` + `counts[MAX_ACTIVE_MATERIALS]`. Total 256 × 4096 × 20 B ≈ 20 MB.
        - Cull shader maps `submesh.material_index` → bucket index (identity if slots are small; otherwise a lookup table passed as SSBO).
        - Draw loop iterates the active bucket list (a small CPU-written SSBO read back every N frames, or a persistent "used this frame" bitmask); for each bucket with nonzero count, binds `material_slot[b].pipeline`, binds the material's descriptor set with dynamic offset, and issues `drawIndexedIndirectCount(commands, b*stride, counts, b*4, MAX_COMMANDS_PER_MATERIAL, stride)`.

        **Alternative — CPU bucketing (simpler, slower):**
        - Cull writes to a single command buffer as today, but stores material slot in `firstInstance`'s high bits.
        - Skip the GPU cull entirely; do CPU-side instance loop, bucket by material, issue N `drawIndexed` calls.
        - Loses the GPU-driven property. Only worth it if you want to defer the cull rewrite.

        **My recommendation:** do the GPU bucketing in Phase 5. The CPU approach would have to be replaced anyway.

        ### 5.5 — Shadow pass

        - Directional shadow: masked materials need `depth_pipeline` from `material_slot`. Non-masked use the existing shared `m_depth_pipeline`.
        - **Two sub-buckets in the shadow cull output**: one for masked, one for non-masked. Same bucketing machinery as 5.4.
        - Local shadow atlas: same.

        ### 5.6 — Deletion of stale artifacts

        - Delete `mesh.vert.glsl`, `mesh.frag.glsl`, `depth_only.frag.glsl`.
        - Delete `gpu_material`, `MATERIAL_FLAG_*`, `MATERIAL_TEMPLATE_*`.
        - Remove `m_shader_compiler.compile_glsl_to_spirv` calls in `create_mesh_resources`; the pipeline creation is now per-material and lazy.

        **Deliverable:** the ocean example renders correctly; a masked material discards correctly in the depth prepass and both shadow passes; materials created at runtime trigger a compile the first time they're drawn.

---

## Phase 6 — Compiler-hash hot reload

Unchanged from v1, plus one addition: when the compiler hash mismatches, the material is recompiled in the background. Until the recompile finishes, the renderer should fall back to the default material for that slot.

Tasks:
1. `deserialize()` compares `asset->compiler_hash` to the active renderer's `implementation_hash()`. If mismatch, set `compiler_hash_matches = false`.
2. A worker thread invokes `material_compiler::run()` on the asset's own `slang_source`, produces the new chunks, and (in editor builds) calls `save()`. In packaged builds, write to the per-renderer cache directory described in the design doc.
3. Renderer observes the recompile and calls `refresh_material(handle)`.

---

## Phase 7 — Material instances

Unchanged from v1:
- Handler resolved in Phase 3.
- Renderer merges overrides into the base's `packed_params` before upload. Since the shader is compiled from the *base material's* source (instances don't have their own source), the instance reuses the base's pipeline and shader modules — only the params UBO differs.
- This means instances are cheap: one extra UBO + a cached handle to the base's pipeline.

**Confirm during Phase 7:** the base material's pipeline handle must be reachable from the instance's `material_slot`, so add a `base_material_slot` field.

---

## Phase 8 — Editor

Unchanged. `decode_reflection()` + `find_param()` (finished in Phase 3) unblock the parameter editor.

---

## Dependency graph

```
Phase 1 (reflection walk) ──┬─> Phase 3 (handler + instance)
                            │
Phase 2 (repack + signature)┘
                            │
                            ├─> Phase 4 (storage) ──> Phase 5 (concat + pipelines + bucketing) ──> Phase 6 (hot reload)
                            │                                                                    └─> Phase 7 (instances)
                            │
                            └─> Phase 8 (editor)
```

Phases 1 and 2 are the critical path. Phase 4 can start in parallel with 2 and 3. Phase 5 waits for 1, 2, and 4 to land.

---

## Risk callouts

1. **Slang concat edge cases.** Two `import` lines in one module — likely fine, test in Phase 1. If the material declares `Globals` or a name that collides with the renderer's prefix, the compile fails opaquely. Enforce naming: materials declare only `material_params` and the three entry points; everything else uses a `mat_` prefix that the renderer avoids.
2. **Per-material pipeline count.** A scene with 500 materials means 500 pipelines. Driver shader cache hits help across runs; first run of a large scene may take seconds. Ship with a default material pipeline that non-compiled materials fall back to.
3. **Bucketing memory.** 20 MB for the command bucket is fine; 80 MB (higher per-material capacity) is not. Size `MAX_ACTIVE_MATERIALS` from the material slot count (256 is a good first guess). Add a runtime check: overflow → fallback to CPU bucketing for the overflow frame.
4. **`alpha_test` and `material_textures` in the depth pass.** The depth pipeline's descriptor layout must bind six textures + sampler, same as the fragment pass, or the masked material's `alpha_test` will fail to find them. This is easy to forget because the non-masked `depth_only.frag.glsl` doesn't declare textures.
5. **`CHUNK_MATERIAL_BYTECODE` repurposing.** It used to hold the single bytecode blob. Now it holds fragment bytecode. Any old asset on disk will fail with a `corrupt_header` or a compile mismatch — bump `CURRENT_VERSION` in `asset/header.h` if you want a clean break, or accept the noise during development.

---

## Files still worth seeing before Phase 5 starts

- `render_graph/render_graph.h` — `pass_builder::read/write` semantics, needed to size Phase 5's per-material draw loop.
- `render/image.h` — for the material preview path if you re-enable it later.
- `config.h` — to confirm `ASSET_DIR` resolves to `core/asset/material_contract/v0/` after install (currently hard-coded as `.../material_contract/v0`).
- Any file that consumes `compiled_shader::bytecode` besides `renderer_shader_compiler.inl` — I didn't see one in the dump, but a grep would confirm.

None block starting Phase 1.