
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
[ ] Post-processing: Complete the render pipeline with tonemapping, bloom, etc
[ ] Renderer feature: anti-aliasing (msaa, ...)
[ ] Renderer feature: post processing
[ ] Renderer feature: SSAO
[ ] Renderer feature: SDFGI


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




















# Rewrites — three sections

---


```cpp
```

---

---

## §15.3 and §15.5 — namespace descent

---

## Notes on the remaining sections

The three rewrites above are self-contained, but they touch assumptions in other sections. The following should be updated in a follow-up pass for full consistency:

| Section | Issue |
|---|---|
| §5.1 | The comment on `on_start` references a `was_restored` flag; the syntax should be `has_flag(ctx.get_flags(), context_flag::was_reloaded)` or `was_restored(ctx.get_flags())`. |
| §6.2 | The "Restore order for `[[=editor]] [[=persist]]` fields" note is fine, but the surrounding table needs to reflect that the flag is called `was_reloaded` not `was_restored`. |
| §8.2 | The `on_start` bullet list mentions `!was_restored` — update to the new flag syntax. |
| §9.3 | The restore procedure references `flags::was_restored` — update to `context_flag::was_reloaded` and add `is_start`. |
| §10.4 | The sleep-resume example uses `ctx.is_sleeping()` (still correct) but the surrounding prose mentions "restore" without specifying which kind — clarify that both hot reload and save restore preserve the sleeping flag. |
| §17.5 | The hot reload description is phase-based and contradicts §9.1. Rewrite to match: the coordinator calls `plugin_manager::unload(name)` / `load(name)` directly, `game_layer` is not involved. |
| §17.6 | The save-file restore description references `was_restored` semantics — update to `from_save`. |
| §18 | The `health::on_start` example uses `!ctx.was_reloaded()` — update to the new flag API. |
| §2.7 | "walking a namespace" is still accurate, but the section could mention that the walk descends sub-namespaces (per §15.5). |