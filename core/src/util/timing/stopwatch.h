#pragma once

#include "util/macros.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::util
{

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief This is a lightest stopwatch that is automatically started when creating an instance.
    //        It can either store the elapsed time in a provided float pointer when the stopwatch is stopped/destroyed,
    //        or it can allow retrieval of the elapsed time by manually calling [stop()] method.
    //        The time is measured in milliseconds.
    class stopwatch
    {
    public:
        stopwatch();

        stopwatch(time_unit precision);

        stopwatch(f32 *result_pointer, time_unit precision = time_unit::milliseconds);

        ~stopwatch();

        GETTER_C(f32, result, *m_result_pointer);

        // @brief Stops the stopwatch and calculates the elapsed time.
        //        If a result pointer was provided, it will be updated with the elapsed time.
        // @return The elapsed time in milliseconds since the stopwatch was started.
        f32 stop();

        // @brief Restarts the stopped stopwatch
        void restart();

    private:
    
        void start();

        f32 *m_result_pointer = &m_result;
        time_unit m_precision = time_unit::milliseconds;
        std::chrono::system_clock::time_point m_start_point{};
        f32 m_result = 0.f;
    };

}
