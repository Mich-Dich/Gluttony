#include "procedural/construct/propagation.h"



namespace GLT::construct {

    // STATIC VARIABLES ================================================================================================

    namespace {

        constexpr std::string_view                      builtin_modes[] = {
            "copy", "interpolate", "reduce_min", "reduce_max", "reduce_sum", "reduce_avg", "decay", "boundary", "drop",
        };


        template<typename T>
        constexpr bool is_vector_v = std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>;


        // Same alternative as [like], from a f64 (numbers) — used to write reduced / scaled values back
        attribute_value scale_value(const attribute_value& v, f64 factor) {

            return std::visit([&](const auto& x) -> attribute_value {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, f32>)                           return static_cast<f32>(x * factor);
                else if constexpr (std::is_same_v<T, f64>)                      return x * factor;
                else if constexpr (std::is_same_v<T, i32>)                      return static_cast<i32>(std::llround(x * factor));
                else if constexpr (std::is_same_v<T, i64>)                      return static_cast<i64>(std::llround(static_cast<f64>(x) * factor));
                else if constexpr (is_vector_v<T>)                              return T(x * static_cast<f32>(factor));
                else                                                            return x;
            }, v);
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::span<const std::string_view> builtin_mode_names() noexcept { return builtin_modes; }


    attribute_value weighted_sum(const attribute_io& io, bool normalize) {

        if (io.source_count() == 0)
            return {};
        const attribute_value& first = io.source(io.dominant());

        f64 total = 0.0;
        for (u32 i = 0; i < io.source_count(); ++i)
            total += io.weight(i);
        const f64 norm = (normalize && total > 0.0) ? 1.0 / total : 1.0;

        return std::visit([&](const auto& like) -> attribute_value {
            using T = std::decay_t<decltype(like)>;
            if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64> || std::is_same_v<T, i32> || std::is_same_v<T, i64>) {
                f64 sum = 0.0;
                for (u32 i = 0; i < io.source_count(); ++i)
                    if (const T* v = std::get_if<T>(&io.source(i)))
                        sum += static_cast<f64>(*v) * io.weight(i);
                sum *= norm;
                if constexpr (std::is_integral_v<T>)    return static_cast<T>(std::llround(sum));
                else                                    return static_cast<T>(sum);
            } else if constexpr (is_vector_v<T>) {
                T sum(0.0f);
                for (u32 i = 0; i < io.source_count(); ++i)
                    if (const T* v = std::get_if<T>(&io.source(i)))
                        sum += *v * io.weight(i);
                return T(sum * static_cast<f32>(norm));
            } else if constexpr (std::is_same_v<T, glm::quat>) {
                glm::quat sum(0.0f, 0.0f, 0.0f, 0.0f);
                for (u32 i = 0; i < io.source_count(); ++i)
                    if (const glm::quat* q = std::get_if<glm::quat>(&io.source(i))) {
                        const glm::quat aligned = glm::dot(sum, *q) < 0.0f ? -*q : *q;
                        sum += aligned * io.weight(i);
                    }
                return glm::length(sum) > 0.0f ? glm::normalize(sum) : like;
            } else {
                return like;                            // bool, names, strings: the dominant source
            }
        }, first);
    }


    bool apply_builtin_mode(name_id mode, const attribute_desc& desc, const lineage_event& event, attribute_io& io) {

        static const name_id copy = intern("copy"), interpolate = intern("interpolate"), reduce_min = intern("reduce_min"),
            reduce_max = intern("reduce_max"), reduce_sum = intern("reduce_sum"), reduce_avg = intern("reduce_avg"),
            decay = intern("decay"), boundary = intern("boundary"), drop = intern("drop");

        if (io.source_count() == 0 && mode != drop) {
            io.leave_default();
            return mode == copy || mode == interpolate || mode == reduce_min || mode == reduce_max || mode == reduce_sum
                || mode == reduce_avg || mode == decay || mode == boundary;
        }

        if (mode == copy) {
            io.write(io.source(io.dominant()));
            return true;
        }
        if (mode == interpolate || mode == reduce_avg) {
            io.write(weighted_sum(io, true));
            return true;
        }
        if (mode == reduce_sum) {
            // Unweighted sum: every source counts once
            f64 sum = 0.0;
            for (u32 i = 0; i < io.source_count(); ++i)
                sum += as_number(io.source(i));
            const attribute_value& like = io.source(io.dominant());
            const f64 base = as_number(like);
            io.write(base != 0.0 ? scale_value(like, sum / base) : like);
            return true;
        }
        if (mode == reduce_min || mode == reduce_max) {
            u32 best = 0;
            for (u32 i = 1; i < io.source_count(); ++i) {
                const f64 a = as_number(io.source(i)), b = as_number(io.source(best));
                if (mode == reduce_min ? a < b : a > b)
                    best = i;
            }
            io.write(io.source(best));
            return true;
        }
        if (mode == decay) {
            const attribute_value& v = io.source(io.dominant());
            io.write(event.depth_delta ? scale_value(v, std::pow(static_cast<f64>(desc.rate), static_cast<f64>(event.depth_delta))) : v);
            return true;
        }
        if (mode == boundary) {
            if (!event.is_seam) {
                io.write(io.source(io.dominant()));
                return true;
            }
            // A new seam gets the "set" value of the attribute's type
            const attribute_value& like = desc.default_value.index() ? desc.default_value : io.source(io.dominant());
            io.write(std::visit([](const auto& x) -> attribute_value {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, bool>)                          return true;
                else if constexpr (std::is_arithmetic_v<T>)                     return T(1);
                else                                                            return x;
            }, like));
            return true;
        }
        if (mode == drop) {
            io.leave_default();
            return true;
        }
        return false;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    u32 attribute_io::dominant() const noexcept {

        u32 best = 0;
        for (u32 i = 1; i < m_sources.size(); ++i)
            if (weight(i) > weight(best))
                best = i;
        return best;
    }


    void next_propagation::operator()(const attribute_desc& desc, const lineage_event& event, attribute_io& io) const {

        if (!m_rest.empty()) {
            m_rest.front()->propagate(desc, event, io, next_propagation(m_rest.subspan(1), m_fallback, m_fallback_mode));
            return;
        }
        if (m_fallback) {
            m_fallback->propagate(desc, event, io, next_propagation({}, nullptr, intern("copy")));
            return;
        }
        if (!apply_builtin_mode(m_fallback_mode, desc, event, io))
            apply_builtin_mode(intern("copy"), desc, event, io);
    }


    mode_propagator::mode_propagator(name_id default_mode, std::span<const tag_schema_desc> schemas) : m_default_mode(default_mode) {

        for (const tag_schema_desc& s : schemas)
            for (const attribute_desc& a : s.attributes)
                m_descs.emplace(a.id, a);
    }


    const attribute_desc& mode_propagator::describe(attribute_id id, const attribute_value& column_default) const {

        if (auto it = m_descs.find(id); it != m_descs.end())
            return it->second;
        std::scoped_lock lock(m_mutex);
        auto& slot = m_synth[id];
        if (!slot) {
            slot = std::make_unique<attribute_desc>();
            slot->id = id;
            slot->default_value = column_default;
            slot->domains = attribute_domain::all;
        }
        return *slot;
    }


    void mode_propagator::apply(const attribute_desc& desc, const lineage_event& event, attribute_io& io) const {

        const name_id mode = desc.propagation.empty() ? m_default_mode : desc.propagation;
        if (!apply_builtin_mode(mode, desc, event, io))
            apply_builtin_mode(intern("copy"), desc, event, io);
    }

}
