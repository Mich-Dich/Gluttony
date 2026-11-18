#include "pack/plant_self_test.h"
#include "pack/plant_pack.h"

#include <procedural/construct/core_pack.h>
#include <procedural/construct/serialize.h>



namespace GLT::construct::plant {

    // FUNCTION IMPLEMENTATION =========================================================================================

    void run_self_test(std::vector<std::string>& failures, u32& passed) {

        auto check = [&](bool ok, std::string what) {
            if (ok) ++passed;
            else failures.push_back("plant: " + std::move(what));
        };

        registry reg;
        register_core_pack(reg, "plant_test");
        register_pack(reg, "plant_test");
        const auto snap = reg.snapshot();

        auto first_problem = [](const evaluation_result& r) {
            for (const diagnostic& d : r.diagnostics)
                if (d.level != severity::info)
                    return std::format(" ([{}] {}: {})", d.source, d.element.to_string(), d.message);
            return std::string();
        };
        auto stage_of = [](const evaluation_result& r, u32 id) -> const stage_stats* {
            for (const stage_stats& s : r.stages)
                if (s.id == id)
                    return &s;
            return nullptr;
        };

        // ---- presets ----------------------------------------------------------------------------------------------------
        for (const preset_desc& p : presets()) {
            auto data = std::make_shared<construct_data>(p.make());
            const evaluation_result r = evaluate(*snap, { .data = data, .seed = 5 });
            const output_result* bark = r.output("bark");
            const output_result* leaves = r.output("leaves");
            check(r.ok(), std::format("preset [{}] evaluates without errors{}", p.display, first_problem(r)));
            check(bark && bark->triangle_count() > 300 && leaves && leaves->triangle_count() > 100,
                  std::format("preset [{}] has bark and leaves", p.display));
            check(std::ranges::all_of(r.stages, [](const stage_stats& s) { return s.ran && s.elements > 0; }),
                  std::format("preset [{}]: every stage made something", p.display));
        }

        // ---- the default tree -------------------------------------------------------------------------------------------
        auto data = std::make_shared<construct_data>(make_default_tree());
        const evaluation_result r = evaluate(*snap, { .data = data, .seed = 5, .editor_data = true });
        const stage_stats* trunk = stage_of(r, 1);
        const stage_stats* branches = stage_of(r, 2);
        check(trunk && trunk->elements == 1, "one trunk");
        check(branches && branches->elements > 3 && branches->elements <= 9, "branches: at most Count per parent");

        // Every branch starts on its parent (within the parent's radius) and gets thinner toward the tip
        bool attached = true;
        for (const element_summary& e : r.elements) {
            if (e.kind != kind_branch() || e.stage != 2 || e.skeleton.size() < 2)
                continue;
            auto parent = std::ranges::find(r.elements, e.parent, &element_summary::path);
            if (parent == r.elements.end() || parent->skeleton.empty()) {
                attached = false;
                continue;
            }
            f32 best = 1e9f;
            for (size_t i = 0; i + 1 < parent->skeleton.size(); ++i) {
                const glm::vec3 a = parent->skeleton[i], b = parent->skeleton[i + 1];
                const f32 t = std::clamp(glm::dot(e.skeleton.front() - a, b - a) / std::max(glm::dot(b - a, b - a), 1e-9f), 0.0f, 1.0f);
                best = std::min(best, glm::length(e.skeleton.front() - (a + (b - a) * t)));
            }
            attached = attached && best < 0.3f;
        }
        check(attached, "branches start on their parent");
        check(std::ranges::any_of(r.relations, [](const relation_summary& x) { return x.kind == intern("plant.fork"); }),
              "forks are reported as relations (editor data)");

        // The registry gets the editor declarations
        {
            const auto* role = snap->find_role(role_branch());
            check(role && !role->point_attributes.empty(), "the branch role lists its per point attributes");
            check(snap->presets.size() == 4 && snap->guide_templates.size() == 2, "presets and guide templates are registered");
            const pack_desc* pd = snap->find_pack(PACK_NAME);
            check(pd && pd->upgrade && pd->self_test && !pd->legacy_asset_types.empty(), "upgrade, self test and legacy asset type are registered");
            const auto* branches_type = snap->find_generator(STAGE_BRANCHES);
            generator_instance twig{ .id = 9, .type = std::string(STAGE_BRANCHES), .label = "x", .enabled = true, .inputs = { 2 } };
            if (branches_type && branches_type->desc->on_add)
                branches_type->desc->on_add(twig, *data);
            check(twig.label == "Twigs" && twig.id == 9 && twig.inputs == std::vector<u32>{ 2 }, "a Branches stage added on Branches gets twig defaults");
        }

        // Seed: trunk and branches change with it, the same seed repeats
        const evaluation_result again = evaluate(*snap, { .data = data, .seed = 5 });
        const evaluation_result other = evaluate(*snap, { .data = data, .seed = 6 });
        check(again.output("bark") && r.output("bark") && again.output("bark")->hash() == r.output("bark")->hash(), "the same seed grows the same tree");
        check(other.output("bark") && r.output("bark") && other.output("bark")->hash() != r.output("bark")->hash(),
              "another seed changes trunk and branches");
        {
            auto trunk_only = std::make_shared<construct_data>(*data);
            trunk_only->generators.resize(1);
            const auto a = evaluate(*snap, { .data = trunk_only, .seed = 5 });
            const auto b = evaluate(*snap, { .data = trunk_only, .seed = 6 });
            check(a.output("bark") && b.output("bark") && a.output("bark")->hash() != b.output("bark")->hash(), "the seed changes the trunk itself");
            auto reseeded = std::make_shared<construct_data>(*trunk_only);
            reseeded->generators[0].seed = 1;
            const auto c = evaluate(*snap, { .data = reseeded, .seed = 5 });
            check(c.output("bark") && a.output("bark") && c.output("bark")->hash() != a.output("bark")->hash(), "a stage's own seed re-rolls it");
        }

        // Disabling the leaves stage removes the leaves; hiding the branches stage removes what grows on it
        {
            auto no_leaves = std::make_shared<construct_data>(*data);
            no_leaves->generators[3].enabled = false;
            const auto a = evaluate(*snap, { .data = no_leaves, .seed = 5 });
            check(!a.output("leaves") || a.output("leaves")->triangle_count() == 0, "a disabled leaves stage makes no leaves");
            auto no_branches = std::make_shared<construct_data>(*data);
            no_branches->generators[1].enabled = false;
            const auto b = evaluate(*snap, { .data = no_branches, .seed = 5 });
            const stage_stats* twigs = stage_of(b, 3);
            check(twigs && twigs->elements == 0, "stages growing on a disabled stage are empty");
        }

        // Gravity: a strongly drooping branch ends below where it starts
        {
            auto droop = std::make_shared<construct_data>(*data);
            droop->generators.resize(2);
            droop->generators[1].set("gravity", 2.5f);
            droop->generators[1].set("light_seeking", 0.0f);
            droop->generators[1].set("angle", 90.0f);
            droop->generators[1].set("angle_tip", 90.0f);
            const auto a = evaluate(*snap, { .data = droop, .seed = 5, .editor_data = true });
            bool down = false;
            for (const element_summary& e : a.elements)
                if (e.stage == 2 && e.skeleton.size() >= 2)
                    down = down || e.skeleton.back().y < e.skeleton.front().y;
            check(down, "gravity bends branches down");
        }

        // Count per parent / density per metre
        {
            auto counted = std::make_shared<construct_data>(*data);
            counted->generators.resize(2);
            counted->generators[1].set("count", 4);
            counted->generators[1].set("density_profile", profile_curve::constant(1.0f));
            counted->generators[1].set("length_jitter", 0.0f);
            const auto a = evaluate(*snap, { .data = counted, .seed = 9 });
            const stage_stats* s = stage_of(a, 2);
            check(s && s->elements == 4, "branches: Count per parent");
        }

        // Hand drawn: guides become branches; a branch starting near another forks from it
        {
            auto drawn = std::make_shared<construct_data>(presets()[3].make());
            const auto a = evaluate(*snap, { .data = drawn, .seed = 1, .editor_data = true });
            u32 drawn_count = 0;
            for (const element_summary& e : a.elements)
                drawn_count += e.stage == 1 && e.kind == kind_branch() ? 1u : 0u;
            check(drawn_count == 4, "drawn branches: one branch per spline");
            const stage_stats* planes = stage_of(a, 4);
            check(planes && planes->products > 0, "leaf planes make leaves");
        }

        // Upgrading an old construct (guides only)
        {
            construct_data old{};
            old.packs = { std::string(PACK_NAME) };
            guide g{};
            g.name = "trunk";
            g.role = role_branch();
            g.shape = curve_guide{ .type = geometry::curve_type::catmull_rom, .points = { { 0, 0, 0 }, { 0, 2, 0 } } };
            old.guides.push_back(g);
            check(upgrade(old) && old.generators.size() == 3 && old.generators[0].type == STAGE_DRAWN, "old constructs get stages");
            check(!upgrade(old), "upgrade runs once");
        }

        // Round trip through the asset format
        {
            const auto restored = deserialize(serialize(*data));
            check(restored.has_value(), "the tree serializes");
            if (restored) {
                const auto a = evaluate(*snap, { .data = std::make_shared<construct_data>(*restored), .seed = 5 });
                check(a.output("bark") && r.output("bark") && a.output("bark")->hash() == r.output("bark")->hash(), "a loaded tree grows the same");
            }
        }
    }

}
