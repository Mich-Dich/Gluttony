
#include "util/pch.h"
#include "chunk_mesh.h"



namespace GLT::terrain {

    chunk_mesh_result build_chunk_mesh(const field& heights, const region& r, const chunk_mesh_options& options) {

        chunk_mesh_result result{};
        auto& mesh = result.data;

        const u32 w = r.width;
        const u32 h = r.height;
        if (w < 2 || h < 2)
            return result;

        const f32 inv = static_cast<f32>(0.5 / r.spacing);
        const bool skirt = options.skirt_depth > 0.0f;
        const u32 border = 2 * (w - 1) + 2 * (h - 1);

        mesh.vertices.resize(static_cast<size_t>(w) * h + (skirt ? border : 0));
        mesh.indices.reserve(static_cast<size_t>(w - 1) * (h - 1) * 6 + (skirt ? static_cast<size_t>(border) * 6 : 0));

        f32 lo = std::numeric_limits<f32>::max();
        f32 hi = std::numeric_limits<f32>::lowest();
        for (u32 j = 0; j < h; ++j)
            for (u32 i = 0; i < w; ++i) {
                const i64 gx = r.x0 + i;
                const i64 gy = r.y0 + j;
                const f32 y = heights.at(gx, gy);
                const f32 dx = (heights.at(gx + 1, gy) - heights.at(gx - 1, gy)) * inv;
                const f32 dz = (heights.at(gx, gy + 1) - heights.at(gx, gy - 1)) * inv;
                const glm::dvec2 p = r.position(gx, gy);

                GLT::asset::mesh::vertex& v = mesh.vertices[static_cast<size_t>(j) * w + i];
                v.position = glm::vec3(static_cast<f32>(p.x), y, static_cast<f32>(p.y));
                v.normal = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
                v.tangent = glm::vec4(glm::normalize(glm::vec3(1.0f, dx, 0.0f)), 1.0f);
                v.uv0 = glm::vec2(static_cast<f32>(p.x), static_cast<f32>(p.y)) * options.uv_scale;
                lo = std::min(lo, y);
                hi = std::max(hi, y);
            }

        // Counter-clockwise seen from above (the renderer's front face)
        for (u32 j = 0; j + 1 < h; ++j)
            for (u32 i = 0; i + 1 < w; ++i) {
                const u32 a = j * w + i, b = a + 1, c = a + w, d = c + 1;
                mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
            }

        if (skirt) {
            // The border as one closed loop: top row, right column, bottom row backwards, left column backwards
            std::vector<u32> loop;
            loop.reserve(border);
            for (u32 i = 0; i + 1 < w; ++i)      loop.push_back(i);
            for (u32 j = 0; j + 1 < h; ++j)      loop.push_back(j * w + (w - 1));
            for (u32 i = w - 1; i > 0; --i)      loop.push_back((h - 1) * w + i);
            for (u32 j = h - 1; j > 0; --j)      loop.push_back(j * w);

            const u32 first_skirt = w * h;
            for (u32 k = 0; k < border; ++k) {
                GLT::asset::mesh::vertex v = mesh.vertices[loop[k]];
                v.position.y -= options.skirt_depth;
                mesh.vertices[first_skirt + k] = v;
            }
            // The loop runs clockwise seen from above, so (a, b, sa) / (b, sb, sa) face outwards. Only that side: a second,
            // inward-facing copy z-fights with it and is shaded with the surface normal from behind (dark seams)
            for (u32 k = 0; k < border; ++k) {
                const u32 a = loop[k], b = loop[(k + 1) % border];
                const u32 sa = first_skirt + k, sb = first_skirt + (k + 1) % border;
                mesh.indices.insert(mesh.indices.end(), { a, b, sa, b, sb, sa });
            }
            lo -= options.skirt_depth;
        }

        result.min_height = lo;
        result.max_height = hi;
        return result;
    }

}
