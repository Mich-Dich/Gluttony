#include "util/pch.h"
#include "scripting/script_fields.h"

#include "reflection/archive_binary.h"



namespace GLT::script {

    // CONSTANTS =======================================================================================================

    static constexpr u32                                BLOB_VERSION = 1;
    static constexpr u32                                MAX_FIELDS = 4096;

    // FUNCTION IMPLEMENTATION =========================================================================================

    bool in_set(const script_field_role role, const field_set set) noexcept {

        switch (set) {
            case field_set::editor:     return role == script_field_role::editor || role == script_field_role::editor_persist;
            case field_set::persist:    return role == script_field_role::persist || role == script_field_role::editor_persist;
        }
        return false;
    }


    void* field_address(const script_class_info& info, i_script& instance, const script_field_info& field) {

        void* object = info.object_of ? info.object_of(&instance) : static_cast<void*>(&instance);
        return static_cast<std::byte*>(object) + field.member->offset;
    }


    std::vector<std::byte> capture_fields(const script_class_info& info, const i_script& instance, const field_set set) {

        auto& object = const_cast<i_script&>(instance);         // write_fn only reads
        u32 count = 0;
        for (const script_field_info& f : info.fields)
            if (f.member && f.member->write_fn && in_set(f.role, set))
                ++count;

        reflect::binary_writer out{};
        out.write(BLOB_VERSION);
        out.write(count);
        for (const script_field_info& f : info.fields) {
            if (!f.member || !f.member->write_fn || !in_set(f.role, set))
                continue;

            reflect::binary_writer value{};
            try {
                f.member->write_fn(field_address(info, object, f), &value);
            } catch (const std::exception& e) {
                LOG(warn, "Script [{}]: field [{}] could not be written ({}), it keeps its source default", info.name,
                    f.member->name, e.what());
                value = reflect::binary_writer{};
            }
            out.write(std::string(f.member->name));
            out.write(f.type_id);
            out.write_blob(value.data());
        }
        return std::move(out).take();
    }


    u32 apply_fields(const script_class_info& info, i_script& instance, std::span<const std::byte> blob, const field_set set) {

        if (blob.empty())
            return 0;

        reflect::binary_reader in(blob);
        u32 version = 0, count = 0;
        if (!in.read(version) || version != BLOB_VERSION || !in.read(count))
            return 0;

        u32 applied = 0;
        for (u32 i = 0; i < count && i < MAX_FIELDS && in.ok(); ++i) {

            std::string name{};
            u32 type_hash = 0;
            std::span<const std::byte> value{};
            if (!in.read(name) || !in.read(type_hash) || !in.read_blob(value))
                break;
            if (value.empty())
                continue;                                       // "source default" marker

            for (const script_field_info& f : info.fields) {
                if (!f.member || !f.member->read_fn || !in_set(f.role, set) || f.type_id != type_hash
                    || name != f.member->name)
                    continue;

                reflect::binary_reader field_in(value);
                try {
                    if (f.member->read_fn(field_address(info, instance, f), &field_in))
                        ++applied;
                } catch (const std::exception& e) {
                    LOG(warn, "Script [{}]: field [{}] could not be read ({}), it keeps its source default", info.name,
                        name, e.what());
                }
                break;
            }
        }
        return applied;
    }


    std::vector<std::byte> default_fields(const script_class_info& info, const field_set set) {

        if (!info.create || !info.destroy)
            return {};
        i_script* temp = info.create();
        std::vector<std::byte> out = capture_fields(info, *temp, set);
        info.destroy(temp);
        return out;
    }

}
