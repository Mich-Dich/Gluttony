
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // Name used to register the component-data lookup table with the reflection system
    constexpr const char*                           COMPONENT_DATA_TABLE_NAME = "COMPONENT_DATA_TABLE_NAME";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Opaque asset identifier. Layout is defined by the registry implementation
    using handle = ::handle;


    // xxh3-style hash of an asset's payload - used for hot-reload detection
    using content_hash = u64;


    // Handler-defined tag identifying a named blob inside an asset file (e.g. mesh: "vertices", "indices", "bvh", ...)
    using chunk_id = u32;


    // @brief Strongly-typed asset type tag
    //
    // Wraps a raw u32 so the reflection system can distinguish asset types from arbitrary integers at compile time
    // values in [0, CUSTOM_TYPE_BEGIN) are reserved for the engine's core types (see [core_types])
    // values at or above CUSTOM_TYPE_BEGIN are handed out by the registry via reserve_type()
    struct type {

        u32                                         value;

        constexpr type() noexcept = default;                                // default ctor
        constexpr explicit type(u32 v) noexcept : value(v) {}

        constexpr bool operator==(const type&) const noexcept = default;
        constexpr auto operator<=>(const type&) const noexcept = default;

        // Hash for unordered_map
        friend constexpr u32 hash(const type& t) noexcept { return t.value; }
    };


    // @brief One row of the compile-time type reflection table
    //
    // [name] must be a [const char*] (not a [string_view]) because the array is built in a [consteval] context
    // and needs a structural element type
    struct type_entry {

        const char*                                 name;
        type                                        value;
    };


    // Core-reserved range: [0, 1024) Plugin range: [1024, ...). Registry hands these out
    // Plugin calls registry->reserve_type("mygame.weapon") once at on_load()
    namespace core_types {

        inline constexpr type invalid               {  0 };
        inline constexpr type world                 {  1 };                 // Complete world/scene file
        inline constexpr type region                {  2 };                 // Sub-section of a world
        inline constexpr type audio                 {  3 };                 // Generic audio asset

		// ------ mesh types ------
        inline constexpr type static_mesh           {  4 };                 // Non-animated mesh geometry
        inline constexpr type procedural_mesh       {  5 };                 // Programmatically generated mesh
        inline constexpr type dynamic_mesh          {  6 };                 // Mesh that can be modified at runtime
        inline constexpr type skeletal_mesh         {  7 };                 // Mesh with bone animation support
        inline constexpr type mesh_collection       {  8 };                 // Collection of multiple meshes

		// ------ texture types ------
        inline constexpr type texture2d             {  9 };                 // Standard 2D texture
        inline constexpr type texture3d             { 10 };                 // 3D volume texture
        inline constexpr type cube_map              { 11 };                 // Cube map texture for sbyboxs/reflection

		// ------ material types ------
        inline constexpr type material              { 12 };                 // Base material definition
        inline constexpr type material_instance     { 13 };                 // Instance of a material with parameter overrides

        inline constexpr type anim                  { 14 };
        inline constexpr type light                 { 15 };                 // RT light profiles
        inline constexpr type bvh                   { 16 };                 // prebuilt BLAS/TLAS
        inline constexpr type volume                { 17 };                 // participating media
    }


    // @brief Per-file capability bits stored in the asset header's [flags] field
    enum class flags : u32 {

        none                                        = 0,
        compressed                                  = BIT(0),               // whole-file (zstd etc.)
        encrypted                                   = BIT(1),
        streaming                                   = BIT(2),               // chunk sizes are hints, decode lazily
        editor_only                                 = BIT(3),
        runtime_only                                = BIT(4),
        hot_reloadable                              = BIT(5),
        ray_traced                                  = BIT(6),               // participates in BVH / BLAS / TLAS
    };


    // @brief Failure modes of a load() attempt
    //
    // Used with std::expected so the registry can propagate "missing / corrupt / unsupported" without exceptions or sentinel pointers
    enum class load_error : u8 {

        not_found = 0,
        corrupt_header,
        unsupported_version,
        checksum_mismatch,
        missing_dependency,
        no_handler,
        out_of_memory,
        needs_reload,                                                       // handler asks registry to rebuild
        cyclic_dependency,                                                  // useful for the loader below
        already_exists,                                                     // path or id already registered
    };


    // @brief Failure modes of an import() attempt
    enum class import_error : u8 {

        not_supported = 0,
        invalid_source,
        unsupported_format,
        handler_rejected,
        io_failure,
    };


    // @brief One entry of an asset file's chunk table. Describes where the raw bytes of a named blob live inside the file,
    // and how to interpret them
    struct chunk_entry {

        chunk_id                                    id{};
        u32                                         compression{};          // codec id, 0 = raw
        u64                                         offset{};               // from start of file
        u64                                         size_on_disk{};
        u64                                         size_decoded{};
    };


    // @brief On-disk dependency row
    //
    // Path is NUL-terminated in the string table; [path_offset == 0] means "id-only" (resolved via the registry's id map)
    struct dependency_disk {

        UUID                                        id{};
        u64                                         path_offset{};
        u32                                         target_type{};
        u32                                         _pad{};
    };
    static_assert(std::is_trivially_copyable_v<dependency_disk>);
    static_assert(sizeof(dependency_disk) == 24);


    // @brief Metadata the registry hands to a handler's deserialize(), plus bookkeeping it fills in for callers
    //
    // Combines identity (copied from the header), paths, the layout of the file (chunk table), and the resolved dependency graph
    // Handlers read whatever they need from here; they should never reach back into the registry while deserialize() is
    // running (the registry holds its lock during the call)
    struct info {

        // --- identity (copied from header) ---
        UUID                                        id{};
        GLT::asset::type                            asset_type{};
        GLT::asset::content_hash                    hash{};
        GLT::asset::flags                           flag_bits{ GLT::asset::flags::none };
        u16                                         format_version{};
        u16                                         engine_version{};

        // paths -------------------------------------------------------------------------------------------------------
        std::filesystem::path                       virtual_path{};         // vfs path the registry loaded
        std::filesystem::path                       source_path{};          // where the import came from
        std::string                                 name{};

        // layout ------------------------------------------------------------------------------------------------------
        std::span<const chunk_entry>                chunks;

        // dependency graph (resolved to handles) ----------------------------------------------------------------------
        std::span<const GLT::asset::handle>         dependencies;
        std::span<const GLT::asset::handle>         dependents;

        // Parallel to [dependencies]: the UUID that each resolved handle came from Entries for INVALID_HANDLE rows are
        // still present (that's the UUID the registry failed to resolve), so positional alignment is preserved
        //
        // Handlers MUST use this when they need to map a serialized UUID back to a live handle
        // Do NOT call registry->info(handle).id from deserialize() -  the registry holds its lock while invoking handlers
        // so any callback into the registry self-deadlocks
        std::span<const UUID>                       dependency_ids;

        // runtime bookkeeping (never serialized) ----------------------------------------------------------------------
        u64                                         bytes_resident{ 0 };
        std::chrono::system_clock::time_point       last_loaded{};
        std::chrono::system_clock::time_point       last_modified{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Engine-reserved lookup (uses core_types by default) -------------------------------------------------------------

    // @brief Maps a core asset type to its registered name
    // @param type The type to look up
    // @return The matching [type_entry::name], or "<unnamed>" if not registered
    FORCE_INLINE_R constexpr std::string_view type_to_string(const type& type) noexcept;


    // @brief Maps a name back to its core asset type
    // @param str The registered name to look up
    // @return The matching type, or std::nullopt if no entry matches
    FORCE_INLINE_R constexpr std::optional<type> string_to_type(std::string_view str) noexcept;

    // general usage ---------------------------------------------------------------------------------------------------

    // @brief Returns the on-disk extension for an asset type, including the leading "glt_"
    // @param type The asset type
    // @return The extension string (e.g. "glt_material")
    FORCE_INLINE_R constexpr std::string_view type_to_extension(GLT::asset::type type) noexcept;


    // @brief Maps a file extension back to an asset type
    //
    // Accepts the extension with or without a leading '.'; anything that does
    // not carry the "glt_" prefix is reported as [core_types::invalid] (this
    // covers source files that live in the content dir but were never imported)
    //
    // @param extension The file extension to parse
    // @return The matching core type, or [core_types::invalid]
    FORCE_INLINE_R constexpr GLT::asset::type extension_to_type(std::string_view extension) noexcept;


    // // Resolve [entry.extension] to a type and ask whether that type is known to carry a CHUNK_THUMBNAIL
    // // Today that's just texture2d; later it'll grow
    // FORCE_INLINE_R bool type_has_baked_thumbnail(GLT::asset::type t) noexcept {
    //     switch (t.value) {
    //         case GLT::asset::core_types::texture2d.value: return true;
    //         // case GLT::asset::core_types::static_mesh.value: return true;
    //         // case GLT::asset::core_types::material.value:    return true;
    //         default: return false;
    //     }
    // }

    // TEMPLATE DECLARATION ============================================================================================

    // @brief Builds a compile-time table of every variable of type [type] inside [Namespace]
    // @tparam Namespace The C++26 reflection value (e.g. [^^core_types]) to scan
    // @return A static array of type_entry records
    template <auto Namespace>
    FORCE_INLINE_R consteval auto make_type_entries();


    // @brief Compile-time table of [type_entry] records for [Namespace]
    template <auto Namespace>
    inline constexpr auto type_entries = make_type_entries<Namespace>();


    // @brief Maps an asset type to its name via reflection over [Namespace]
    // @tparam Namespace The namespace to search. Defaults to [^^core_types]
    // @param type The type to look up
    // @return The matching name, or "<unnamed>" if none matches
    template <auto Namespace = ^^core_types>
    FORCE_INLINE_R constexpr std::string_view type_to_string(const type& type);


    // @brief Maps a name back to its asset type via reflection over [Namespace]
    // @tparam Namespace The namespace to search. Defaults to [^^core_types]
    // @param str The name to look up
    // @return The matching type, or std::nullopt if no entry matches
    template <auto Namespace = ^^core_types>
    FORCE_INLINE_R constexpr std::optional<type> string_to_type(std::string_view str);
    
    // CLASS DECLARATION ===============================================================================================

    // @brief Base class for every decoded asset
    //
    // The registry stores handlers' outputs behind this interface so it can own their lifetime without knowing their concrete type
    // Handlers fill in [type()] and optionally [memory_usage()]
    class i_runtime_asset {
    public:

        virtual ~i_runtime_asset() = default;

        // Cheap tag for the registry / debug tooling. Handlers fill this in
        [[nodiscard]] virtual GLT::asset::type type() const noexcept = 0;


        // Approximate resident bytes, reported to the profiler
        [[nodiscard]] virtual u64 memory_usage() const noexcept { return 0; }


        // Called by the registry when this asset's dependents need to know it changed (hot-reload). Default: no-op
        virtual void on_reloaded() noexcept {}
    };


}

// ---- std::hash specialization ---------------------------------------------------------------------------------------
namespace std {

    template<>
    struct hash<GLT::asset::type> {
        size_t operator()(const GLT::asset::type& type) const noexcept { return std::hash<u32>{}(type.value); }
    };

}

#include "type.inl"
