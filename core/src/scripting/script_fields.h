#pragma once

#include "scripting/i_script_plugin.h"

// Reading and writing the reflected fields of a script instance as a blob: the editor settings of a binding
// ([[=editor]] fields, stored in the world) and the persist snapshot of hot reload ([[=persist]] fields).
//
// Blob layout (stable, tolerant): u32 version (1), u32 count, then per field
//     string name, u32 type hash, blob (the member's own serialization, reflect::binary_writer)
// A field is applied only if the class still has a field of that name AND type: renamed, removed or retyped fields are
// skipped (they keep the source default), new fields keep the source default.



namespace GLT::script {

    // Which fields a blob holds
    enum class field_set : u8 {
        editor,                                     // [[=editor]] (also [[=editor]] [[=persist]])
        persist,                                    // [[=persist]] (also [[=editor]] [[=persist]])
    };

    [[nodiscard]] bool in_set(script_field_role role, field_set set) noexcept;


    // @brief The fields of [set] of a live instance
    [[nodiscard]] std::vector<std::byte> capture_fields(const script_class_info& info, const i_script& instance, field_set set);


    // @brief Applies a blob made by capture_fields to [instance]. Returns the number of fields applied
    u32 apply_fields(const script_class_info& info, i_script& instance, std::span<const std::byte> blob, field_set set);


    // @brief The [set] fields of a fresh instance (source defaults). Creates and destroys a temporary instance
    [[nodiscard]] std::vector<std::byte> default_fields(const script_class_info& info, field_set set);


    // @brief Address of [field] inside [instance]
    [[nodiscard]] void* field_address(const script_class_info& info, i_script& instance, const script_field_info& field);

}
