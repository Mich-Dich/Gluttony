#include "procedural/construct/generator.h"

#include "procedural/construct/evaluation.h"



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    // profile_curve: monotone cubic (Fritsch-Carlson): smooth, never overshoots between points ----------------------------

    f32 profile_curve::evaluate(f32 x) const noexcept {

        if (points.empty())
            return 0.0f;
        if (points.size() == 1 || x <= points.front().x)
            return points.front().y;
        if (x >= points.back().x)
            return points.back().y;

        size_t i = 1;
        while (i < points.size() - 1 && points[i].x < x)
            ++i;
        const glm::vec2 p0 = points[i - 1], p1 = points[i];
        const f32 h = p1.x - p0.x;
        if (h <= 1e-6f)
            return p1.y;

        auto secant = [&](size_t k) {
            const f32 dx = points[k + 1].x - points[k].x;
            return dx > 1e-6f ? (points[k + 1].y - points[k].y) / dx : 0.0f;
        };
        auto tangent = [&](size_t k) -> f32 {
            if (k == 0)
                return secant(0);
            if (k == points.size() - 1)
                return secant(k - 1);
            const f32 a = secant(k - 1), b = secant(k);
            if (a * b <= 0.0f)
                return 0.0f;
            return 2.0f / (1.0f / a + 1.0f / b);            // harmonic mean: monotone
        };
        const f32 m0 = tangent(i - 1), m1 = tangent(i);
        const f32 t = (x - p0.x) / h, t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * p0.y + (t3 - 2 * t2 + t) * h * m0 + (-2 * t3 + 3 * t2) * p1.y + (t3 - t2) * h * m1;
    }


    f32 profile_curve::max_value() const noexcept {

        f32 m = points.empty() ? 0.0f : points.front().y;
        for (const glm::vec2& p : points)
            m = std::max(m, p.y);
        return m;
    }


    void profile_curve::sanitize() {

        for (glm::vec2& p : points)
            p.x = std::clamp(p.x, 0.0f, 1.0f);
        std::ranges::stable_sort(points, {}, &glm::vec2::x);
        if (points.empty())
            points = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
        if (points.size() == 1)
            points.push_back({ 1.0f, points.front().y });
        points.front().x = 0.0f;
        points.back().x = 1.0f;
    }

    // generator_instance ---------------------------------------------------------------------------------------------------

    const setting_value* generator_instance::find(std::string_view name) const noexcept {

        for (const auto& [n, v] : settings)
            if (n == name)
                return &v;
        return nullptr;
    }


    void generator_instance::set(std::string_view name, setting_value value) {

        for (auto& [n, v] : settings)
            if (n == name) {
                v = std::move(value);
                return;
            }
        settings.emplace_back(std::string(name), std::move(value));
    }


    void generator_instance::reset(std::string_view name) {

        std::erase_if(settings, [&](const auto& s) { return s.first == name; });
    }


    const setting_desc* generator_desc::find_setting(std::string_view setting) const noexcept {

        for (const setting_desc& s : settings)
            if (s.name == setting)
                return &s;
        return nullptr;
    }


    setting_kind kind_of(const setting_value& v) noexcept {

        switch (v.index()) {
            case 0:  return setting_kind::number;
            case 1:  return setting_kind::integer;
            case 2:  return setting_kind::toggle;
            case 3:  return setting_kind::range;
            case 4:  return setting_kind::vector;
            case 5:  return setting_kind::profile;
            default: return setting_kind::text;
        }
    }


    f64 setting_number(const setting_value& v) noexcept {

        return std::visit([](const auto& x) -> f64 {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, i32>)    return static_cast<f64>(x);
            else if constexpr (std::is_same_v<T, bool>)                         return x ? 1.0 : 0.0;
            else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3>) return static_cast<f64>(x.x);
            else if constexpr (std::is_same_v<T, profile_curve>)                return x.points.empty() ? 0.0 : static_cast<f64>(x.points.front().y);
            else                                                                return 0.0;
        }, v);
    }


    void prune_defaults(generator_instance& instance, const generator_desc& desc) {

        std::erase_if(instance.settings, [&](const auto& s) {
            const setting_desc* d = desc.find_setting(s.first);
            return !d || d->default_value == s.second;
        });
    }


    u32 next_stage_id(std::span<const generator_instance> stages) noexcept {

        u32 id = 0;
        for (const generator_instance& g : stages)
            id = std::max(id, g.id);
        return id + 1;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    generator_context::generator_context(const evaluation& e, const generator_instance& instance, const generator_desc& desc,
                                         std::vector<const element*> inputs, std::vector<const element*> guides)
        : m_expand(e, instance.label, intern("generate")), m_instance(&instance), m_desc(&desc),
          m_inputs(std::move(inputs)), m_guides(std::move(guides)) {}


    const setting_value& generator_context::setting(std::string_view name) const {

        if (const setting_value* v = m_instance->find(name))
            return *v;
        if (const setting_desc* d = m_desc->find_setting(name))
            return d->default_value;
        static const setting_value none{ 0.0f };
        return none;
    }


    f32 generator_context::number(std::string_view name, f32 fallback) const {

        const setting_value& v = setting(name);
        return std::holds_alternative<std::string>(v) ? fallback : static_cast<f32>(setting_number(v));
    }


    i32 generator_context::integer(std::string_view name, i32 fallback) const {

        const setting_value& v = setting(name);
        return std::holds_alternative<std::string>(v) ? fallback : static_cast<i32>(std::lround(setting_number(v)));
    }


    bool generator_context::toggle(std::string_view name, bool fallback) const {

        const setting_value& v = setting(name);
        return std::holds_alternative<std::string>(v) ? fallback : setting_number(v) != 0.0;
    }


    element_builder generator_context::emit(const element& parent, std::string_view name, i64 index, kind_id kind) {

        return m_expand.emit(parent, std::format("{}@{}", name, m_instance->id), index, kind);
    }


    element_builder generator_context::emit_root(std::string_view name, i64 index, kind_id kind) {

        return m_expand.emit_at(element_path::root(std::format("stage{}", m_instance->id)).child(name, index), kind);
    }


    void generator_context::relate(relation_id kind, name_id type, element_handle a, element_handle b, attribute_set params) {

        m_expand.relate(kind, type, a, b, std::move(params));
    }


    void generator_context::diagnose(severity s, const element* on, std::string message) {

        if (on)
            m_expand.diagnose(s, *on, std::move(message));
        else
            m_expand.diagnostics().push_back({ s, {}, m_instance->label, std::move(message) });
    }


    u64 generator_context::stage_salt(u64 salt) const noexcept {

        return hash_combine(hash_combine(salt, 0x57a6e5eedull + m_instance->id), m_instance->seed);
    }


    rng generator_context::random(const element& e, u64 salt) const { return query().random(e, stage_salt(salt)); }


    rng generator_context::random(u64 salt) const {

        return rng(hash_combine(query().construct_seed(), stage_salt(salt)));
    }

}
