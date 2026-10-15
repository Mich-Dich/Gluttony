
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

when the compiler hash mismatches, the material is recompiled in the background. Until the recompile finishes, the renderer should fall back to the default material for that slot.

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








libc.so.6!__pthread_kill_implementation(pthread_t threadid, int signo, int no_tid) (pthread_kill.c:44)
libc.so.6!__pthread_kill_internal(pthread_t threadid, int signo) (pthread_kill.c:89)
libc.so.6!__GI___pthread_kill(pthread_t threadid, int signo, int signo@entry) (pthread_kill.c:100)
libc.so.6!__GI_raise(int sig, int sig@entry) (raise.c:26)
libc.so.6!__GI_abort() (abort.c:77)
libstdc++.so.6![Unknown/Just-In-Time compiled code] (Unknown Source:0)
libstdc++.so.6!std::terminate() (Unknown Source:0)
libstdc++.so.6!__cxa_throw (Unknown Source:0)
librenderer_vk_rasterizer.so!vk::detail::throwResultException(enum vk::Result result, const char * message) (/usr/include/vulkan/vulkan.hpp:8241)
librenderer_vk_rasterizer.so!vk::detail::resultCheck(enum vk::Result result, const char * message, class std::initializer_list<vk::Result> successCodes) (/usr/include/vulkan/vulkan.hpp:8513)
librenderer_vk_rasterizer.so!vk::Device::createGraphicsPipeline<vk::detail::DispatchLoaderStatic, true>(const class vk::Device * const this, class vk::PipelineCache pipelineCache, class vk::Optional<vk::AllocationCallbacks const> allocator, const class vk::detail::DispatchLoaderStatic & d) (/usr/include/vulkan/vulkan_funcs.hpp:5010)
librenderer_vk_rasterizer.so!GLT::renderer::vk_rasterizer::renderer::create_material_pipeline(class GLT::renderer::vk_rasterizer::renderer * const this, const struct GLT::renderer::vk_rasterizer::material_slot & slot, enum GLT::renderer::vk_rasterizer::material_pipeline_kind kind) (/home/mich/workspace/Gluttony/plugin/renderer_vk_rasteriser/src/renderer_material.inl:872)
librenderer_vk_rasterizer.so!GLT::renderer::vk_rasterizer::renderer::install_fallback_material(class GLT::renderer::vk_rasterizer::renderer * const this) (/home/mich/workspace/Gluttony/plugin/renderer_vk_rasteriser/src/renderer_material.inl:923)
librenderer_vk_rasterizer.so!GLT::renderer::vk_rasterizer::renderer::create(class GLT::renderer::vk_rasterizer::renderer * const this) (/home/mich/workspace/Gluttony/plugin/renderer_vk_rasteriser/src/renderer.inl:68)
GLT::application::application(class GLT::application * const this, const class std::filesystem::__cxx11::path & project_path) (/home/mich/workspace/Gluttony/core/src/application.cpp:65)
main(int argc, char ** argv) (/home/mich/workspace/Gluttony/core/src/entry_point.cpp:74)


Here is the log output:
[15:03:44:168] [renderer_vk_rasterizer WARN  main - utils.cpp:renderer::vk_rasterizer::util::vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void*):55] [VULKAN] CreatePipeline: failed to parse SPIR-V: 2
terminate called after throwing an instance of 'vk::UnknownError'
[15:03:44:169] [renderer_vk_rasterizer WARN  main - utils.cpp:renderer::vk_rasterizer::util::vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void*):55] [VULKAN] CreatePipeline: unexpected compilation failure
  what():  vk::Device::createGraphicsPipeline: ErrorUnknown
[15:03:44:169] [renderer_vk_rasterizer WARN  main - utils.cpp:renderer::vk_rasterizer::util::vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void*):55] [VULKAN] CreateGraphicsPipeline: unexpected failure compiling SPIR-V shader: 0x9ff3c7d9b47bbeda




























    

==== VERTEX SHADER ====

import material_contract_v0;

