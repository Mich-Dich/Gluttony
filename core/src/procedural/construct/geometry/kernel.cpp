#include "procedural/construct/geometry/kernel.h"

#include <unordered_set>



namespace GLT::construct::geometry {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        constexpr f32                                   TAU = 6.283185307179586f;

        u64 edge_key(u32 a, u32 b) noexcept { return a < b ? (static_cast<u64>(a) << 32) | b : (static_cast<u64>(b) << 32) | a; }


        // Polygon soup with per-corner uv and per-face group, built by the primitive generators
        struct poly_builder {

            std::vector<glm::vec3>                      positions{};
            std::vector<std::vector<u32>>               faces{};
            std::vector<std::vector<glm::vec2>>         uvs{};
            std::vector<name_id>                        groups{};

            u32 vertex(const glm::vec3& p) { positions.push_back(p); return static_cast<u32>(positions.size() - 1); }

            // Adds a face, reversing it if its normal does not point along [outward]
            void face(std::vector<u32> loop, std::vector<glm::vec2> uv, name_id group, const glm::vec3& outward) {
                glm::vec3 n(0.0f);
                for (size_t i = 0; i < loop.size(); ++i) {
                    const glm::vec3& a = positions[loop[i]];
                    const glm::vec3& b = positions[loop[(i + 1) % loop.size()]];
                    n += glm::cross(a, b);
                }
                if (glm::dot(n, outward) < 0.0f) {
                    std::reverse(loop.begin(), loop.end());
                    std::reverse(uv.begin(), uv.end());
                }
                faces.push_back(std::move(loop));
                uvs.push_back(std::move(uv));
                groups.push_back(group);
            }

            // [hard]: undirected edges (vertex pairs) to mark core.hard
            mesh build(const std::unordered_set<u64>& hard = {}) {
                mesh m = mesh::from_polygons(positions, faces);
                m.add_column(attribute_domain::face, attr_group(), name_id{});
                m.add_column(attribute_domain::corner, attr_uv(), glm::vec2(0.0f));
                m.add_column(attribute_domain::edge, attr_hard(), false);
                u32 h = 0;
                for (u32 f = 0; f < faces.size(); ++f) {
                    m.set(attribute_domain::face, attr_group(), f, groups[f]);
                    for (u32 c = 0; c < faces[f].size(); ++c, ++h) {
                        m.set(attribute_domain::corner, attr_uv(), h, uvs[f][c]);
                        const u32 a = faces[f][c], b = faces[f][(c + 1) % faces[f].size()];
                        if (hard.contains(edge_key(a, b)))
                            m.set(attribute_domain::edge, attr_hard(), h, true);
                    }
                }
                return m;
            }
        };


        mesh make_box(const primitive_params& p) {

            poly_builder b{};
            const glm::vec3 h = p.size * 0.5f;
            u32 v[8];
            for (u32 i = 0; i < 8; ++i)
                v[i] = b.vertex(glm::vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z));

