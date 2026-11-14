# Engine settings

Values of the core and of plugins that a project, and later a player, can change: shadow distance, number of shadow
casting lights, tonemapper, ... They are plain reflected structs owned by whoever uses them. `GLT::engine_settings`
(core, `settings/engine_settings.h`) fills them from two files and writes changes back. No editor is involved, so a
packaged game starts with the same values.

## Where a value comes from

Looked up top-down, the first one that exists wins:

| Layer | File | Meant for |
|---|---|---|
| this computer | `<project>/config/engine.user.config` | one person's changes (the player's options later). Keep it out of version control |
| project | `<project>/config/engine.config` | the project's values. Shared, shipped with the game |
| default | the member initializer in the code | everything nobody changed |

A file only lists values that were changed at its layer, so a new default in the code reaches every project that never
touched the value. Blocks and keys nobody registered (a plugin that is not loaded) are kept as they are.

```yaml
renderer.vk_rasterizer.visual:
  local_shadow_distance: 150
  sun_shadow_resolution: 4096
```

## Adding settings

```cpp
struct [[=name{"my_plugin.settings"}]] my_settings {

    [[=category{"Streaming|Loading"}]]                  // page "Streaming", section "Loading" in the settings window
    [[=display_name{"Load radius (m)"}]]
    [[=tooltip{"Cells closer to the camera than this are loaded"}]]
    [[=range{64.0, 4096.0}]]
    f32 load_radius = 1024.0f;

    [[=category{"Streaming|Loading"}]]
    [[=display_name{"Quality"}]]
    [[=options{"Low|Medium|High"}]]                     // an integer shown as a choice; "512=Low|1024=High" for other values
    u32 quality = 1u;
};

// once the instance exists (on_load):
GLT::reflect::register_type<my_settings>();
GLT::engine_settings::register_block({ "my_plugin.settings", "World", GLT::reflect::type_of<my_settings>(), &m_settings,
    [this] { /* optional: something derived from the values has to be rebuilt */ } });

// before it goes away (on_unload):
GLT::engine_settings::unregister_block("my_plugin.settings");
```

The stored values are written into the instance right away and whenever they change; the owner just reads its struct.
Stored and shown: `bool`, integers, `f32` / `f64`. Members marked `skip`, `transient` or `readonly` are left alone.
Values are clamped to `range`.

## Editor

Edit > Engine Settings (Ctrl+Shift+,). The switch at the top of the sidebar selects the file the rows edit: "Project"
or "This computer". The reset arrow removes the value from that file. A project row that this computer overrides says
so. The viewport's quick "Settings" popup edits the same values; what is changed there is kept as a change on this
computer.
