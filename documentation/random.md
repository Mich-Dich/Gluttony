
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




# Help ful snippets


ImGui focus visualization. Draws a box around the last focus to the cursor!

```cpp
ImGuiContext& g = *ImGui::GetCurrentContext();

static ImGuiID last_active = 0;
if (g.ActiveId != 0) last_active = g.ActiveId;

ImGui::Separator();
ImGui::Text("Active: 0x%08X (last non-zero: 0x%08X)", g.ActiveId, last_active);
ImGui::Text("Hovered: 0x%08X  HoveredWindow: %s", g.HoveredId, g.HoveredWindow ? g.HoveredWindow->Name : "-");
ImGui::Text("NavId: 0x%08X  NavWindow: %s", g.NavId, g.NavWindow ? g.NavWindow->Name : "-");
ImGui::Text("NavDisableMouseHover: %d", (int)g.NavHighlightItemUnderNav);
ImGui::Text("MovingWindow: %s", g.MovingWindow ? g.MovingWindow->Name : "-");
ImGui::Text("MousePos: %.0f, %.0f   delta: %.1f, %.1f", g.IO.MousePos.x, g.IO.MousePos.y, g.IO.MouseDelta.x, g.IO.MouseDelta.y);
ImGui::Text("Popups: %d  Modal: %s", g.OpenPopupStack.Size, g.NavWindowingTarget ? g.NavWindowingTarget->Name : "-");

// Draws a red box around whichever item owns this ID
ImGui::DebugLocateItem(last_active);
ImGui::DebugLocateItem(g.NavId);
```




