
#include "util/pch.h"
#include "util/system.h"
#include "interval_controller.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    interval_controller::interval_controller(std::chrono::microseconds target_interval)
        : m_target_interval_duration(target_interval), m_last_timestamp(std::chrono::high_resolution_clock::now()) {
    }


    interval_controller::~interval_controller() {
        // Destructor - nothing special needed
    }

    // CLASS PUBLIC ====================================================================================================

    f32 interval_controller::limit() {

        auto now = std::chrono::high_resolution_clock::now();
        auto target_time = m_last_timestamp + m_target_interval_duration;
        if (target_time > now) {                    // If the next target time is in the future, sleep until then

            m_delta_time = std::chrono::duration_cast<std::chrono::microseconds>(target_time - m_last_timestamp).count() / 1000.f;
            m_total_time += m_delta_time;
            m_last_timestamp = target_time;
            precision_sleep_until(target_time);

        } else {

            m_delta_time = std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_timestamp).count() / 1000.f;
            m_total_time += m_delta_time;
            m_last_timestamp = now;
        }
        return m_delta_time;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
