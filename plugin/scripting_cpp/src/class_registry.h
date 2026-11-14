
#pragma once

#include "scripting/i_script_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::scripting_cpp {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Owns the script_class_info entries registered via register_script_class_impl.
    //
    // Storage contract: every string_view inside script_class_info (name, editor_label, registrant, fields[*].editor_category,
    // methods[*].name, methods[*].label) must have static storage duration. This holds for anything produced by register_script_class<T>():
    //   - reflect::type_name(^^T)        -> std::define_static_string output
    //   - editor_label_of<T>()           -> std::define_static_string output
    //   - field/method tables            -> std::define_static_array output
    //   - registrant                     -> caller-supplied; must be a string literal or plugin-owned static
    //
    // Pass a temporary std::string as registrant and the string_view dangles - a caller bug.
    //
    // The `all()` span is valid until the next add / remove / remove_by_registrant call.
    class class_registry {
    public:

        class_registry() = default;
        ~class_registry() = default;

        DELETE_COPY_AND_MOVE_CONSTRUCTOR(class_registry)


        // Rejects duplicates by name (logs and keeps the first). Returns true if inserted.
        bool add(script::script_class_info info);


        // Idempotent - no-op if no class with that name is registered
        void remove(std::string_view name);


        // Idempotent - no-op if the registrant has no classes
        void remove_by_registrant(std::string_view registrant);


        // Returns nullptr if not registered. Pointer valid until the next mutation.
        [[nodiscard]] const script::script_class_info* find(std::string_view name) const;


        // Span valid until the next add / remove / remove_by_registrant call
        [[nodiscard]] std::span<const script::script_class_info> all() const;


        [[nodiscard]] bool empty() const;


        [[nodiscard]] size_t size() const;

    private:

        std::vector<script::script_class_info>                                  m_entries{};
        mutable std::shared_mutex                                               m_mutex;
    };


    // Default-global bookkeeping. Grouped per registrant so clear_default_globals(r) can remove only
    // that registrant's entries without touching anyone else's.
    //
    // `default_global` holds two string_views - same stability rule as above.
    //
    // `all()` returns a flattened view across every registrant. The flat cache is rebuilt lazily on
    // the next call after any set / clear.
    class global_registry {
    public:

        global_registry()  = default;
        ~global_registry() = default;

        DELETE_COPY_AND_MOVE_CONSTRUCTOR(global_registry)

        // Replaces any existing globals for this registrant. Empty `globals` is equivalent to clear().
        void set(std::string_view registrant, std::span<const script::default_global> globals);


        void clear(std::string_view registrant);


        // Span valid until the next set / clear / all() call
        [[nodiscard]] std::span<const script::default_global> all() const;

    private:

        std::unordered_map<std::string, std::vector<script::default_global>>    m_by_registrant{};
        mutable std::vector<script::default_global>                             m_flat_cache;
        mutable bool                                                            m_flat_dirty = true;
        mutable std::shared_mutex                                               m_mutex;
    };

}
