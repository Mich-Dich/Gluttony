
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // TODO: Class description
    class interval_controller {
    public:

        // Default constructor
        interval_controller(std::chrono::microseconds target_interval = std::chrono::microseconds(16667));
        ~interval_controller();

        DEFAULT_GETTER_C(f32,                               delta_time)
        DEFAULT_GETTER_C(f32,                               total_time)
        DEFAULT_GETTER_SETTER_C(std::chrono::microseconds,  target_interval_duration)

        f32 limit();

    private:

        f32                                                 m_delta_time = 0.f;
        f32                                                 m_total_time = 0.f;
        std::chrono::microseconds                           m_target_interval_duration{};
        std::chrono::high_resolution_clock::time_point      m_last_timestamp{};

    };

}
