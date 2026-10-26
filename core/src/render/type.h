
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Renderer capability flags (optional, can be used for feature queries)
    enum class renderer_feature : u8 {

        none                                    = 0,
        compute_shaders                         = BIT(0),
        tessellation                            = BIT(1),
        ray_tracing                             = BIT(2),
        bindless_resources                      = BIT(3),
    };


    enum class backend_api : u8 {

        vulkan = 0,
        open_gl,
        direct_x,
        metal,
    };


    // driven by the scene's directional light
    struct sun_settings {

        glm::vec3                               direction = { 0.5f, 1.0f, 0.3f };       // pointing TOWARD the sun
        glm::vec3                               color = { 1.0f, 0.95f, 0.85f };         // linear
        f32                                     intensity = 3.0f;
        f32                                     angular_radius = 0.035f;                // radians
        u32                                     samples = 4u;                           // shadow rays per hit
        f32                                     shadow_ray_tmax = 10000.0f;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
