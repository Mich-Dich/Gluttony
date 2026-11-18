#include "procedural/construct/evaluate.h"



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    kind_id kind_guide_volume()          { static const kind_id id = intern("guide.volume");   return id; }
    kind_id kind_guide_curve()           { static const kind_id id = intern("guide.curve");    return id; }
    kind_id kind_guide_surface()         { static const kind_id id = intern("guide.surface");  return id; }
    kind_id kind_guide_point()           { static const kind_id id = intern("guide.point");    return id; }
    relation_id relation_child_of()      { static const relation_id id = intern("core.child_of");  return id; }
    relation_id relation_mirror_of()     { static const relation_id id = intern("core.mirror_of"); return id; }
    attribute_id attr_radius()           { static const attribute_id id = intern("core.radius");   return id; }
    attribute_id attr_hidden()           { static const attribute_id id = intern("core.hidden");   return id; }


    bool scope_matches(const policy_scope& scope, const element& e, const region_selector& region) {

        switch (scope.level) {
            case scope_level::element:
                return scope.path == e.path;
            case scope_level::region:
                return scope.path == e.path && scope.region == region;
            case scope_level::attribute: {
                const attribute_value* v = e.attributes.find(scope.attribute, region);
                if (!v)
                    return false;
                return scope.value.index() == 0 || *v == scope.value;
            }
            case scope_level::kind:
                return scope.kind == e.kind;
            default:
                return true;
        }
    }

    // CLASS IMPLEMENTATION ============================================================================================

    bool face_selection::matches(const geometry::mesh& m, u32 face) const {

        if (group) {
            const attribute_value& g = m.get(attribute_domain::face, geometry::attr_group(), face);
            const name_id* n = std::get_if<name_id>(&g);
            if (!n || *n != *group)
                return false;
        }
        if (normal) {
            const glm::vec3 fn = m.face_normal(face);
            const f32 limit = std::cos(glm::radians(max_angle_degrees));
            if (glm::dot(fn, glm::normalize(*normal)) < limit)
                return false;
        }
        if (inside) {
            const glm::vec3 c = m.face_centroid(face);
            if (glm::any(glm::lessThan(c, inside->first)) || glm::any(glm::greaterThan(c, inside->second)))
                return false;
        }
        return true;
    }


    bool matcher::matches(const element& e, const context& ctx) const {

        if (!kinds.empty() && std::ranges::find(kinds, e.kind) == kinds.end())
            return false;
        if (!shapes.empty() && std::ranges::find(shapes, e.shape) == shapes.end())
            return false;
        if (!roles.empty() && std::ranges::find(roles, e.role) == roles.end())
            return false;
        if (origin && *origin != e.origin)
            return false;
        if (max_depth && e.depth > *max_depth)
            return false;
        for (const auto& [id, value] : require) {
            const attribute_value v = ctx.attribute(e, id);
            if (!(v == value))
                return false;
        }
        for (const attribute_id id : require_present)
            if (!e.attributes.has(id))
                return false;
        for (const attribute_id id : exclude_present)
            if (e.attributes.has(id))
                return false;
        if (predicate && !predicate(e, ctx))
            return false;
        return true;
    }


    bool product_filter::matches(const product& p) const {

        if (!names.empty() && std::ranges::find(names, p.name) == names.end())
            return false;
        if (!kinds.empty() && std::ranges::find(kinds, p.element_kind) == kinds.end())
            return false;
        for (const attribute_id id : require_tags)
            if (!p.tags.has(id))
                return false;
        for (const attribute_id id : exclude_tags)
            if (p.tags.has(id))
                return false;
        return true;
    }


    u32 output_result::triangle_count() const noexcept {

        u32 n = 0;
        for (const output_part& part : parts)
            for (const output_group& g : part.groups)
                n += static_cast<u32>(g.mesh.indices.size() / 3);
        return n;
    }


    u64 output_result::hash() const noexcept {

        auto bytes = [](const auto& vec) {
            using V = typename std::decay_t<decltype(vec)>::value_type;
            return fnv1a_64(std::string_view(reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(V)));
        };
        u64 h = fnv1a_64(desc.name);
        h = hash_combine(h, static_cast<u64>(desc.topo));
        for (const output_part& part : parts) {
            h = hash_combine(h, part.element.hash());
            for (const output_group& g : part.groups) {
                h = hash_combine(h, g.material.hash);
                h = hash_combine(h, bytes(g.mesh.positions));
                h = hash_combine(h, bytes(g.mesh.normals));
                h = hash_combine(h, bytes(g.mesh.uvs));
                h = hash_combine(h, bytes(g.mesh.indices));
            }
        }
        for (const output_instance& i : instances) {
            h = hash_combine(h, fnv1a_64(i.asset));
            h = hash_combine(h, fnv1a_64(std::string_view(reinterpret_cast<const char*>(&i.transform), sizeof(glm::mat4))));
        }
        for (const output_marker& m : markers) {
            h = hash_combine(h, fnv1a_64(m.name));
            h = hash_combine(h, m.element.hash());
            h = hash_combine(h, fnv1a_64(std::string_view(reinterpret_cast<const char*>(&m.at.origin), sizeof(glm::dvec3))));
        }
        return h;
    }


    bool evaluation_result::ok() const noexcept {

        return !cancelled && std::ranges::none_of(diagnostics, [](const diagnostic& d) { return d.level == severity::error; });
    }


    u64 evaluation_result::hash() const noexcept {

        u64 h = 0x6a09e667f3bcc909ull;
        for (const output_result& o : outputs)
            h = hash_combine(h, o.hash());
        return h;
    }


    const output_result* evaluation_result::output(std::string_view name) const noexcept {

        for (const output_result& o : outputs)
            if (o.desc.name == name)
                return &o;
        return nullptr;
    }


    geometry::mesh apply_guide_edits(geometry::mesh m, std::span<const edit_step> edits, const propagator& prop, std::vector<std::string>* warnings) {

        auto warn = [&](std::string text) { if (warnings) warnings->push_back(std::move(text)); };
        for (const edit_step& step : edits) {
            std::visit([&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, edit_transform>) {
                    geometry::transform(m, s.matrix);
                } else if constexpr (std::is_same_v<T, edit_loop_cut>) {
                    u32 best = geometry::INVALID_INDEX;
                    f32 best_d = std::numeric_limits<f32>::max();
                    for (u32 h = 0; h < m.halfedge_count(); ++h) {
                        const glm::vec3 mid = (m.position(m.from_vertex(h)) + m.position(m.he(h).vertex)) * 0.5f;
                        const f32 d = glm::length(mid - s.near_point);
                        if (d < best_d - 1e-6f) {
                            best_d = d;
                            best = h;
                        }
                    }
                    if (best == geometry::INVALID_INDEX)
                        warn("loop cut: the guide has no edges");
                    else
                        m = geometry::loop_cut(m, best, s.positions, prop);
                } else if constexpr (std::is_same_v<T, edit_assign_group>) {
                    for (u32 f = 0; f < m.face_count(); ++f)
                        if (s.faces.matches(m, f))
                            m.set(attribute_domain::face, geometry::attr_group(), f, s.group);
                } else if constexpr (std::is_same_v<T, edit_set_attribute>) {
                    std::vector<u32> vs, hs;
                    for (u32 f = 0; f < m.face_count(); ++f) {
                        if (!s.faces.matches(m, f))
                            continue;
                        switch (s.domain) {
                            case attribute_domain::face:
                                m.set(attribute_domain::face, s.id, f, s.value);
                                break;
                            case attribute_domain::vertex:
                                m.face_vertices(f, vs);
                                for (const u32 v : vs)
                                    m.set(attribute_domain::vertex, s.id, v, s.value);
                                break;
                            case attribute_domain::corner:
                            case attribute_domain::edge:
                                m.face_halfedges(f, hs);
                                for (const u32 h : hs)
                                    m.set(s.domain, s.id, h, s.value);
                                break;
                            default:
                                warn("set attribute: unsupported domain for a mesh edit");
                                break;
                        }
                    }
                }
            }, step);
        }
        return m;
    }


    std::optional<geometry::mesh> guide_mesh(const guide& g) {

        static const mode_propagator modes{};
        if (const auto* v = std::get_if<volume_guide>(&g.shape))
            return apply_guide_edits(geometry::make_primitive(v->primitive, v->params), g.edits, modes);
        if (const auto* s = std::get_if<surface_guide>(&g.shape)) {
            geometry::primitive_params p{};
            p.size = glm::vec3(s->size.x, 0.0f, s->size.y);
            p.divisions = s->divisions;
            return apply_guide_edits(geometry::make_primitive(geometry::primitive_kind::plane, p), g.edits, modes);
        }
        return std::nullopt;
    }

}
