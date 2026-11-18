#include "pack/ship_pack.h"
#include "pack/ship_self_test.h"

#include <procedural/construct/reflect.h>



namespace GLT::construct::ship {

    // TYPES ===========================================================================================================

    namespace pack {

        struct [[=tag_schema{ text("ship") }]] ship_tags {

            [[=attr{ .display = text("Frame spacing"), .tooltip = text("Metres between two ribs"), .propagation = text("copy"),
                     .min = 0.1f, .max = 20.0f, .affects_topology = true }]]
            f32 frame_spacing = 1.0f;

            [[=attr{ .display = text("Rib depth"), .tooltip = text("How far a rib reaches in from the hull"), .propagation = text("copy"),
                     .min = 0.01f, .max = 5.0f }]]
            f32 rib_depth = 0.15f;

            [[=attr{ .display = text("Rib thickness"), .propagation = text("copy"), .min = 0.005f, .max = 2.0f }]]
            f32 rib_thickness = 0.06f;

            [[=attr{ .display = text("Axis"), .tooltip = text("World axis the hull runs along (0 x, 1 y, 2 z)"), .propagation = text("copy"),
                     .min = 0.0f, .max = 2.0f, .affects_topology = true }]]
            i32 axis = 2;

            [[=attr{ .display = text("Stringers"), .tooltip = text("Longitudinal members around the hull (0: none)"), .propagation = text("copy"),
                     .min = 0.0f, .max = 64.0f, .affects_topology = true }]]
            i32 stringers = 8;

            [[=attr{ .display = text("Stringer size"), .propagation = text("copy"), .min = 0.005f, .max = 1.0f }]]
            f32 stringer_size = 0.04f;

            [[=attr{ .display = text("Show skin"), .tooltip = text("Also build the hull surface itself"), .propagation = text("copy") }]]
            bool show_skin = false;

            [[=attr{ .display = text("Clearance"), .tooltip = text("Gap kept between ribs / stringers and this component"),
                     .propagation = text("copy"), .min = 0.0f, .max = 2.0f }]]
            f32 clearance = 0.08f;

            [[=attr{ .display = text("Show block-out"), .tooltip = text("Build the component's volume as a block-out mesh"),
                     .propagation = text("copy") }]]
            bool show_component = true;
        };


        struct [[=kind_info{ text("ship.rib"), shape_kind::frame, text("Rib") }]] rib_params {
            [[=param{ .display = text("Axis") }]]    i32 axis = 2;
            [[=param{ .display = text("Offset") }]]  f64 offset = 0.0;
            [[=param{ .display = text("Station") }]] i32 index = 0;
        };


        struct [[=kind_info{ text("ship.stringer"), shape_kind::curve, text("Stringer") }]] stringer_params {};

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    role_id role_hull()      { static const role_id id = intern("ship.hull"); return id; }
    role_id role_component() { static const role_id id = intern("ship.component"); return id; }
    kind_id kind_rib()       { static const kind_id id = intern("ship.rib"); return id; }
    kind_id kind_stringer()  { static const kind_id id = intern("ship.stringer"); return id; }

    namespace tags {
        attribute_id frame_spacing()  { return attribute_of<^^pack::ship_tags::frame_spacing>(); }
        attribute_id rib_depth()      { return attribute_of<^^pack::ship_tags::rib_depth>(); }
        attribute_id rib_thickness()  { return attribute_of<^^pack::ship_tags::rib_thickness>(); }
        attribute_id axis()           { return attribute_of<^^pack::ship_tags::axis>(); }
        attribute_id stringers()      { return attribute_of<^^pack::ship_tags::stringers>(); }
        attribute_id stringer_size()  { return attribute_of<^^pack::ship_tags::stringer_size>(); }
        attribute_id show_skin()      { return attribute_of<^^pack::ship_tags::show_skin>(); }
        attribute_id clearance()      { return attribute_of<^^pack::ship_tags::clearance>(); }
        attribute_id show_component() { return attribute_of<^^pack::ship_tags::show_component>(); }
    }

    namespace {

        name_id material_frame()     { static const name_id id = intern("frame"); return id; }
        name_id material_hull()      { static const name_id id = intern("hull"); return id; }
        name_id material_component() { static const name_id id = intern("component"); return id; }

        f32 number(const context& ctx, const element& e, attribute_id id, f32 fallback) {
            const attribute_value v = ctx.attribute(e, id);
            return v.index() ? static_cast<f32>(as_number(v)) : fallback;
        }

