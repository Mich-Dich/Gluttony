

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







