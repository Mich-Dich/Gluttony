#include "procedural/construct/evaluation.h"

#include <chrono>
#include <unordered_set>



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        using clock = std::chrono::steady_clock;

        f64 ms_since(clock::time_point t) {
            return std::chrono::duration<f64, std::milli>(clock::now() - t).count();
        }


        // Topological order of named items with after / before constraints; ties by [order]. Returns false on a cycle
        template<typename Item>
        bool order_items(std::vector<const Item*>& items, auto name_of, auto after_of, auto before_of, auto order_of,
                         std::string& cycle) {

            const size_t n = items.size();
            std::unordered_map<std::string, size_t> index;
            for (size_t i = 0; i < n; ++i)
                index.emplace(std::string(name_of(*items[i])), i);
            std::vector<std::vector<size_t>> out(n);
            std::vector<u32> in(n, 0);
            auto edge = [&](size_t a, size_t b) { out[a].push_back(b); ++in[b]; };
            for (size_t i = 0; i < n; ++i) {
                for (const std::string& a : after_of(*items[i]))
                    if (auto it = index.find(a); it != index.end())
                        edge(it->second, i);
                for (const std::string& b : before_of(*items[i]))
                    if (auto it = index.find(b); it != index.end())
                        edge(i, it->second);
            }
            std::vector<const Item*> sorted;
            std::vector<bool> done(n, false);
            for (size_t step = 0; step < n; ++step) {
                size_t best = n;
                for (size_t i = 0; i < n; ++i)
                    if (!done[i] && in[i] == 0 && (best == n || order_of(*items[i]) < order_of(*items[best])))
                        best = i;
                if (best == n) {
                    for (size_t i = 0; i < n; ++i)
                        if (!done[i])
                            cycle += std::string(cycle.empty() ? "" : ", ") + std::string(name_of(*items[i]));
                    return false;
                }
                done[best] = true;
                sorted.push_back(items[best]);
                for (const size_t j : out[best])
                    --in[j];
            }
            items = std::move(sorted);
            return true;
        }


        // Signed distance to an axis-aligned primitive in its local frame
        f64 primitive_distance(const volume_guide& v, const glm::vec3& p) {

            const geometry::primitive_params& pp = v.params;
            switch (v.primitive) {
                case geometry::primitive_kind::box: {
                    const glm::vec3 q = glm::abs(p) - pp.size * 0.5f;
                    return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
                }
                case geometry::primitive_kind::sphere:
                    return glm::length(p) - pp.radius;
                case geometry::primitive_kind::cylinder: {
                    const glm::vec2 d = glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y)) - glm::vec2(pp.radius, pp.height * 0.5f);
                    return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
                }
                case geometry::primitive_kind::capsule: {
                    const f32 hy = pp.height * 0.5f;
                    const glm::vec3 c(0.0f, std::clamp(p.y, -hy, hy), 0.0f);
                    return glm::length(p - c) - pp.radius;
                }
                case geometry::primitive_kind::plane:
                    return std::abs(p.y);
            }
            return 0.0;
        }


        f64 mesh_box_distance(const geometry::mesh& m, const glm::vec3& p) {

            if (m.vertex_count() == 0)
                return std::numeric_limits<f64>::max();
            glm::vec3 lo(std::numeric_limits<f32>::max()), hi(-std::numeric_limits<f32>::max());
            for (const glm::vec3& v : m.positions()) {
                lo = glm::min(lo, v);
                hi = glm::max(hi, v);
            }
            const glm::vec3 c = (lo + hi) * 0.5f, h = (hi - lo) * 0.5f;
            const glm::vec3 q = glm::abs(p - c) - h;
            return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
        }


        geometry::triangle_mesh transformed_triangles(const geometry::mesh& m, const glm::mat4& matrix, f32 smooth_angle) {

            geometry::mesh copy = m;
            geometry::transform(copy, matrix);
            return geometry::triangulate(copy, smooth_angle);
        }


        void append_triangles(geometry::triangle_mesh& dst, const geometry::triangle_mesh& src, u32 first_triangle, u32 count) {

            // Only the vertices the selected triangles use
            std::unordered_map<u32, u32> remap;
            for (u32 t = first_triangle; t < first_triangle + count; ++t)
                for (u32 k = 0; k < 3; ++k) {
                    const u32 i = src.indices[t * 3 + k];
                    auto [it, inserted] = remap.try_emplace(i, static_cast<u32>(dst.positions.size()));
                    if (inserted) {
                        dst.positions.push_back(src.positions[i]);
                        dst.normals.push_back(src.normals[i]);
                        dst.uvs.push_back(src.uvs[i]);
                    }
                    dst.indices.push_back(it->second);
                }
            for (u32 t = first_triangle; t < first_triangle + count; ++t)
                dst.triangle_material.push_back(src.triangle_material[t]);
        }


        // Splits a triangle mesh into material groups and appends them to [groups]
        void add_by_material(std::vector<output_group>& groups, const geometry::triangle_mesh& tris) {

            const u32 count = static_cast<u32>(tris.triangle_material.size());
            u32 t = 0;
            while (t < count) {
                const name_id material = tris.triangle_material[t];
                u32 run = 1;
                while (t + run < count && tris.triangle_material[t + run] == material)
                    ++run;
                output_group* g = nullptr;
                for (output_group& existing : groups)
                    if (existing.material == material)
                        g = &existing;
                if (!g) {
                    groups.push_back({ material, {} });
                    g = &groups.back();
                }
                append_triangles(g->mesh, tris, t, run);
                t += run;
            }
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    evaluation::evaluation(const registry_snapshot& snapshot, const evaluation_request& req)
        : snap(snapshot), request(req), data(*req.data) {

        construct_seed = hash_combine(req.seed, 0x51ed270b27f3c5a1ull);
        resolve_active();
    }


    evaluation::~evaluation() = default;


    bool evaluation::pack_active(std::string_view pack) const noexcept { return pack_rank(pack) >= 0; }


    i32 evaluation::pack_rank(std::string_view pack) const noexcept {

        for (size_t i = 0; i < active_packs.size(); ++i)
            if (active_packs[i] == pack)
                return static_cast<i32>(i);
        return -1;
    }


    bool evaluation::cancelled() const noexcept {

        return request.cancel && request.cancel->load(std::memory_order_relaxed);
    }


    const attribute_desc* evaluation::describe(attribute_id id) const noexcept {

        if (auto it = attribute_descs.find(id); it != attribute_descs.end())
            return it->second;
        return nullptr;
    }


    const element_override* evaluation::override_for(const element_path& p) const noexcept {

        for (const element_override& o : data.overrides)
            if (o.path == p)
                return &o;
        return nullptr;
    }


    bool evaluation::hidden(const element& e) const noexcept {

        if (const element_override* o = override_for(e.path); o && o->hidden)
            return true;
        if (const attribute_value* v = e.attributes.find(attr_hidden()))
            if (const bool* b = std::get_if<bool>(v))
                return *b;
        return false;
    }


    void evaluation::diagnose(severity s, const element_path& on, std::string_view source, std::string message) {

        diagnostics.push_back({ s, on, std::string(source), std::move(message) });
    }


    void evaluation::resolve_active() {

        // ---- active packs: core, then the construct's list with what it depends on / extends (dependencies first) ------
        active_packs.push_back("core");
        std::vector<std::string> wanted;
        if (data.packs.empty()) {
            for (const auto& p : snap.packs)
                wanted.push_back(p.desc->name);
        } else {
            wanted = data.packs;
        }
        std::function<void(const std::string&, u32)> activate = [&](const std::string& name, u32 depth) {
            if (pack_active(name) || depth > 32)
                return;
            const pack_desc* p = snap.find_pack(name);
            if (!p) {
                diagnose(severity::error, {}, "packs", std::format("pack [{}] is not loaded", name));
                return;
            }
            for (const std::string& d : p->depends_on)
                activate(d, depth + 1);
            for (const std::string& d : p->extends)
                activate(d, depth + 1);
            active_packs.push_back(name);
        };
        for (const std::string& name : wanted)
            activate(name, 0);

        // ---- attribute descriptors of active schemas (later packs override earlier) ------------------------------------
        for (const auto& s : snap.tag_schemas)
            if (pack_active(s.pack))
                for (const attribute_desc& a : s.desc->attributes) {
                    auto [it, inserted] = attribute_descs.try_emplace(a.id, &a);
                    if (!inserted) {
                        const i32 old_rank = -1;
                        (void)old_rank;
                        it->second = &a;                    // registration order: later wins within the same rank
                    }
                }

        // ---- name conflicts: an active descriptor per name, the most derived pack wins -----------------------------------
        auto pick = [&]<typename D>(const std::vector<registered<D>>& all, std::vector<const registered<D>*>& out, std::string_view what) {
            std::map<std::string, std::vector<const registered<D>*>> by_name;
            for (const auto& r : all)
                if (pack_active(r.pack))
                    by_name[std::string(desc_name(any_desc(*r.desc)))].push_back(&r);
            for (auto& [name, list] : by_name) {
                const registered<D>* best = list.front();
                for (const registered<D>* r : list)
                    if (snap.extends(r->pack, best->pack) || (pack_rank(r->pack) > pack_rank(best->pack) && !snap.extends(best->pack, r->pack)))
                        best = r;
                for (const registered<D>* r : list)
                    if (r != best && !snap.extends(best->pack, r->pack) && r->pack != "core")
                        diagnose(severity::error, {}, "packs", std::format("{} [{}] is registered by packs [{}] and [{}], and neither extends the other; using [{}]",
                                                                             what, name, r->pack, best->pack, best->pack));
                out.push_back(best);
            }
            std::ranges::sort(out, {}, [](const registered<D>* r) { return r->order; });
        };
        pick(snap.passes, passes, "pass");
        pick(snap.productions, productions, "production");
        pick(snap.realizers, realizers, "realizer");
        pick(snap.finishers, finishers, "finisher");

        std::string cycle;
        if (!order_items(passes, [](const auto& r) -> std::string_view { return r.desc->name; },
                         [](const auto& r) -> const std::vector<std::string>& { return r.desc->after; },
                         [](const auto& r) -> const std::vector<std::string>& { return r.desc->before; },
                         [](const auto& r) { return r.order; }, cycle))
            diagnose(severity::error, {}, "passes", std::format("pass order has a cycle between: {}", cycle));
        cycle.clear();
        if (!order_items(finishers, [](const auto& r) -> std::string_view { return r.desc->name; },
                         [](const auto& r) -> const std::vector<std::string>& { return r.desc->after; },
                         [](const auto& r) -> const std::vector<std::string>& { return r.desc->before; },
                         [](const auto& r) { return r.order; }, cycle))
            diagnose(severity::error, {}, "finishers", std::format("finisher order has a cycle between: {}", cycle));

        // Realizers: priority, then the more specific pack, then later registration
        std::ranges::stable_sort(realizers, [&](const auto* a, const auto* b) {
            if (a->desc->priority != b->desc->priority)
                return a->desc->priority > b->desc->priority;
            if (pack_rank(a->pack) != pack_rank(b->pack))
                return pack_rank(a->pack) > pack_rank(b->pack);
            return a->order > b->order;
        });
        // Productions: priority, then registration
        std::ranges::stable_sort(productions, [](const auto* a, const auto* b) {
            if (a->desc->priority != b->desc->priority)
                return a->desc->priority > b->desc->priority;
            return a->order < b->order;
        });
    }


    element_handle evaluation::commit(element e, std::string_view stage) {

        if (by_path.contains(e.path)) {
            diagnose(severity::error, e.path, stage, std::format("element path [{}] exists already; the second one is dropped", e.path.to_string()));
            return {};
        }
        if (const element_override* o = override_for(e.path))
            for (const auto& entry : o->attributes.entries())
                e.attributes.set(entry.id, entry.value, entry.region);

        const u32 index = static_cast<u32>(elements.size());
        by_path.emplace(e.path, index);
        children_of.emplace_back();
        relations_of.emplace_back();
        analytic_volume.emplace_back();
        if (e.parent.valid() && e.parent.index < children_of.size())
            children_of[e.parent.index].push_back(index);
        elements.push_back(std::move(e));
        return { index };
    }


    void evaluation::add_relation(relation r) {

        if (!r.a.valid() || !r.b.valid() || r.a.index >= elements.size() || r.b.index >= elements.size())
            return;
        const u32 index = static_cast<u32>(relations.size());
        relations_of[r.a.index].push_back(index);
        if (r.b.index != r.a.index)
            relations_of[r.b.index].push_back(index);
        relations.push_back(std::move(r));
    }


    void evaluation::policies(std::string_view hook, const element* e, const region_selector& region, std::vector<const void*>& out,
                              std::vector<policy_scope>* scopes, composition& mode) const {

        struct candidate { const void* instance; policy_scope scope; i32 level; i32 rank; u64 order; };
        std::vector<candidate> found;

        auto consider = [&](const policy_desc& d, const policy_scope& scope, i32 rank, u64 order) {
            if (d.hook != hook || !d.instance)
                return;
            i32 level = static_cast<i32>(scope.level);
            switch (scope.level) {
                case scope_level::element:
                case scope_level::region:
                case scope_level::attribute:
                case scope_level::kind:
                    if (!e || !scope_matches(scope, *e, region))
                        return;
                    break;
                default:
                    break;
            }
            found.push_back({ d.instance.get(), scope, level, rank, order });
        };

        for (const auto& r : snap.policies) {
            if (!pack_active(r.pack))
                continue;
            policy_scope scope = r.desc->scope;
            if (r.pack == "core" && scope.level == scope_level::pack)
                scope.level = scope_level::global;
            consider(*r.desc, scope, pack_rank(r.pack), r.order);
        }
        // Construct bindings: a registered policy by name, at its own scope or at construct level
        for (size_t i = 0; i < data.policies.size(); ++i) {
            const policy_binding& b = data.policies[i];
            for (const auto& r : snap.policies)
                if (r.desc->name == b.policy && pack_active(r.pack)) {
                    policy_scope scope = b.scope ? *b.scope : policy_scope{ .level = scope_level::construct };
                    consider(*r.desc, scope, 1000, std::numeric_limits<u64>::max() - i);
                    break;
                }
        }

        std::ranges::stable_sort(found, [](const candidate& a, const candidate& b) {
            if (a.level != b.level)
                return a.level < b.level;
            if (a.rank != b.rank)
                return a.rank > b.rank;
            return a.order > b.order;
        });
        for (const candidate& c : found) {
            out.push_back(c.instance);
            if (scopes)
                scopes->push_back(c.scope);
        }

        // Composition: the construct's choice, else the most specific active pack's override, else the hook default
        for (const auto& [h, m] : data.compositions)
            if (h == hook) {
                mode = m;
                return;
            }
        i32 best_rank = -1;
        for (const auto& r : snap.compositions)
            if (r.desc->hook == hook && pack_rank(r.pack) > best_rank) {
                best_rank = pack_rank(r.pack);
                mode = r.desc->mode;
            }
    }


    const i_propagation* evaluation::mode_impl(name_id mode) const {

        const registered<propagation_mode_desc>* best = nullptr;
        for (const auto& r : snap.propagation_modes)
            if (pack_active(r.pack) && intern(r.desc->name) == mode && (!best || pack_rank(r.pack) >= pack_rank(best->pack)))
                best = &r;
        return best ? best->desc->impl.get() : nullptr;
    }


    const propagator& evaluation::propagator_for(const element& e) const {

        std::scoped_lock lock(m_propagator_mutex);
        u32 key = ~0u;
        if (auto it = by_path.find(e.path); it != by_path.end())
            key = it->second;
        auto& slot = m_propagators[key];
        if (key == ~0u || !slot)                                // uncommitted elements (guides in Interpret): fresh each time
            slot = std::make_unique<policy_propagator>(*this, e);
        return *slot;
    }


    u64 evaluation::seed_of(const element& e, u64 salt) const {

        std::vector<const void*> raw;
        composition mode = seed_hook::default_composition;
        policies(seed_hook::name, &e, whole_element{}, raw, nullptr, mode);
        if (!raw.empty())
            return static_cast<const i_seed*>(raw.front())->seed(e.path, construct_seed, salt);
        return hash_combine(hash_combine(e.path.hash(), construct_seed), salt);
    }


    attribute_value evaluation::parameter(std::string_view name) const {

        for (const exposed_parameter& p : request.parameters)
            if (p.name == name)
                return p.value;
        for (const exposed_parameter& p : data.parameters)
            if (p.name == name)
                return p.value;
        return {};
    }

    // Interpret ---------------------------------------------------------------------------------------------------------

    void evaluation::interpret_guide(const guide& g) {

        element e{};
        e.path = element_path::root(g.name.empty() ? "guide" : g.name, g.index);
        e.origin = origin_kind::authored;
        e.role = g.role;
        e.placement = g.placement;
        e.attributes = g.attributes;
        e.pass = intern("authored");
        e.source = "guide";

        // The propagator for the guide's edits sees the guide with its kind / role / attributes
        std::optional<volume_guide> analytic;
        std::visit([&](const auto& shape) {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, volume_guide>) {
                e.kind = kind_guide_volume();
                e.shape = shape_kind::volume;
                if (g.edits.empty())
                    analytic = shape;
            } else if constexpr (std::is_same_v<T, curve_guide>) {
                e.kind = kind_guide_curve();
                e.shape = shape_kind::curve;
            } else if constexpr (std::is_same_v<T, surface_guide>) {
                e.kind = kind_guide_surface();
                e.shape = shape_kind::surface;
            } else {
                e.kind = kind_guide_point();
                e.shape = shape_kind::point;
            }
        }, g.shape);

        const propagator& prop = propagator_for(e);

        auto apply_edits = [&](geometry::mesh m) {
            std::vector<std::string> warnings;
            m = apply_guide_edits(std::move(m), g.edits, prop, &warnings);
            for (std::string& w : warnings)
                diagnose(severity::warning, e.path, "interpret", std::move(w));
            return m;
        };

        std::visit([&](const auto& shape) {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, volume_guide>) {
                volume_shape v{};
                v.cage = apply_edits(geometry::make_primitive(shape.primitive, shape.params));
                v.side = shape.side;
                e.data = std::move(v);
            } else if constexpr (std::is_same_v<T, curve_guide>) {
                curve_shape c{ geometry::curve(shape.type, shape.points, shape.closed) };
                for (const auto& [id, values] : shape.point_attributes)
                    for (u32 i = 0; i < values.size(); ++i)
                        c.curve.set_point_attribute(id, i, values[i]);
                if (!g.edits.empty())
                    diagnose(severity::warning, e.path, "interpret", "edit steps on curve guides are not supported yet; ignored");
                e.data = std::move(c);
            } else if constexpr (std::is_same_v<T, surface_guide>) {
                geometry::primitive_params p{};
                p.size = glm::vec3(shape.size.x, 0.0f, shape.size.y);
                p.divisions = shape.divisions;
                e.data = surface_shape{ apply_edits(geometry::make_primitive(geometry::primitive_kind::plane, p)) };
            } else {
                e.data = point_shape{ shape.points };
            }
        }, g.shape);

        element mirrored{};
        const bool has_mirror = g.modifiers.mirror_plane.has_value();
        if (has_mirror) {
            mirrored = e;
            mirrored.path = e.path.child("mirror");
            mirrored.origin = origin_kind::derived;
            mirrored.depth = 1;
            const glm::vec4 plane = *g.modifiers.mirror_plane;
            const glm::vec3 n = glm::normalize(glm::vec3(plane));
            const f32 d = plane.w / glm::length(glm::vec3(plane));
            auto reflect = [&](const glm::vec3& p) { return p - 2.0f * (glm::dot(n, p) + d) * n; };
            std::visit([&](auto& shape) {
                using T = std::decay_t<decltype(shape)>;
                if constexpr (std::is_same_v<T, volume_shape>)        shape.cage = geometry::mirror(shape.cage, plane, prop);
                else if constexpr (std::is_same_v<T, surface_shape>)  shape.surface = geometry::mirror(shape.surface, plane, prop);
                else if constexpr (std::is_same_v<T, point_shape>)    { for (glm::vec3& p : shape.points) p = reflect(p); }
                else if constexpr (std::is_same_v<T, curve_shape>) {
                    std::vector<glm::vec3> pts(shape.curve.points().begin(), shape.curve.points().end());
                    for (glm::vec3& p : pts) p = reflect(p);
                    geometry::curve c(shape.curve.type(), std::move(pts), shape.curve.closed());
                    for (const geometry::column& col : shape.curve.point_columns())
                        for (u32 i = 0; i < col.values.size(); ++i)
                            c.set_point_attribute(col.id, i, col.values[i]);
                    shape.curve = std::move(c);
                }
            }, mirrored.data);
        }

        const element_handle h = commit(std::move(e), "interpret");
        if (h.valid() && analytic)
            analytic_volume[h.index] = analytic;
        if (h.valid() && has_mirror) {
            mirrored.parent = h;
            const element_handle mh = commit(std::move(mirrored), "interpret");
            if (mh.valid())
                add_relation({ relation_mirror_of(), relation_mirror_of(), mh, h, {} });
        }
    }


    void evaluation::interpret() {

        if (interpreted)
            return;
        interpreted = true;
        // Guides in path order: the result does not depend on the order they were authored in
        std::vector<const guide*> sorted;
        for (const guide& g : data.guides)
            sorted.push_back(&g);
        std::ranges::stable_sort(sorted, [](const guide* a, const guide* b) {
            return element_path::root(a->name.empty() ? "guide" : a->name, a->index) < element_path::root(b->name.empty() ? "guide" : b->name, b->index);
        });
        for (const guide* g : sorted)
            interpret_guide(*g);
    }

    // Expand --------------------------------------------------------------------------------------------------------------

    void evaluation::commit_staged(expand_context& ec, std::string_view stage, std::vector<u32>& committed) {

        std::vector<element_handle> slot_handles(ec.staged_elements().size());
        for (size_t s = 0; s < ec.staged_elements().size(); ++s) {
            auto& staged = ec.staged_elements()[s];
            if (staged.value.kind.empty())
                continue;
            const element_handle h = commit(std::move(staged.value), stage);
            slot_handles[s] = h;
            if (h.valid()) {
                committed.push_back(h.index);
                if (elements[h.index].parent.valid())
                    add_relation({ relation_child_of(), relation_child_of(), h, elements[h.index].parent, {} });
            }
        }
        auto resolve = [&](element_handle h) -> element_handle {
            if (h.valid() && (h.index & expand_context::STAGED_BIT)) {
                const u32 slot = h.index & ~expand_context::STAGED_BIT;
                return slot < slot_handles.size() ? slot_handles[slot] : element_handle{};
            }
            return h;
        };
        for (auto& r : ec.staged_relations()) {
            r.value.a = resolve(r.value.a);
            r.value.b = resolve(r.value.b);
            if (r.value.a.valid() && r.value.b.valid())
                add_relation(std::move(r.value));
        }
        for (diagnostic& d : ec.diagnostics())
            diagnostics.push_back(std::move(d));
    }


    void evaluation::run_pass(std::string_view pass_name) {

        const registered<pass_desc>* pass = nullptr;
        for (const auto* p : passes)
            if (p->desc->name == pass_name)
                pass = p;
        if (!pass) {
            diagnose(severity::error, {}, "expand", std::format("pass [{}] is not registered", pass_name));
            return;
        }
        const pass_id pid = intern(pass_name);
        if (std::ranges::find(request.lod.dropped_passes, pid) != request.lod.dropped_passes.end())
            return;

        std::vector<const registered<production_desc>*> prods;
        for (const auto* p : productions)
            if (p->desc->pass == pass_name)
                prods.push_back(p);
        if (prods.empty())
            return;

        const context ctx(*this);
        std::vector<u32> candidates(elements.size());
        for (u32 i = 0; i < elements.size(); ++i)
            candidates[i] = i;
        bool depth_warned = false;

        for (u32 round = 0; round <= pass->desc->max_depth + 1 && !candidates.empty(); ++round) {
            if (cancelled())
                return;
            std::ranges::sort(candidates, [&](u32 a, u32 b) { return elements[a].path < elements[b].path; });

            std::vector<std::unique_ptr<expand_context>> contexts;
            std::unordered_set<u32> claimed;                       // taken by an exclusive production
            for (const auto* p : prods) {
                std::vector<const element*> matches;
                for (const u32 i : candidates) {
                    const element& el = elements[i];
                    if (claimed.contains(i) || !p->desc->match.matches(el, ctx))
                        continue;
                    if (el.depth >= pass->desc->max_depth) {
                        if (!depth_warned)
                            diagnose(severity::warning, el.path, p->desc->name, std::format("depth limit {} of pass [{}] reached; not expanded further",
                                                                                             pass->desc->max_depth, pass_name));
                        depth_warned = true;
                        continue;
                    }
                    matches.push_back(&el);
                    if (p->desc->exclusive)
                        claimed.insert(i);
                }
                if (matches.empty() || !p->desc->create)
                    continue;

                auto instance = p->desc->create();
                if (p->desc->mode == expand_mode::batch) {
                    auto ec = std::make_unique<expand_context>(*this, p->desc->name, pid);
                    try {
                        instance->expand_all(*ec, matches);
                    } catch (const std::exception& ex) {
                        diagnose(severity::error, {}, p->desc->name, std::format("exception: {}", ex.what()));
                        continue;
                    }
                    contexts.push_back(std::move(ec));
                } else {
                    for (const element* m : matches) {
                        auto ec = std::make_unique<expand_context>(*this, p->desc->name, pid);
                        try {
                            instance->expand(*ec, *m);
                        } catch (const std::exception& ex) {
                            diagnose(severity::error, m->path, p->desc->name, std::format("exception: {}", ex.what()));
                            continue;
                        }
                        contexts.push_back(std::move(ec));
                    }
                }
                // Emitted kinds must be declared
                if (!p->desc->emits.empty())
                    for (const auto& ec : contexts)
                        for (auto& s : ec->staged_elements())
                            if (std::ranges::find(p->desc->emits, s.value.kind) == p->desc->emits.end() && !s.value.kind.empty()) {
                                diagnose(severity::error, s.value.path, p->desc->name,
                                         std::format("emits kind [{}], which it does not declare", s.value.kind.str()));
                                s.value.kind = {};             // marks it dropped
                            }
            }

            // ---- commit in processing order ----------------------------------------------------------------------------
            std::vector<u32> committed;
            for (const auto& ec : contexts)
                commit_staged(*ec, "expand", committed);
            candidates = std::move(committed);
        }
    }


    void evaluation::run_all_passes() {

        for (const auto* p : passes)
            run_pass(p->desc->name);
    }

    // Stages (generators) ---------------------------------------------------------------------------------------------------

    void evaluation::run_generators() {

        for (const generator_instance& inst : data.generators) {
            stage_stats stats{};
            stats.id = inst.id;
            stats.label = inst.label;
            stats.type = inst.type;
            if (cancelled())
                break;
            const registered<generator_desc>* reg = snap.find_generator(inst.type);
            if (!inst.enabled || inst.id == 0) {
                stage_results.push_back(std::move(stats));
                continue;
            }
            if (!reg || !pack_active(reg->pack) || !reg->desc->create) {
                diagnose(severity::warning, {}, inst.label, std::format("stage type [{}] is not available (is its plugin loaded?)", inst.type));
                stage_results.push_back(std::move(stats));
                continue;
            }
            const generator_desc& desc = *reg->desc;

            // Inputs: elements of the input stages; guides: authored elements with one of the type's roles
            std::vector<const element*> inputs, guides;
            for (const element& e : elements) {
                if (e.stage != 0 && std::ranges::find(inst.inputs, e.stage) != inst.inputs.end() && !hidden(e))
                    inputs.push_back(&e);
                if (e.origin == origin_kind::authored && !desc.guide_roles.empty()
                    && std::ranges::find(desc.guide_roles, e.role) != desc.guide_roles.end())
                    guides.push_back(&e);
            }
            auto by_path_order = [](const element* a, const element* b) { return a->path < b->path; };
            std::ranges::sort(inputs, by_path_order);
            std::ranges::sort(guides, by_path_order);
            if (desc.takes_input && inst.inputs.empty())
                diagnose(severity::warning, {}, inst.label, "this stage grows on another stage, but none is chosen as its input");

            const auto start = clock::now();
            generator_context gc(*this, inst, desc, std::move(inputs), std::move(guides));
            try {
                auto instance = desc.create();
                instance->run(gc);
                stats.ran = true;
            } catch (const std::exception& ex) {
                diagnose(severity::error, {}, inst.label, std::format("exception: {}", ex.what()));
            }
            if (!desc.emits.empty())
                for (auto& st : gc.expansion().staged_elements())
                    if (!st.value.kind.empty() && std::ranges::find(desc.emits, st.value.kind) == desc.emits.end()) {
                        diagnose(severity::error, st.value.path, inst.label, std::format("emits kind [{}], which its type does not declare",
                                                                                          st.value.kind.str()));
                        st.value.kind = {};
                    }
            for (auto& st : gc.expansion().staged_elements()) {
                st.value.stage = inst.id;
                st.value.source = inst.label;
            }
            std::vector<u32> committed;
            commit_staged(gc.expansion(), inst.label, committed);
            stats.milliseconds = ms_since(start);
            stage_results.push_back(std::move(stats));
        }
    }

    // Realize / finish / outputs ------------------------------------------------------------------------------------------

    void evaluation::realize_all() {

        const context ctx(*this);
        std::vector<u32> order(elements.size());
        for (u32 i = 0; i < elements.size(); ++i)
            order[i] = i;
        std::ranges::sort(order, [&](u32 a, u32 b) { return elements[a].path < elements[b].path; });

        std::unordered_map<std::string, std::unique_ptr<realizer>> instances;
        for (const u32 i : order) {
            if (cancelled())
                return;
            const element& e = elements[i];
            if (hidden(e))
                continue;
            for (const auto* r : realizers) {
                if (!r->desc->match.matches(e, ctx))
                    continue;
                auto& inst = instances[r->desc->name];
                if (!inst && r->desc->create)
                    inst = r->desc->create();
                if (!inst)
                    break;
                realize_context rc(*this, e, r->desc->name);
                try {
                    inst->realize(rc, e);
                } catch (const std::exception& ex) {
                    diagnose(severity::error, e.path, r->desc->name, std::format("exception: {}", ex.what()));
                }
                for (product& p : rc.products()) {
                    p.stage = e.stage;
                    products.push_back(std::move(p));
                }
                for (diagnostic& d : rc.diagnostics())
                    diagnostics.push_back(std::move(d));
                break;
            }
        }
    }


    void evaluation::finish_all() {

        for (const auto* f : finishers) {
            if (cancelled())
                return;
            if (!f->desc->create)
                continue;
            auto inst = f->desc->create();
            finish_context fc(*this, products);
            try {
                inst->finish(fc);
            } catch (const std::exception& ex) {
                diagnose(severity::error, {}, f->desc->name, std::format("exception: {}", ex.what()));
            }
            for (diagnostic& d : fc.diagnostics()) {
                d.source = f->desc->name;
                diagnostics.push_back(std::move(d));
            }
        }
    }


    void evaluation::build_outputs() {

        // ---- which outputs: every pack-level (or more specific) policy adds; the core's only when nobody else did -------
        std::vector<const void*> raw;
        std::vector<policy_scope> scopes;
        composition mode = outputs_hook::default_composition;
        policies(outputs_hook::name, nullptr, whole_element{}, raw, &scopes, mode);
        std::vector<output_desc> descs;
        const output_query query{ active_packs };
        bool any_specific = false;
        for (size_t i = 0; i < raw.size(); ++i)
            if (scopes[i].level != scope_level::global) {
                static_cast<const i_outputs*>(raw[i])->outputs(query, descs);
                any_specific = any_specific || !descs.empty();
                if (mode == composition::first_match && !descs.empty())
                    break;
            }
        if (!any_specific)
            for (size_t i = 0; i < raw.size(); ++i)
                if (scopes[i].level == scope_level::global)
                    static_cast<const i_outputs*>(raw[i])->outputs(query, descs);
        // Unique names: the first (most specific) wins
        std::vector<output_desc> unique;
        for (output_desc& d : descs)
            if (std::ranges::none_of(unique, [&](const output_desc& u) { return u.name == d.name; }))
                unique.push_back(std::move(d));
        if (!request.outputs.empty())
            std::erase_if(unique, [&](const output_desc& d) { return std::ranges::find(request.outputs, d.name) == request.outputs.end(); });

        // ---- build each ---------------------------------------------------------------------------------------------------
        for (output_desc& desc : unique) {
            if (cancelled())
                return;
            output_result result{};
            result.desc = desc;
            if (desc.topo == topology::watertight)
                diagnose(severity::warning, {}, "outputs", std::format("output [{}]: watertight needs a boolean backend, which is not "
                                                                         "implemented yet; built as merged shells", desc.name));

            std::vector<const product*> selected;
            for (const product& p : products)
                if (desc.include.matches(p))
                    selected.push_back(&p);

            auto mesh_triangles = [&](const product& p) {
                const mesh_product& mp = std::get<mesh_product>(p.data);
                return transformed_triangles(mp.mesh, p.placement.matrix_relative_to(data.origin), mp.smooth_angle_degrees);
            };

            switch (desc.topo) {
                case topology::as_realized:
                    for (const product* p : selected)
                        if (std::holds_alternative<mesh_product>(p->data)) {
                            output_part part{ p->element, {} };
                            add_by_material(part.groups, mesh_triangles(*p));
                            result.parts.push_back(std::move(part));
                        }
                    break;
                case topology::shells_per_element: {
                    std::map<element_path, output_part> by_element;
                    for (const product* p : selected)
                        if (std::holds_alternative<mesh_product>(p->data)) {
                            output_part& part = by_element[p->element];
                            part.element = p->element;
                            add_by_material(part.groups, mesh_triangles(*p));
                        }
                    for (auto& [path, part] : by_element)
                        result.parts.push_back(std::move(part));
                    break;
                }
                case topology::merged_shells:
                case topology::watertight:
                case topology::open_surface: {
                    output_part part{};
                    for (const product* p : selected)
                        if (std::holds_alternative<mesh_product>(p->data))
                            add_by_material(part.groups, mesh_triangles(*p));
                    if (!part.groups.empty())
                        result.parts.push_back(std::move(part));
                    break;
                }
            }

            for (const product* p : selected) {
                if (const auto* ip = std::get_if<instance_product>(&p->data))
                    for (const frame& f : ip->placements)
                        result.instances.push_back({ ip->asset, p->placement.compose(f).matrix_relative_to(data.origin), p->element });
                else if (const auto* mp = std::get_if<marker_product>(&p->data))
                    result.markers.push_back({ p->name, p->placement.compose(mp->at), p->element, p->tags });
            }
            outputs.push_back(std::move(result));
        }
    }


    void evaluation::run(evaluation_result& result) {

        // ---- stage list: inserts ordered by after / before, then replacements, then wrappers ----------------------------
        std::vector<const registered<stage_desc>*> inserts, replaces, wraps;
        {
            std::map<std::string, std::vector<const registered<stage_desc>*>> by_name;
            for (const auto& r : snap.stages)
                if (pack_active(r.pack))
                    by_name[r.desc->name].push_back(&r);
            for (auto& [name, list] : by_name) {
                const registered<stage_desc>* best = list.front();
                for (const auto* r : list)
                    if (pack_rank(r->pack) > pack_rank(best->pack))
                        best = r;
                switch (best->desc->mode) {
                    case stage_mode::insert:  inserts.push_back(best);  break;
                    case stage_mode::replace: replaces.push_back(best); break;
                    case stage_mode::wrap:    wraps.push_back(best);    break;
                }
            }
            std::ranges::sort(inserts, {}, [](const auto* r) { return r->order; });
            std::string cycle;
            if (!order_items(inserts, [](const auto& r) -> std::string_view { return r.desc->name; },
                             [](const auto& r) -> const std::vector<std::string>& { return r.desc->after; },
                             [](const auto& r) -> const std::vector<std::string>& { return r.desc->before; },
                             [](const auto& r) { return r.order; }, cycle))
                diagnose(severity::error, {}, "stages", std::format("stage order has a cycle between: {}", cycle));
        }

        struct slot { std::string name; std::unique_ptr<stage> instance; std::vector<std::unique_ptr<stage>> wrappers; };
        std::vector<slot> slots;
        for (const auto* r : inserts) {
            const registered<stage_desc>* chosen = r;
            for (const auto* rep : replaces)                       // the most specific replacement wins
                if (rep->desc->target == r->desc->name && (chosen == r || pack_rank(rep->pack) > pack_rank(chosen->pack)))
                    chosen = rep;
            slot s{ r->desc->name, chosen->desc->create ? chosen->desc->create() : nullptr, {} };
            stage_order.push_back(chosen == r ? r->desc->name : std::format("{} (replaced by {})", r->desc->name, chosen->desc->name));
            // Wrappers, least specific innermost
            std::vector<const registered<stage_desc>*> mine;
            for (const auto* w : wraps)
                if (w->desc->target == r->desc->name)
                    mine.push_back(w);
            std::ranges::sort(mine, [&](const auto* a, const auto* b) { return pack_rank(a->pack) < pack_rank(b->pack); });
            stage* inner = s.instance.get();
            for (const auto* w : mine) {
                auto wi = w->desc->create ? w->desc->create() : nullptr;
                if (auto* wrapper = dynamic_cast<stage_wrapper*>(wi.get())) {
                    wrapper->set_next(inner);
                    inner = wrapper;
                    stage_order.back() += std::format(" (wrapped by {})", w->desc->name);
                    s.wrappers.push_back(std::move(wi));
                } else {
                    diagnose(severity::error, {}, "stages", std::format("stage [{}] wraps [{}] but does not derive from stage_wrapper",
                                                                         w->desc->name, r->desc->name));
                }
            }
            slots.push_back(std::move(s));
        }
        for (const auto* rep : replaces)
            if (std::ranges::none_of(inserts, [&](const auto* r) { return r->desc->name == rep->desc->target; }))
                diagnose(severity::error, {}, "stages", std::format("stage [{}] replaces [{}], which does not exist", rep->desc->name, rep->desc->target));

        // ---- run --------------------------------------------------------------------------------------------------------
        stage_context sc(*this);
        for (slot& s : slots) {
            if (cancelled())
                break;
            stage* top = s.wrappers.empty() ? s.instance.get() : s.wrappers.back().get();
            if (!top)
                continue;
            const auto start = clock::now();
            try {
                top->run(sc);
            } catch (const std::exception& ex) {
                diagnose(severity::error, {}, s.name, std::format("exception: {}", ex.what()));
            }
            timings.push_back({ s.name, ms_since(start) });
            if (!request.stop_after.empty() && s.name == request.stop_after) {
                result.stopped_after = s.name;
                break;
            }
        }

        // ---- result -----------------------------------------------------------------------------------------------------
        result.cancelled = cancelled();
        result.outputs = std::move(outputs);
        result.diagnostics = std::move(diagnostics);
        result.timings = std::move(timings);
        result.stage_order = std::move(stage_order);
        result.registry_generation = snap.generation;
        // Per stage counts (elements, products, triangles)
        std::unordered_map<u32, size_t> stage_index;
        for (size_t i = 0; i < stage_results.size(); ++i)
            stage_index[stage_results[i].id] = i;
        for (const element& e : elements)
            if (auto it = stage_index.find(e.stage); it != stage_index.end())
                ++stage_results[it->second].elements;
        for (const product& p : products)
            if (auto it = stage_index.find(p.stage); it != stage_index.end()) {
                stage_stats& st = stage_results[it->second];
                ++st.products;
                if (const auto* mp = std::get_if<mesh_product>(&p.data)) {
                    std::vector<u32> vs;
                    for (u32 f = 0; f < mp->mesh.face_count(); ++f) {
                        mp->mesh.face_vertices(f, vs);
                        st.triangles += vs.size() >= 3 ? static_cast<u32>(vs.size() - 2) : 0u;
                    }
                }
            }
        result.stages = std::move(stage_results);

        for (const element& e : elements) {
            element_summary summary{ e.path, e.kind, e.origin, e.source, e.depth, e.stage };
            if (request.editor_data) {
                summary.parent = e.parent.valid() && e.parent.index < elements.size() ? elements[e.parent.index].path : element_path{};
                summary.role = e.role;
                summary.shape = e.shape;
                summary.attributes = e.attributes;
                if (const auto* c = std::get_if<curve_shape>(&e.data)) {
                    for (const geometry::curve_sample& smp : c->curve.samples())
                        summary.skeleton.push_back(glm::vec3(e.placement.to_world(smp.position) - data.origin));
                } else if (const auto* pts = std::get_if<point_shape>(&e.data); pts && !pts->points.empty()) {
                    for (const glm::vec3& p : pts->points)
                        summary.skeleton.push_back(glm::vec3(e.placement.to_world(p) - data.origin));
                } else {
                    summary.skeleton.push_back(glm::vec3(e.placement.origin - data.origin));
                }
            }
            result.elements.push_back(std::move(summary));
        }
        std::ranges::sort(result.elements, {}, &element_summary::path);
        if (request.editor_data) {
            result.products = std::move(products);
            for (const relation& r : relations)
                if (r.kind != relation_child_of() && r.a.valid() && r.b.valid())
                    result.relations.push_back({ r.kind, elements[r.a.index].path, elements[r.b.index].path });
        }
    }

    // context ---------------------------------------------------------------------------------------------------------

    const element& context::get(element_handle h) const { return m_eval->elements.at(h.index); }


    std::optional<element_handle> context::find(const element_path& p) const {

        if (auto it = m_eval->by_path.find(p); it != m_eval->by_path.end())
            return element_handle{ it->second };
        return std::nullopt;
    }


    std::vector<element_handle> context::elements(const matcher& m) const {

        std::vector<element_handle> out;
        for (u32 i = 0; i < m_eval->elements.size(); ++i)
            if (m.matches(m_eval->elements[i], *this))
                out.push_back({ i });
        std::ranges::sort(out, [&](element_handle a, element_handle b) { return m_eval->elements[a.index].path < m_eval->elements[b.index].path; });
        return out;
    }


    std::vector<element_handle> context::children(element_handle h) const {

        std::vector<element_handle> out;
        if (h.index < m_eval->children_of.size())
            for (const u32 c : m_eval->children_of[h.index])
                out.push_back({ c });
        return out;
    }


    std::vector<const relation*> context::relations(element_handle h, relation_id kind) const {

        std::vector<const relation*> out;
        if (h.index < m_eval->relations_of.size())
            for (const u32 r : m_eval->relations_of[h.index])
                if (kind.empty() || m_eval->relations[r].kind == kind)
                    out.push_back(&m_eval->relations[r]);
        return out;
    }


    u32 context::element_count() const noexcept { return static_cast<u32>(m_eval->elements.size()); }


    f64 context::distance(const glm::dvec3& p, const matcher& filter) const {

        f64 best = std::numeric_limits<f64>::max();
        for (u32 i = 0; i < m_eval->elements.size(); ++i) {
            const element& e = m_eval->elements[i];
            const auto* v = std::get_if<volume_shape>(&e.data);
            if (!v || !filter.matches(e, *this))
                continue;
            // world -> element local
            const glm::vec3 local = glm::inverse(e.placement.rotation) * glm::vec3(p - e.placement.origin) / e.placement.scale;
            const f32 scale = std::min(e.placement.scale.x, std::min(e.placement.scale.y, e.placement.scale.z));
            f64 d = m_eval->analytic_volume[i] ? primitive_distance(*m_eval->analytic_volume[i], local) : mesh_box_distance(v->cage, local);
            d *= static_cast<f64>(scale);
            if (v->side == orientation::cavity)
                d = -d;
            best = std::min(best, d);
        }
        return best;
    }


    geometry::curve_sample context::sample_curve(element_handle h, f64 t) const {

        if (const auto* c = std::get_if<curve_shape>(&get(h).data))
            return c->curve.sample(t);
        return {};
    }


    attribute_value context::attribute(const element& e, attribute_id id, const region_selector& where) const {

        if (const attribute_value* v = e.attributes.find(id, where))
            return *v;
        if (const attribute_desc* d = m_eval->describe(id))
            return d->default_value;
        return {};
    }


    void context::policies(std::string_view hook_name, const element* e, const region_selector& where, std::vector<const void*>& out,
                           std::vector<policy_scope>* scopes, composition& mode) const {
        m_eval->policies(hook_name, e, where, out, scopes, mode);
    }


    attribute_value context::parameter(std::string_view name, attribute_value fallback) const {

        attribute_value v = m_eval->parameter(name);
        return v.index() ? v : fallback;
    }


    u64 context::construct_seed() const noexcept { return m_eval->construct_seed; }
    u64 context::seed_of(const element& e, u64 salt) const { return m_eval->seed_of(e, salt); }
    const lod_request& context::lod() const noexcept { return m_eval->request.lod; }
    bool context::cancelled() const noexcept { return m_eval->cancelled(); }
    const propagator& context::propagation_for(const element& e) const { return m_eval->propagator_for(e); }

    // expand_context / element_builder ------------------------------------------------------------------------------------

    element_builder expand_context::emit(const element& parent, std::string_view name, i64 index, kind_id kind) {

        staged_element s{};
        element& e = s.value;
        e.path = parent.path.child(name, index);
        e.kind = kind;
        e.origin = origin_kind::derived;
        e.placement = parent.placement;
        e.pass = m_pass;
        e.depth = parent.depth + 1;
        e.source = m_producer;
        e.stage = parent.stage;
        if (auto h = m_query.find(parent.path))
            e.parent = *h;

        // Inherit the parent's whole-element attributes through derive events (the parent's propagation policies decide)
        const propagator& prop = m_query.propagation_for(parent);
        for (const attribute_set::entry& entry : parent.attributes.entries()) {
            if (!std::holds_alternative<whole_element>(entry.region))
                continue;
            const attribute_desc& desc = prop.describe(entry.id, entry.value);
            const lineage_source src{ 0, 1.0f };
            lineage_event ev{};
            ev.kind = lineage_kind::derive;
            ev.domain = attribute_domain::element;
            ev.sources = std::span(&src, 1);
            ev.depth_delta = 1;
            attribute_io io(std::span(&entry.value, 1), ev.sources);
            prop.apply(desc, ev, io);
            if (io.written())
                e.attributes.set(entry.id, io.result());
        }

        m_elements.push_back(std::move(s));
        return element_builder(*this, static_cast<u32>(m_elements.size() - 1));
    }


    element_builder expand_context::emit_at(const element_path& path, kind_id kind) {

        staged_element s{};
        element& e = s.value;
        e.path = path;
        e.kind = kind;
        e.origin = origin_kind::derived;
        e.pass = m_pass;
        e.depth = 0;
        e.source = m_producer;
        m_elements.push_back(std::move(s));
        return element_builder(*this, static_cast<u32>(m_elements.size() - 1));
    }


    void expand_context::relate(relation_id kind, name_id type, element_handle a, element_handle b, attribute_set params) {

        m_relations.push_back({ relation{ kind, type, a, b, std::move(params) } });
    }


    void expand_context::diagnose(severity s, const element& on, std::string message) {

        m_diagnostics.push_back({ s, on.path, m_producer, std::move(message) });
    }


    element_builder& element_builder::placement(const frame& f) { staged().placement = f; return *this; }
    element_builder& element_builder::shape(shape_data d) {
        element& e = staged();
        e.data = std::move(d);
        e.shape = std::visit([](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, point_shape>)        return shape_kind::point;
            else if constexpr (std::is_same_v<T, frame_shape>)   return shape_kind::frame;
            else if constexpr (std::is_same_v<T, curve_shape>)   return shape_kind::curve;
            else if constexpr (std::is_same_v<T, surface_shape>) return shape_kind::surface;
            else if constexpr (std::is_same_v<T, volume_shape>)  return shape_kind::volume;
            else if constexpr (std::is_same_v<T, graph_shape>)   return shape_kind::graph;
            else                                                 return shape_kind::none;
        }, e.data);
        return *this;
    }
    element_builder& element_builder::role(role_id r) { staged().role = r; return *this; }
    element_builder& element_builder::set(attribute_id id, attribute_value value, const region_selector& where) {
        staged().attributes.set(id, std::move(value), where);
        return *this;
    }
    element_builder& element_builder::params_block(param_block block) { staged().params = std::move(block); return *this; }
    element_handle element_builder::handle() const noexcept { return { m_slot | expand_context::STAGED_BIT }; }
    element& element_builder::staged() const { return m_ctx->staged_elements()[m_slot].value; }

    // realize_context -----------------------------------------------------------------------------------------------------

    void realize_context::mesh(std::string_view name, geometry::mesh m, f32 smooth_angle_degrees) {

        m_products.push_back({ m_element->path, m_element->kind, std::string(name), m_element->placement, m_element->attributes,
                               mesh_product{ std::move(m), smooth_angle_degrees } });
    }


    void realize_context::instances(std::string_view name, std::string asset, std::vector<frame> placements) {

        m_products.push_back({ m_element->path, m_element->kind, std::string(name), m_element->placement, m_element->attributes,
                               instance_product{ std::move(asset), std::move(placements) } });
    }


    void realize_context::marker(std::string_view name, const frame& at, attribute_set tags) {

        attribute_set merged = m_element->attributes;
        for (const auto& e : tags.entries())
            merged.set(e.id, e.value, e.region);
        m_products.push_back({ m_element->path, m_element->kind, std::string(name), m_element->placement, std::move(merged), marker_product{ at } });
    }


    void realize_context::diagnose(severity s, std::string message) {

        m_diagnostics.push_back({ s, m_element->path, m_realizer, std::move(message) });
    }

    // stage_context -------------------------------------------------------------------------------------------------------

    context stage_context::query() const noexcept { return context(*m_eval); }
    void stage_context::run_interpret() { m_eval->interpret(); }
    void stage_context::run_productions(std::string_view pass) { m_eval->run_pass(pass); }
    void stage_context::run_generators() { m_eval->run_generators(); }
    void stage_context::run_all_passes() { m_eval->run_all_passes(); }
    void stage_context::run_realizers() { m_eval->realize_all(); }
    void stage_context::run_finishers() { m_eval->finish_all(); }
    void stage_context::build_outputs() { m_eval->build_outputs(); }
    element_handle stage_context::add_element(element e) { return m_eval->commit(std::move(e), "stage"); }
    void stage_context::add_relation(relation r) { m_eval->add_relation(std::move(r)); }
    std::vector<product>& stage_context::products() { return m_eval->products; }
    std::vector<output_result>& stage_context::outputs() { return m_eval->outputs; }
    bool stage_context::cancelled() const noexcept { return m_eval->cancelled(); }
    void stage_context::diagnose(severity s, std::string source, std::string message) { m_eval->diagnose(s, {}, source, std::move(message)); }

    // evaluate ------------------------------------------------------------------------------------------------------------

    evaluation_result evaluate(const registry_snapshot& snapshot, const evaluation_request& request) {

        evaluation_result result{};
        if (!request.data) {
            result.diagnostics.push_back({ severity::error, {}, "evaluate", "no construct data" });
            return result;
        }
        evaluation e(snapshot, request);
        e.run(result);
        return result;
    }


    // FUNCTION IMPLEMENTATION (products) ==============================================================================

    geometry::triangle_mesh product_triangles(const product& p, const glm::dvec3& origin) {

        const auto* mp = std::get_if<mesh_product>(&p.data);
        if (!mp)
            return {};
        return transformed_triangles(mp->mesh, p.placement.matrix_relative_to(origin), mp->smooth_angle_degrees);
    }


    output_part merge_products(std::span<const product* const> list, const glm::dvec3& origin) {

        output_part part{};
        for (const product* p : list)
            if (p && std::holds_alternative<mesh_product>(p->data))
                add_by_material(part.groups, product_triangles(*p, origin));
        return part;
    }

}
