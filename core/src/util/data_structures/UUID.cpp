
#include "util/pch.h"

#include "UUID.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	// #define USE_EXPERIMENTAL_COLLISION_AVOIDANCE			// TODO: enable once mature & needed

	// TYPES ===========================================================================================================

	// STATIC VARIABLES ================================================================================================

	static std::random_device 						s_random_device;
	static std::mt19937_64 							s_engine(s_random_device());
	static std::uniform_int_distribution<u64> 		s_uniform_distribution;

	#ifdef USE_EXPERIMENTAL_COLLISION_AVOIDANCE

		static std::unordered_set<u64> 				s_generated_UUIDs;		// To track generated UUIDs and avoid duplicates
		FORCE_INLINE static bool is_unique(u64 uuid) { return s_generated_UUIDs.find(uuid) == s_generated_UUIDs.end(); }

	#endif

	// FUNCTION IMPLEMENTATION =========================================================================================

	// CLASS IMPLEMENTATION ============================================================================================

	UUID::UUID() {

		#ifdef USE_EXPERIMENTAL_COLLISION_AVOIDANCE
			do {
				m_UUID = s_uniform_distribution(s_engine);
			} while (!is_unique(m_UUID));
			s_generated_UUIDs.insert(m_UUID);

		#else

			m_UUID = s_uniform_distribution(s_engine);

		#endif
	}


	UUID::UUID(u64 uuid)
		: m_UUID(uuid) { }

	// CLASS PUBLIC ====================================================================================================

	// CLASS PROTECTED =================================================================================================

	// CLASS PRIVATE ===================================================================================================

}
