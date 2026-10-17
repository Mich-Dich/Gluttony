
#include "util/pch.h"
#include "util/jolt_pch.h"

#include "shape_builder.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    constexpr u32                                   kMaxCompoundDepth = 8;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    std::expected<JPH::ShapeRefC, physics_error> build_compound(const compound& c, u32 depth);

    std::expected<JPH::ShapeRefC, physics_error> build_data(const shape_data& data, u32 depth);

    u8 compute_motion_mask(const shape_data& data);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::expected<JPH::ShapeRefC, physics_error> build_compound(const compound& c, u32 depth) {

        if (c.children.empty())
            return std::unexpected(physics_error::invalid_argument);

        JPH::StaticCompoundShapeSettings settings;
        for (const auto& child : c.children) {
            if (!child.shape)
                return std::unexpected(physics_error::invalid_argument);

            auto child_result = build_data(*child.shape, depth + 1);
            if (!child_result)
                return child_result;      // propagate error

            // Decompose the child->parent mat4. We require unit scale for v0; supporting ScaledShape in a compound is possible
            // but adds a separate shape category and a separate build path, which v0 doesn't need yet
            const glm::vec3 translation(child.local[3]);
            const glm::mat3 rot_scale(child.local);

            const glm::vec3 sx = rot_scale[0], sy = rot_scale[1], sz = rot_scale[2];
            const f32 kEps = 1e-4f;
            if (std::abs(glm::dot(sx, sx) - 1.f) > kEps ||
                std::abs(glm::dot(sy, sy) - 1.f) > kEps ||
                std::abs(glm::dot(sz, sz) - 1.f) > kEps) {
                return std::unexpected(physics_error::unsupported_feature);
            }

            const glm::mat3 rot_only(glm::normalize(sx), glm::normalize(sy), glm::normalize(sz));
            const glm::quat rotation = glm::quat_cast(rot_only);

            settings.AddShape(to_jolt(translation), to_jolt(rotation), child_result->GetPtr());
        }

        auto result = settings.Create();
        if (result.HasError())
            return std::unexpected(physics_error::invalid_argument);
        return result.Get();
    }


    std::expected<JPH::ShapeRefC, physics_error> build_data(const shape_data& data, u32 depth) {

        if (depth > kMaxCompoundDepth)
            return std::unexpected(physics_error::invalid_argument);

        return std::visit([&](auto&& v) -> std::expected<JPH::ShapeRefC, physics_error> {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, sphere>)
                return JPH::ShapeRefC(new JPH::SphereShape(v.radius));

            else if constexpr (std::is_same_v<T, box>)
                return JPH::ShapeRefC(new JPH::BoxShape(to_jolt(v.half_extents)));

            else if constexpr (std::is_same_v<T, capsule>)
                // Jolt: CapsuleShape(halfHeightOfCylinder, radius).
                // Contract: half_height is the half-distance between sphere
                // centers, which is exactly Jolt's halfHeightOfCylinder.
                return JPH::ShapeRefC(new JPH::CapsuleShape(v.half_height, v.radius));

            else if constexpr (std::is_same_v<T, cylinder>)
                return JPH::ShapeRefC(new JPH::CylinderShape(v.half_height, v.radius));

            else if constexpr (std::is_same_v<T, plane>) {

                const f32 n2 = glm::dot(v.normal, v.normal);
                if (std::abs(n2 - 1.f) > 1e-4f)
                    return std::unexpected(physics_error::invalid_argument);

                return JPH::ShapeRefC(new JPH::PlaneShape(JPH::Plane(to_jolt(v.normal), v.distance)));

            } else if constexpr (std::is_same_v<T, convex_hull>) {

                if (v.points.size() < 4)
                    return std::unexpected(physics_error::invalid_argument);

                JPH::ConvexHullShapeSettings settings;
                settings.mPoints.reserve(static_cast<JPH::uint>(v.points.size()));
                for (const auto& p : v.points)
                    settings.mPoints.push_back(to_jolt(p));

                auto result = settings.Create();
                if (result.HasError())
                    return std::unexpected(physics_error::invalid_argument);
                return result.Get();

            } else if constexpr (std::is_same_v<T, triangle_mesh>) {

                if (v.indices.size() % 3 != 0)
                    return std::unexpected(physics_error::invalid_argument);

                JPH::MeshShapeSettings settings;
                settings.mTriangleVertices.reserve(static_cast<JPH::uint>(v.vertices.size()));
                for (const auto& p : v.vertices)
                    settings.mTriangleVertices.push_back(JPH::Float3(p.x, p.y, p.z));

                settings.mIndexedTriangles.reserve(static_cast<JPH::uint>(v.indices.size() / 3));
                for (size_t i = 0; i + 2 < v.indices.size(); i += 3)
                    settings.mIndexedTriangles.push_back(JPH::IndexedTriangle(v.indices[i], v.indices[i + 1], v.indices[i + 2]));

                auto result = settings.Create();
                if (result.HasError())
                    return std::unexpected(physics_error::invalid_argument);
                return result.Get();

            } else if constexpr (std::is_same_v<T, height_field>) {

                // Validate
                if (v.width < 2 || v.height < 2)
                    return std::unexpected(physics_error::invalid_argument);
                if (v.samples.size() < static_cast<size_t>(v.width) * v.height)
                    return std::unexpected(physics_error::invalid_argument);

                // Jolt's HeightFieldShape is square-only, but the contract permits rectangular fields. For v0 we build a MeshShape
                // from the grid, which is always correct. A future revision can dispatch to HeightFieldShape when width == height
                const u32 w = v.width;
                const u32 h = v.height;

                JPH::MeshShapeSettings settings;
                settings.mTriangleVertices.reserve(static_cast<JPH::uint>(w) * h);
                for (u32 y = 0; y < h; ++y) {
                    for (u32 x = 0; x < w; ++x) {
                        const f32 sample = v.samples[static_cast<size_t>(y) * w + x];
                        settings.mTriangleVertices.push_back(JPH::Float3(
                            static_cast<float>(x) * v.scale.x,
                            sample                * v.scale.y,
                            static_cast<float>(y) * v.scale.z));
                    }
                }

                settings.mIndexedTriangles.reserve(static_cast<JPH::uint>((w - 1) * (h - 1) * 2));
                for (u32 y = 0; y < h - 1; ++y) {
                    for (u32 x = 0; x < w - 1; ++x) {
                        const u32 i00 = y * w + x;
                        const u32 i10 = i00 + 1;
                        const u32 i01 = i00 + w;
                        const u32 i11 = i01 + 1;
                        settings.mIndexedTriangles.push_back(JPH::IndexedTriangle(i00, i01, i10));
                        settings.mIndexedTriangles.push_back(JPH::IndexedTriangle(i10, i01, i11));
                    }
                }

                auto result = settings.Create();
                if (result.HasError())
                    return std::unexpected(physics_error::invalid_argument);
                return result.Get();
            
            } else if constexpr (std::is_same_v<T, GLT::ref<const compound>>) {

                if (!v)
                    return std::unexpected(physics_error::invalid_argument);
                return build_compound(*v, depth);
            
            } else
                static_assert(sizeof(T) == 0, "unhandled shape variant");

        }, data);
    }

    // Recursive mask. Compound = bitwise AND of children's masks (contract §Cached validation). Empty compound → 0 (invalid)
    u8 compute_motion_mask(const shape_data& data) {

        return std::visit([&](auto&& v) -> u8 {
            using T = std::decay_t<decltype(v)>;

            if constexpr (std::is_same_v<T, sphere>
                        || std::is_same_v<T, box>
                        || std::is_same_v<T, capsule>
                        || std::is_same_v<T, cylinder>
                        || std::is_same_v<T, convex_hull>)
                return MASK_ALL;

            else if constexpr (std::is_same_v<T, plane>)
                return MASK_STATIC_KINEMATIC;

            else if constexpr (std::is_same_v<T, triangle_mesh>
                            || std::is_same_v<T, height_field>)
                return MASK_STATIC_ONLY;

            else if constexpr (std::is_same_v<T, GLT::ref<const compound>>) {

                if (!v || v->children.empty())
                    return 0;
                u8 mask = MASK_ALL;
                for (const auto& child : v->children) {
                    if (!child.shape)
                        return 0;
                    mask &= compute_motion_mask(*child.shape);
                }
                return mask;

            } else
                static_assert(sizeof(T) == 0, "unhandled shape variant");

        }, data);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::expected<built_shape, physics_error> build_shape(const shape_desc& desc) {

        auto shape_result = build_data(desc.data, 0);
        if (!shape_result)
            return std::unexpected(shape_result.error());

        return built_shape{
            .shape = *shape_result,
            .motion_mask = compute_motion_mask(desc.data),
        };
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