// ---- set 0: frame-scoped 
struct RendererInstance {
    float4x4 model;
    float4x4 normal_matrix;
    uint     mesh_index;
    uint     material_index;
    uint     _pad0;
    uint     _pad1;
};

struct RendererLight {
    float4 position_radius;
    float4 color_intensity;
    float4 direction_inner_cos;
    float4 params;
};

struct RendererShadowView {
    float4x4 view_proj;
    float4   atlas_rect;
    float4   params;
    float4   sphere;
    float4   planes[6];
    float4   _pad[3];
};

struct RendererCamera {
    float4x4 view_proj;
    float4x4 view;
    float4   camera_pos;
    float4   frustum_planes[6];
    uint4    flags;
    float4x4 projection;
    float4x4 inv_projection;
    uint4    cluster_dims;
    float4   cluster_depth;      // x=near y=far z=render_w w=render_h
    float4x4 light_view_proj;
    float4   shadow_params;      // x=world_per_texel y=normal_bias z=depth_bias w=1/res
};

[[vk::binding(0, 0)]] ConstantBuffer<RendererCamera>          renderer_camera;
[[vk::binding(1, 0)]] StructuredBuffer<RendererInstance>      renderer_instances;
[[vk::binding(2, 0)]] StructuredBuffer<RendererLight>         renderer_lights;
[[vk::binding(3, 0)]] StructuredBuffer<uint2>renderer_light_grid;
[[vk::binding(4, 0)]] StructuredBuffer<uint>renderer_light_list;
[[vk::binding(5, 0)]] Texture2Drenderer_shadow_map;
[[vk::binding(6, 0)]] StructuredBuffer<RendererShadowView>    renderer_shadow_views;
[[vk::binding(7, 0)]] Texture2Drenderer_shadow_atlas;
[[vk::binding(8, 0)]] SamplerComparisonStaterenderer_shadow_sampler;

// ---- set 1: per-material textures 
[[vk::binding(1, 1)]] Texture2D    renderer_tex_base_color;
[[vk::binding(2, 1)]] Texture2D    renderer_tex_metallic_roughness;
[[vk::binding(3, 1)]] Texture2D    renderer_tex_normal;
[[vk::binding(4, 1)]] Texture2D    renderer_tex_emissive;
[[vk::binding(5, 1)]] Texture2D    renderer_tex_occlusion;
[[vk::binding(6, 1)]] Texture2D    renderer_tex_height;
[[vk::binding(7, 1)]] SamplerState renderer_material_sampler;

// ---- interpolants 
struct renderer_vertex_outputs {
    [[vk::location(0)]] float3 position_ws;
    [[vk::location(1)]] float3 normal_ws;
    [[vk::location(2)]] float4 tangent_ws;
    [[vk::location(3)]] float2 uv0;
};

// ---- helpers 
static const float  RENDERER_PI = 3.14159265359;
static const float3 RENDERER_AMBIENT = float3(0.03, 0.03, 0.03);

float renderer_linearize_depth(float d) {
  
    float4 view = mul(renderer_camera.inv_projection, float4(0.0, 0.0, d, 1.0));
    return -view.z / view.w;
}

uint renderer_point_face(float3 d) {
  
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z)return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

float renderer_sample_shadow(float3 world_pos, float3 N, float ndotl) {
  
    float normal_offset = renderer_camera.shadow_params.x * renderer_camera.shadow_params.y * (1.0 - ndotl);
    float4 lc = mul(renderer_camera.light_view_proj, float4(world_pos + N * normal_offset, 1.0));
    float3 ndc = lc.xyz / lc.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
        return 1.0;
    float ref_depth = ndc.z - renderer_camera.shadow_params.z;
    float texel = renderer_camera.shadow_params.w;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
sum += renderer_shadow_map.SampleCmpLevelZero(renderer_shadow_sampler, uv + float2(x, y) * texel, ref_depth);
    return sum / 9.0;
}

