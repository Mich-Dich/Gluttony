
#pragma once

#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect {
    class reflection_registry;
}

namespace GLT::reflect {

    // CONSTANTS =======================================================================================================

    constexpr u32                               INVALID_TYPE_HASH = 0;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class type_kind : u8 {

        unknown = 0,

        // primitives ------------------------------------------------------------------
        boolean,
        u8, u16, u32, u64,
        i8, i16, i32, i64,
        f32, f64,

        // composite -------------------------------------------------------------------
        enum_type,
        struct_type,
        string_type,
        path_type,
        handle_type,
        uuid_type,
        container_type,
        opaque_type,
    };


    enum class member_flags : u32 {

        none                                            = 0,
        serialize                                       = BIT(0),           // include in serialization
        skip                                            = BIT(1),           // explicitly excluded
        asset_ref                                       = BIT(2),           // handle-like; serialize as UUID
        readonly                                        = BIT(3),           // editor: not editable
        transient                                       = BIT(4),           // runtime-only
        color                                           = BIT(5),           // editor widget hint
        multiline                                       = BIT(6),           // editor widget hint
        has_range                                       = BIT(7),           // range_min/max are valid
        is_enum                                         = BIT(8),
        is_container                                    = BIT(9),
    };
    // (BIT-OR operators alongside, as you already have elsewhere.)


    struct enum_entry {

        const char*                                     name;
        i64                                             value;
    };


    struct member_descriptor {

        const char*                                     name{};
        u32                                             type_hash{};
        u32                                             offset{};           // valid for standard-layout
        u32                                             flags{};            // member_flags
        const char*                                     display_name{};     // empty = use name
        const char*                                     tooltip{};
        const char*                                     category{};
        f64                                             range_min{};
        f64                                             range_max{};
        type_kind                                       kind{ type_kind::unknown };

        // Fallback accessor for non-standard-layout types. Null → use offset
        // Generated once per member at compile time; see build.inl
        void*                                           (*access)(void*) = nullptr;
    };


    struct type_descriptor {

        const char*                                     name{};
        u32                                             hash{};
        u32                                             size{};
        u32                                             alignment{};
        type_kind                                       kind{ type_kind::unknown };
        std::span<const member_descriptor>              members{};          // struct_type
        std::span<const enum_entry>                     enum_values{};      // enum_type
        u16                                             version{ 1 };

        void (*default_construct)(void*) = nullptr;
        void (*copy_construct)(void*, const void*) = nullptr;
        void (*destruct)(void*) = nullptr;

        [[nodiscard]] bool is_struct() const noexcept { return kind == type_kind::struct_type; }
        [[nodiscard]] bool is_enum() const noexcept { return kind == type_kind::enum_type; }
        [[nodiscard]] bool is_container() const noexcept { return kind == type_kind::container_type; }
    };


    // A consumer that needs custom handling for one type registers here. The generic visitor checks for an override before
    // falling back to the default walk
    struct serialization_override {

        void (*write)(const void* ptr, void* archive);
        void (*read)(void* ptr, void* archive);
    };


    struct editor_draw_override {

        bool (*draw)(const member_descriptor&, void* ptr);
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================
    
    FORCE_INLINE_R constexpr u32 fnv1a_32(std::string_view s) noexcept;


    // Process-wide instance. Function-local static → no static-init order problems
    [[nodiscard]] reflection_registry& registry();

    // ---- registration API -------------------------------------------------------

    // Call after all registrations for a phase are done. Sorts and indexes
    void finalize();

    // ---- lookup helpers ---------------------------------------------------------

    FORCE_INLINE_R const type_descriptor* type_of(std::string_view name) noexcept;


    FORCE_INLINE_R const type_descriptor* type_of(u32 hash) noexcept;

    // ---- visitor ----------------------------------------------------------------

    // One level; recursion is the consumer's job (they know what to skip)
    using member_callback = std::function<bool(const member_descriptor&, void* member_ptr)>;


    void for_each_member(const type_descriptor& td, void* obj, const member_callback& cb);

    // ---- migration --------------------------------------------------------------

    using migrate_fn = void(*)(void* obj, u16 from_version);


    void register_migration(u32 type_hash, u16 from_version, migrate_fn fn);


    bool run_migrations(u32 type_hash, u16 from_version, void* obj);

    // ---- overrides (consumer behavior) ------------------------------------------


    void register_serializer(u32 type_hash, serialization_override o);


    void register_editor_draw(u32 type_hash, editor_draw_override o);


    [[nodiscard]] const serialization_override* serializer_override(u32 type_hash) noexcept;


    [[nodiscard]] const editor_draw_override* editor_draw_override_for(u32 type_hash) noexcept;

    // TEMPLATE DECLARATION ============================================================================================

    // ---- registration API -------------------------------------------------------

    // Explicit. Call from a known point (engine init, plugin on_load). Never from static-init — that fights the plugin model
    template <typename T>
    void register_type();

    template <typename E>
    requires std::is_enum_v<E>
    void register_enum();

    // ---- lookup helpers ---------------------------------------------------------

    template <typename T>
    [[nodiscard]] constexpr u32 hash_of() noexcept;

    template <typename T>
    [[nodiscard]] const type_descriptor* type_of() noexcept;

    // ---- visitor ----------------------------------------------------------------

    template <typename T>
    void for_each_member(T* obj, const member_callback& cb);

    // CLASS DECLARATION ===============================================================================================

    class reflection_registry {
    public:

        void add(const type_descriptor* td);

        void remove(u32 hash) noexcept;

        void finalize();

        [[nodiscard]] const type_descriptor* find(u32 hash) const noexcept;

        [[nodiscard]] const type_descriptor* find(std::string_view name) const noexcept;

        [[nodiscard]] std::span<const type_descriptor* const> all() const noexcept;

        [[nodiscard]] bool empty() const noexcept;

    private:

        std::unordered_map<u32, const type_descriptor*>                 m_by_hash;
        std::unordered_map<std::string_view, const type_descriptor*>    m_by_name;
        std::vector<const type_descriptor*>                             m_all;
        mutable std::shared_mutex                                       m_mutex;
        bool                                                            m_finalized{ false };
    };

}

#include "reflection/build.inl"
#include "reflection/registry.inl"