            struct side { const char* name; glm::vec3 n; u32 c[4]; i32 u_axis, v_axis; };
            const side sides[6] = {
                { "+x", {  1, 0, 0 }, { 1, 3, 7, 5 }, 2, 1 }, { "-x", { -1, 0, 0 }, { 0, 4, 6, 2 }, 2, 1 },
                { "+y", { 0,  1, 0 }, { 2, 6, 7, 3 }, 0, 2 }, { "-y", { 0, -1, 0 }, { 0, 1, 5, 4 }, 0, 2 },
                { "+z", { 0, 0,  1 }, { 4, 5, 7, 6 }, 0, 1 }, { "-z", { 0, 0, -1 }, { 0, 2, 3, 1 }, 0, 1 },
            };
            std::unordered_set<u64> hard;
            for (const side& s : sides) {
                std::vector<u32> loop{ v[s.c[0]], v[s.c[1]], v[s.c[2]], v[s.c[3]] };
                std::vector<glm::vec2> uv;
                for (const u32 c : s.c) {
                    const glm::vec3& pos = b.positions[v[c]];
                    uv.emplace_back(pos[s.u_axis] / p.size[s.u_axis] + 0.5f, pos[s.v_axis] / p.size[s.v_axis] + 0.5f);
                }
                for (u32 i = 0; i < 4; ++i)
                    hard.insert(edge_key(loop[i], loop[(i + 1) % 4]));
                b.face(std::move(loop), std::move(uv), intern(s.name), s.n);
            }
            return b.build(hard);
        }


        mesh make_plane(const primitive_params& p) {

            poly_builder b{};
            const u32 nx = std::max(1u, p.divisions.x), nz = std::max(1u, p.divisions.y);
            std::vector<u32> grid((nx + 1) * (nz + 1));
            for (u32 z = 0; z <= nz; ++z)
                for (u32 x = 0; x <= nx; ++x)
                    grid[z * (nx + 1) + x] = b.vertex(glm::vec3((static_cast<f32>(x) / nx - 0.5f) * p.size.x, 0.0f,
                                                                (static_cast<f32>(z) / nz - 0.5f) * p.size.z));
            const name_id top = intern("top");
            for (u32 z = 0; z < nz; ++z)
                for (u32 x = 0; x < nx; ++x) {
                    const u32 i00 = z * (nx + 1) + x;
                    std::vector<u32> loop{ grid[i00], grid[i00 + nx + 1], grid[i00 + nx + 2], grid[i00 + 1] };
                    std::vector<glm::vec2> uv{
                        { static_cast<f32>(x) / nx, static_cast<f32>(z) / nz }, { static_cast<f32>(x) / nx, static_cast<f32>(z + 1) / nz },
                        { static_cast<f32>(x + 1) / nx, static_cast<f32>(z + 1) / nz }, { static_cast<f32>(x + 1) / nx, static_cast<f32>(z) / nz } };
                    b.face(std::move(loop), std::move(uv), top, glm::vec3(0, 1, 0));
                }
            return b.build();
        }


        mesh make_cylinder(const primitive_params& p) {

            poly_builder b{};
            const u32 n = std::max(3u, p.segments);
            const f32 hy = p.height * 0.5f;
            std::vector<u32> bottom(n), top(n);
            for (u32 i = 0; i < n; ++i) {
                const f32 a = TAU * static_cast<f32>(i) / static_cast<f32>(n);
                const glm::vec3 d(std::cos(a) * p.radius, 0.0f, std::sin(a) * p.radius);
                bottom[i] = b.vertex(d + glm::vec3(0, -hy, 0));
                top[i] = b.vertex(d + glm::vec3(0, hy, 0));
            }
            const name_id side = intern("side");
            for (u32 i = 0; i < n; ++i) {
                const u32 j = (i + 1) % n;
                const f32 u0 = static_cast<f32>(i) / n, u1 = static_cast<f32>(i + 1) / n;
                const f32 mid = TAU * (static_cast<f32>(i) + 0.5f) / static_cast<f32>(n);
                b.face({ bottom[i], top[i], top[j], bottom[j] }, { { u0, 0 }, { u0, 1 }, { u1, 1 }, { u1, 0 } }, side,
                       glm::vec3(std::cos(mid), 0.0f, std::sin(mid)));
            }
            std::unordered_set<u64> hard;
            if (p.caps) {
                std::vector<glm::vec2> uv_top, uv_bottom;
                for (u32 i = 0; i < n; ++i) {
                    const f32 a = TAU * static_cast<f32>(i) / static_cast<f32>(n);
                    uv_top.emplace_back(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
                    hard.insert(edge_key(top[i], top[(i + 1) % n]));
                    hard.insert(edge_key(bottom[i], bottom[(i + 1) % n]));
                }
                uv_bottom = uv_top;
                b.face(top, uv_top, intern("cap_top"), glm::vec3(0, 1, 0));
                b.face(bottom, uv_bottom, intern("cap_bottom"), glm::vec3(0, -1, 0));
            }
            return b.build(hard);
        }


        // Rings top to bottom: (y, radius). Poles at [top_y] / [bottom_y]
        mesh make_latitude_mesh(const std::vector<std::pair<f32, f32>>& rings, f32 top_y, f32 bottom_y, u32 segments) {

            poly_builder b{};
            const u32 n = std::max(3u, segments);
            const name_id side = intern("side");
            const u32 north = b.vertex(glm::vec3(0, top_y, 0));
            std::vector<std::vector<u32>> ring_vertices;
            for (const auto& [y, r] : rings) {
                std::vector<u32> ring(n);
                for (u32 i = 0; i < n; ++i) {
                    const f32 a = TAU * static_cast<f32>(i) / static_cast<f32>(n);
                    ring[i] = b.vertex(glm::vec3(std::cos(a) * r, y, std::sin(a) * r));
                }
                ring_vertices.push_back(std::move(ring));
            }
            const u32 south = b.vertex(glm::vec3(0, bottom_y, 0));
            const f32 span = top_y - bottom_y;
            auto vcoord = [&](f32 y) { return span != 0.0f ? (y - bottom_y) / span : 0.5f; };

            auto outward = [&](const std::vector<u32>& loop) {
                glm::vec3 c(0.0f);
                for (const u32 v : loop) c += b.positions[v];
                c /= static_cast<f32>(loop.size());
                return glm::vec3(c.x, c.y - (top_y + bottom_y) * 0.5f, c.z);
            };

            for (u32 i = 0; i < n; ++i) {
                const u32 j = (i + 1) % n;
                const f32 u0 = static_cast<f32>(i) / n, u1 = static_cast<f32>(i + 1) / n;
                {
                    std::vector<u32> loop{ north, ring_vertices.front()[j], ring_vertices.front()[i] };
                    const glm::vec3 o = outward(loop);
                    b.face(std::move(loop), { { (u0 + u1) * 0.5f, 1.0f }, { u1, vcoord(rings.front().first) }, { u0, vcoord(rings.front().first) } }, side, o);
                }
                for (size_t r = 0; r + 1 < ring_vertices.size(); ++r) {
                    std::vector<u32> loop{ ring_vertices[r][i], ring_vertices[r][j], ring_vertices[r + 1][j], ring_vertices[r + 1][i] };
                    const glm::vec3 o = outward(loop);
                    const f32 va = vcoord(rings[r].first), vb = vcoord(rings[r + 1].first);
                    b.face(std::move(loop), { { u0, va }, { u1, va }, { u1, vb }, { u0, vb } }, side, o);
                }
                {
                    std::vector<u32> loop{ south, ring_vertices.back()[i], ring_vertices.back()[j] };
                    const glm::vec3 o = outward(loop);
                    b.face(std::move(loop), { { (u0 + u1) * 0.5f, 0.0f }, { u0, vcoord(rings.back().first) }, { u1, vcoord(rings.back().first) } }, side, o);
                }
            }
            return b.build();
        }


        mesh make_sphere(const primitive_params& p) {

            const u32 rings = std::max(2u, p.rings);
            std::vector<std::pair<f32, f32>> lat;
            for (u32 r = 1; r < rings; ++r) {
                const f32 phi = glm::pi<f32>() * static_cast<f32>(r) / static_cast<f32>(rings);
                lat.emplace_back(std::cos(phi) * p.radius, std::sin(phi) * p.radius);
            }
            return make_latitude_mesh(lat, p.radius, -p.radius, p.segments);
        }


        mesh make_capsule(const primitive_params& p) {

            const u32 half = std::max(1u, p.rings / 2);
            const f32 hy = p.height * 0.5f;
            std::vector<std::pair<f32, f32>> lat;
            for (u32 r = 1; r <= half; ++r) {                  // top hemisphere down to its equator
                const f32 phi = glm::half_pi<f32>() * static_cast<f32>(r) / static_cast<f32>(half);
                lat.emplace_back(hy + std::cos(phi) * p.radius, std::sin(phi) * p.radius);
            }
            for (u32 r = 0; r < half; ++r) {                   // bottom hemisphere from its equator
                const f32 phi = glm::half_pi<f32>() * static_cast<f32>(r) / static_cast<f32>(half);
                lat.emplace_back(-hy - std::sin(phi) * p.radius, std::cos(phi) * p.radius);
            }
            return make_latitude_mesh(lat, hy + p.radius, -hy - p.radius, p.segments);
        }


        // Rebuilds [m] with every face loop reversed; corner / edge values follow their geometric corner / edge
        mesh reversed(const mesh& m, std::vector<element_origin>* corner_origins = nullptr, std::vector<element_origin>* edge_origins = nullptr) {

            std::vector<std::vector<u32>> faces(m.face_count());
            std::vector<element_origin> corners, edges;
            std::vector<u32> hs;
            for (u32 f = 0; f < m.face_count(); ++f) {
                m.face_halfedges(f, hs);
                // old loop: hs[i] goes from vertex L[i] to L[i+1]; new loop: L[0], L[n-1], ..., L[1]
                const size_t n = hs.size();
                for (size_t k = 0; k < n; ++k) {
                    const size_t old_corner = (n - k) % n;          // the old half-edge starting at the same vertex
                    faces[f].push_back(m.from_vertex(hs[old_corner]));
                    corners.push_back(element_origin::copy_of(hs[old_corner]));
                    // new half-edge k runs L[n-k] -> L[n-k-1]: the old half-edge L[n-k-1] -> L[n-k] reversed
                    edges.push_back(element_origin::copy_of(hs[(2 * n - k - 1) % n]));
                }
            }
            mesh out = mesh::from_polygons(m.positions(), faces);
            if (corner_origins) *corner_origins = corners;
            if (edge_origins) *edge_origins = edges;
            const mode_propagator copy_only{};
            std::vector<element_origin> identity_v(m.vertex_count()), identity_f(m.face_count());
            for (u32 i = 0; i < m.vertex_count(); ++i) identity_v[i] = element_origin::copy_of(i);
            for (u32 i = 0; i < m.face_count(); ++i) identity_f[i] = element_origin::copy_of(i);
            propagate_columns(m, out, attribute_domain::vertex, identity_v, copy_only);
            propagate_columns(m, out, attribute_domain::face, identity_f, copy_only);
            propagate_columns(m, out, attribute_domain::corner, corners, copy_only);
            propagate_columns(m, out, attribute_domain::edge, edges, copy_only);
            return out;
        }

    }


    mesh make_primitive(primitive_kind kind, const primitive_params& params) {

        switch (kind) {
            case primitive_kind::box:       return make_box(params);
            case primitive_kind::cylinder:  return make_cylinder(params);
            case primitive_kind::sphere:    return make_sphere(params);
            case primitive_kind::capsule:   return make_capsule(params);
            case primitive_kind::plane:     return make_plane(params);
        }
        return {};
    }


    void transform(mesh& m, const glm::mat4& matrix) {

        for (u32 v = 0; v < m.vertex_count(); ++v)
            m.set_position(v, glm::vec3(matrix * glm::vec4(m.position(v), 1.0f)));
        if (glm::determinant(glm::mat3(matrix)) < 0.0f)
            m = reversed(m);
    }


    void propagate_columns(const mesh& src, mesh& dst, attribute_domain domain, std::span<const element_origin> origins,
                           const propagator& prop) {

        std::vector<attribute_value> values;
        for (const column& col : src.columns(domain)) {
            column& out = dst.add_column(domain, col.id, col.default_value);
            out.values.assign(dst.domain_size(domain), col.default_value);
            const attribute_desc& desc = prop.describe(col.id, col.default_value);
            auto value_at = [&](u32 i) -> const attribute_value& { return i < col.values.size() ? col.values[i] : col.default_value; };

            for (u32 i = 0; i < origins.size() && i < out.values.size(); ++i) {
                const element_origin& o = origins[i];
                if (o.kind == lineage_kind::copy && o.source_count == 1 && !o.is_seam) {
                    out.values[i] = value_at(o.sources[0].index);
                    continue;
                }
                values.clear();
                for (u8 s = 0; s < o.source_count; ++s)
                    values.push_back(value_at(o.sources[s].index));
                lineage_event ev{};
                ev.kind = o.kind;
                ev.domain = domain;
                ev.sources = std::span(o.sources.data(), o.source_count);
                ev.target = i;
                ev.is_seam = o.is_seam;
                attribute_io io(values, ev.sources);
                prop.apply(desc, ev, io);
                out.values[i] = io.written() ? io.result() : col.default_value;
            }
        }
    }


    edge_ring find_edge_ring(const mesh& m, u32 h) {

        edge_ring ring{};
        if (h >= m.halfedge_count())
            return ring;

        std::unordered_set<u32> seen;
        std::vector<u32> forward;
        u32 cur = h;
        while (true) {
            const u32 f = m.he(cur).face;
            if (m.face_size(f) != 4 || seen.contains(f))
                break;
            seen.insert(f);
            forward.push_back(cur);
            const u32 o = m.he(m.he(cur).next).next;
            const u32 t = m.he(o).twin;
            if (t == INVALID_INDEX)
                break;
            if (t == h) {
                ring.closed = true;
                break;
            }
            cur = t;
        }

        std::vector<u32> backward;
        if (!ring.closed) {
            u32 back = m.he(h).twin;
            while (back != INVALID_INDEX) {
                const u32 g = m.he(back).face;
                if (m.face_size(g) != 4 || seen.contains(g))
                    break;
                seen.insert(g);
                const u32 e = m.he(m.he(back).next).next;
                backward.push_back(e);
                back = m.he(e).twin;
            }
        }
        ring.entries.assign(backward.rbegin(), backward.rend());
        ring.entries.insert(ring.entries.end(), forward.begin(), forward.end());
        return ring;
    }


    mesh loop_cut(const mesh& m, u32 h, std::span<const f32> positions, const propagator& prop) {

        std::vector<f32> ts;
        for (const f32 t : positions)
            if (t > 0.0f && t < 1.0f)
                ts.push_back(t);
        std::ranges::sort(ts);
        ts.erase(std::unique(ts.begin(), ts.end()), ts.end());
        const edge_ring ring = find_edge_ring(m, h);
        if (ts.empty() || ring.entries.empty())
            return m;
        const u32 k = static_cast<u32>(ts.size());

        // ---- new vertices: k per cut edge, ordered from rail A to rail B -----------------------------------------------
        struct cut_edge { u32 rail_a, rail_b; u32 first_new; };
        std::unordered_map<u64, cut_edge> cuts;
        std::unordered_map<u32, u32> ring_face_entry;           // face -> entry half-edge
        std::vector<glm::vec3> out_positions(m.positions().begin(), m.positions().end());
        std::vector<element_origin> vertex_origins(m.vertex_count());
        for (u32 v = 0; v < m.vertex_count(); ++v)
            vertex_origins[v] = element_origin::copy_of(v);

        auto add_cut = [&](u32 rail_a, u32 rail_b) {
            const u64 key = edge_key(rail_a, rail_b);
            if (cuts.contains(key))
                return;
            cut_edge c{ rail_a, rail_b, static_cast<u32>(out_positions.size()) };
            for (const f32 t : ts) {
                out_positions.push_back(glm::mix(m.position(rail_a), m.position(rail_b), t));
                element_origin o{};
                o.kind = lineage_kind::split;
                o.sources[0] = { rail_a, 1.0f - t };
                o.sources[1] = { rail_b, t };
                o.source_count = 2;
                vertex_origins.push_back(o);
            }
            cuts.emplace(key, c);
        };
        for (const u32 e : ring.entries) {
            ring_face_entry.emplace(m.he(e).face, e);
            const u32 o = m.he(m.he(e).next).next;
            add_cut(m.from_vertex(e), m.he(e).vertex);              // entry: a -> b
            add_cut(m.he(o).vertex, m.from_vertex(o));              // opposite: c -> d, rail A is d
        }

        // ---- faces ------------------------------------------------------------------------------------------------------
        std::vector<std::vector<u32>> faces;
        std::vector<element_origin> face_origins, corner_origins, edge_origins;
        std::vector<u32> hs;

        auto split_origin = [](u32 a, f32 wa, u32 b, f32 wb, bool seam = false) {
            element_origin o{};
            o.kind = lineage_kind::split;
            o.sources[0] = { a, wa };
            o.sources[1] = { b, wb };
            o.source_count = 2;
            o.is_seam = seam;
            return o;
        };
        auto split_one = [](u32 a) {
            element_origin o = element_origin::copy_of(a);
            o.kind = lineage_kind::split;
            return o;
        };

        for (u32 f = 0; f < m.face_count(); ++f) {
            if (auto it = ring_face_entry.find(f); it != ring_face_entry.end()) {
                const u32 e = it->second;                           // a -> b
                const u32 n1 = m.he(e).next;                        // b -> c
                const u32 o = m.he(n1).next;                        // c -> d
                const u32 n3 = m.he(o).next;                        // d -> a
                const u32 a = m.from_vertex(e), b = m.he(e).vertex, c = m.he(n1).vertex, d = m.he(o).vertex;
                const cut_edge& ce = cuts.at(edge_key(a, b));
                const cut_edge& co = cuts.at(edge_key(c, d));
                auto P = [&](u32 j) { return j == 0 ? a : (j == k + 1 ? b : ce.first_new + j - 1); };
                auto Q = [&](u32 j) { return j == 0 ? d : (j == k + 1 ? c : co.first_new + j - 1); };
                auto t_of = [&](u32 j) { return j == 0 ? 0.0f : (j == k + 1 ? 1.0f : ts[j - 1]); };
                auto corner_p = [&](u32 j) { return j == 0 ? element_origin::copy_of(e) : (j == k + 1 ? element_origin::copy_of(n1) : split_origin(e, 1.0f - t_of(j), n1, t_of(j))); };
                auto corner_q = [&](u32 j) { return j == 0 ? element_origin::copy_of(n3) : (j == k + 1 ? element_origin::copy_of(o) : split_origin(n3, 1.0f - t_of(j), o, t_of(j))); };
                auto rung = [&](u32 j, bool forward_dir) {             // edge between P_j and Q_j
                    if (j == 0)     return element_origin::copy_of(n3);
                    if (j == k + 1) return element_origin::copy_of(n1);
                    (void)forward_dir;
                    return split_origin(n1, t_of(j), n3, 1.0f - t_of(j), true);
                };
                for (u32 j = 0; j <= k; ++j) {
                    faces.push_back({ P(j), P(j + 1), Q(j + 1), Q(j) });
                    face_origins.push_back(split_one(f));
                    corner_origins.push_back(corner_p(j));
                    corner_origins.push_back(corner_p(j + 1));
                    corner_origins.push_back(corner_q(j + 1));
                    corner_origins.push_back(corner_q(j));
                    edge_origins.push_back(split_one(e));           // P_j -> P_j+1
                    edge_origins.push_back(rung(j + 1, true));      // P_j+1 -> Q_j+1
                    edge_origins.push_back(split_one(o));           // Q_j+1 -> Q_j
                    edge_origins.push_back(rung(j, false));         // Q_j -> P_j
                }
                continue;
            }

            // Not in the ring: keep, inserting new vertices on edges that were cut (open ring ends)
            m.face_halfedges(f, hs);
            std::vector<u32> loop;
            for (const u32 hh : hs) {
                const u32 u = m.from_vertex(hh), w = m.he(hh).vertex;
                loop.push_back(u);
                corner_origins.push_back(element_origin::copy_of(hh));
                const auto it = cuts.find(edge_key(u, w));
                if (it == cuts.end()) {
                    edge_origins.push_back(element_origin::copy_of(hh));
                    continue;
                }
                const cut_edge& ce = it->second;
                const bool along = ce.rail_a == u;
                edge_origins.push_back(split_one(hh));
                for (u32 s = 0; s < k; ++s) {
                    const u32 j = along ? s : k - 1 - s;
                    const f32 t = ts[j];
                    loop.push_back(ce.first_new + j);
                    const f32 wu = along ? 1.0f - t : t;
                    corner_origins.push_back(split_origin(hh, wu, m.he(hh).next, 1.0f - wu));
                    edge_origins.push_back(split_one(hh));
                }
            }
            faces.push_back(std::move(loop));
            face_origins.push_back(element_origin::copy_of(f));
        }

        mesh out = mesh::from_polygons(out_positions, faces);
        propagate_columns(m, out, attribute_domain::vertex, vertex_origins, prop);
        propagate_columns(m, out, attribute_domain::face, face_origins, prop);
        propagate_columns(m, out, attribute_domain::corner, corner_origins, prop);
        propagate_columns(m, out, attribute_domain::edge, edge_origins, prop);
        return out;
    }


    mesh mirror(const mesh& m, const glm::vec4& plane, const propagator& prop) {

        const glm::vec3 n = glm::normalize(glm::vec3(plane));
        const f32 d = plane.w / glm::length(glm::vec3(plane));
        mesh flipped = m;
        for (u32 v = 0; v < flipped.vertex_count(); ++v) {
            const glm::vec3 p = flipped.position(v);
            flipped.set_position(v, p - 2.0f * (glm::dot(n, p) + d) * n);
        }
        std::vector<element_origin> corners, edges;
        mesh out = reversed(flipped, &corners, &edges);
        // Re-run the columns through the propagator as mirror events (packs may e.g. swap "left" / "right" tags)
        auto as_mirror = [](std::vector<element_origin> v) { for (auto& o : v) o.kind = lineage_kind::mirror; return v; };
        std::vector<element_origin> vs(m.vertex_count()), fs(m.face_count());
        for (u32 i = 0; i < m.vertex_count(); ++i) vs[i] = element_origin::copy_of(i);
        for (u32 i = 0; i < m.face_count(); ++i) fs[i] = element_origin::copy_of(i);
        propagate_columns(m, out, attribute_domain::vertex, as_mirror(vs), prop);
        propagate_columns(m, out, attribute_domain::face, as_mirror(fs), prop);
        propagate_columns(m, out, attribute_domain::corner, as_mirror(corners), prop);
        propagate_columns(m, out, attribute_domain::edge, as_mirror(edges), prop);
        return out;
    }


    void append(mesh& dst, const mesh& src) {

        const u32 v0 = dst.vertex_count(), h0 = dst.halfedge_count(), f0 = dst.face_count();
        for (const attribute_domain d : { attribute_domain::vertex, attribute_domain::face, attribute_domain::corner, attribute_domain::edge })
            for (const column& c : src.columns(d))
                dst.add_column(d, c.id, c.default_value);

        for (u32 v = 0; v < src.vertex_count(); ++v)
            dst.add_vertex(src.position(v));
        std::vector<u32> vs;
        for (u32 f = 0; f < src.face_count(); ++f) {
            src.face_vertices(f, vs);
            for (u32& v : vs)
                v += v0;
            dst.add_face(vs);
        }
        dst.link_twins();
        dst.sync_columns();

        for (const attribute_domain d : { attribute_domain::vertex, attribute_domain::face, attribute_domain::corner, attribute_domain::edge }) {
            const u32 base = d == attribute_domain::vertex ? v0 : (d == attribute_domain::face ? f0 : h0);
            for (const column& c : src.columns(d)) {
                column* out = dst.find_column(d, c.id);
                for (u32 i = 0; i < c.values.size() && base + i < out->values.size(); ++i)
                    out->values[base + i] = c.values[i];
            }
        }
    }


    mesh sweep_circle(const curve& c, const sweep_settings& settings) {

        c.build(std::max(1u, settings.segments_per_span));
        const std::span<const curve_sample> samples = c.samples();
        poly_builder b{};
        if (samples.size() < 2)
            return {};

        const u32 sides = std::max(3u, settings.sides);
        size_t ring_count = samples.size();
        const bool closed = c.closed();
        if (closed && ring_count > 2)
            --ring_count;                                       // the last sample repeats the first

        std::vector<std::vector<u32>> rings(ring_count);
        for (size_t r = 0; r < ring_count; ++r) {
            const curve_sample& s = samples[r];
            f32 radius = settings.radius;
            if (!settings.radius_attribute.empty())
                if (const attribute_value v = c.attribute(settings.radius_attribute, s.t); v.index() != 0)
                    radius = static_cast<f32>(as_number(v));
            for (u32 i = 0; i < sides; ++i) {
                const f32 a = TAU * static_cast<f32>(i) / static_cast<f32>(sides);
                rings[r].push_back(b.vertex(s.position + (s.normal * std::cos(a) + s.binormal * std::sin(a)) * radius));
            }
        }

        const name_id side = intern("side");
        const size_t spans = closed ? ring_count : ring_count - 1;
        for (size_t r = 0; r < spans; ++r) {
            const size_t r1 = (r + 1) % ring_count;
            const f32 v0 = static_cast<f32>(samples[r].distance);
            const f32 v1 = static_cast<f32>(r + 1 < samples.size() ? samples[r + 1].distance : samples.back().distance);
            const glm::vec3 mid = (samples[r].position + samples[r1].position) * 0.5f;
            for (u32 i = 0; i < sides; ++i) {
                const u32 j = (i + 1) % sides;
                const f32 u0 = static_cast<f32>(i) / sides, u1 = static_cast<f32>(i + 1) / sides;
                std::vector<u32> loop{ rings[r][i], rings[r][j], rings[r1][j], rings[r1][i] };
                const glm::vec3 centre = (b.positions[loop[0]] + b.positions[loop[1]] + b.positions[loop[2]] + b.positions[loop[3]]) * 0.25f;
                b.face(std::move(loop), { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } }, side, centre - mid);
            }
        }

        std::unordered_set<u64> hard;
        if (settings.caps && !closed) {
            for (const bool start : { true, false }) {
                const std::vector<u32>& ring = start ? rings.front() : rings.back();
                const curve_sample& s = start ? samples.front() : samples.back();
                std::vector<glm::vec2> uv;
                for (u32 i = 0; i < sides; ++i) {
                    const f32 a = TAU * static_cast<f32>(i) / static_cast<f32>(sides);
                    uv.emplace_back(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a));
                    hard.insert(edge_key(ring[i], ring[(i + 1) % sides]));
                }
                b.face(ring, std::move(uv), intern(start ? "cap_start" : "cap_end"), start ? -s.tangent : s.tangent);
            }
        }
        return b.build(hard);
    }


    triangle_mesh triangulate(const mesh& m, f32 smooth_angle_degrees) {

        triangle_mesh out{};
        const f32 cos_limit = std::cos(glm::radians(std::clamp(smooth_angle_degrees, 0.0f, 180.0f)));
        const column* uv_col = m.find_column(attribute_domain::corner, attr_uv());
        const column* hard_col = m.find_column(attribute_domain::edge, attr_hard());
        const column* mat_col = m.find_column(attribute_domain::face, attr_material());

        std::vector<glm::vec3> face_n(m.face_count());
        for (u32 f = 0; f < m.face_count(); ++f)
            face_n[f] = m.face_normal(f);

        auto is_hard = [&](u32 h) {
            if (!hard_col || h >= hard_col->values.size())
                return false;
            const bool* b = std::get_if<bool>(&hard_col->values[h]);
            return b && *b;
        };
        auto smooth_across = [&](u32 h) {                       // can normals be shared across the edge of h?
            const u32 t = m.he(h).twin;
            if (t == INVALID_INDEX || is_hard(h))
                return false;
            return glm::dot(face_n[m.he(h).face], face_n[m.he(t).face]) >= cos_limit;
        };

        // Normal of a corner: face normals of the fan around its vertex, up to hard / sharp edges. Summed in face order,
        // so every corner of one smoothing fan gets bit-identical normals (and shares one output vertex)
        std::vector<u32> fan;
        auto corner_normal = [&](u32 h) {
            const u32 f0 = m.he(h).face;
            fan.clear();
            fan.push_back(f0);
            u32 cur = h;
            for (u32 guard = 0; guard < 64; ++guard) {            // rotate one way: across the edge entering the vertex
                const u32 in = m.prev(cur);
                if (!smooth_across(in))
                    break;
                const u32 next_out = m.he(in).twin;
                if (m.he(next_out).face == f0)
                    break;
                fan.push_back(m.he(next_out).face);
                cur = next_out;
            }
            cur = h;
            for (u32 guard = 0; guard < 64; ++guard) {            // the other way: across the edge leaving the vertex
                if (!smooth_across(cur))
                    break;
                const u32 t = m.he(cur).twin;
                const u32 next_out = m.he(t).next;
                if (m.he(next_out).face == f0)
                    break;
                fan.push_back(m.he(next_out).face);
                cur = next_out;
            }
            std::ranges::sort(fan);
            fan.erase(std::unique(fan.begin(), fan.end()), fan.end());
            glm::vec3 n(0.0f);
            for (const u32 f : fan)
                n += face_n[f];
            const f32 len = glm::length(n);
            return len > 0.0f ? n / len : face_n[f0];
        };

        auto box_uv = [](const glm::vec3& p, const glm::vec3& n) {
            const glm::vec3 a = glm::abs(n);
            if (a.x >= a.y && a.x >= a.z) return glm::vec2(p.z, p.y);
            if (a.y >= a.z)               return glm::vec2(p.x, p.z);
            return glm::vec2(p.x, p.y);
        };

        struct key { glm::vec3 p, n; glm::vec2 uv; bool operator==(const key&) const = default; };
        struct key_hash {
            size_t operator()(const key& k) const noexcept {
                return static_cast<size_t>(fnv1a_64(std::string_view(reinterpret_cast<const char*>(&k), sizeof(key))));
            }
        };
        std::unordered_map<key, u32, key_hash> unique;

        std::vector<u32> hs, corner_index;
        for (u32 f = 0; f < m.face_count(); ++f) {
            m.face_halfedges(f, hs);
            if (hs.size() < 3)
                continue;
            corner_index.clear();
            for (const u32 h : hs) {
                key k{};
                k.p = m.position(m.from_vertex(h));
                k.n = corner_normal(h);
                if (uv_col && h < uv_col->values.size() && std::holds_alternative<glm::vec2>(uv_col->values[h]))
                    k.uv = std::get<glm::vec2>(uv_col->values[h]);
                else
                    k.uv = box_uv(k.p, k.n);
                auto [it, inserted] = unique.try_emplace(k, static_cast<u32>(out.positions.size()));
                if (inserted) {
                    out.positions.push_back(k.p);
                    out.normals.push_back(k.n);
                    out.uvs.push_back(k.uv);
                }
                corner_index.push_back(it->second);
            }
            name_id material{};
            if (mat_col && f < mat_col->values.size())
                if (const name_id* mid = std::get_if<name_id>(&mat_col->values[f]))
                    material = *mid;
            for (size_t i = 1; i + 1 < corner_index.size(); ++i) {
                out.indices.push_back(corner_index[0]);
                out.indices.push_back(corner_index[i]);
                out.indices.push_back(corner_index[i + 1]);
                out.triangle_material.push_back(material);
            }
        }
        return out;
    }


    std::vector<contour> slice(const mesh& m, const glm::vec4& plane) {

        const glm::vec3 n(plane);
        const f32 len = glm::length(n);
        std::vector<contour> out;
        if (len <= 0.0f)
            return out;
        const glm::vec3 nn = n / len;
        const f32 d = plane.w / len;
        constexpr f32 EPS = 1e-6f;

        std::vector<f32> dist(m.vertex_count());
        for (u32 v = 0; v < m.vertex_count(); ++v) {
            f32 s = glm::dot(nn, m.position(v)) - d;
            if (std::abs(s) < EPS)
                s = EPS;                                    // on the plane: count as above (no degenerate crossings)
            dist[v] = s;
        }

        // One segment per face crossing: from the edge going up to the edge going down (consistent orientation)
        struct segment { u64 from, to; glm::vec3 a, b; };
        std::vector<segment> segments;
        std::vector<u32> hs;
        for (u32 f = 0; f < m.face_count(); ++f) {
            m.face_halfedges(f, hs);
            std::vector<std::pair<u64, glm::vec3>> ups, downs;
            for (const u32 h : hs) {
                const u32 a = m.from_vertex(h), b = m.he(h).vertex;
                if ((dist[a] < 0.0f) == (dist[b] < 0.0f))
                    continue;
                const f32 t = dist[a] / (dist[a] - dist[b]);
                const glm::vec3 p = glm::mix(m.position(a), m.position(b), t);
                (dist[a] < 0.0f ? ups : downs).emplace_back(edge_key(a, b), p);
            }
            for (size_t i = 0; i < std::min(ups.size(), downs.size()); ++i)
                segments.push_back({ downs[i].first, ups[i].first, downs[i].second, ups[i].second });
        }

        // Chain segments into loops by shared edge keys
        std::unordered_map<u64, u32> starting_at;
        for (u32 i = 0; i < segments.size(); ++i)
            starting_at.emplace(segments[i].from, i);
        std::vector<bool> used(segments.size(), false);
        for (u32 s = 0; s < segments.size(); ++s) {
            if (used[s])
                continue;
            contour c{};
            u32 cur = s;
            while (!used[cur]) {
                used[cur] = true;
                c.points.push_back(segments[cur].a);
                auto it = starting_at.find(segments[cur].to);
                if (it == starting_at.end()) {
                    c.points.push_back(segments[cur].b);  // open end
                    break;
                }
                if (it->second == s) {
                    c.closed = true;
                    break;
                }
                cur = it->second;
            }
            if (c.points.size() >= 2)
                out.push_back(std::move(c));
        }
        return out;
    }


    contour resample(const contour& c, f32 spacing, u32 min_points) {

        contour out{};
        out.closed = c.closed;
        const size_t n = c.points.size();
        if (n < 2)
            return c;
        const size_t edges = c.closed ? n : n - 1;
        f32 total = 0.0f;
        for (size_t i = 0; i < edges; ++i)
            total += glm::length(c.points[(i + 1) % n] - c.points[i]);
        const u32 count = std::max(min_points, static_cast<u32>(std::ceil(total / std::max(spacing, 1e-4f))));
        const f32 step = total / static_cast<f32>(c.closed ? count : count - 1);

        size_t edge = 0;
        f32 edge_start = 0.0f;
        for (u32 k = 0; k < count; ++k) {
            const f32 target = step * static_cast<f32>(k);
            while (edge < edges) {
                const f32 l = glm::length(c.points[(edge + 1) % n] - c.points[edge]);
                if (target <= edge_start + l || edge + 1 == edges) {
                    const f32 u = l > 0.0f ? std::clamp((target - edge_start) / l, 0.0f, 1.0f) : 0.0f;
                    out.points.push_back(glm::mix(c.points[edge], c.points[(edge + 1) % n], u));
                    break;
                }
                edge_start += l;
                ++edge;
            }
        }
        return out;
    }


    f32 signed_area(const contour& c, const glm::vec3& n) {

        glm::vec3 sum(0.0f);
        const size_t count = c.points.size();
        for (size_t i = 0; i < count; ++i)
            sum += glm::cross(c.points[i], c.points[(i + 1) % count]);
        return 0.5f * glm::dot(sum, glm::normalize(n));
    }

}
