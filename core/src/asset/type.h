
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using handle = ::handle;

    using content_hash = u64;    // xxh3 of the payload — hot-reload detection

    using chunk_id = u32;    // handler-defined (mesh: "vertices", "indices", "bvh", ...)


    struct type {

        u32                                             value;

        constexpr type() noexcept = default;                            // default ctor
        constexpr explicit type(u32 v) noexcept : value(v) {}

        constexpr bool operator==(const type&) const noexcept = default;
        constexpr auto operator<=>(const type&) const noexcept = default;

        // Hash for unordered_map
        friend constexpr u32 hash(const type& t) noexcept { return t.value; }
    };


    // Core-reserved range: [0, 1024) Plugin range: [1024, ...). Registry hands these out.
    // Plugin calls registry->reserve_type("mygame.weapon") once at on_load().
    namespace core_types {

        inline constexpr type invalid               {  0 };
        inline constexpr type world                 {  1 };             // Complete world/scene file  
        inline constexpr type map                   {  2 };             // Sub-section of a world (chunk/region)  
        inline constexpr type audio                 {  3 };             // Generic audio asset (to be specialized later)  

		// ------ mesh types ------
        inline constexpr type static_mesh           {  4 };             // Non-animated mesh geometry
        inline constexpr type procedural_mesh       {  5 };             // Programmatically generated mesh
        inline constexpr type dynamic_mesh          {  6 };             // Mesh that can be modified at runtime
        inline constexpr type skeletal_mesh         {  7 };             // Mesh with bone animation support
        inline constexpr type mesh_collection       {  8 };             // Collection of multiple meshes

		// ------ texture types ------
        inline constexpr type texture2D             {  9 };             // Standard 2D texture
        inline constexpr type texture3D             { 10 };             // 3D volume texture
        inline constexpr type cube_map              { 11 };             // Cube map texture for sbyboxs/reflection

		// ------ material types ------
        inline constexpr type material              { 12 };             // Base material definition
        inline constexpr type material_instance     { 13 };             // Instance of a material with parameter overrides

        inline constexpr type anim                  { 14 };
        inline constexpr type light                 { 15 };             // RT light profiles
        inline constexpr type bvh                   { 16 };             // prebuilt BLAS/TLAS
        inline constexpr type volume                { 17 };             // participating media
    }


    enum class flags : u32 {

        none                                    = 0,
        compressed                              = BIT(0),               // whole-file (zstd etc.)
        encrypted                               = BIT(1),
        streaming                               = BIT(2),               // chunk sizes are hints, decode lazily
        editor_only                             = BIT(3),
        runtime_only                            = BIT(4),
        hot_reloadable                          = BIT(5),
        ray_traced                              = BIT(6),               // participates in BVH / BLAS / TLAS
    };


    // Result of a load attempt; use std::expected everywhere so the registry can propagate "missing / corrupt / unsupported" without exceptions or nulls.
    enum class load_error : u8 {

        not_found = 0,
        corrupt_header,
        unsupported_version,
        checksum_mismatch,
        missing_dependency,
        no_handler,
        out_of_memory,
        needs_reload,                                                   // handler asks registry to rebuild
        cyclic_dependency,                                              // useful for the loader below
    };


    enum class import_error : u8 {

        not_supported = 0,
        invalid_source,
        unsupported_format,
        handler_rejected,
        io_failure,
    };


    struct chunk_entry {                                                // 32 bytes, alignas(8)

        chunk_id                                id{};
        u32                                     compression{};          // codec id, 0 = raw
        u64                                     offset{};               // from start of file
        u64                                     size_on_disk{};
        u64                                     size_decoded{};
    };


    // On-disk dependency row. Path is NUL-terminated in the string table;
    // path_offset == 0 means "id-only" (resolved via the registry's id map).
    struct dependency_disk {

        UUID                                    id{};
        u64                                     path_offset{};
        u32                                     target_type{};
        u32                                     _pad{};
    };
    static_assert(std::is_trivially_copyable_v<dependency_disk>);
    static_assert(sizeof(dependency_disk) == 24);


    // General data that every asset file must have + custom data that is decided by the asset handler
    // TODO: finish this struct
    struct info {

        // --- identity (copied from header) ---
        UUID                                        id{};
        GLT::asset::type                            asset_type{};
        GLT::asset::content_hash                    hash{};
        GLT::asset::flags                           flag_bits{ GLT::asset::flags::none };
        u16                                         format_version{};
        u16                                         engine_version{};

        // paths -------------------------------------------------------------------------------------------------------
        std::filesystem::path                       virtual_path{};     // vfs path the registry loaded
        std::filesystem::path                       source_path{};      // where the import came from
        std::string                                 name{};

        // layout ------------------------------------------------------------------------------------------------------
        std::span<const chunk_entry>                chunks;

        // dependency graph (resolved to handles) ----------------------------------------------------------------------
        std::span<const GLT::asset::handle>         dependencies;
        std::span<const GLT::asset::handle>         dependents;

        // runtime bookkeeping (never serialized) ----------------------------------------------------------------------
        u64                                         bytes_resident{ 0 };
        std::chrono::system_clock::time_point       last_loaded{};
        std::chrono::system_clock::time_point       last_modified{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] constexpr std::string_view extension_for_type(GLT::asset::type t) noexcept {

        using namespace core_types;
        if (t == static_mesh || t == procedural_mesh || t == dynamic_mesh || 
            t == skeletal_mesh || t == mesh_collection)             return "glt_mesh";
        if (t == texture2D || t == texture3D || t == cube_map)      return "glt_texture";
        if (t == material || t == material_instance)                return "glt_material";
        if (t == audio)                                             return "glt_audio";
        if (t == anim)                                              return "glt_anim";
        if (t == light)                                             return "glt_light";
        if (t == bvh)                                               return "glt_bvh";
        if (t == volume)                                            return "glt_volume";
        if (t == world)                                             return "glt_world";
        if (t == map)                                               return "glt_map";
        return "glt_asset";
    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_runtime_asset {
    public:

        virtual ~i_runtime_asset() = default;

        // Cheap tag for the registry / debug tooling. Handlers fill this in.
        [[nodiscard]] virtual GLT::asset::type type() const noexcept = 0;

        // Approximate resident bytes, reported to the profiler.
        [[nodiscard]] virtual u64 memory_usage() const noexcept { return 0; }

        // Called by the registry when this asset's dependents need to know it changed (hot-reload). Default: no-op.
        virtual void on_reloaded() noexcept {}
    };

}

// ---- std::hash specialization -------------------------------------------------
namespace std {

    template<>
    struct hash<GLT::asset::type> {
        size_t operator()(const GLT::asset::type& t) const noexcept { return std::hash<u32>{}(t.value); }
    };

}