float renderer_sample_local_shadow(uint view_index, float3 world_pos, float3 N, float3 L, float ndotl, float dist) {
  
    RendererShadowView v = renderer_shadow_views[view_index];
    if (v.atlas_rect.z <= 0.0) return 1.0;

    float texel_world = dist * v.params.x;
    float3 biased = world_pos + N * (texel_world * v.params.y * (1.0 - ndotl)) + L * (texel_world * v.params.z);
    float4 clip = mul(v.view_proj, float4(biased, 1.0));
    if (clip.w <= 0.0) return 1.0;

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) return 1.0;

    float2 atlas_uv = v.atlas_rect.xy + uv * v.atlas_rect.zw;
    float  t = v.params.w;
    float2 lo = v.atlas_rect.xy + 0.5 * t;
    float2 hi = v.atlas_rect.xy + v.atlas_rect.zw - 0.5 * t;

    float s = 0.0;
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5, 0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5, 0.5)*t, lo, hi), ndc.z);
    return s * 0.25;
}

float renderer_D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (RENDERER_PI * d * d);
}

float renderer_V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 renderer_F_Schlick(float VoH, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

float3 renderer_output_transform(float3 c) {
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, float3(1.0 / 2.2));
}


import material_contract_v0;


struct material_params {
    float3          base_color = float3(0.8, 0.8, 0.8);
    float           roughness = 0.5;
};

surface_output surface_evaluate(surface_input s, material_params p, material_textures tex) {
    surface_output o = make_default_surface(s);
    o.base_color = p.base_color;
    o.roughness = p.roughness;
    return o;
}



// ---- vertex stage 
struct renderer_vert_attributes {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float4 tangent;
    [[vk::location(3)]] float2 uv0;
};

struct renderer_vert_result {
    float4clip_pos : SV_Position;
    renderer_vertex_outputs varyings;
};

