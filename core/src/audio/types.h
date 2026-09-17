
#pragma once

#include <glm/glm.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::audio {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using audio_handle = u32;


    enum class attenuation_model : u8 {

        none = 0,
        inverse_distance,
        linear_distance,
        exponential_distance,
    };


    enum class audio_state : u8 {

        stopped = 0,
        playing,
        paused,
    };


    struct audio_source_config {

        f32                             volume = 1.0f;
        f32                             pan = 0.0f;         // -1 = left, 1 = right (2D)
        bool                            loop = false;
        f32                             play_speed = 1.0f;
        bool                            is_3d = false;

        // 3D parameters (used when is_3d is true)
        glm::vec3                       position = { 0.0f, 0.0f, 0.0f };
        glm::vec3                       velocity = { 0.0f, 0.0f, 0.0f };
        f32                             min_distance = 1.0f;
        f32                             max_distance = 100.0f;
        f32                             rolloff_factor = 1.0f;
        attenuation_model               attenuation = attenuation_model::inverse_distance;
    };


    struct listener_config {

        glm::vec3                       position = { 0.0f, 0.0f, 0.0f };
        glm::vec3                       forward = { 0.0f, 0.0f, -1.0f };
        glm::vec3                       up = { 0.0f, 1.0f, 0.0f };
        glm::vec3                       velocity = { 0.0f, 0.0f, 0.0f };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
