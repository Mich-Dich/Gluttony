
#pragma once

#include "asset/type.h"
#include "asset/header.h"
#include "plugin_system/i_asset_handler_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // A single (source extension → target type) pairing the factory claims.
    // `source_extension` is lowercase, no dot. Empty string = "any extension"
    // (useful for formats detected by magic bytes rather than suffix).
    struct binding {

        std::string_view                source_extension{};     // "fbx", "obj", "gltf", "png", "wav", ""
        GLT::asset::type                target_type;            // what it produces
    };


    // Metadata the factory hands back to the registry after a successful write.
    // The registry uses this to update its source→asset index and to decide
    // whether a re-import is actually needed on the next pass.
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

    // Editor / build-pipeline plugin: converts an external format into one of our .glt_* files by writing chunks through 
    // an asset_writer. The registry owns the file header, string table, dependency table and checksums, 
    // the factory just emits the payload.
    //
    // A factory should never link against runtime handlers, and vice versa. The only shared contract is the chunk layout 
    // each side agrees on (via the shared header that defines chunk_id constants, e.g. mesh.h).
    class i_asset_factory_plugin : public GLT::plugin_manager::i_plugin {
    public:

        virtual ~i_asset_factory_plugin() = default;

        // capability declaration --------------------------------------------------------------------------------------

        // All (extension → type) pairs this factory can produce.
        // The registry consults this for routing AND for building the editor's "import as…" menu.
        [[nodiscard]] virtual std::span<const GLT::asset::factory::binding> bindings() const noexcept = 0;


        // does this factory want to see the request even if it doesn't match a binding? Useful for magic-byte sniffers.
        [[nodiscard]] virtual bool can_sniff(const std::filesystem::path& /*source*/) const noexcept { return false; }

        // importing ---------------------------------------------------------------------------------------------------

        // Read `source` and emit chunks for `target_type` through `out`.
        // On success the registry will:
        //   1. finalize the file at `result.output_path`
        //   2. register the asset in the source→asset index
        //   3. optionally call registry.load(result.output_path) to bring it in immediately (editor preview path).
        //
        // On failure, the registry discards whatever `out` accumulated;
        // writers are transactional.
        [[nodiscard]] virtual std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> import(
            const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& opts, 
            GLT::asset::asset_writer& out) = 0;

        // editor conveniences (defaults are no-ops) -------------------------------------------------------------------

        // Some factories can round-trip back to the source format. Used by the editor for "Export as .fbx" and for lossless diff tooling.
        [[nodiscard]] virtual std::expected<void, GLT::asset::import_error> export_to_source(const std::filesystem::path& /*out*/,
            GLT::asset::type /*src_type*/, chunk_reader& /*in*/) { 
                
            return std::unexpected{ import_error::not_supported };
        }


        // Extra knobs the factory wants surfaced in the editor's import panel (e.g. "weld vertices", "flip UVs", "mip chain depth"). 
        // The editor renders these generically from a small schema.
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

        [[nodiscard]] virtual std::span<const option_descriptor> option_schema(const GLT::asset::type& /*target_type*/) const noexcept { return {}; }

    };

}
