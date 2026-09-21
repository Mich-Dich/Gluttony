
#pragma once

#include "asset/type.h"

#include "plugin_system/i_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Provided by the registry to deserialize(). Backed by the file mapping
    // (or the in-memory blob during import). Valid only during deserialize().
    class chunk_reader {
    public:

        virtual ~chunk_reader() = default;


        [[nodiscard]] virtual bool has(chunk_id id) const noexcept = 0;


        // Decompresses on demand if the chunk_entry says so.
        [[nodiscard]] virtual std::span<const std::byte> get(chunk_id id) const = 0;


        [[nodiscard]] virtual std::span<const chunk_id> available() const noexcept = 0;


        // T must be trivially copyable; returns a view into the chunk buffer.
        template<typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]] std::span<const T> get_as(chunk_id id) const {

            auto bytes = get(id);
            return { reinterpret_cast<const T*>(bytes.data()), bytes.size() / sizeof(T) };
        }

    };

    // Writer handed to import(). Lets the importer emit chunks without
    // ever knowing about the on-disk layout - the registry writes the header.
    class asset_writer {
    public:

        virtual ~asset_writer() = default;

        virtual void write_chunk(chunk_id id, std::span<const std::byte> data, u32 compression = 0) = 0;

        // ID form - caller already knows the target asset.
        virtual void declare_dependency(const UUID id) = 0;

        // Path form - the registry resolves (and imports if needed) on finalize.
        // `virtual_path` is relative to the source being imported.
        virtual void declare_dependency(std::string_view virtual_path, GLT::asset::type target_type) = 0;

        virtual void set_name(std::string_view name) = 0;
    };


    struct import_options {

        bool                            editor_preview_only{ false };
        bool                            strip_editor_data{ true  };
        std::span<const std::byte>      type_specific;      // per-type knobs live in a blob the handler parses
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class i_asset_handler : public GLT::plugin_manager::i_plugin {
    public:

        // ---- decoding ----
        [[nodiscard]] virtual std::span<const GLT::asset::type> types() const noexcept = 0;


        [[nodiscard]] virtual std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> deserialize(
            const GLT::asset::info& info, GLT::asset::chunk_reader& reader) = 0;


        // Hot-reload path. Default: rebuild from scratch by returning
        // std::unexpected{ GLT::asset::load_error::needs_reload }. The registry then
        // tears down and re-runs deserialize(). Handlers that can patch
        // incrementally override this.
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> on_dependency_changed(GLT::asset::i_runtime_asset& /*asset*/,
            const GLT::asset::info& /*info*/, GLT::asset::chunk_reader& /*reader*/) {

            return std::unexpected{ GLT::asset::load_error::needs_reload };
        }

    };

}
