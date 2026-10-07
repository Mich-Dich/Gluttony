
#pragma once

#include "asset/type.h"

#include "plugin_system/i_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Read-only view over an asset file's chunks, provided by the registry to a handler's `deserialize()`
    //
    // Backed by the file mapping (or by the in-memory blob during import). Only valid for the duration of the `deserialize()`
    // call - handlers must copy anything they intend to keep past that point
    class chunk_reader {
    public:

        virtual ~chunk_reader() = default;


        // @brief Reports whether a chunk with the given id is present
        // @param id The chunk id to test
        // @return true if the chunk exists in the file
        [[nodiscard]] virtual bool has(chunk_id id) const noexcept = 0;


        // @brief Returns the raw bytes of a chunk
        //
        // Decompresses on demand if the chunk's entry says so. Returns an empty span when the chunk is missing
        //
        // @param id The chunk id to fetch
        // @return View over the (possibly decompressed) chunk bytes
        [[nodiscard]] virtual std::span<const std::byte> get(chunk_id id) const = 0;


        // @brief Returns every chunk id present in the file
        // @return Span over the chunk-id list. Valid until the next call
        [[nodiscard]] virtual std::span<const chunk_id> available() const noexcept = 0;


        // @brief Typed convenience wrapper around `get()`
        //
        // @tparam T A trivially copyable type
        // @param id The chunk id to fetch
        // @return A typed view over the chunk bytes; empty if the chunk is missing or its byte length is not a whole multiple
        // of [sizeof(T)]
        template<typename T> requires std::is_trivially_copyable_v<T>
        [[nodiscard]] std::span<const T> get_as(chunk_id id) const {

            auto bytes = get(id);
            return { reinterpret_cast<const T*>(bytes.data()), bytes.size() / sizeof(T) };
        }

    };

    // @brief Writer handed to [i_asset_factory_plugin::import()]
    //
    // Lets an importer emit chunks without ever knowing about the on-disk layout - the registry writes the header,
    // string table, dependency table, and checksums on its behalf
    class asset_writer {
    public:

        virtual ~asset_writer() = default;


        // @brief Adds a chunk to the file being composed
        // @param id           Handler-defined chunk id
        // @param data         Raw chunk bytes (copied)
        // @param compression  Codec id; 0 = raw
        virtual void write_chunk(chunk_id id, std::span<const std::byte> data, u32 compression = 0) = 0;


        // @brief Declares a dependency by UUID alone
        //
        // Use when the target's id is already known (e.g. re-saving a live asset whose deps have all been resolved)
        //
        // @param id UUID of the dependency
        virtual void declare_dependency(const UUID id) = 0;


        // @brief Declares a dependency by content-relative path
        //
        // Resolved by the registry on finalize. Used by importers that only know where the target file will live, not its UUID
        //
        // @param virtual_path  Content-relative path of the dependency
        // @param target_type   Asset type the dependency is expected to have
        virtual void declare_dependency(std::string_view virtual_path, GLT::asset::type target_type) = 0;


        // @brief Declares a dependency carrying both id and path
        //
        // Used by `save()`: the loader shortcuts by id when the dep is already resident, and falls back to the path on cold start
        // Pass an empty path for "id-only"
        //
        // @param id            UUID of the dependency
        // @param virtual_path  Content-relative path (may be empty)
        // @param target_type   Asset type the dependency is expected to have
        virtual void declare_dependency(const UUID id, std::string_view virtual_path, GLT::asset::type target_type) = 0;


        // @brief Sets the asset's display name in the file's string table
        // @param name Human-readable name
        virtual void set_name(std::string_view name) = 0;
    };


    // @brief Bag of options passed through to a factory's [import()]
    struct import_options {

        bool                            editor_preview_only{ false };
        bool                            strip_editor_data{ true  };
        std::span<const std::byte>      type_specific;      // per-type knobs live in a blob the handler parses
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Runtime plugin that decodes (and optionally encodes) one or more asset types
    //
    // Registered with the registry via [register_handler()] on the plugin's [on_load()], unregistered on [on_unload()]
    // Handlers are the only side that knows how to turn chunks into engine objects - factories are the only side that
    // knows how to turn source files into chunks
    class i_asset_handler : public GLT::plugin_manager::i_plugin {
    public:

        // @brief Returns the asset types this handler can deserialize
        // @return Span over the handler's static type list
        [[nodiscard]] virtual std::span<const GLT::asset::type> types() const noexcept = 0;

        // ---- decoding ----

        // @brief Builds the runtime representation of an asset from its chunks
        //
        // The registry holds its lock while this runs, so handlers must not call back into the registry (that would self-deadlock)
        // Anything the handler needs from the registry should already be in `info` - particularly `info.dependency_ids`, 
        // which is the sanctioned way to map a serialized UUID back to a live handle
        //
        // @param info    Metadata read from the file header plus resolved deps
        // @param reader  Read-only view over the file's chunks
        // @return The decoded asset, or a load_error
        [[nodiscard]] virtual std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> deserialize(
            const GLT::asset::info& info, GLT::asset::chunk_reader& reader) = 0;


        // ---- encoding ----

        // @brief Re-emits a live asset back to disk
        //
        // Called by the registry when the user asks to persist a live asset. The handler is expected to emit the SAME chunk layout
        // it consumes in [deserialize()], plus re-declare its dependencies via [writer.declare_dependency(...)] so
        // cold-start resolution keeps working
        //
        // Default: refuses with [load_error::no_handler]. Override only for asset types whose runtime state can actually
        // diverge from disk (worlds, regions, user-edited materials, ...). Meshes and textures can leave this alone
        //
        // @param info   Metadata of the asset being saved
        // @param asset  Live runtime asset to serialize
        // @param out    Writer the handler emits chunks into
        // @return Success, or a load_error
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> serialize(const GLT::asset::info& /*info*/, 
            const GLT::asset::i_runtime_asset& /*asset*/, GLT::asset::asset_writer& /*out*/) const {

            return std::unexpected{ GLT::asset::load_error::no_handler };
        }


        // ---- hot reload ----

        // @brief Reacts to a change in one of this asset's dependencies
        //
        // Default: asks the registry to rebuild from scratch by returning [load_error::needs_reload]; the registry then
        // tears the asset down and re-runs [deserialize()]. Handlers that can patch incrementally override this
        //
        // @param asset   Live runtime asset that needs updating
        // @param info    Current metadata of the asset
        // @param reader  Fresh chunk view over the re-read file
        // @return Success, or a load_error
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> on_dependency_changed(GLT::asset::i_runtime_asset& /*asset*/,
            const GLT::asset::info& /*info*/, GLT::asset::chunk_reader& /*reader*/) {

            return std::unexpected{ GLT::asset::load_error::needs_reload };
        }

    };

}