        glm::vec3 axis_vector(i32 axis) { return axis == 0 ? glm::vec3(1, 0, 0) : (axis == 1 ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1)); }

        // Two directions spanning the section plane
        std::pair<glm::vec3, glm::vec3> section_basis(i32 axis) {
            if (axis == 0) return { glm::vec3(0, 0, 1), glm::vec3(0, 1, 0) };
            if (axis == 1) return { glm::vec3(1, 0, 0), glm::vec3(0, 0, 1) };
            return { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0) };
        }

        // The hull's cage in world (construct) space
        geometry::mesh world_cage(const element& hull) {
            geometry::mesh m = std::get<volume_shape>(hull.data).cage;
            for (u32 v = 0; v < m.vertex_count(); ++v)
                m.set_position(v, glm::vec3(hull.placement.to_world(m.position(v))));
            return m;
        }

        // Closed sections of the hull at [offset] along [axis], counter-clockwise seen from +axis, largest first
        std::vector<geometry::contour> sections(const geometry::mesh& cage, i32 axis, f64 offset) {
            const glm::vec3 n = axis_vector(axis);
            std::vector<geometry::contour> out = geometry::slice(cage, glm::vec4(n, static_cast<f32>(offset)));
            std::erase_if(out, [](const geometry::contour& c) { return !c.closed || c.points.size() < 3; });
            for (geometry::contour& c : out)
                if (geometry::signed_area(c, n) < 0.0f)
                    std::ranges::reverse(c.points);
            std::ranges::stable_sort(out, [&](const geometry::contour& a, const geometry::contour& b) {
                return geometry::signed_area(a, n) > geometry::signed_area(b, n);
            });
            return out;
        }

        // Distance from [p] to the nearest component, minus that component's clearance (< 0: too close)
        f64 component_gap(const context& ctx, const std::vector<element_handle>& components, const glm::vec3& p) {
            f64 best = std::numeric_limits<f64>::max();
            for (const element_handle h : components) {
                const element& c = ctx.get(h);
                const element_path path = c.path;
                const f64 d = ctx.distance(glm::dvec3(p), { .predicate = [path](const element& e, const context&) { return e.path == path; } });
                best = std::min(best, d - static_cast<f64>(number(ctx, c, tags::clearance(), 0.08f)));
            }
            return best;
        }

        std::vector<element_handle> components_of(const context& ctx) {
            return ctx.elements({ .kinds = { kind_guide_volume() }, .roles = { role_component() } });
        }

        // Polygon soup with outward-facing faces, made into a mesh at the end
        struct soup {
            std::vector<glm::vec3>                      positions{};
            std::vector<std::vector<u32>>               faces{};

            u32 vertex(const glm::vec3& p) { positions.push_back(p); return static_cast<u32>(positions.size() - 1); }

            void quad(u32 a, u32 b, u32 c, u32 d, const glm::vec3& facing) {
                const glm::vec3 n = glm::cross(positions[b] - positions[a], positions[c] - positions[a]) +
                                    glm::cross(positions[c] - positions[a], positions[d] - positions[a]);
                if (glm::dot(n, facing) >= 0.0f) faces.push_back({ a, b, c, d });
                else                             faces.push_back({ a, d, c, b });
            }

