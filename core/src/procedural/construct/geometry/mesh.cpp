#include "procedural/construct/geometry/mesh.h"



namespace GLT::construct::geometry {

    // FUNCTION IMPLEMENTATION =========================================================================================

    attribute_id attr_group()    { static const attribute_id id = intern("core.group");    return id; }
    attribute_id attr_uv()       { static const attribute_id id = intern("core.uv");       return id; }
    attribute_id attr_hard()     { static const attribute_id id = intern("core.hard");     return id; }
    attribute_id attr_material() { static const attribute_id id = intern("core.material"); return id; }

    namespace {

        const attribute_value                           no_value{};

        u64 edge_key(u32 a, u32 b) noexcept { return (static_cast<u64>(a) << 32) | b; }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    u32 mesh::add_vertex(const glm::vec3& position) {

        m_positions.push_back(position);
        return static_cast<u32>(m_positions.size() - 1);
    }


    u32 mesh::add_face(std::span<const u32> loop) {

        const u32 face = static_cast<u32>(m_face_halfedge.size());
        const u32 first = static_cast<u32>(m_halfedges.size());
        const u32 n = static_cast<u32>(loop.size());
        for (u32 i = 0; i < n; ++i) {
            halfedge h{};
            h.vertex = loop[(i + 1) % n];
            h.next = first + (i + 1) % n;
            h.face = face;
            m_halfedges.push_back(h);
        }
        m_face_halfedge.push_back(first);
        m_face_size.push_back(n);
        return face;
    }


    void mesh::link_twins() {

        std::unordered_map<u64, u32> by_edge;
        by_edge.reserve(m_halfedges.size());
        for (u32 h = 0; h < m_halfedges.size(); ++h) {
            m_halfedges[h].twin = INVALID_INDEX;
            by_edge.try_emplace(edge_key(from_vertex(h), m_halfedges[h].vertex), h);
        }
        for (u32 h = 0; h < m_halfedges.size(); ++h) {
            if (auto it = by_edge.find(edge_key(m_halfedges[h].vertex, from_vertex(h))); it != by_edge.end() && it->second != h)
                m_halfedges[h].twin = it->second;
        }
        // Non-manifold edges (more than two faces): keep only mutual pairs
        for (u32 h = 0; h < m_halfedges.size(); ++h) {
            const u32 t = m_halfedges[h].twin;
            if (t != INVALID_INDEX && m_halfedges[t].twin != h)
                m_halfedges[h].twin = INVALID_INDEX;
        }
    }


    mesh mesh::from_polygons(std::span<const glm::vec3> positions, std::span<const std::vector<u32>> faces) {

        mesh m{};
        m.m_positions.assign(positions.begin(), positions.end());
        for (const auto& f : faces)
            m.add_face(f);
        m.link_twins();
        return m;
    }


    u32 mesh::domain_size(attribute_domain d) const noexcept {

        switch (d) {
            case attribute_domain::vertex:  return vertex_count();
            case attribute_domain::face:    return face_count();
            case attribute_domain::corner:
            case attribute_domain::edge:    return halfedge_count();
            default:                        return 0;
        }
    }


    u32 mesh::from_vertex(u32 h) const noexcept {

        return m_halfedges[prev(h)].vertex;
    }


    u32 mesh::prev(u32 h) const noexcept {

        u32 p = h;
        for (u32 guard = 0; guard < 4096; ++guard) {
            const u32 n = m_halfedges[p].next;
            if (n == h)
                return p;
            p = n;
        }
        return h;
    }


    void mesh::face_halfedges(u32 f, std::vector<u32>& out) const {

        out.clear();
        const u32 start = m_face_halfedge[f];
        u32 h = start;
        do {
            out.push_back(h);
            h = m_halfedges[h].next;
        } while (h != start && out.size() < 4096);
    }


    void mesh::face_vertices(u32 f, std::vector<u32>& out) const {

        std::vector<u32> hs;
        face_halfedges(f, hs);
        out.clear();
        for (const u32 h : hs)
            out.push_back(from_vertex(h));
    }


    glm::vec3 mesh::face_normal(u32 f) const {

        std::vector<u32> vs;
        face_vertices(f, vs);
        glm::vec3 n(0.0f);
        for (size_t i = 0; i < vs.size(); ++i) {
            const glm::vec3& a = m_positions[vs[i]];
            const glm::vec3& b = m_positions[vs[(i + 1) % vs.size()]];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
        }
        const f32 len = glm::length(n);
        return len > 0.0f ? n / len : glm::vec3(0.0f);
    }


    glm::vec3 mesh::face_centroid(u32 f) const {

        std::vector<u32> vs;
        face_vertices(f, vs);
        glm::vec3 c(0.0f);
        for (const u32 v : vs)
            c += m_positions[v];
        return vs.empty() ? c : c / static_cast<f32>(vs.size());
    }


    std::vector<column>& mesh::domain_columns(attribute_domain domain) noexcept {

        switch (domain) {
            case attribute_domain::vertex:  return m_vertex_columns;
            case attribute_domain::face:    return m_face_columns;
            case attribute_domain::corner:  return m_corner_columns;
            default:                        return m_edge_columns;
        }
    }


