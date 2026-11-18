#include "procedural/construct/geometry/curve.h"



namespace GLT::construct::geometry {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        glm::vec3 any_perpendicular(const glm::vec3& t) {

            const glm::vec3 axis = std::abs(t.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            return glm::normalize(glm::cross(axis, t));
        }


        attribute_value lerp_value(const attribute_value& a, const attribute_value& b, f32 u) {

            if (a.index() != b.index())
                return u < 0.5f ? a : b;
            return std::visit([&](const auto& x) -> attribute_value {
                using T = std::decay_t<decltype(x)>;
                const T& y = std::get<T>(b);
                if constexpr (std::is_same_v<T, f32>)                                   return x + (y - x) * u;
                else if constexpr (std::is_same_v<T, f64>)                              return x + (y - x) * static_cast<f64>(u);
                else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
                                                                                        return glm::mix(x, y, u);
                else if constexpr (std::is_same_v<T, glm::quat>)                        return glm::slerp(x, y, u);
                else if constexpr (std::is_same_v<T, i32> || std::is_same_v<T, i64>)    return static_cast<T>(std::llround(static_cast<f64>(x) + static_cast<f64>(y - x) * u));
                else                                                                    return u < 0.5f ? x : y;
            }, a);
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    curve::curve(curve_type type, std::vector<glm::vec3> points, bool closed)
        : m_type(type), m_closed(closed), m_points(std::move(points)) {}


    void curve::set_point_attribute(attribute_id id, u32 point, attribute_value value) {

        column* c = nullptr;
        for (column& col : m_columns)
            if (col.id == id)
                c = &col;
        if (!c) {
            m_columns.push_back({ id, attribute_value{}, std::vector<attribute_value>(m_points.size()) });
            c = &m_columns.back();
        }
        c->values.resize(m_points.size(), c->default_value);
        if (point < c->values.size())
            c->values[point] = std::move(value);
    }


    const column* curve::find_point_column(attribute_id id) const noexcept {

        for (const column& c : m_columns)
            if (c.id == id)
                return &c;
        return nullptr;
    }


    glm::vec3 curve::evaluate_span(u32 span, f32 u) const {

        const u32 n = point_count();
        auto at = [&](i64 i) -> glm::vec3 {
            if (m_closed)
                return m_points[static_cast<size_t>(((i % n) + n) % n)];
            return m_points[static_cast<size_t>(std::clamp<i64>(i, 0, n - 1))];
        };
        const glm::vec3 p1 = at(span), p2 = at(static_cast<i64>(span) + 1);
        if (m_type == curve_type::polyline || n < 3)
            return glm::mix(p1, p2, u);

        // Centripetal Catmull-Rom (alpha 0.5), Barry-Goldman form. Ends: mirrored phantom points
        glm::vec3 p0 = at(static_cast<i64>(span) - 1), p3 = at(static_cast<i64>(span) + 2);
        if (!m_closed && span == 0)                 p0 = p1 + (p1 - p2);
        if (!m_closed && span + 2 >= n)             p3 = p2 + (p2 - p1);

        auto knot = [](f32 t, const glm::vec3& a, const glm::vec3& b) { return t + std::max(std::sqrt(glm::length(b - a)), 1e-4f); };
        const f32 t0 = 0.0f, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
        const f32 t = t1 + (t2 - t1) * u;
        const glm::vec3 a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
        const glm::vec3 a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
        const glm::vec3 a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
        const glm::vec3 b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
        const glm::vec3 b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
        return (t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2;
    }


    void curve::build(u32 segments_per_span) const {

        segments_per_span = std::max(1u, segments_per_span);
        if (m_built_resolution == segments_per_span && !m_samples.empty())
            return;
        m_samples.clear();
        m_point_t.clear();
        m_built_resolution = segments_per_span;

        const u32 n = point_count();
        if (n == 0)
            return;
        const u32 spans = n < 2 ? 0 : (m_closed ? n : n - 1);
        const u32 per = (m_type == curve_type::polyline) ? 1u : segments_per_span;

        std::vector<u32> point_sample(n, 0);
        curve_sample first{};
        first.position = m_points[0];
        m_samples.push_back(first);
        for (u32 s = 0; s < spans; ++s) {
            for (u32 k = 1; k <= per; ++k) {
                curve_sample cs{};
                cs.position = evaluate_span(s, static_cast<f32>(k) / static_cast<f32>(per));
                cs.distance = m_samples.back().distance + static_cast<f64>(glm::length(cs.position - m_samples.back().position));
                m_samples.push_back(cs);
            }
            if (s + 1 < n)
                point_sample[s + 1] = static_cast<u32>(m_samples.size() - 1);
        }

        const f64 total = m_samples.back().distance;
        for (curve_sample& cs : m_samples)
            cs.t = total > 0.0 ? cs.distance / total : 0.0;
        for (u32 i = 0; i < n; ++i)
            m_point_t.push_back(m_samples[point_sample[i]].t);

        // Tangents (central differences), then rotation-minimizing frames by double reflection (Wang et al. 2008)
        const size_t count = m_samples.size();
        for (size_t i = 0; i < count; ++i) {
            const glm::vec3 a = m_samples[i > 0 ? i - 1 : i].position;
            const glm::vec3 b = m_samples[i + 1 < count ? i + 1 : i].position;
            const glm::vec3 d = b - a;
            m_samples[i].tangent = glm::length(d) > 0.0f ? glm::normalize(d) : (i ? m_samples[i - 1].tangent : glm::vec3(0, 0, 1));
        }
        m_samples[0].normal = any_perpendicular(m_samples[0].tangent);
        for (size_t i = 0; i + 1 < count; ++i) {
            curve_sample& c0 = m_samples[i];
            curve_sample& c1 = m_samples[i + 1];
            const glm::vec3 v1 = c1.position - c0.position;
            const f32 c1d = glm::dot(v1, v1);
            if (c1d < 1e-12f) {
                c1.normal = c0.normal;
                continue;
            }
            const glm::vec3 rl = c0.normal - (2.0f / c1d) * glm::dot(v1, c0.normal) * v1;
            const glm::vec3 tl = c0.tangent - (2.0f / c1d) * glm::dot(v1, c0.tangent) * v1;
            const glm::vec3 v2 = c1.tangent - tl;
            const f32 c2 = glm::dot(v2, v2);
            glm::vec3 r = c2 > 1e-12f ? rl - (2.0f / c2) * glm::dot(v2, rl) * v2 : rl;
            r = r - glm::dot(r, c1.tangent) * c1.tangent;
            c1.normal = glm::length(r) > 1e-6f ? glm::normalize(r) : any_perpendicular(c1.tangent);
        }
        for (curve_sample& cs : m_samples)
            cs.binormal = glm::cross(cs.tangent, cs.normal);
    }


    f64 curve::length() const {

        build(m_built_resolution ? m_built_resolution : 16);
        return m_samples.empty() ? 0.0 : m_samples.back().distance;
    }


    f64 curve::point_parameter(u32 i) const {

        build(m_built_resolution ? m_built_resolution : 16);
        return i < m_point_t.size() ? m_point_t[i] : 1.0;
    }


    std::span<const curve_sample> curve::samples() const {

        build(m_built_resolution ? m_built_resolution : 16);
        return m_samples;
    }


    curve_sample curve::sample(f64 t) const {

        build(m_built_resolution ? m_built_resolution : 16);
        if (m_samples.empty())
            return {};
        t = std::clamp(t, 0.0, 1.0);
        const auto it = std::ranges::lower_bound(m_samples, t, {}, &curve_sample::t);
        if (it == m_samples.begin())
            return m_samples.front();
        if (it == m_samples.end())
            return m_samples.back();
        const curve_sample& b = *it;
        const curve_sample& a = *(it - 1);
        const f64 span = b.t - a.t;
        const f32 u = span > 0.0 ? static_cast<f32>((t - a.t) / span) : 0.0f;
        curve_sample out{};
        out.position = glm::mix(a.position, b.position, u);
        out.tangent = glm::normalize(glm::mix(a.tangent, b.tangent, u));
        out.normal = glm::mix(a.normal, b.normal, u);
        out.normal = glm::normalize(out.normal - glm::dot(out.normal, out.tangent) * out.tangent);
        out.binormal = glm::cross(out.tangent, out.normal);
        out.t = t;
        out.distance = a.distance + (b.distance - a.distance) * static_cast<f64>(u);
        return out;
    }


    attribute_value curve::attribute(attribute_id id, f64 t) const {

        const column* c = find_point_column(id);
        if (!c || c->values.empty())
            return {};
        build(m_built_resolution ? m_built_resolution : 16);
        const u32 n = static_cast<u32>(std::min(c->values.size(), m_point_t.size()));
        if (n == 0)
            return c->default_value;
        if (t <= m_point_t[0])
            return c->values[0];
        for (u32 i = 0; i + 1 < n; ++i) {
            if (t <= m_point_t[i + 1]) {
                const f64 span = m_point_t[i + 1] - m_point_t[i];
                const f32 u = span > 0.0 ? static_cast<f32>((t - m_point_t[i]) / span) : 0.0f;
                return lerp_value(c->values[i], c->values[i + 1], u);
            }
        }
        return c->values[n - 1];
    }


    u64 curve::hash() const noexcept {

        u64 h = hash_combine(static_cast<u64>(m_type), m_closed ? 1u : 0u);
        h = hash_combine(h, fnv1a_64(std::string_view(reinterpret_cast<const char*>(m_points.data()), m_points.size() * sizeof(glm::vec3))));
        for (const column& c : m_columns) {
            h = hash_combine(h, c.id.hash);
            for (const attribute_value& v : c.values)
                h = hash_combine(h, hash_value(v));
        }
        return h;
    }

}