[shader(\"vertex\")]
renderer_vert_result vertex_main(renderer_vert_attributes v, uint instance_id : SV_InstanceID) {
  
    RendererInstance inst = renderer_instances[instance_id];

    vertex_input vi;
    vi.position_ws = mul(inst.model,         float4(v.position,     1.0)).xyz;
    vi.normal_ws = normalize(mul(inst.normal_matrix, float4(v.normal, 0.0)).xyz);
    vi.tangent_ws = float4(normalize(mul(inst.model, float4(v.tangent.xyz, 0.0)).xyz), v.tangent.w);
    vi.uv0 = v.uv0;
    vi.uv1 = float2(0.0, 0.0);
    vi.color = float3(1.0, 1.0, 1.0);

    vertex_output vo = passthrough(vi);

    renderer_vert_result o;
    o.clip_pos = mul(renderer_camera.view_proj, float4(vo.position_ws, 1.0));
    o.varyings.position_ws = vo.position_ws;
    o.varyings.normal_ws = vo.normal_ws;
    o.varyings.tangent_ws = vo.tangent_ws;
    o.varyings.uv0 = vo.uv0;
    return o;
}








==== FRAGMENT SHADER ====

import material_contract_v0;

// ---- set 0: frame-scoped 
struct RendererInstance {
      float4x4 model;
    float4x4 normal_matrix;
    uint     mesh_index;
    uint     material_index;
    uint     _pad0;
    uint     _pad1;
};

struct RendererLight {
      float4 position_radius;
    float4 color_intensity;
    float4 direction_inner_cos;
    float4 params;
};

struct RendererShadowView {
      float4x4 view_proj;
    float4   atlas_rect;
    float4   params;
    float4   sphere;
    float4   planes[6];
    float4   _pad[3];
};

struct RendererCamera {
      float4x4 view_proj;
    float4x4 view;
    float4   camera_pos;
    float4   frustum_planes[6];
    uint4    flags;
    float4x4 projection;
    float4x4 inv_projection;
    uint4    cluster_dims;
    float4   cluster_depth;      // x=near y=far z=render_w w=render_h
    float4x4 light_view_proj;
    float4   shadow_params;      // x=world_per_texel y=normal_bias z=depth_bias w=1/res
};

[[vk::binding(0, 0)]] ConstantBuffer<RendererCamera>          renderer_camera;
[[vk::binding(1, 0)]] StructuredBuffer<RendererInstance>      renderer_instances;
[[vk::binding(2, 0)]] StructuredBuffer<RendererLight>         renderer_lights;
[[vk::binding(3, 0)]] StructuredBuffer<uint2>renderer_light_grid;
[[vk::binding(4, 0)]] StructuredBuffer<uint>renderer_light_list;
[[vk::binding(5, 0)]] Texture2Drenderer_shadow_map;
[[vk::binding(6, 0)]] StructuredBuffer<RendererShadowView>    renderer_shadow_views;
[[vk::binding(7, 0)]] Texture2Drenderer_shadow_atlas;
[[vk::binding(8, 0)]] SamplerComparisonStaterenderer_shadow_sampler;

// ---- set 1: per-material textures 
[[vk::binding(1, 1)]] Texture2D    renderer_tex_base_color;
[[vk::binding(2, 1)]] Texture2D    renderer_tex_metallic_roughness;
[[vk::binding(3, 1)]] Texture2D    renderer_tex_normal;
[[vk::binding(4, 1)]] Texture2D    renderer_tex_emissive;
[[vk::binding(5, 1)]] Texture2D    renderer_tex_occlusion;
[[vk::binding(6, 1)]] Texture2D    renderer_tex_height;
[[vk::binding(7, 1)]] SamplerState renderer_material_sampler;

// ---- interpolants 
struct renderer_vertex_outputs {
      [[vk::location(0)]] float3 position_ws;
    [[vk::location(1)]] float3 normal_ws;
    [[vk::location(2)]] float4 tangent_ws;
    [[vk::location(3)]] float2 uv0;
};

// ---- helpers 
static const float  RENDERER_PI = 3.14159265359;
static const float3 RENDERER_AMBIENT = float3(0.03, 0.03, 0.03);

float renderer_linearize_depth(float d) {
  
    float4 view = mul(renderer_camera.inv_projection, float4(0.0, 0.0, d, 1.0));
    return -view.z / view.w;
}

uint renderer_point_face(float3 d) {
  
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z)return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

float renderer_sample_shadow(float3 world_pos, float3 N, float ndotl) {
  
    float normal_offset = renderer_camera.shadow_params.x * renderer_camera.shadow_params.y * (1.0 - ndotl);
    float4 lc = mul(renderer_camera.light_view_proj, float4(world_pos + N * normal_offset, 1.0));
    float3 ndc = lc.xyz / lc.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
        return 1.0;
    float ref_depth = ndc.z - renderer_camera.shadow_params.z;
    float texel = renderer_camera.shadow_params.w;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
sum += renderer_shadow_map.SampleCmpLevelZero(renderer_shadow_sampler, uv + float2(x, y) * texel, ref_depth);
    return sum / 9.0;
}

float renderer_sample_local_shadow(uint view_index, float3 world_pos, float3 N, float3 L, float ndotl, float dist) {
  
    RendererShadowView v = renderer_shadow_views[view_index];
    if (v.atlas_rect.z <= 0.0) return 1.0;

    float texel_world = dist * v.params.x;
    float3 biased = world_pos + N * (texel_world * v.params.y * (1.0 - ndotl))
+ L * (texel_world * v.params.z);
    float4 clip = mul(v.view_proj, float4(biased, 1.0));
    if (clip.w <= 0.0) return 1.0;

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) return 1.0;

    float2 atlas_uv = v.atlas_rect.xy + uv * v.atlas_rect.zw;
    float  t = v.params.w;
    float2 lo = v.atlas_rect.xy + 0.5 * t;
    float2 hi = v.atlas_rect.xy + v.atlas_rect.zw - 0.5 * t;

    float s = 0.0;
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5, 0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5, 0.5)*t, lo, hi), ndc.z);
    return s * 0.25;
}

float renderer_D_GGX(float NoH, float a) {
      float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (RENDERER_PI * d * d);
}

