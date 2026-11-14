# Asset identity: UUIDs, handles and the asset catalog

An asset has two identities, used for different things:

| | What it is | Lives | Use it for |
|---|---|---|---|
| **UUID** (`GLT::UUID`, 64 bit) | Persistent identity of an asset file | In the file header (`asset::header::id`), forever | Anything stored: references between assets, files about an asset (`metadata/asset_stats/<uuid>.yml`), save data, tools |
| **Handle** (`asset::handle`) | Slot index + generation of a loaded asset | Only in the running registry | Everything at runtime: fast lookup, and a handle to an unloaded asset is detectably stale |

Both are kept on purpose. A handle can't be written to disk (the slot differs every session); a UUID is too slow and
too weak for runtime use (no stale detection). The rule: **store UUIDs, hold handles.**

## Where the UUID comes from

- **Import:** the factory's result carries a fresh UUID, written into the new file's header.
- **Runtime assets** (`register_runtime`): a fresh UUID unless the caller passes one; written by the first `save()`.
- **Save:** keeps the UUID of the asset. "Save as" to a new path keeps it too and the catalog points to the new file;
  if the old file stays on disk, the next scan gives the old one a fresh UUID (see copies below).
- **Dependencies** are stored as UUID + path rows in the dependency table. Loading resolves them by: already loaded
  (UUID) → path → **catalog (UUID)**. So a material that was moved since the mesh referencing it was saved is still
  found.

## The asset catalog

The registry (`plugin/asset_registry_default/src/catalog.inl`) keeps a map **UUID → content-relative path of every
asset file of the project, loaded or not**:

```cpp
auto registry = GLT::asset::registry::get_ref();
std::filesystem::path path = registry->find_path_by_id(id);    // where is it now? (empty: unknown)
auto ref = registry->load_by_id(id);                           // load it (or get the loaded one)
registry->refresh_catalog();                                   // rescan after big changes outside the engine
registry->find_by_id(id);                                      // unchanged: handle of a LOADED asset only
```

- **Built** on the first `load()` / `find_path_by_id()` / `load_by_id()` of a project by reading only the fixed header of
  every `.glt_asset` under `content/` (fast: a few hundred bytes per file).
- **Kept current** by `load`, `save` / "save as", `import` and `move` (files and folders).
- **Changes outside the engine** are picked up: if the catalogued file of a UUID is missing, or a UUID is unknown, the
  folder is rescanned (unknown UUIDs at most every 2 s, so a lookup of something that does not exist stays cheap).
- **Persisted** in `<project>/metadata/asset_catalog.yml` through `GLT::serializer::yaml` (section `asset_catalog`: a
  version and one record per asset with UUID, type and path, sorted by path). It
  is a cache: deleting it is harmless.

### Copies: two files with the same UUID

Copying an asset in a file manager copies its header, so two files claim one identity. The scan repairs that:

1. The file the last catalog knew at that path keeps the UUID.
2. If the catalog knew neither, the older file keeps it (a copy is usually the newer one).
3. Every other file gets a fresh UUID **written into its header** (only the 8 id bytes change; the content hash covers
   the chunk payload only, so nothing else is touched). The log says which file got which UUID.

A file with UUID 0 gets one the same way. A copy made while the engine runs is repaired by the next rescan
(`refresh_catalog()`, or automatically when it is looked up); until then the registry logs a warning when both are
loaded and finds the second one by path only.

## The metadata folder

`<project>/metadata/` (`config::METADATA_DIR`, `PROJECT_METADATA_DIR`) holds engine-owned files that are not game
content and are never loaded by the game: the asset catalog and data editors collect about assets, e.g. the mesh
editor's render samples in `metadata/asset_stats/<uuid>.yml` (see `virtual_geometry.md`). Files there are named by
UUID, so they follow an asset through moves and renames. Most of it is specific to one machine (GPU timings):
**add `metadata/` to the project's `.gitignore`**.
