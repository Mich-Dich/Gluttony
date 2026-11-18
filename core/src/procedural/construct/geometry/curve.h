#pragma once

#include "procedural/construct/attributes.h"
#include "procedural/construct/geometry/mesh.h"

// Procedural construction: curves (splines) with rotation-minimizing frames and attributes along them.
// Control points carry attribute columns (radius, twist, tags); values between control points are interpolated by the
// curve parameter. The parameter t runs 0..1 by arc length.



namespace GLT::construct::geometry {

    // TYPES ===========================================================================================================

    enum class curve_type : u8 {

        polyline,
        catmull_rom,        // centripetal Catmull-Rom through every control point
    };


    struct curve_sample {

        glm::vec3                                       position{ 0.0f };
        glm::vec3                                       tangent{ 0.0f, 0.0f, 1.0f };
        glm::vec3                                       normal{ 1.0f, 0.0f, 0.0f };     // rotation-minimizing frame
        glm::vec3                                       binormal{ 0.0f, 1.0f, 0.0f };
        f64                                             t = 0.0;
        f64                                             distance = 0.0;                 // arc length from the start
    };


    class curve {
    public:

        curve() = default;
        curve(curve_type type, std::vector<glm::vec3> points, bool closed = false);

        [[nodiscard]] curve_type type() const noexcept { return m_type; }
        [[nodiscard]] bool closed() const noexcept { return m_closed; }
        [[nodiscard]] std::span<const glm::vec3> points() const noexcept { return m_points; }
        [[nodiscard]] u32 point_count() const noexcept { return static_cast<u32>(m_points.size()); }

        // Attributes per control point (created on first use)
        void set_point_attribute(attribute_id id, u32 point, attribute_value value);
        [[nodiscard]] const column* find_point_column(attribute_id id) const noexcept;
        [[nodiscard]] std::span<const column> point_columns() const noexcept { return m_columns; }

        // Arc-length table. Built on first use; [segments_per_span] samples between two control points
        void build(u32 segments_per_span = 16) const;
        [[nodiscard]] f64 length() const;
        // Parameter of control point [i] (its arc length / total length)
        [[nodiscard]] f64 point_parameter(u32 i) const;

        [[nodiscard]] curve_sample sample(f64 t) const;
        // The built samples (with frames), first to last
        [[nodiscard]] std::span<const curve_sample> samples() const;
        // Interpolated attribute at t: numbers / vectors linearly between the two control points around t, other types
        // from the nearer one. Monostate if the curve has no such column
        [[nodiscard]] attribute_value attribute(attribute_id id, f64 t) const;

        [[nodiscard]] u64 hash() const noexcept;

    private:

        [[nodiscard]] glm::vec3 evaluate_span(u32 span, f32 u) const;

        curve_type                                      m_type = curve_type::polyline;
        bool                                            m_closed = false;
        std::vector<glm::vec3>                          m_points{};
        std::vector<column>                             m_columns{};

        mutable std::vector<curve_sample>               m_samples{};
        mutable std::vector<f64>                        m_point_t{};
        mutable u32                                     m_built_resolution = 0;
    };

}
