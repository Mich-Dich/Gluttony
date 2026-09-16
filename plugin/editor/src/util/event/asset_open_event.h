
#pragma once

#include <event/event.h>



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

        asset_open_event(const asset_category category, const std::filesystem::path& path) 
            : m_asset_category(category), m_path(path) {}

        DEFAULT_GETTER_CC(asset_category,              asset_category)
        DEFAULT_GETTER_CC(std::filesystem::path,       path)

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("asset open event [{}] at [{}]", GLT::util::enum_to_string(m_asset_category), m_path);
        }

    private:

        asset_category                              m_asset_category{};
        std::filesystem::path                       m_path{};

    };

}
