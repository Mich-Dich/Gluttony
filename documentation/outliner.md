# Outliner: folders, hiding, editing several entities

## Folders

Folders only organise the outliner. They are editor data: not part of the world, not in the world file, unknown to the
game. Code: `plugin/editor/src/window/outliner_layout.{h,cpp}` (the data), `world_viewport.cpp` (rows, drag and drop).

- New folder: the folder button next to "+ Add" (inside the folder that is selected), or right-click an entity >
  Move to Folder > New Folder (takes the selection along). The name can be typed right away; F2 or a double click renames.
- Drag entities (a drag that starts on a selected row carries the whole selection) or folders onto a folder. Drop on the
  empty space below the list or on the World row to take them out again. A folder cannot be put into itself.
- Only top-level entities sit in folders. Entities below another entity stay with their parent.
- Del with a folder selected removes the folder, its content moves up one level. "Delete Folder and Entities" is in the
  folder's context menu.
- Everything is undoable (Ctrl+Z), except opening / closing a folder.

Stored per world in `<project>/metadata/outliner/<world asset id>.txt`:

```
version 1
folder <id> <parent id> <open 0|1> <hidden 0|1> <name>
entity <index> <generation> <folder id>
hidden <index> <generation>
```

Entity ids are the ones the world file stores, so the file stays valid across sessions and while regions stream in and
out. A world that was never saved has no id yet: its folders live until the editor closes.

## The eye

Hides an entity **in the editor only**: its meshes are not drawn and its lights give no light, and the same goes for
everything below it. A folder's eye does this for every entity in the folder. A faint eye means "hidden through a parent
or a folder". Hidden entities can still be selected, their outline is drawn as usual.

This is separate from the mesh component's "Visible" switch, which is part of the world and is what the game sees.

How it works: the editor tells the world which entities are hidden (`i_world::set_hidden`). The world keeps handing them
to the renderers, marked (`asset::mesh::INSTANCE_FLAG_HIDDEN`, `light_snapshot::hidden`); renderers skip marked entries
except for `i_debug_draw::highlight_entity`.

## Details with several entities selected

The panel shows the components of the primary entity (the one clicked last) and applies a change to the others:

- **Shared**: only components every selected entity has.
- **All**: every component any of them has ("2 of 5" in the card header); a change reaches the entities that have it.

Only the value that was changed is copied, every entity keeps its other values. "+ Add" adds to every entity that lacks
the component, "Remove from all" is in the card's menu. One undo step covers all entities.

How it works: `component::i_storage::capture()` before the component is drawn, `apply_changes()` afterwards
(`plugin/world_ecs_entt/src/multi_edit.h`). Plain-data components work out of the box (compared in steps of four
bytes); a component that owns resources or needs work after a write gets a `multi_edit<T>` specialisation (mesh and
audio source have one). Components without support (name, hierarchy) say so in the panel and only change the shown entity.

## Keys and mouse

| | |
|---|---|
| Ctrl+A (outliner) | selects every entity that has a row right now. With a filter: the entities that match it. While the filter field has the focus Ctrl+A selects its text; press Enter first |
| Ctrl+F (outliner or viewport) | moves the camera, keeping its direction, to where the selection fills the view. The distance follows the size of the selected entities (mesh bounds, lights, everything below them) |
| Right click on a row | Add Child Entity, Duplicate, Hide / Show, Move to Folder, Delete, Add Component |
| F2, Del | rename / remove the selected folder |

## Gizmo with several entities

The gizmo sits on the primary entity. The others get the same change: the same distance moved, the same turn (each
around its own origin), the same scale factor per axis. An entity below another selected one is not touched, it follows
its parent. One undo step for all of them.

## Large selections

Thousands of selected entities are meant to work. What that needed: the renderer looks the outlined entities up with one
pass over the scene (`scene_renderer::process_debug_primitives`), boxes outside the view are dropped before their lines
are made, and a full vertex budget leaves lines out instead of skipping the frame. The Details panel and the gizmo only
take their undo snapshots on frames in which they can change something.
