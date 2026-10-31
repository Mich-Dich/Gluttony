
#pragma once

#include <render/i_renderer.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::icon_manager {

    // CONSTANTS =======================================================================================================
    
    using thumbnail_handle = u32;

    constexpr thumbnail_handle          invalid_thumbnail_handle = 0;

    // Core-reserved range: [1, 1024). Plugins: [1024, ...)
    constexpr u32                       ICON_TYPE_CUSTOM_BEGIN = 1024;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Strongly-typed icon identifier
    //
    // Wraps a u32 so plugin icons can be handed out at runtime without recompiling the editor
    // Values in [1, 1024) are reserved for the editor's core icons (see [icon])
    // Values at or above ICON_TYPE_CUSTOM_BEGIN are handed out by register_icon()
    struct icon_type {

        u32                             value;

        constexpr icon_type() noexcept = default;
        constexpr explicit icon_type(u32 v) noexcept : value(v) {}

        constexpr bool operator==(const icon_type&) const noexcept = default;
        constexpr auto operator<=>(const icon_type&) const noexcept = default;

        // Hash for unordered_map
        friend constexpr u32 hash(const icon_type& t) noexcept { return t.value; }
    };


    // @brief Core icons shipped with the editor
    //
    // For best atlas packing, prefer 24x24 or 128x128 px icon images. The manager groups icons by native pixel size, and each unique
    // size gets its own atlas page. Sticking to a couple of standard sizes keeps the texture pool small and cache-friendly when many
    // icons are drawn together, mixing many sizes fragments it and wastes VRAM
    //
    // Icons are grouped by purpose: window controls, connection status, media control, warning/refresh, and NewTec branding
    namespace icon {

        inline constexpr icon_type      logo                        {  1 };     // main Gluttony logo
        inline constexpr icon_type      logo_small                  {  2 };
        inline constexpr icon_type      play                        {  3 };
        inline constexpr icon_type      pause                       {  4 };
        inline constexpr icon_type      stop                        {  5 };
        inline constexpr icon_type      step                        {  6 };
        inline constexpr icon_type      file                        {  7 };
        inline constexpr icon_type      file_big                    {  8 };
        inline constexpr icon_type      script                      {  9 };
        inline constexpr icon_type      script_big                  { 10 };
        inline constexpr icon_type      folder_add                  { 11 };
        inline constexpr icon_type      folder_big                  { 12 };
        inline constexpr icon_type      folder_open                 { 13 };
        inline constexpr icon_type      folder                      { 14 };
        inline constexpr icon_type      hide                        { 15 };
        inline constexpr icon_type      material_inst               { 16 };
        inline constexpr icon_type      material                    { 17 };
        inline constexpr icon_type      material_big                { 18 };
        inline constexpr icon_type      mesh_asset                  { 19 };
        inline constexpr icon_type      mesh_asset_big              { 20 };
        inline constexpr icon_type      mesh_mini                   { 21 };
        inline constexpr icon_type      relation                    { 22 };
        inline constexpr icon_type      settings                    { 23 };
        inline constexpr icon_type      show                        { 24 };
        inline constexpr icon_type      texture                     { 25 };
        inline constexpr icon_type      texture_big                 { 26 };
        inline constexpr icon_type      transfrom_rotation          { 27 };
        inline constexpr icon_type      transfrom_scale             { 28 };
        inline constexpr icon_type      transfrom_translation       { 29 };
        inline constexpr icon_type      warning                     { 30 };
        inline constexpr icon_type      world                       { 31 };
        inline constexpr icon_type      sun                         { 32 };
        inline constexpr icon_type      point_light                 { 33 };
        inline constexpr icon_type      spot_light                  { 34 };
        inline constexpr icon_type      camera                      { 35 };
        inline constexpr icon_type      audio                       { 36 };
        inline constexpr icon_type      empty                       { 37 };
    };


    enum class thumbnail_state : u8 {

        pending,    // load job is queued or in flight
        ready,      // atlas slot is populated, tex_ref is valid
        failed,     // couldn't load (not an image, missing, corrupted, ...)
    };


    struct thumbnail_data {

        thumbnail_state                 state = thumbnail_state::failed;
        ImTextureRef                    tex_ref{};      // valid only when state == ready
        ImVec2                          image_size{};   // scaled size, largest side <= 128 px
        ImVec2                          uv0{};          // top-left UV of the image inside its atlas page
        ImVec2                          uv1{};          // bottom-right UV
    };


    struct icon_data {

        ImTextureRef                    tex_ref{};      // texture to pass to ImGui::Image()
        ImVec2                          image_size{};   // native pixel size (for layout math)
        ImVec2                          uv0{};          // top-left UV
        ImVec2                          uv1{};          // bottom-right UV
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Initialise the icon manager
    //
    // This function must be called once before any icons are requested. It sets up the internal resource manager and
    // starts the background cleanup thread
    void init();


    // @brief Shut down the icon manager and release all loaded icons
    //
    // Stops the background cleanup thread and releases all cached icon resources. No further calls to @ref get() should be
    // made after shutdown
    void shutdown();


    //
    icon_data get(const icon_type type);


    // @brief Check whether an icon is currently loaded and has active references
    //
    // Useful for debugging or for determining if an icon should be kept in memory
    //
    // @param type The icon type to check
    // @return true if the icon is loaded and its reference count > 0,
    // @return false otherwise
    bool is_asset_in_active_use(const icon_type type);


    // @brief Register a new icon with the manager
    //
    // The icon's image must exist at:
    //   <executable_path>/<ASSET_DIR>/image/<name>.png
    //
    // Idempotent: if [name] is already registered, returns the existing icon_type. New registrations are handed out starting
    // at ICON_TYPE_CUSTOM_BEGIN. Names that collide with a core icon (see [icon]) return that core icon_type — plugins cannot
    // shadow built-ins
    //
    // Plugins should call this during on_load() (before init()). If the manager has already been initialised, call reload()
    // afterwards to rebuild the atlases
    //
    // For best atlas locality, use a 24x24 or 128x128 px image — see [icon]
    icon_type register_icon(std::string_view name);


    // @brief Look up the name of an icon_type. Returns "<unnamed>" if unknown
    std::string_view icon_to_string(icon_type type);


    // @brief Look up icon_type by name. Returns std::nullopt if not registered
    std::optional<icon_type> string_to_icon(std::string_view name);


    // @brief Rebuild the icon atlases from all currently registered icons
    //
    // Invalidates every previously returned icon_data — the underlying texture is destroyed and rebuilt. Only call when
    // no icon is currently drawn, e.g right after register_icon() during plugin load, or between frames
    void reload();

    // Request a thumbnail for `path`. The first call queues a load job and returns a handle in the `pending` state
    // Subsequent calls with the same path return the same handle. Poll get_thumbnail() to know when it's done
    //
    // Passing an empty path returns invalid_thumbnail_handle
    thumbnail_handle request_thumbnail(const std::filesystem::path& path);


    // Poll a thumbnail handle. Cheap - safe to call every frame. Returns a default-constructed (failed) thumbnail_data
    // for an invalid handle
    thumbnail_data get_thumbnail(thumbnail_handle handle);


    // Convenience one-shot: request_thumbnail + get_thumbnail. The first call for a given path returns `pending`; subsequent
    // calls return `ready` or `failed`. Prefer the handle-based form when you can cache the handle
    thumbnail_data get_thumbnail(const std::filesystem::path& path);


    // Must be called once per frame on the main thread, after all request_thumbnail() calls for the frame have been made
    // Drains finished load jobs into their atlas pages and uploads any dirty pages to the GPU
    //
    // Cheap no-op when nothing is pending
    void flush_thumbnail_uploads();


    // Approximate VRAM usage of all thumbnail atlas pages, in bytes. Debug only
    u64 thumbnail_atlas_memory_usage();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

// std::hash specialization --------------------------------------------------------------------------------------------
namespace std {

    template<>
    struct hash<GLT::editor::icon_manager::icon_type> {
        size_t operator()(const GLT::editor::icon_manager::icon_type& t) const noexcept { return std::hash<u32>{}(t.value); }
    };

}


/*

Usage example in a plugin:

in plugin on_load(): 
```cpp
    // Image must live at:  <exe>/assets/image/my_plugin_weapon.png
    const auto my_icon = GLT::editor::icon_manager::register_icon("my_plugin_weapon");
```

Later usage:
```cpp
const auto data = GLT::editor::icon_manager::get(my_icon);
if (data.tex_ref)
    ImGui::Image(data.tex_ref, data.image_size, data.uv0, data.uv1);
```
*/
