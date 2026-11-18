#pragma once

#include "procedural/construct/generator.h"

// Procedural construction: the authored data of a construct (what construct assets store).



namespace GLT::construct {

    // TYPES ===========================================================================================================

    // @brief Per element override, keyed by path (editor: pin / hide / change attributes of one element)
    struct element_override {

        element_path                                    path{};
        bool                                            hidden = false;
        attribute_set                                   attributes{};       // set on the element when it is committed
    };


    struct exposed_parameter {

        std::string                                     name{};
        attribute_value                                 value{};
    };


    // @brief The authored data of a construct (what the .glt_construct asset stores)
    struct construct_data {

        std::vector<std::string>                        packs{};            // active packs in order (empty: every loaded pack). "core" is always active
        std::vector<guide>                              guides{};
        std::vector<generator_instance>                 generators{};       // stages, run in this order (core.generate)
        std::vector<exposed_parameter>                  parameters{};
        std::vector<element_override>                   overrides{};
        std::vector<policy_binding>                     policies{};         // construct-level policy choices
        std::vector<std::pair<std::string, composition>> compositions{};    // hook -> composition for this construct
        glm::dvec3                                      origin{ 0.0 };      // outputs are expressed relative to this
    };

}
