
#pragma once

#include "asset/type.h"
#include "asset/header.h"
#include "asset/i_asset_handler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory {
    class i_asset_factory_plugin;
}

namespace GLT::asset::factory {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief One (source-extension -> target-type) pairing a factory claims
    //
    // [source_extension] is lowercase and carries no leading dot. An empty string means "any extension"
    // useful for formats detected by magic bytes rather than by suffix (e.g. a sniffer)
    struct binding {

        std::string_view                source_extension{};     // "fbx", "obj", "gltf", "png", "wav", ""
        GLT::asset::type                target_type;            // what it produces
        const i_asset_factory_plugin*   factory{ nullptr };     // set by the registry; lets callers reach option_schema()
    };


    // @brief Metadata the factory hands back to the registry after a successful write
    //
    // The registry uses this to update its source -> asset index and to decide whether a re-import is actually needed
    // on the next pass. [source_hash] and [payload_hash] are compared separately so a touching-only source edit
    // (same bytes, new mtime) doesn't trigger a rebuild
    struct import_result {

        std::filesystem::path           output_path{};          // where the .glt_* landed
        UUID                            id{};                   // freshly minted (or preserved)
        GLT::asset::content_hash        source_hash{};          // xxh3 of the SOURCE file(s)
        GLT::asset::content_hash        payload_hash{};         // xxh3 of the emitted chunks
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Editor / build-pipeline plugin: converts an external format into one of the engine's [.glt_*] files
    //
    // A factory writes chunks through an [asset_writer]; the registry owns the file header, string table, dependency table,
    // and checksums. Factories and runtime handlers are deliberately decoupled - they only share the chunk layout, 
    // declared in a common header (e.g. [mesh.h]) that both sides include
    //
    // Lifecycle: registered with the registry via [register_factory()] on the plugin's [on_load()], and unregistered on [on_unload()]
    class i_asset_factory_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_asset_factory_plugin() = default;

        // capability declaration --------------------------------------------------------------------------------------

        // @brief Returns all (extension -> type) pairs this factory can produce
        //
        // The registry consults this for routing AND for building the editor's "Import as..." context menu
        //
        // @return Span over the factory's static bindings table
        [[nodiscard]] virtual std::span<const GLT::asset::factory::binding> bindings() const noexcept = 0;


        // @brief Asks whether this factory wants to see the request even when it doesn't match any declared binding
        //
        // Useful for magic-byte sniffers. Default: no
        //
        // @param source Path to the candidate source file
        // @return true if this factory should be offered the import
        [[nodiscard]] virtual bool can_sniff(const std::filesystem::path& /*source*/) const noexcept { return false; }

        // importing ---------------------------------------------------------------------------------------------------

        // @brief Reads [source] and emits chunks for [target_type] through [out]
        //
        // On success the registry will:
        //   1. finalize the file at [result.output_path]
        //   2. register the asset in the source -> asset index
        //   3. optionally call [registry.load(result.output_path)] to bring it in immediately (editor preview path)
        //
        // On failure, the registry discards whatever [out] accumulated; writers are transactional
        //
        // @param source       Absolute path to the source file
        // @param target_type  Asset type the factory should emit
        // @param opts         Import-time options (see [import_options])
        // @param out          Writer the factory emits chunks into
        // @return Metadata describing the emitted file, or an import_error
        [[nodiscard]] virtual std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> import(
            const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& opts, 
            GLT::asset::asset_writer& out) = 0;

        // editor conveniences (defaults are no-ops) -------------------------------------------------------------------

        // @brief Round-trips a runtime asset back into the source format
        //
        // Used by the editor for "Export as .fbx" and for lossless diff tooling Default implementation refuses with
        // [import_error::not_supported]
        //
        // @param out       Path the factory should write to
        // @param src_type  Type of the asset being exported
        // @param in        Chunk reader over the asset's on-disk chunks
        // @return Success, or an import_error
        [[nodiscard]] virtual std::expected<void, GLT::asset::import_error> export_to_source(const std::filesystem::path& /*out*/,
            GLT::asset::type /*src_type*/, chunk_reader& /*in*/) { 
                
            return std::unexpected{ import_error::not_supported };
        }


        // @brief Schema entry describing one import-time option
        //
        // The editor renders these generically into its import panel: booleans as checkboxes, integers / reals as drags,
        // enumerations as combo boxes built from [enumeration_values] (pipe-separated)
        //
        // The registry serializes user choices into [import_options::type_specific] in the same order the factory returns
        // them from [option_schema()]; factories are expected to parse that blob back with a matching layout
        struct option_descriptor {

            std::string_view                    key;
            std::string_view                    label;
            enum class kind : u8 {

                boolean, 
                integer, 
                real, 
                enumeration, 
                path 
            }                                   type{};
            std::string_view                    default_value{};
            std::string_view                    enumeration_values{};   // "a|b|c" for kind::enumeration
            std::string_view                    tooltip{};
        };

        // @brief Returns the import-option schema for one target type
        //
        // Default: no options. Order is significant - it defines the wire layout the factory will read back out of
        // [import_options::type_specific]
        //
        // @param target_type  The target type whose schema is being queried
        // @return Span over the factory's static schema table, or empty
        [[nodiscard]] virtual std::span<const option_descriptor> option_schema(const GLT::asset::type& /*target_type*/) const noexcept { return {}; }

    };

}
