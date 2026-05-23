
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

    // @brief Represents a 3D camera with position, rotation, and projection parameters.
    //        Provides view and projection matrix generation, movement, and rotation utilities.
	class camera {
	public:

        // @brief Constructs a camera with specified position, rotation, and projection properties.
        // @param position     Initial world position of the camera.
        // @param rotation     Initial orientation as a quaternion.
        // @param fov          Field of view in degrees.
        // @param aspect_ratio Width/height ratio of the viewport.
        // @param near_plane   Distance to the near clipping plane.
        // @param far_plane    Distance to the far clipping plane.
		camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			f32 fov = 45.0f, f32 aspect_ratio = 16.0f / 9.0f, f32 near_plane = 0.1f, f32 far_plane = 100.0f);


        // @brief Computes the view matrix based on current position and rotation.
        // @return 4x4 view matrix that transforms world space to camera space.
		glm::mat4 get_view_matrix();


        // @brief Computes the perspective projection matrix.
        //        The Y axis is flipped to match a Y‑up coordinate system.
        // @return 4x4 projection matrix.
		glm::mat4 get_projection_matrix();

		
        // @brief Moves the camera by a given vector, multiplying each component by the movement speed.
        // @param distance Translation vector (forward, right, up components).
		void move(const glm::vec3 distance);
		

        // @brief Moves the camera forward/backward along its local front axis.
        // @param distance Signed distance to move (multiplied by speed).
		void move_forward(const f32 distance);
		

        // @brief Moves the camera left/right along its local right axis.
        // @param distance Signed distance to move (multiplied by speed).
		void move_right(const f32 distance);
		

        // @brief Moves the camera up/down along its local up axis.
        // @param distance Signed distance to move (multiplied by speed).
		void move_up(const f32 distance);


        // @brief Applies Euler angle rotation to the camera (pitch, yaw, roll) using sensitivity multipliers.
        // @param pitch Rotation around local X axis (degrees).
        // @param yaw   Rotation around world Y axis (degrees).
        // @param roll  Rotation around local Z axis (degrees, inverted sign).
		void rotate(const f32 pitch, const f32 yaw, const f32 roll);
		

        // @brief Applies rotation from a vector of Euler angles.
        // @param rotation Vector containing pitch, yaw, roll (degrees).
		void rotate(const glm::vec3 rotation);
		

        // @brief Applies an additional rotation via quaternion multiplication.
        // @param rotation Quaternion representing the rotation to add.
		void rotate(const glm::quat& rotation);

	private:

        // @brief Recalculates the front, right, and up direction vectors from the current rotation.
        //        Automatically called by rotate methods, but can be invoked manually if needed.
		// When the camera is rotated, the front, right and up vectors need to be recalculated
		// Done automatically by the Rotate function, but can be done manually if needed
		void update_directions();


		glm::vec3 			m_position;      				// Camera position
		glm::quat 			m_rotation;      				// Camera rotation
		f32 				m_fov;         					// Field of view (in degrees)
		f32 				m_aspect_ratio; 				// Aspect ratio of the viewport
		f32 				m_near_plane;   				// Near clipping plane
		f32 				m_far_plane;    				// Far clipping plane

		f32 				m_sensitivity = 75000.0f; 		// Pitch and Yaw sensitivity
		f32 				m_roll_sensitivity = 100.0f; 	// Roll sensitivity
		f32 				m_speed = 25.0f;       			// Camera movement speed

		glm::vec3 			m_front = glm::vec3(0.0, 0.0, -1.0);
		glm::vec3 			m_right = glm::vec3(1.0, 0.0, 0.0);
		glm::vec3 			m_up = glm::vec3(0.0, 1.0, 0.0);

	};

}