float renderer_V_SmithGGXCorrelated(float NoV, float NoL, float a) {
      float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 renderer_F_Schlick(float VoH, float3 f0) {
      return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

float3 renderer_output_transform(float3 c) {
      c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, float3(1.0 / 2.2));
}


import material_contract_v0;


struct material_params {
      float3      base_color = float3(0.8, 0.8, 0.8);
    float       roughness = 0.5;
};

surface_output surface_evaluate(surface_input s, material_params p, material_textures tex) {
      surface_output o = make_default_surface(s);
    o.base_color = p.base_color;
    o.roughness = p.roughness;
    return o;
}



// ---- fragment stage 
[[vk::binding(0, 1)]] ConstantBuffer<material_params> renderer_material_params;

material_textures renderer_assemble_textures() {
      material_textures t;
    t.base_color = renderer_tex_base_color;
    t.metallic_roughness = renderer_tex_metallic_roughness;
    t.normal = renderer_tex_normal;
    t.emissive = renderer_tex_emissive;
    t.occlusion = renderer_tex_occlusion;
    t.height = renderer_tex_height;
    t.sampler = renderer_material_sampler;
    return t;
}

[shader(\"fragment\")]
float4 fragment_main(renderer_vertex_outputs i, float4 sv_pos : SV_Position) : SV_Target {
  
    material_textures tex = renderer_assemble_textures();

    surface_input s = {};
    s.position_ws = i.position_ws;
    s.normal_ws = normalize(i.normal_ws);
    s.tangent_ws = i.tangent_ws;
    s.uv0 = i.uv0;
    s.uv1 = float2(0.0, 0.0);
    s.color = float3(1.0, 1.0, 1.0);
    s.view_dir_ws = normalize(renderer_camera.camera_pos.xyz - i.position_ws);
    s.screen_uv = sv_pos.xy / renderer_camera.cluster_depth.zw;
    s.view_depth = renderer_linearize_depth(sv_pos.z);
    s.ddx_ws = ddx(i.position_ws);
    s.ddy_ws = ddy(i.position_ws);

    surface_output o = surface_evaluate(s, renderer_material_params, tex);

    const float3 Ng = normalize(i.normal_ws);
    const float3 N = normalize(o.normal_ws);
    const float3 V = s.view_dir_ws;
    const float  NoV = max(dot(N, V), 1e-4);

    const float3 diffuse_color = o.base_color * (1.0 - o.metallic);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), o.base_color, o.metallic);
    const float  a = o.roughness * o.roughness;

    // ---- cluster lookup 
    uint3  grid_dims = renderer_camera.cluster_dims.xyz;
    uint2  tile = uint2(sv_pos.xy / float2(16.0, 16.0));
    float  near_z = renderer_camera.cluster_depth.x;
    float  far_z = renderer_camera.cluster_depth.y;
    float  lz = clamp(s.view_depth, near_z, far_z);
    float  slice_f = float(grid_dims.z) * log(lz / near_z) / log(far_z / near_z);
    uint   slice = uint(clamp(slice_f, 0.0, float(grid_dims.z - 1u)));
    uint   cluster = tile.x + tile.y * grid_dims.x + slice * grid_dims.x * grid_dims.y;
    uint2  entry = renderer_light_grid[cluster];

    // ---- Forward+ 
    float3 Lo = float3(0.0, 0.0, 0.0);

    for (uint li = 0u; li < entry.y; ++li) {
  
        RendererLight light = renderer_lights[renderer_light_list[entry.x + li]];
        uint  type = uint(light.params.y);
        float3 L;
        float attenuation = 1.0;
        float light_dist = 0.0;

        if (type == 0u) {
  L = normalize(-light.direction_inner_cos.xyz);
        } else {
  float3 to_light = light.position_radius.xyz - i.position_ws;
float  dist = length(to_light);
float  radius = light.position_radius.w;
L = to_light / max(dist, 1e-4);
light_dist = dist;
float x = clamp(dist / radius, 0.0, 1.0);
float window = 1.0 - x * x;
attenuation = window * window;
if (type == 2u) {
  float cd = dot(-L, normalize(light.direction_inner_cos.xyz));
float cos_outer = light.params.x;
float cos_inner = light.direction_inner_cos.w;
float spot = clamp((cd - cos_outer) / max(cos_inner - cos_outer, 1e-4), 0.0, 1.0);
attenuation *= spot * spot;
}
        }

        float NoL = clamp(dot(N, L), 0.0, 1.0);
        if (NoL <= 0.0 || attenuation <= 0.0) continue;

        if (light.params.z > 0.5) {
  float ndotl_geo = clamp(dot(Ng, L), 0.0, 1.0);
if (type == 0u) {
  attenuation *= renderer_sample_shadow(i.position_ws, Ng, ndotl_geo);
} else if (attenuation > 0.002) {
  uint view_index = uint(light.params.z) - 1u;
if (type == 1u)
view_index += renderer_point_face(i.position_ws - light.position_radius.xyz);
attenuation *= renderer_sample_local_shadow(view_index, i.position_ws, Ng, L, ndotl_geo, light_dist);
}
        }

        float3 H = normalize(V + L);
        float  NoH = clamp(dot(N, H), 0.0, 1.0);
        float  VoH = clamp(dot(V, H), 0.0, 1.0);
        float3 spec = renderer_D_GGX(NoH, a) * renderer_V_SmithGGXCorrelated(NoV, NoL, a) * renderer_F_Schlick(VoH, f0);
        float3 radiance = light.color_intensity.rgb * light.color_intensity.a * attenuation;
        Lo += (diffuse_color + spec * RENDERER_PI) * radiance * NoL;
    }

    float3 ambient = RENDERER_AMBIENT * (diffuse_color + f0 * lerp(0.5, 0.1, o.roughness)) * o.ao;
    float3 final_c = renderer_output_transform(ambient + Lo + o.emissive);

    return float4(final_c, o.opacity);
}









==== DEPTH SHADER ====

import material_contract_v0;

// ---- set 0: frame-scoped 
struct RendererInstance {
      float4x4 model;
    float4x4 normal_matrix;
    uint     mesh_index;
    uint     material_index;
    uint     _pad0;
    uint     _pad1;
};

struct RendererLight {
      float4 position_radius;
    float4 color_intensity;
    float4 direction_inner_cos;
    float4 params;
};

struct RendererShadowView {
      float4x4 view_proj;
    float4   atlas_rect;
    float4   params;
    float4   sphere;
    float4   planes[6];
    float4   _pad[3];
};

struct RendererCamera {
      float4x4 view_proj;
    float4x4 view;
    float4   camera_pos;
    float4   frustum_planes[6];
    uint4    flags;
    float4x4 projection;
    float4x4 inv_projection;
    uint4    cluster_dims;
    float4   cluster_depth;      // x=near y=far z=render_w w=render_h
    float4x4 light_view_proj;
    float4   shadow_params;      // x=world_per_texel y=normal_bias z=depth_bias w=1/res
};

[[vk::binding(0, 0)]] ConstantBuffer<RendererCamera>          renderer_camera;
[[vk::binding(1, 0)]] StructuredBuffer<RendererInstance>      renderer_instances;
[[vk::binding(2, 0)]] StructuredBuffer<RendererLight>         renderer_lights;
[[vk::binding(3, 0)]] StructuredBuffer<uint2>renderer_light_grid;
[[vk::binding(4, 0)]] StructuredBuffer<uint>renderer_light_list;
[[vk::binding(5, 0)]] Texture2Drenderer_shadow_map;
[[vk::binding(6, 0)]] StructuredBuffer<RendererShadowView>    renderer_shadow_views;
[[vk::binding(7, 0)]] Texture2Drenderer_shadow_atlas;
[[vk::binding(8, 0)]] SamplerComparisonStaterenderer_shadow_sampler;

// ---- set 1: per-material textures 
[[vk::binding(1, 1)]] Texture2D    renderer_tex_base_color;
[[vk::binding(2, 1)]] Texture2D    renderer_tex_metallic_roughness;
[[vk::binding(3, 1)]] Texture2D    renderer_tex_normal;
[[vk::binding(4, 1)]] Texture2D    renderer_tex_emissive;
[[vk::binding(5, 1)]] Texture2D    renderer_tex_occlusion;
[[vk::binding(6, 1)]] Texture2D    renderer_tex_height;
[[vk::binding(7, 1)]] SamplerState renderer_material_sampler;

// ---- interpolants 
struct renderer_vertex_outputs {
      [[vk::location(0)]] float3 position_ws;
    [[vk::location(1)]] float3 normal_ws;
    [[vk::location(2)]] float4 tangent_ws;
    [[vk::location(3)]] float2 uv0;
};

// ---- helpers 
static const float  RENDERER_PI = 3.14159265359;
static const float3 RENDERER_AMBIENT = float3(0.03, 0.03, 0.03);

float renderer_linearize_depth(float d) {
  
    float4 view = mul(renderer_camera.inv_projection, float4(0.0, 0.0, d, 1.0));
    return -view.z / view.w;
}

uint renderer_point_face(float3 d) {
  
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z)return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

float renderer_sample_shadow(float3 world_pos, float3 N, float ndotl) {
  
    float normal_offset = renderer_camera.shadow_params.x * renderer_camera.shadow_params.y * (1.0 - ndotl);
    float4 lc = mul(renderer_camera.light_view_proj, float4(world_pos + N * normal_offset, 1.0));
    float3 ndc = lc.xyz / lc.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
        return 1.0;
    float ref_depth = ndc.z - renderer_camera.shadow_params.z;
    float texel = renderer_camera.shadow_params.w;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
sum += renderer_shadow_map.SampleCmpLevelZero(renderer_shadow_sampler, uv + float2(x, y) * texel, ref_depth);
    return sum / 9.0;
}

float renderer_sample_local_shadow(uint view_index, float3 world_pos, float3 N, float3 L, float ndotl, float dist) {
  
    RendererShadowView v = renderer_shadow_views[view_index];
    if (v.atlas_rect.z <= 0.0) return 1.0;

    float texel_world = dist * v.params.x;
    float3 biased = world_pos + N * (texel_world * v.params.y * (1.0 - ndotl))
+ L * (texel_world * v.params.z);
    float4 clip = mul(v.view_proj, float4(biased, 1.0));
    if (clip.w <= 0.0) return 1.0;

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) return 1.0;

    float2 atlas_uv = v.atlas_rect.xy + uv * v.atlas_rect.zw;
    float  t = v.params.w;
    float2 lo = v.atlas_rect.xy + 0.5 * t;
    float2 hi = v.atlas_rect.xy + v.atlas_rect.zw - 0.5 * t;

    float s = 0.0;
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5, 0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5, 0.5)*t, lo, hi), ndc.z);
    return s * 0.25;
}