    const std::vector<column>& mesh::domain_columns(attribute_domain domain) const noexcept {

        return const_cast<mesh*>(this)->domain_columns(domain);
    }


    column& mesh::add_column(attribute_domain domain, attribute_id id, attribute_value default_value) {

        if (column* c = find_column(domain, id))
            return *c;
        auto& cols = domain_columns(domain);
        column c{};
        c.id = id;
        c.default_value = default_value;
        c.values.assign(domain_size(domain), default_value);
        cols.push_back(std::move(c));
        return cols.back();
    }


    column* mesh::find_column(attribute_domain domain, attribute_id id) noexcept {

        for (column& c : domain_columns(domain))
            if (c.id == id)
                return &c;
        return nullptr;
    }


    const column* mesh::find_column(attribute_domain domain, attribute_id id) const noexcept {

        return const_cast<mesh*>(this)->find_column(domain, id);
    }


    std::span<const column> mesh::columns(attribute_domain domain) const noexcept { return domain_columns(domain); }
    std::span<column> mesh::columns(attribute_domain domain) noexcept { return domain_columns(domain); }


    const attribute_value& mesh::get(attribute_domain domain, attribute_id id, u32 index) const noexcept {

        const column* c = find_column(domain, id);
        if (!c)
            return no_value;
        return index < c->values.size() ? c->values[index] : c->default_value;
    }


    void mesh::set(attribute_domain domain, attribute_id id, u32 index, attribute_value value) {

        column& c = add_column(domain, id, attribute_value{});
        if (c.values.size() < domain_size(domain))
            c.values.resize(domain_size(domain), c.default_value);
        if (domain == attribute_domain::edge && index < m_halfedges.size()) {
            const u32 t = m_halfedges[index].twin;
            if (t != INVALID_INDEX)
                c.values[t] = value;
        }
        c.values[index] = std::move(value);
    }


    void mesh::sync_columns() {

        for (const attribute_domain d : { attribute_domain::vertex, attribute_domain::face, attribute_domain::corner, attribute_domain::edge })
            for (column& c : domain_columns(d))
                c.values.resize(domain_size(d), c.default_value);
    }


    std::vector<u32> mesh::faces_in_group(name_id group) const {

        std::vector<u32> out;
        const column* c = find_column(attribute_domain::face, attr_group());
        if (!c)
            return out;
        for (u32 f = 0; f < face_count(); ++f)
            if (const name_id* g = std::get_if<name_id>(f < c->values.size() ? &c->values[f] : &c->default_value); g && *g == group)
                out.push_back(f);
        return out;
    }


    u64 mesh::hash() const noexcept {

        u64 h = fnv1a_64(std::string_view(reinterpret_cast<const char*>(m_positions.data()), m_positions.size() * sizeof(glm::vec3)));
        for (const halfedge& e : m_halfedges)
            h = hash_combine(h, hash_combine(hash_combine(e.vertex, e.next), hash_combine(e.twin, e.face)));
        for (const attribute_domain d : { attribute_domain::vertex, attribute_domain::face, attribute_domain::corner, attribute_domain::edge }) {
            // Columns in id order: the order they were added in must not matter
            std::vector<const column*> sorted;
            for (const column& c : domain_columns(d))
                sorted.push_back(&c);
            std::ranges::sort(sorted, [](const column* a, const column* b) { return a->id.hash < b->id.hash; });
            for (const column* c : sorted) {
                h = hash_combine(h, c->id.hash);
                for (const attribute_value& v : c->values)
                    h = hash_combine(h, hash_value(v));
            }
        }
        return h;
    }


    std::string mesh::validate() const {

        for (u32 h = 0; h < m_halfedges.size(); ++h) {
            const halfedge& e = m_halfedges[h];
            if (e.vertex >= m_positions.size())
                return std::format("half-edge {} points to vertex {} of {}", h, e.vertex, m_positions.size());
            if (e.next >= m_halfedges.size())
                return std::format("half-edge {} has no next", h);
            if (e.face >= m_face_halfedge.size())
                return std::format("half-edge {} has no face", h);
            if (e.twin != INVALID_INDEX) {
                if (m_halfedges[e.twin].twin != h)
                    return std::format("half-edge {} and its twin {} disagree", h, e.twin);
                if (m_halfedges[e.twin].vertex != from_vertex(h))
                    return std::format("half-edge {}: twin {} does not run the other way", h, e.twin);
            }
        }
        for (u32 f = 0; f < m_face_halfedge.size(); ++f) {
            std::vector<u32> hs;
            face_halfedges(f, hs);
            if (hs.size() != m_face_size[f])
                return std::format("face {} loop has {} half-edges, expected {}", f, hs.size(), m_face_size[f]);
            for (const u32 h : hs)
                if (m_halfedges[h].face != f)
                    return std::format("face {} loop contains half-edge {} of face {}", f, h, m_halfedges[h].face);
        }
        return {};
    }

}
