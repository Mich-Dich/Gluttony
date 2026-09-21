
#pragma once

#include <event/event.h>
#include <asset/type.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class asset_category : u8 {

        image = 0,  // raster textures and photos
        world,      // scenes / levels
        source,     // code and shaders
        material,   // materials and material instances
        mesh,       // 3D geometry
        config,     // json / yaml / toml / etc.
        audio,      // sound files
        other,      // anything we don't recognize
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class asset_open_event : public event {
    public:

        asset_open_event(const GLT::asset::type asset_type, const std::filesystem::path& path) 
            : m_asset_type(asset_type), m_path(path) {}

        DEFAULT_GETTER_CC(GLT::asset::type,                         asset_type)
        DEFAULT_GETTER_CC(std::filesystem::path,                    path)

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("asset open event at [{}]", m_path.generic_string());
        }

    private:

        GLT::asset::type                                            m_asset_type{};
        std::filesystem::path                                       m_path{};

    };


    class asset_import_request_event : public event {
    public:

        asset_import_request_event(const std::vector<std::filesystem::path> sources, const std::filesystem::path& target_dir) 
            : m_sources(sources), m_target_dir(target_dir) {}

        DEFAULT_GETTER_CC(std::vector<std::filesystem::path>,       sources)
        DEFAULT_GETTER_CC(std::filesystem::path,                    target_dir)

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("asset import event for [{}] assets at [{}]", m_sources.size(), m_target_dir.generic_string());
        }

    private:

        std::vector<std::filesystem::path>                          m_sources{};
        std::filesystem::path                                       m_target_dir{};

    };
    
}
