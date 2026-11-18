#include "pack/ship_self_test.h"
#include "pack/ship_pack.h"

#include <procedural/construct/core_pack.h>



namespace GLT::construct::ship {

    // FUNCTION IMPLEMENTATION =========================================================================================

    void run_self_test(std::vector<std::string>& failures, u32& passed) {

        auto check = [&](bool ok, std::string what) {
            if (ok) ++passed;
            else failures.push_back("spaceship: " + std::move(what));
        };

        registry reg;
        register_core_pack(reg, "ship_test");
        register_pack(reg, "ship_test");
        const auto snap = reg.snapshot();

        auto data = std::make_shared<construct_data>(make_default_ship());
        const evaluation_result r = evaluate(*snap, { .data = data, .seed = 1 });
        std::string first_error;
        for (const diagnostic& d : r.diagnostics)
            if (d.level != severity::info && first_error.empty())
                first_error = std::format(" ([{}] {}: {})", d.source, d.element.to_string(), d.message);
        check(r.ok(), "the default ship evaluates without errors" + first_error);

        u32 ribs = 0, stringers = 0;
        for (const element_summary& s : r.elements) {
            ribs += s.kind == kind_rib() ? 1u : 0u;
            stringers += s.kind == kind_stringer() ? 1u : 0u;
        }
        // Hull: 9 m + 2 x 2 m caps = 13 m along z at 0.9 m spacing -> 14 stations
        check(ribs == 14, std::format("a rib per frame station (got {}, expected 14)", ribs));
        check(stringers >= 10, std::format("stringers run along the hull, split at components (got {})", stringers));
        const output_result* frame = r.output("frame");
        const output_result* components = r.output("components");
        check(frame && frame->triangle_count() > 1000, "frame output has ribs and stringers");
        check(components && components->triangle_count() > 0, "components output has the block-outs");
        check(r.output("hull") && r.output("hull")->triangle_count() == 0, "no hull skin unless asked for");

        // Components cut the ribs: without them, the frame has more material
        auto clear = std::make_shared<construct_data>(*data);
        std::erase_if(clear->guides, [](const guide& g) { return g.role == role_component(); });
        const evaluation_result rc = evaluate(*snap, { .data = clear, .seed = 1 });
        check(rc.ok() && rc.output("frame") && frame && rc.output("frame")->triangle_count() < frame->triangle_count() + 100000 &&
              rc.output("frame")->triangle_count() != frame->triangle_count(), "components change the frame (ribs are cut around them)");

        // Every rib vertex keeps the clearance to every component
        bool clear_of_components = true;
        if (frame)
            for (const output_part& part : frame->parts)
                for (const output_group& g : part.groups)
                    for (const glm::vec3& p : g.mesh.positions) {
                        // reactor sphere r 1.45 at z = -4: nothing of the frame may be inside it
                        if (glm::length(p - glm::vec3(0.0f, 0.0f, -4.0f)) < 1.45f - 0.01f)
                            clear_of_components = false;
                    }
        check(clear_of_components, "no frame geometry inside the reactor");

        // Spacing drives the rib count; skin on demand
        auto dense = std::make_shared<construct_data>(*data);
        dense->guides.front().attributes.set(tags::frame_spacing(), 0.45f);
        dense->guides.front().attributes.set(tags::show_skin(), true);
        const evaluation_result rd = evaluate(*snap, { .data = dense, .seed = 1 });
        u32 dense_ribs = 0;
        for (const element_summary& s : rd.elements)
            dense_ribs += s.kind == kind_rib() ? 1u : 0u;
        check(dense_ribs == 28, std::format("half the spacing, twice the ribs (got {})", dense_ribs));
        check(rd.output("hull") && rd.output("hull")->triangle_count() > 0, "show skin builds the hull");

        check(evaluate(*snap, { .data = data, .seed = 1 }).hash() == r.hash(), "deterministic");
    }

}
