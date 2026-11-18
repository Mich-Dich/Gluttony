#pragma once

#include <procedural/construct/types.h>

// Checks of the spaceship pack (engine-independent): run from the Spaceship editor (Tools > Run self test)



namespace GLT::construct::ship {

    // Appends a message per failed check, counts the passed ones
    void run_self_test(std::vector<std::string>& failures, u32& passed);

}
