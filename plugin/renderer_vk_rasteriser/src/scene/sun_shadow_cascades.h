#pragma once

#include "scene/type.h"



// Sun (directional light) shadows over long distances. CPU only, no Vulkan: scene_renderer feeds it the sun direction,
// the camera position and the bounds of casters that moved, and draws / samples what it plans (scene_renderer_shadow.inl).
//
// The shadow map is a set of nested square maps around the camera ("cascades"). Cascade 0 covers the first metres at a
// high resolution, every following one a multiple of that range with the same number of texels, up to the shadow
// distance. All of them live side by side in one atlas texture and are drawn and sampled like the views of the local
// lights (gpu_shadow_view, shadow_cull.comp.glsl, renderer_project_vertex / renderer_sample_shadow in shading_strings.h).
//
// What keeps long shadow distances cheap:
//   - A cascade is centred on the camera POSITION and is larger than its range by SUN_CASCADE_SLACK. Where the camera
//     looks does not matter, and it only has to be drawn again once the camera has travelled out of that slack. The
//     far cascades, the ones that hold most of the world, therefore follow the camera in rare, large steps
//   - A cascade is otherwise kept until a caster inside it changes (only casters that are large enough to show up in
//     its texels count) or the sun moves. Such redraws are spread out: cascade c at most every 2^c frames
//   - At most SUN_CASCADES_PER_FRAME cascades are drawn in one frame. A cascade that waits keeps being sampled with
//     the view it was drawn with, so nothing flickers while the work is caught up on
//   - The cull shader drops casters smaller than a texel of the cascade and picks their LOD by the size in texels: the
//     far cascades draw few, simple meshes
// Texel grids are fixed in the world (the centre moves in whole texels), so shadow edges do not crawl when a cascade
// follows the camera.

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // A cascade's map is this much larger than the range it has to cover. The camera may move slack * range away from
    // the map's centre before the map is drawn again around the new position
    inline constexpr f32                                SUN_CASCADE_SLACK = 0.2f;

    // Scene extent along the light is rounded outwards to this step (metres), so the depth range of the maps does not
    // change with every caster that moves a little
    inline constexpr f32                                SUN_DEPTH_STEP = 32.0f;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class sun_shadow_cascades {
    public:

        struct settings {

            u32                                         cascades = MAX_SUN_CASCADES;    // at most (a short distance uses fewer, see used_cascades)
            u32                                         resolution = 2048;          // texels per side of one cascade
            f32                                         first_range = 30.0f;        // distance from the camera that cascade 0 covers
            f32                                         distance = 1500.0f;         // ... and the last cascade (the shadow distance)
            f32                                         normal_bias = 2.0f;         // texels of the cascade, along the surface normal
            f32                                         bias = 1.0f;                // texels of the cascade, towards the sun
        };

        struct frame_stats {

            u32                                         cascades = 0;               // cascades with a depth map (sampled this frame)
            u32                                         drawn = 0;                  // drawn this frame
            u32                                         waiting = 0;                // out of date, drawn in a later frame
        };

        // Forgets every depth map
        void clear();

        // Casters changed in an unknown way: every cascade is drawn again (spread over the next frames)
        void invalidate_all();

        // Casters moved, appeared or disappeared: [spheres] are their world bounds (xyz = centre, w = radius), old and new
        // position. Only cascades that one of them reaches, and in which it is large enough to be drawn, are drawn again
        void invalidate(std::span<const glm::vec4> spheres);

        // The cascades of the last plan() were never drawn (the frame was not rendered): they get their previous maps back
        void discard_frame();

        // The atlas image was replaced: only what the last plan() draws is in it
        void atlas_lost();

        // [direction]: the way the sun light travels (unit vector). [caster_near] / [caster_far]: extent of all shadow
        // casters along it, as dot(position, direction) of the points closest to / farthest from the sun.
        // [draw_budget]: most cascades to draw now
        void plan(const glm::vec3& direction, const glm::vec3& camera_position, f32 caster_near, f32 caster_far, const settings& config,
            u32 draw_budget);

        // One view per cascade, to upload at SUN_SHADOW_VIEW_FIRST. atlas_rect.z == 0: no depth map (yet). Cascades to
        // draw this frame are flagged (gpu_shadow_view::cull) and listed in render_tiles() (view = index in the buffer)
        [[nodiscard]] const std::array<gpu_shadow_view, MAX_SUN_CASCADES>& views() const { return m_views; }
        [[nodiscard]] u32 cascade_count() const { return m_cascade_count; }
        [[nodiscard]] const std::vector<local_shadow_tile>& render_tiles() const { return m_render_tiles; }
        [[nodiscard]] const frame_stats& stats() const { return m_stats; }

        // Side of the atlas texture that holds the cascades (a 2 x 2 grid of tiles)
        [[nodiscard]] static u32 atlas_size(const u32 resolution) { return clamp_resolution(resolution) * 2u; }

        // Cascades in use: as many as [config] allows, but each one covers at least twice the range of the one before it
        // (a shadow distance close to first_range does not need four maps of nearly the same size)
        [[nodiscard]] static u32 used_cascades(const settings& config);

        // Distance from the camera that [cascade] covers: first_range for cascade 0, distance for the last one, a geometric
        // series in between
        [[nodiscard]] static f32 cascade_range(const settings& config, u32 cascade);

        // Shadow distance for a scene whose casters end [reach] metres from the camera: no further than needed, so the
        // cascades in between get finer. [limit]: the distance that is set. The result moves in steps of sqrt(2) and
        // keeps [current] while that is between the reach and twice the reach: every change draws the cascades again
        [[nodiscard]] static f32 fit_distance(f32 current, f32 reach, f32 limit);

    private:

        struct cascade {

            bool                                        rendered = false;           // the atlas tile holds the depth map of [view]
            bool                                        dirty = true;               // out of date: drawn again when it is its turn
            u64                                         rendered_frame = 0;
            f32                                         range = 0.0f;               // the settings it was drawn with
            u32                                         resolution = 0;
            glm::vec3                                   direction{ 0.0f };          // the sun direction it was drawn with
            f32                                         depth_near = 0.0f;          // its depth range along the light
            f32                                         depth_far = 0.0f;
            gpu_shadow_view                             view{};                     // as it was drawn: sampled until the next draw
        };

        [[nodiscard]] static u32 clamp_resolution(const u32 resolution) { return std::clamp(std::bit_floor(std::max(resolution, 1u)), 256u, 4096u); }
        void render_cascade(u32 index, const glm::vec3& direction, const glm::vec3& camera_position, f32 depth_near, f32 depth_far,
            const settings& config);

        std::array<cascade, MAX_SUN_CASCADES>           m_cascades{};
        std::array<cascade, MAX_SUN_CASCADES>           m_previous{};               // state before the last plan() drew over it
        u64                                             m_frame = 0;

        // per frame
        std::array<gpu_shadow_view, MAX_SUN_CASCADES>   m_views{};
        std::vector<local_shadow_tile>                  m_render_tiles{};
        u32                                             m_cascade_count = 0;
        frame_stats                                     m_stats{};
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION: sun_shadow_cascades =======================================================================

    inline u32 sun_shadow_cascades::used_cascades(const settings& config) {

        const f32 last = std::max(config.distance, 1.f);
        const f32 first = std::min(std::max(config.first_range, 1.f), last);
        const u32 doublings = static_cast<u32>(std::clamp(std::floor(std::log2(last / first)), 0.f, 16.f));
        return std::clamp(1u + doublings, 1u, std::clamp(config.cascades, 1u, MAX_SUN_CASCADES));
    }


    inline f32 sun_shadow_cascades::cascade_range(const settings& config, const u32 cascade) {

        const u32 count = used_cascades(config);
        const f32 last = std::max(config.distance, 1.f);
        const f32 first = std::min(std::max(config.first_range, 1.f), last);
        if (count == 1 || cascade + 1 >= count)
            return last;
        return first * std::pow(last / first, static_cast<f32>(cascade) / static_cast<f32>(count - 1));
    }


    inline f32 sun_shadow_cascades::fit_distance(const f32 current, const f32 reach, const f32 limit) {

        if (!(reach < limit))                           // the scene goes on beyond the limit (or there is no scene)
            return limit;

        const f32 needed = std::max(reach, 1.f);
        if (current >= needed && current <= needed * 2.f && current <= limit)
            return current;
        return std::min(limit, std::exp2(std::ceil(std::log2(needed) * 2.f) * 0.5f));
    }


    inline void sun_shadow_cascades::clear() {

        m_cascades = {};
        m_previous = {};
        m_views = {};
        m_render_tiles.clear();
        m_cascade_count = 0;
        m_stats = {};
    }


    inline void sun_shadow_cascades::invalidate_all() {

        for (auto& item : m_cascades)
            item.dirty = true;
    }


    inline void sun_shadow_cascades::invalidate(const std::span<const glm::vec4> spheres) {

        if (spheres.empty())
            return;

        for (auto& item : m_cascades) {

            if (!item.rendered || item.dirty)
                continue;

            // The cull shader drops casters whose bounds are smaller than a texel (shadow_cull.comp.glsl). It measures the
            // sphere around the world box, which is at most sqrt(3) times the sphere given here
            const f32 smallest = item.view.params.x / 1.75f;

            for (const auto& sphere : spheres) {

                if (sphere.w < smallest)
                    continue;
                bool inside = true;
                for (const auto& plane : item.view.planes)
                    if (glm::dot(glm::vec3(plane), glm::vec3(sphere)) + plane.w < -sphere.w) { inside = false; break; }
                if (inside) {
                    item.dirty = true;
                    break;
                }
            }
        }
    }


    inline void sun_shadow_cascades::discard_frame() {

        for (u32 index = 0; index < MAX_SUN_CASCADES; ++index) {
            cascade& item = m_cascades[index];
            if (item.rendered && item.rendered_frame == m_frame) {
                item = m_previous[index];               // the atlas still holds that map
                item.dirty = true;
            }
        }
    }


    inline void sun_shadow_cascades::atlas_lost() {

        for (auto& item : m_cascades)
            if (item.rendered && item.rendered_frame != m_frame) {
                item.rendered = false;
                item.dirty = true;
            }
        for (auto& item : m_previous)
            item.rendered = false;                      // nothing to go back to
    }


    // Cascade [index] around the camera as it is now. The map's centre is a whole number of texels away from the origin
    // of the light's frame, so a texel covers the same piece of the world before and after
    inline void sun_shadow_cascades::render_cascade(const u32 index, const glm::vec3& direction, const glm::vec3& camera_position,
        const f32 depth_near, const f32 depth_far, const settings& config) {

        cascade& item = m_cascades[index];
        m_previous[index] = item;

        const u32 resolution = clamp_resolution(config.resolution);
        const f32 range = cascade_range(config, index);
        const f32 half = range * (1.f + SUN_CASCADE_SLACK);
        const f32 texel = 2.f * half / static_cast<f32>(resolution);
        const f32 atlas = static_cast<f32>(resolution * 2u);

        // Light frame: x right, y up, -z along the light (a rotation only, the origin stays the world origin)
        const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f);
        const glm::mat4 rotation = glm::lookAt(glm::vec3(0.f), direction, up);
        const glm::vec3 camera_ls = glm::vec3(rotation * glm::vec4(camera_position, 1.f));
        const f32 center_x = std::round(camera_ls.x / texel) * texel;
        const f32 center_y = std::round(camera_ls.y / texel) * texel;

        // Light frame -> clip space, written out (no GLM projection: its depth convention is a build setting). x, y in
        // [-1, 1] over the map with y flipped like the camera's projection (same triangle winding, so the materials'
        // depth pipelines cull the same faces), depth 0 at [depth_near] and 1 at [depth_far] along the light
        const f32 depth_range = std::max(depth_far - depth_near, 1.f);
        glm::mat4 projection(1.f);
        projection[0][0] = 1.f / half;
        projection[3][0] = -center_x / half;
        projection[1][1] = -1.f / half;
        projection[3][1] = center_y / half;
        projection[2][2] = -1.f / depth_range;
        projection[3][2] = -depth_near / depth_range;

        const f32 tile_x = static_cast<f32>((index % 2u) * resolution);
        const f32 tile_y = static_cast<f32>((index / 2u) * resolution);

        gpu_shadow_view view{};
        view.view_proj = projection * rotation;
        view.atlas_rect = glm::vec4(tile_x / atlas, tile_y / atlas, static_cast<f32>(resolution) / atlas, static_cast<f32>(resolution) / atlas);
        view.params = glm::vec4(texel, config.normal_bias, config.bias, 1.f / atlas);
        view.sphere = glm::vec4(camera_position, half);

        // The box of the map in the world, pointing inwards (cull shader, invalidate): four sides, then towards / away
        // from the sun
        const glm::vec3 right(rotation[0][0], rotation[1][0], rotation[2][0]);
        const glm::vec3 upward(rotation[0][1], rotation[1][1], rotation[2][1]);
        view.planes[0] = glm::vec4( right,   -(center_x - half));
        view.planes[1] = glm::vec4(-right,     center_x + half);
        view.planes[2] = glm::vec4( upward,  -(center_y - half));
        view.planes[3] = glm::vec4(-upward,    center_y + half);
        view.planes[4] = glm::vec4( direction, -depth_near);
        view.planes[5] = glm::vec4(-direction,  depth_far);

        // Cull shader: a group of one view, parallel light (no light sphere, a texel has the same size at every depth)
        view.cull = glm::vec4(0.f, 1.f, 1.f, 0.f);
        view.group_sphere = glm::vec4(0.f, 0.f, 0.f, 1e18f);

        item.view = view;
        item.rendered = true;
        item.dirty = false;
        item.rendered_frame = m_frame;
        item.range = range;
        item.resolution = resolution;
        item.direction = direction;
        item.depth_near = depth_near;
        item.depth_far = depth_far;
    }


    inline void sun_shadow_cascades::plan(const glm::vec3& direction, const glm::vec3& camera_position, const f32 caster_near,
        const f32 caster_far, const settings& config, const u32 draw_budget) {

        ++m_frame;
        m_render_tiles.clear();
        m_stats = {};

        const u32 count = used_cascades(config);
        const u32 resolution = clamp_resolution(config.resolution);
        for (u32 index = count; index < MAX_SUN_CASCADES; ++index)
            m_cascades[index] = {};                     // fewer cascades than before: the others are gone
        m_cascade_count = count;

        // Depth range of new maps: everything that can cast a shadow, rounded outwards. No casters: any range will do
        const bool has_casters = std::isfinite(caster_near) && std::isfinite(caster_far) && caster_near <= caster_far;
        const f32 depth_near = has_casters ? std::floor((caster_near - 1.f) / SUN_DEPTH_STEP) * SUN_DEPTH_STEP : 0.f;
        const f32 depth_far = has_casters ? std::ceil((caster_far + 1.f) / SUN_DEPTH_STEP) * SUN_DEPTH_STEP : SUN_DEPTH_STEP;

        // ---- 1. What is out of date, and how urgent ------------------------------------------------------------------
        // 3: no usable map. 2: the camera left the map's slack (the far edge of its range is no longer covered).
        // 1: a caster in it or the sun moved, and its interval is over. 0: nothing to do (or not yet)
        std::array<u32, MAX_SUN_CASCADES> urgency{};
        for (u32 index = 0; index < count; ++index) {

            cascade& item = m_cascades[index];
            const f32 range = cascade_range(config, index);

            if (!item.rendered || item.resolution != resolution || item.range != range) {
                urgency[index] = 3;
                continue;
            }

            // The sun moved, or a caster now lies outside the depth range the map was drawn with
            if (glm::dot(item.direction, direction) < 1.f - 1e-7f || (has_casters && (caster_near < item.depth_near || caster_far > item.depth_far)))
                item.dirty = true;

            // Camera in the map's own frame: +-1 is the edge of the map
            const glm::vec4 clip = item.view.view_proj * glm::vec4(camera_position, 1.f);
            const f32 offset = std::max(std::abs(clip.x), std::abs(clip.y));
            if (offset > SUN_CASCADE_SLACK / (1.f + SUN_CASCADE_SLACK))
                urgency[index] = 2;
            else if (item.dirty && m_frame - item.rendered_frame >= (u64{1} << index))
                urgency[index] = 1;
        }

        // ---- 2. Draw: the most urgent first. Among equals cascade 0 (the shadows next to the camera), then the one that
        //         waited longest for its interval
        for (u32 budget = draw_budget; budget > 0; --budget) {

            u32 best = UINT32_MAX;
            f64 best_score = 0.0;
            for (u32 index = 0; index < count; ++index) {
                if (urgency[index] == 0)
                    continue;
                const f64 waited = static_cast<f64>(m_frame - m_cascades[index].rendered_frame) / static_cast<f64>(u64{1} << index);
                const f64 score = static_cast<f64>(urgency[index]) * 1e6 + (index == 0 ? 9e5 : std::min(waited, 8e5));
                if (best == UINT32_MAX || score > best_score) {
                    best = index;
                    best_score = score;
                }
            }
            if (best == UINT32_MAX)
                break;

            render_cascade(best, direction, camera_position, depth_near, depth_far, config);
            urgency[best] = 0;
        }

        // ---- 3. Views -------------------------------------------------------------------------------------------------
        for (u32 index = 0; index < MAX_SUN_CASCADES; ++index) {

            const cascade& item = m_cascades[index];
            gpu_shadow_view view{};                     // atlas_rect.z == 0: no depth map, the next cascade takes over
            view.cull = glm::vec4(0.f, 1.f, 1.f, 0.f);
            view.group_sphere = glm::vec4(0.f, 0.f, 0.f, 1e18f);

            if (index < count && item.rendered) {
                view = item.view;
                view.params.y = config.normal_bias;     // sampling only: no need to draw again for these
                view.params.z = config.bias;
                if (item.rendered_frame == m_frame) {
                    view.cull.x = 1.f;
                    const u32 tile_x = (index % 2u) * item.resolution;
                    const u32 tile_y = (index / 2u) * item.resolution;
                    m_render_tiles.push_back({ SUN_SHADOW_VIEW_FIRST + index, tile_x, tile_y, item.resolution });
                    ++m_stats.drawn;
                }
                ++m_stats.cascades;
            }
            if (index < count && (urgency[index] != 0 || (item.rendered && item.dirty && item.rendered_frame != m_frame)))
                ++m_stats.waiting;
            m_views[index] = view;
        }
    }

}
