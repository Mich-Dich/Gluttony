#pragma once

#include "procedural/construct/evaluate.h"

// Construct self test. Engine-independent: runs from every construct tool window (Tools > Run self test) and from the standalone
// test executable (target procedural_construct_tests).



namespace GLT::construct {

    struct self_test_report {

        u32                                             passed = 0;
        u32                                             failed = 0;
        std::vector<std::string>                        failures{};
        f64                                             milliseconds = 0.0;
    };

    [[nodiscard]] self_test_report run_self_test();

}
