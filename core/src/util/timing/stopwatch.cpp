
#include "util/pch.h"
#include "stopwatch.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    stopwatch::stopwatch() {};


    stopwatch::stopwatch(time_unit precision)
        : m_precision(precision) { start(); }


    stopwatch::stopwatch(f32 *result_pointer, time_unit precision)
        : m_result_pointer(result_pointer), m_precision(precision) { start(); }


    stopwatch::~stopwatch() { stop(); }

    // CLASS PUBLIC ====================================================================================================

    f32 stopwatch::stop() {

        std::chrono::system_clock::time_point end_point = std::chrono::system_clock::now();
        f32 buffer{};
        switch (m_precision) {
            case time_unit::microseconds:
                buffer = std::chrono::duration_cast<std::chrono::nanoseconds>(end_point - m_start_point).count() / 1000.f;
                break;

            case time_unit::seconds:
                buffer = std::chrono::duration_cast<std::chrono::milliseconds>(end_point - m_start_point).count() / 1000.f;
                break;

            default:
            case time_unit::milliseconds:
                buffer = std::chrono::duration_cast<std::chrono::microseconds>(end_point - m_start_point).count() / 1000.f;
                break;
        }

        if (m_result_pointer)
            *m_result_pointer = buffer;

        return buffer;
    }


    void stopwatch::restart() { start(); }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void stopwatch::start() { m_start_point = std::chrono::system_clock::now(); }

}
