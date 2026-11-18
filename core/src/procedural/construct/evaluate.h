#pragma once

#include "procedural/construct/registry.h"

// Procedural construction: how a construct (construct_data, data.h) is evaluated.



namespace GLT::construct {

    // TYPES ===========================================================================================================

    struct evaluation_request {

        std::shared_ptr<const construct_data>           data{};
        u64                                             seed = 0;
        std::vector<exposed_parameter>                  parameters{};       // override construct_data::parameters
        std::vector<std::string>                        outputs{};          // only these outputs (empty: all)
        lod_request                                     lod{};
        const std::atomic<bool>*                        cancel = nullptr;
        // Editors: keep every product and per element detail (attributes, skeleton, relations) in the result. Costs memory
        bool                                            editor_data = false;
        // Stop after this pipeline stage ("core.interpret", "core.generate", "core.expand", "core.realize", "core.finish";
        // empty: run all). The later, more expensive stages do not run: for looking at intermediate results
        std::string                                     stop_after{};
    };


    // @brief A relation between two elements (editor_data only)
    struct relation_summary {

        relation_id                                     kind{};
        element_path                                    a{}, b{};
    };


    struct stage_timing { std::string stage{}; f64 milliseconds = 0.0; };

    struct element_summary {

        element_path                                    path{};
        kind_id                                         kind{};
        origin_kind                                     origin = origin_kind::derived;
        std::string                                     source{};
        u32                                             depth = 0;
        u32                                             stage = 0;
        // editor_data only ----------------------------------------------------------------------------------------------
        element_path                                    parent{};
        role_id                                         role{};
        shape_kind                                      shape = shape_kind::none;
        attribute_set                                   attributes{};
        std::vector<glm::vec3>                          skeleton{};         // curves: centre line, points: the points, others: origin. Output space
    };


    // @brief What one stage did in one evaluation
    struct stage_stats {

        u32                                             id = 0;
        std::string                                     label{};
        std::string                                     type{};
        bool                                            ran = false;        // false: disabled, type missing or failed
        u32                                             elements = 0;       // emitted by the stage (and derived from them)
        u32                                             products = 0;
        u32                                             triangles = 0;
        f64                                             milliseconds = 0.0; // the stage's own run (not realization)
    };


    struct evaluation_result {

        std::vector<output_result>                      outputs{};
        std::vector<diagnostic>                         diagnostics{};
        std::vector<element_summary>                    elements{};         // every element, in path order
        std::vector<stage_timing>                       timings{};
        std::vector<std::string>                        stage_order{};
        std::vector<stage_stats>                        stages{};           // one per construct stage, in order
        std::vector<product>                            products{};         // editor_data only
        std::vector<relation_summary>                   relations{};        // editor_data only (core.child_of left out)
        std::string                                     stopped_after{};    // the stage the evaluation stopped after (empty: ran all)
        u64                                             registry_generation = 0;
        bool                                            cancelled = false;

        [[nodiscard]] bool ok() const noexcept;                             // no error diagnostics, not cancelled
        [[nodiscard]] u64 hash() const noexcept;                            // of the outputs (determinism checks)
        [[nodiscard]] const output_result* output(std::string_view name) const noexcept;
    };

    // FUNCTION DECLARATION ============================================================================================

    // Evaluates on the calling thread. Pure: same snapshot + request -> identical result
    [[nodiscard]] evaluation_result evaluate(const registry_snapshot& snapshot, const evaluation_request& request);

}
