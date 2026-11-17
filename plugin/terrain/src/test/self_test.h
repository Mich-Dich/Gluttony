
#pragma once

#include <terrain/node.h>



namespace GLT::terrain {

    struct self_test_report {

        u32                                             passed = 0;
        u32                                             failed = 0;
        std::vector<std::string>                        failures{};
        f64                                             milliseconds = 0.0;
    };

    // T1 acceptance test, runnable from the editor (Tools > Terrain > Run self test) or any code:
    //  - seams: for every registered node type (all outputs), four quadrant evaluations equal the same samples of one
    //    evaluation of the whole region, bit for bit (inputs fed by distinct noise)
    //  - CHUNK_GRAPH round trip
    //  - expression compiler: results of a few formulas, rejection of malformed ones
    [[nodiscard]] self_test_report run_self_test(const i_node_registry& registry);

}
