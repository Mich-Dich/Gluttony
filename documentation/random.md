
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








































