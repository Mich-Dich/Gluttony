#pragma once

#include "util/macros.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// TEMPLATE DECLARATION ============================================================================================

	// CLASS DECLARATION ===============================================================================================

	// @brief A Universally Unique Identifier class that wraps a 64-bit unsigned integer.
	//        Generates random UUIDs using a Mersenne Twister engine and uniform distribution.
	//        Can be used as a key in hash-based containers and supports experimental collision avoidance.
	class UUID {
	public:

		// @brief Constructs a UUID with a randomly generated 64-bit value.
		//        Uses a Mersenne Twister engine with uniform distribution for generation.
		//        When experimental collision avoidance is enabled, ensures uniqueness among all generated UUIDs.
		UUID();

		// @brief Constructs a UUID with the specified 64-bit value.
		// @param [uuid] The 64-bit unsigned integer value to use for this UUID.
		UUID(u64 uuid);

		// @brief Default copy constructor for UUID objects.
		DEFAULT_COPY_CONSTRUCTOR(UUID);

		// @brief Destroys the UUID object. When experimental collision avoidance is enabled, removes the UUID from the tracking set.
		~UUID();

		// @brief Conversion operator that allows UUID to be used as a 64-bit unsigned integer.
		//        Enables implicit conversion in arithmetic operations and comparisons.
		// @return The underlying 64-bit unsigned integer value of the UUID.
		operator u64() const { return m_UUID; }

	private:

		u64 				m_UUID;		// The underlying 64-bit unsigned integer that stores the unique identifier
	};

}


namespace std {

	// @brief Hash function template declaration for use in unordered containers.
	template <typename T> struct hash;

	// @brief Specialization of the hash template for GLT::UUID objects.
	//        Allows UUID objects to be used as keys in unordered containers like std::unordered_map.
	//        The hash value is simply the 64-bit integer value of the UUID.
	template<>
	struct hash<GLT::UUID> 		{ std::size_t operator()(const GLT::UUID& uuid) const { return (u64)uuid; } };

}
