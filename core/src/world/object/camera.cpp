
#include "util/pch.h"
#include "camera.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    camera::camera(glm::vec3 position, glm::quat rotation, f32 fov, f32 aspect_ratio, f32 near_plane, f32 far_plane)
        : m_position(position), m_rotation(rotation), m_fov(fov), m_aspect_ratio(aspect_ratio), m_near_plane(near_plane), m_far_plane(far_plane) {}

    // CLASS PUBLIC ====================================================================================================

    glm::mat4 camera::get_view_matrix() {

        glm::mat4 view_matrix = glm::mat4_cast(m_rotation);
        view_matrix = glm::translate(view_matrix, -m_position);
        return view_matrix;
    }


    glm::mat4 camera::get_projection_matrix() {

        auto projection_matrix = glm::perspective(glm::radians(m_fov), m_aspect_ratio, m_near_plane, m_far_plane);
        projection_matrix[1][1] *= -1; // Flip Y axis, now it's y-up
        return projection_matrix;
    }


    void camera::move(const glm::vec3 distance) {
        
        move_forward(distance.x);
        move_right(distance.y);
        move_up(distance.z);
    }


    void camera::move_forward(const f32 distance)       { m_position += m_front * distance * m_speed; }


    void camera::move_right(const f32 distance)         { m_position += m_right * distance * m_speed; }


    void camera::move_up(const f32 distance)            { m_position += m_up * distance * m_speed; }


    void camera::rotate(const f32 pitch, const f32 yaw, const f32 roll) {

        glm::quat pitchQuat = glm::angleAxis(glm::radians(pitch * m_sensitivity), glm::vec3(1, 0, 0));
        glm::quat yawQuat = glm::angleAxis(glm::radians(yaw * m_sensitivity), glm::vec3(0, 1, 0));
        glm::quat rollQuat = glm::angleAxis(glm::radians(roll * m_roll_sensitivity), glm::vec3(0, 0, -1));

        m_rotation = pitchQuat * m_rotation * yawQuat * rollQuat;
        m_front = glm::inverse(m_rotation) * glm::vec3(0, 0, -1);
        m_right = glm::inverse(m_rotation) * glm::vec3(1, 0, 0);
        m_up = glm::cross(m_right, m_front);
    }


	void camera::rotate(const glm::vec3 rotation)       { rotate(rotation.x, rotation.y, rotation.z); }


    void camera::rotate(const glm::quat& rotation) {

        m_rotation = rotation * m_rotation;
        update_directions();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void camera::update_directions() {

        m_front = glm::normalize(glm::inverse(m_rotation) * glm::vec3(0, 0, -1));
        m_right = glm::normalize(glm::inverse(m_rotation) * glm::vec3(1, 0, 0));
        m_up = glm::cross(m_right, m_front);
    }

}
