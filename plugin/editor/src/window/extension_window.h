
#pragma once

#include <editor/i_editor.h>

#include "window/base/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Editor window around a plugin's i_editor_window: owns Begin / End, docking, the close button and focus tracking,
    // the plugin draws the content. Tagged with the plugin's name so the windows can be closed before it unloads
    class extension_window final : public base_window {
    public:

        extension_window(std::string owner, GLT::unique_ref<GLT::editor::i_editor_window> content);

        ~extension_window() override;


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        [[nodiscard]] const std::string& owner() const noexcept { return m_owner; }


        [[nodiscard]] std::filesystem::path asset_path() const;

    private:

        std::string                                     m_owner{};
        GLT::unique_ref<GLT::editor::i_editor_window>   m_content{};
    };

}
