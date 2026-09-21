
#pragma once

#include <event/event.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class file_event_type : std::uint8_t {
        created,
        modified,
        deleted,
        moved_from,
        moved_to,
    };


    [[nodiscard]] const char* to_string(file_event_type type) noexcept;


    class file_event : public GLT::event {
    public:

        file_event(file_event_type type, std::filesystem::path path)
            : m_type(type), m_path(std::move(path)) {}

        DEFAULT_GETTER_CC(file_event_type, type)
        DEFAULT_GETTER_CC(std::filesystem::path, path)

        [[nodiscard]] std::string to_string() const override {
            return std::format("file_event [{}] at [{}]", GLT::util::enum_to_string(m_type), m_path.generic_string());
        }

    private:

        file_event_type                             m_type;
        std::filesystem::path                       m_path;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
