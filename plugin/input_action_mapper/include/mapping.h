
#pragma once

#include "action.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::input_action_mapper {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

	// Manages a collection of input actions and handles their serialization to/from configuration files.
	// Provides centralized input action registration and retrieval.
    class mapping {
    public:

		// Constructs an empty input mapping collection.
		mapping(const std::string& name);


		// Default destructor.
		~mapping() = default;


		DELETE_COPY_CONSTRUCTOR(mapping);


		DEFAULT_GETTER_REF(std::vector<action>, 	actions)


		// Gets the number of registered input actions.
		// @return The count of registered actions.
		size_t get_length() const { return m_actions.size(); }


		// Gets a specific input action by index.
		// @param index The zero-based index of the action to retrieve.
		// @return Pointer to the input action, or nullptr if index is invalid.
		action& get_action(u32 index) { return m_actions[index]; }


		// Registers an input action and loads its configuration from file if available.
		// @param action Pointer to the input action to register.
		// @param force_override Whether to overwrite existing configuration with default values.
		// @param path The filesystem path to the configuration file.
		void register_action(action&& action);


		// Gets an iterator to the beginning of the input actions collection.
		// @return Iterator pointing to the first input action.
		std::vector<action>::iterator begin() { return m_actions.begin(); }

		
		// Gets an iterator to the end of the input actions collection.
		// @return Iterator pointing to the position after the last input action.
		std::vector<action>::iterator end() { return m_actions.end(); }


		void update();
		
    private:
    
		std::vector<action> 						m_actions{};	// Collection of registered input actions
		std::string									m_name{};		// the name of the config file

    };
    
}