            geometry::mesh build(name_id material) const {
                geometry::mesh m = geometry::mesh::from_polygons(positions, faces);
                for (u32 f = 0; f < m.face_count(); ++f)
                    m.set(attribute_domain::face, geometry::attr_material(), f, material);
                return m;
            }
        };

    }

    // Productions ----------------------------------------------------------------------------------------------------------

    namespace pack {

        // Frame stations along the hull axis: one rib each
        struct [[=production_info{ text("ship.frames"), text("primary") }]] frames final : production {

            static matcher match() { return { .kinds = { kind_guide_volume() }, .roles = { role_hull() } }; }
            static std::vector<kind_id> emits() { return { kind_rib() }; }

            void expand(expand_context& ctx, const element& hull) override {

                const context& q = ctx.query();
                const i32 axis = std::clamp(static_cast<i32>(number(q, hull, tags::axis(), 2.0f)), 0, 2);
                const f32 spacing = std::max(number(q, hull, tags::frame_spacing(), 1.0f), 0.05f);
                const geometry::mesh cage = world_cage(hull);
                if (cage.vertex_count() == 0)
                    return;
                f32 lo = std::numeric_limits<f32>::max(), hi = -std::numeric_limits<f32>::max();
                for (const glm::vec3& p : cage.positions()) {
                    lo = std::min(lo, p[axis]);
                    hi = std::max(hi, p[axis]);
                }
                const f32 length = hi - lo;
                const i32 count = std::clamp(static_cast<i32>(std::floor(length / spacing)), 1, 4096);
                const f32 first = lo + (length - static_cast<f32>(count - 1) * spacing) * 0.5f;
                for (i32 k = 0; k < count; ++k) {
                    const f64 offset = static_cast<f64>(first + static_cast<f32>(k) * spacing);
                    if (sections(cage, axis, offset).empty())
                        continue;
                    ctx.emit(hull, "rib", k, kind_rib())
                        .placement(frame{})                     // ribs are built in construct space
                        .shape(frame_shape{})
                        .params(rib_params{ .axis = axis, .offset = offset, .index = k });
                }
            }
        };


        // Longitudinal members through every station, split where they would run through a component
        struct [[=production_info{ text("ship.stringers"), text("secondary") }]] stringers final : production {

            static matcher match() { return { .kinds = { kind_guide_volume() }, .roles = { role_hull() } }; }
            static std::vector<kind_id> emits() { return { kind_stringer() }; }

            void expand(expand_context& ctx, const element& hull) override {

                const context& q = ctx.query();
                const i32 count = static_cast<i32>(number(q, hull, tags::stringers(), 8.0f));
                if (count <= 0)
                    return;
                const auto self = q.find(hull.path);
                if (!self)
                    return;

                std::vector<std::pair<i32, f64>> stations;              // (index, offset)
                i32 axis = 2;
                for (const element_handle h : q.children(*self)) {
                    const element& rib = q.get(h);
                    if (rib.kind != kind_rib())
                        continue;
                    if (const rib_params* p = rib.params.get<rib_params>()) {
                        stations.emplace_back(p->index, p->offset);
                        axis = p->axis;
                    }
                }
                std::ranges::sort(stations);
                if (stations.size() < 2)
                    return;

                const geometry::mesh cage = world_cage(hull);
                const f32 depth = number(q, hull, tags::rib_depth(), 0.15f);
                const auto [u, v] = section_basis(axis);
                const std::vector<element_handle> components = components_of(q);

                for (i32 s = 0; s < count; ++s) {
                    const f32 angle = 6.2831853f * (static_cast<f32>(s) + 0.5f) / static_cast<f32>(count);
                    const glm::vec3 dir = u * std::cos(angle) + v * std::sin(angle);
                    std::vector<glm::vec3> run;
                    i32 run_index = 0;
                    auto flush = [&] {
                        if (run.size() >= 2) {
                            ctx.emit(hull, "stringer", s * 100 + run_index, kind_stringer())
                                .placement(frame{})
                                .shape(curve_shape{ geometry::curve(geometry::curve_type::polyline, run) })
                                .params(stringer_params{});
                            ++run_index;
                        }
                        run.clear();
                    };
                    for (const auto& [index, offset] : stations) {
                        const auto cs = sections(cage, axis, offset);
                        if (cs.empty()) { flush(); continue; }
                        const geometry::contour& c = cs.front();
                        glm::vec3 centre(0.0f);
                        for (const glm::vec3& p : c.points)
                            centre += p;
                        centre /= static_cast<f32>(c.points.size());
                        // Where the ray from the centre along [dir] leaves the section
                        std::optional<glm::vec3> hit;
                        f32 best = std::numeric_limits<f32>::max();
                        for (size_t i = 0; i < c.points.size(); ++i) {
                            const glm::vec3 a = c.points[i] - centre, b = c.points[(i + 1) % c.points.size()] - centre;
                            const glm::vec2 a2(glm::dot(a, u), glm::dot(a, v)), b2(glm::dot(b, u), glm::dot(b, v));
                            const glm::vec2 d2(std::cos(angle), std::sin(angle)), e = b2 - a2;
                            const f32 den = d2.x * e.y - d2.y * e.x;
                            if (std::abs(den) < 1e-9f)
                                continue;
                            const f32 t = (a2.x * e.y - a2.y * e.x) / den;      // along the ray
                            const f32 w = (a2.x * d2.y - a2.y * d2.x) / den;    // along the edge
                            if (t > 0.0f && w >= 0.0f && w <= 1.0f && t < best) {
                                best = t;
                                hit = centre + dir * t;
                            }
                        }
                        if (!hit) { flush(); continue; }
                        const glm::vec3 p = *hit - dir * (depth * 0.5f);
                        if (component_gap(q, components, p) < 0.0) { flush(); continue; }
                        run.push_back(p);
                    }
                    flush();
                }
            }
        };

        // Realizers --------------------------------------------------------------------------------------------------------

        struct [[=realizer_info{ text("ship.rib") }]] rib_realizer final : realizer {

            static matcher match() { return { .kinds = { kind_rib() } }; }

            void realize(realize_context& ctx, const element& e) override {

                const context& q = ctx.query();
                const rib_params* p = e.params.get<rib_params>();
                if (!p || !e.parent.valid())
                    return;
                const element& hull = q.get(e.parent);
                const f32 depth = number(q, e, tags::rib_depth(), 0.15f);
                const f32 half = number(q, e, tags::rib_thickness(), 0.06f) * 0.5f;
                const glm::vec3 n = axis_vector(p->axis);
                const std::vector<element_handle> components = components_of(q);

                soup s{};
                for (const geometry::contour& section : sections(world_cage(hull), p->axis, p->offset)) {
                    f32 perimeter = 0.0f;
                    for (size_t i = 0; i < section.points.size(); ++i)
                        perimeter += glm::length(section.points[(i + 1) % section.points.size()] - section.points[i]);
                    const geometry::contour c = geometry::resample(section, std::clamp(perimeter / 160.0f, 0.02f, 0.25f), 32);
                    const size_t count = c.points.size();
                    if (count < 3)
                        continue;

                    // Inner edge: inset along the miter of the inward (left) normals, limited at sharp corners
                    std::vector<glm::vec3> outer = c.points, inner(count);
                    for (size_t i = 0; i < count; ++i) {
                        const glm::vec3 d_prev = glm::normalize(outer[i] - outer[(i + count - 1) % count]);
                        const glm::vec3 d_next = glm::normalize(outer[(i + 1) % count] - outer[i]);
                        const glm::vec3 in_prev = glm::cross(n, d_prev), in_next = glm::cross(n, d_next);
                        glm::vec3 miter = in_prev + in_next;
                        const f32 len = glm::length(miter);
                        miter = len > 1e-6f ? miter / len : in_next;
                        const f32 scale = 1.0f / std::max(glm::dot(miter, in_next), 0.5f);
                        inner[i] = outer[i] + miter * depth * scale;
                    }

                    // Which segments survive the components (both edges and the middle must keep their clearance)
                    std::vector<bool> keep(count);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t j = (i + 1) % count;
                        const glm::vec3 mid_outer = (outer[i] + outer[j]) * 0.5f, mid_inner = (inner[i] + inner[j]) * 0.5f;
                        keep[i] = component_gap(q, components, mid_outer) >= 0.0 && component_gap(q, components, mid_inner) >= 0.0
                               && component_gap(q, components, (mid_outer + mid_inner) * 0.5f) >= 0.0;
                    }

                    // Four vertices per point: outer / inner, front / back
                    std::vector<std::array<u32, 4>> v(count);
                    for (size_t i = 0; i < count; ++i)
                        v[i] = { s.vertex(outer[i] - n * half), s.vertex(inner[i] - n * half), s.vertex(outer[i] + n * half), s.vertex(inner[i] + n * half) };

                    for (size_t i = 0; i < count; ++i) {
                        if (!keep[i])
                            continue;
                        const size_t j = (i + 1) % count;
                        const glm::vec3 along = outer[j] - outer[i];
                        const glm::vec3 inward = glm::cross(n, along);
                        s.quad(v[i][0], v[i][1], v[j][1], v[j][0], -n);         // front
                        s.quad(v[i][2], v[j][2], v[j][3], v[i][3], n);          // back
                        s.quad(v[i][0], v[j][0], v[j][2], v[i][2], -inward);    // outer face (against the hull)
                        s.quad(v[i][1], v[i][3], v[j][3], v[j][1], inward);     // inner face
                        // Caps where a run of kept segments starts / ends
                        if (!keep[(i + count - 1) % count])
                            s.quad(v[i][0], v[i][1], v[i][3], v[i][2], -along);
                        if (!keep[j])
                            s.quad(v[j][0], v[j][2], v[j][3], v[j][1], along);
                    }
                }
                if (!s.faces.empty())
                    ctx.mesh("rib", s.build(material_frame()), 35.0f);
            }
        };


        struct [[=realizer_info{ text("ship.stringer") }]] stringer_realizer final : realizer {

            static matcher match() { return { .kinds = { kind_stringer() } }; }

            void realize(realize_context& ctx, const element& e) override {
                const auto& c = std::get<curve_shape>(e.data).curve;
                const f32 size = number(ctx.query(), e, tags::stringer_size(), 0.04f);
                geometry::mesh m = geometry::sweep_circle(c, { .sides = 4, .radius = size, .radius_attribute = {}, .caps = true, .segments_per_span = 1 });
                for (u32 f = 0; f < m.face_count(); ++f)
                    m.set(attribute_domain::face, geometry::attr_material(), f, material_frame());
                ctx.mesh("stringer", std::move(m), 30.0f);
            }
        };


        struct [[=realizer_info{ text("ship.hull") }]] hull_realizer final : realizer {

            static matcher match() { return { .kinds = { kind_guide_volume() }, .roles = { role_hull() } }; }

            void realize(realize_context& ctx, const element& e) override {
                if (number(ctx.query(), e, tags::show_skin(), 0.0f) == 0.0f)
                    return;
                geometry::mesh m = std::get<volume_shape>(e.data).cage;
                for (u32 f = 0; f < m.face_count(); ++f)
                    m.set(attribute_domain::face, geometry::attr_material(), f, material_hull());
                ctx.mesh("skin", std::move(m));
            }
        };


        struct [[=realizer_info{ text("ship.component") }]] component_realizer final : realizer {

            static matcher match() { return { .kinds = { kind_guide_volume() }, .roles = { role_component() } }; }

            void realize(realize_context& ctx, const element& e) override {
                if (number(ctx.query(), e, tags::show_component(), 1.0f) == 0.0f)
                    return;
                geometry::mesh m = std::get<volume_shape>(e.data).cage;
                for (u32 f = 0; f < m.face_count(); ++f)
                    m.set(attribute_domain::face, geometry::attr_material(), f, material_component());
                ctx.mesh("component", std::move(m));
            }
        };

        // Policies -----------------------------------------------------------------------------------------------------------

        struct [[=policy_info{ text("spaceship.outputs") }]] outputs_policy final : i_outputs {
            using hook = outputs_hook;
            void outputs(const output_query&, std::vector<output_desc>& out) const override {
                out.push_back({ .name = "frame", .purpose = output_purpose::render, .include = { .names = { "rib", "stringer" } }, .topo = topology::merged_shells });
                out.push_back({ .name = "hull", .purpose = output_purpose::render, .include = { .names = { "skin" } }, .topo = topology::merged_shells });
                out.push_back({ .name = "components", .purpose = output_purpose::render, .include = { .names = { "component" } }, .topo = topology::merged_shells });
            }
        };

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        guide make_volume(std::string name, role_id role, geometry::primitive_kind kind, geometry::primitive_params params, glm::dvec3 at) {

            guide g{};
            g.name = std::move(name);
            g.role = role;
            g.placement.origin = at;
            g.shape = volume_guide{ kind, params, orientation::solid };
            return g;
        }


        // Hulls run along z: rotate primitives whose long axis is y
        guide make_hull(geometry::primitive_kind kind, geometry::primitive_params params, bool rotate) {

            guide g = make_volume("hull", role_hull(), kind, params, glm::dvec3(0.0));
            if (rotate)
                g.placement.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            g.attributes.set(tags::frame_spacing(), 1.0f);
            return g;
        }


        // Components go where the selected guide is (offset a little so they are visible)
        glm::dvec3 near_selection(const guide* selected) {
            return selected ? selected->placement.origin + glm::dvec3(0.0, 0.0, 1.5) : glm::dvec3(0.0);
        }

    }


    void register_pack(i_registry& registry, std::string_view owner) {

        construct::register_pack<^^pack>(registry, owner, {
            .name = std::string(PACK_NAME),
            .version = { 0, 2, 0 },
            .extends = {},
            .depends_on = {},
            .display = "Spaceship",
            .description = "Internal frame from the hull's shape: ribs at frame stations, stringers, cut around components",
            .upgrade = {},
            .self_test = [](std::vector<std::string>& failures, u32& passed) { run_self_test(failures, passed); },
            .legacy_asset_types = { "construct_spaceship" },
        });

        // Editor declarations ---------------------------------------------------------------------------------------------
        using geometry::primitive_kind;
        const std::string pack(PACK_NAME);
        registry.add(owner, pack, role_desc{
            .name = "ship.hull",
            .display = "Hull",
            .description = "Ribs follow its sections at every frame station; loop cuts and scaling of the hull change them",
            .attributes = { tags::axis(), tags::frame_spacing(), tags::rib_depth(), tags::rib_thickness(), tags::stringers(),
                            tags::stringer_size(), tags::show_skin() },
            .point_attributes = {},
        });
        registry.add(owner, pack, role_desc{
            .name = "ship.component",
            .display = "Component",
            .description = "Blocks the frame: ribs and stringers are cut around it",
            .attributes = { tags::clearance(), tags::show_component() },
            .point_attributes = {},
        });

        struct tpl { const char* name; const char* display; const char* description; guide (*make)(const construct_data&, const guide*); };
        static constexpr tpl templates[] = {
            { "ship.hull_capsule", "Hull: capsule", "A rounded hull along z",
              [](const construct_data&, const guide*) { return make_hull(primitive_kind::capsule, { .radius = 2.0f, .height = 8.0f, .segments = 24, .rings = 12 }, true); } },
            { "ship.hull_box", "Hull: box", "A boxy hull along z (add loop cuts and scale them for shape)",
              [](const construct_data&, const guide*) { return make_hull(primitive_kind::box, { .size = glm::vec3(3.5f, 2.5f, 10.0f) }, false); } },
            { "ship.hull_cylinder", "Hull: cylinder", "A round hull along z",
              [](const construct_data&, const guide*) { return make_hull(primitive_kind::cylinder, { .radius = 1.8f, .height = 10.0f, .segments = 24 }, true); } },
            { "ship.component_box", "Component: box", "Blocks the frame: ribs and stringers are cut around it",
              [](const construct_data&, const guide* sel) { return make_volume("component", role_component(), primitive_kind::box, { .size = glm::vec3(1.2f) }, near_selection(sel)); } },
            { "ship.component_sphere", "Component: sphere", "Blocks the frame (reactor, tank, ...)",
              [](const construct_data&, const guide* sel) { return make_volume("component", role_component(), primitive_kind::sphere, { .radius = 0.8f, .segments = 20, .rings = 12 }, near_selection(sel)); } },
            { "ship.component_cylinder", "Component: cylinder", "Blocks the frame (pipe trunk, engine, ...)",
              [](const construct_data&, const guide* sel) { return make_volume("component", role_component(), primitive_kind::cylinder, { .radius = 0.5f, .height = 2.0f, .segments = 16 }, near_selection(sel)); } },
        };
        for (const tpl& t : templates)
            registry.add(owner, pack, guide_template_desc{ .name = t.name, .display = t.display, .description = t.description, .make = t.make, .after_add = {} });

        registry.add(owner, pack, preset_desc{
            .name = "ship.frame",
            .display = "Spaceship frame",
            .description = "A capsule hull with three components: ribs at frame stations and stringers",
            .make = [] { return make_default_ship(); },
        });
    }


    construct_data make_default_ship() {

        construct_data data{};
        data.packs = { std::string(PACK_NAME) };

        guide hull{};
        hull.name = "hull";
        hull.role = role_hull();
        hull.placement.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));   // capsule axis y -> z
        hull.placement.scale = glm::vec3(1.3f, 1.0f, 0.85f);                                          // wider than tall
        hull.shape = volume_guide{ geometry::primitive_kind::capsule, { .radius = 2.0f, .height = 9.0f, .segments = 24, .rings = 12 }, orientation::solid };
        hull.attributes.set(tags::frame_spacing(), 0.9f);
        hull.attributes.set(tags::stringers(), i32(10));
        data.guides.push_back(std::move(hull));

        auto component = [&](std::string name, geometry::primitive_kind kind, geometry::primitive_params params, glm::dvec3 at) {
            guide g{};
            g.name = std::move(name);
            g.role = role_component();
            g.placement.origin = at;
            g.shape = volume_guide{ kind, params, orientation::solid };
            data.guides.push_back(std::move(g));
        };
        component("reactor", geometry::primitive_kind::sphere, { .radius = 1.45f, .segments = 20, .rings = 12 }, { 0.0, 0.0, -4.0 });
        component("backbone", geometry::primitive_kind::box, { .size = glm::vec3(0.7f, 0.45f, 13.0f) }, { 0.0, -1.45, 0.0 });
        component("cargo", geometry::primitive_kind::box, { .size = glm::vec3(1.4f, 1.3f, 3.0f) }, { 1.75, 0.25, 2.5 });
        return data;
    }

}