float renderer_D_GGX(float NoH, float a) {
      float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (RENDERER_PI * d * d);
}

float renderer_V_SmithGGXCorrelated(float NoV, float NoL, float a) {
      float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 renderer_F_Schlick(float VoH, float3 f0) {
      return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

float3 renderer_output_transform(float3 c) {
      c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, float3(1.0 / 2.2));
}


import material_contract_v0;


struct material_params {
      float3      base_color = float3(0.8, 0.8, 0.8);
    float       roughness = 0.5;
};

surface_output surface_evaluate(surface_input s, material_params p, material_textures tex) {
      surface_output o = make_default_surface(s);
    o.base_color = p.base_color;
    o.roughness = p.roughness;
    return o;
}



// ---- depth-only fragment stage (opaque materials) 
[shader(\"fragment\")]
void depth_main(renderer_vertex_outputs i, float4 sv_pos : SV_Position) {
  
    // Opaque materials write depth only. Slang eliminates a fragment entry point whose body
    // has no observable effect, and the resulting SPIR-V is malformed (the parser hits
    // SPV_END_OF_STREAM at pipeline creation). The unreachable `discard` below is a marker:
    // it tells the compiler this is a real fragment shader that may kill fragments, so the
    // entry point survives DCE with a well-formed body.
    //
    // The compiler cannot prove the branch dead: `sv_pos.xy` are fragment coordinates in
    // pixels, and the compiler has no knowledge of the render target's extent.
    if (sv_pos.x > 1e30 || sv_pos.y > 1e30)
        discard;
}
