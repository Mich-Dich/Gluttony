
#pragma once

#include "scene/type.h"



// Local (point / spot) light shadows: which lights get shadow maps, where they live in the atlas, and what has to be
// drawn this frame. CPU only, no Vulkan: scene_renderer feeds it the frame's lights and the bounds of casters that
// moved, and draws / samples what it plans (scene_renderer_shadow.inl).
//
// Every light keeps its own tiles (one per spot light, six per point light) until it is resized or evicted, and a tile
// is only drawn again when the light or a caster inside that face's frustum changed. Nothing here depends on where the
// camera LOOKS: the set of shadowed lights and their resolution follow the camera position only (with hysteresis), so
// turning the camera never switches shadows on or off. The camera frustum only orders the work (visible faces first).
//
// Two ranges. Lights within settings::distance of the camera share the atlas among themselves exactly as if there were
// no others (up to 80 % of it, 92 % before one has to shrink). Lights further away, up to settings::far_distance, get
// small tiles in what is left above that, at most 1 / LOCAL_SHADOW_FAR_ATLAS_SHARE of the atlas, and give them up
// again when a near light needs the room: distant lights stop shining through walls without taking resolution from
// the shadows next to the camera.

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // A light beyond settings::distance only gets shadow maps if it is at least this large on screen (radius / distance):
    // below that its lit area is a few pixels and a shadow in it cannot be seen
    inline constexpr f32                                LOCAL_SHADOW_FAR_MIN_RATIO = 0.02f;

    // Part of the atlas the lights beyond settings::distance may use together (1 / this)
    inline constexpr u64                                LOCAL_SHADOW_FAR_ATLAS_SHARE = 4;

    // FUNCTION DECLARATION ============================================================================================

    // View direction of a point light's cube face (same order as renderer_point_face in shading_strings.h)
    [[nodiscard]] inline glm::vec3 shadow_face_direction(u32 face);

    [[nodiscard]] inline glm::mat4 shadow_look_view(const glm::vec3& position, const glm::vec3& forward);

    // Planes of a view-projection matrix, normalised, pointing inwards (same as extract_frustum_planes in scene_renderer.cpp)
    inline void shadow_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]);

    // Conservative: false only if all 8 corners of the frustum are behind one of the camera planes
    [[nodiscard]] inline bool shadow_view_visible(const glm::mat4& view, const glm::vec3& position, f32 half_tan, f32 near_d, f32 far_d,
        const glm::vec4 (&camera_planes)[6]);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Square power-of-two tiles inside the square shadow atlas. A quadtree: allocating splits free nodes, releasing merges
    // them back, so the atlas does not fragment over time. Both walk one path of the tree (O(depth))
    class shadow_atlas_allocator {
    public:

        void reset(u32 atlas_size, u32 min_tile);
        [[nodiscard]] bool allocate(u32 size, u32& x, u32& y);
        void release(u32 x, u32 y, u32 size);

        [[nodiscard]] u64 used_texels() const { return m_used_texels; }
        [[nodiscard]] u32 atlas_size() const { return m_atlas_size; }

    private:

        enum : u8 { node_free = 0, node_split = 1, node_used = 2 };

        [[nodiscard]] u32 level_of(u32 size) const { return static_cast<u32>(std::countr_zero(m_atlas_size) - std::countr_zero(size)); }
        void refresh_largest(u32 node);

        // Implicit tree: children of node n are 4n + 1 .. 4n + 4, level 0 is the whole atlas
        std::vector<u8>                                 m_state{};
        std::vector<u8>                                 m_largest{};        // largest free tile below a node: (levels - its level), 0 = none
        u32                                             m_atlas_size = 0;
        u32                                             m_min_tile = 0;
        u32                                             m_levels = 0;
        u64                                             m_used_texels = 0;
    };


    class local_shadow_cache {
    public:

        struct settings {

            u32                                         atlas_size = 4096;
            u32                                         max_lights = MAX_SHADOW_LIGHTS;     // shadowed lights at once
            f32                                         distance = 80.0f;                   // camera to the light's sphere: these lights come first
            f32                                         far_distance = 0.0f;                // lights up to here use what is left of the atlas (<= distance: none)
            f32                                         normal_bias = 1.5f;
            f32                                         bias = 1.0f;
        };

        struct frame_stats {

            u32                                         candidates = 0;         // local lights in range
            u32                                         far_candidates = 0;     // of those: beyond settings::distance
            u32                                         shadowed = 0;           // of those: with at least one usable face
            u32                                         views = 0;              // views in the buffer (sampled this frame)
            u32                                         rendered = 0;           // views drawn this frame
            u32                                         waiting = 0;            // views that are out of date and wait for a later frame
            u32                                         unplaced = 0;           // lights in range without tiles (budget or atlas full)
            f32                                         atlas_fill = 0.0f;      // allocated part of the atlas, 0..1
        };

        // Forgets every light and frees the atlas
        void clear();

        // Casters changed in an unknown way: every tile is drawn again (spread over the next frames)
        void invalidate_all();

        // Casters moved, appeared or disappeared: [spheres] are their world bounds (xyz = centre, w = radius), old and new
        // position. Only faces whose frustum reaches one of them are drawn again
        void invalidate(std::span<const glm::vec4> spheres);

        // The tiles of the last plan() were never drawn (the frame was not rendered)
        void discard_frame();

        // The atlas image was replaced: only what the last plan() draws is in it
        void atlas_lost();

        // [lights]: this frame's packed lights; params.z of every local light is set (first shadow view + 1, or 0).
        // [keys]: one stable id per light (same light = same key in every frame). [view_budget]: most views to draw now
        void plan(std::span<gpu_light> lights, std::span<const u64> keys, const glm::vec3& camera_position,
            const glm::vec4 (&camera_planes)[6], const settings& config, u32 view_budget);

        // Views to upload (index = gpu_light::params.z - 1, plus the face for point lights). Everything that is drawn
        // this frame is inside the first cull_view_count() views; the views to draw are flagged (gpu_shadow_view::cull)
        [[nodiscard]] const std::vector<gpu_shadow_view>& views() const { return m_views; }
        [[nodiscard]] u32 cull_view_count() const { return m_cull_view_count; }
        [[nodiscard]] const std::vector<local_shadow_tile>& render_tiles() const { return m_render_tiles; }
        [[nodiscard]] const frame_stats& stats() const { return m_stats; }
        [[nodiscard]] bool empty() const { return m_lookup.empty(); }

    private:

        struct face {

            u32                                         x = 0;
            u32                                         y = 0;
            bool                                        allocated = false;
            bool                                        rendered = false;       // the tile holds the depth map of [view] (out of date if dirty)
            bool                                        dirty = true;           // has to be drawn (again)
            u64                                         rendered_frame = 0;
            gpu_shadow_view                             view{};                 // as it was drawn: sampled until the next draw
        };

        struct entry {

            u64                                         key = 0;
            u64                                         seen_frame = 0;
            bool                                        alive = false;
            u32                                         type = 0;               // 1 point, 2 spot
            u32                                         face_count = 0;
            u32                                         size = 0;               // tile size of every face, 0 = no tiles
            i32                                         tier = -1;              // resolution step by distance (hysteresis state)
            bool                                        far_range = false;      // this frame: beyond settings::distance
            f32                                         importance = 0.0f;      // this frame: its place among the candidates
            glm::vec4                                   position_radius{ 0.0f };
            glm::vec3                                   direction{ 0.0f };
            f32                                         cos_outer = 0.0f;
            std::array<face, 6>                         faces{};
        };

        struct candidate {

            u32                                         light = 0;
            u32                                         entry = UINT32_MAX;
            u64                                         key = 0;
            f32                                         sort_key = 0.0f;
            f32                                         ratio = 0.0f;           // radius / distance to the camera
            bool                                        far_range = false;      // beyond settings::distance: only what the near lights leave free
            u32                                         type = 0;
            u32                                         faces = 0;
            u32                                         desired = 0;            // tile size by distance alone
            u32                                         target = 0;             // tile size this frame (what the atlas has room for)
            bool                                        renders = false;        // draws at least one face this frame
        };

        [[nodiscard]] u32 find(u64 key) const;
        [[nodiscard]] u32 create(u64 key);
        void release_tiles(entry& item);
        [[nodiscard]] bool allocate_tiles(entry& item, u32 size);
        [[nodiscard]] bool resize(entry& item, u32 size);
        [[nodiscard]] bool evict_one(bool far_lights);
        void collect_garbage();
        [[nodiscard]] u32 visible_faces(const entry& item, const glm::vec4 (&camera_planes)[6]) const;
        void render_face(entry& item, u32 face_index, const settings& config);

        std::vector<entry>                              m_entries{};
        std::vector<u32>                                m_free_entries{};
        std::unordered_map<u64, u32>                    m_lookup{};
        shadow_atlas_allocator                          m_allocator{};
        u64                                             m_frame = 0;

        // per frame
        std::vector<candidate>                          m_candidates{};
        std::vector<u64>                                m_reserve{};            // texels the candidates from [i] on need (plan scratch)
        std::vector<gpu_shadow_view>                    m_views{};
        std::vector<local_shadow_tile>                  m_render_tiles{};
        u32                                             m_cull_view_count = 0;
        frame_stats                                     m_stats{};
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    inline glm::vec3 shadow_face_direction(const u32 face) {

        switch (face) {
            case 0:     return {  1.f,  0.f,  0.f };
            case 1:     return { -1.f,  0.f,  0.f };
            case 2:     return {  0.f,  1.f,  0.f };
            case 3:     return {  0.f, -1.f,  0.f };
            case 4:     return {  0.f,  0.f,  1.f };
            default:    return {  0.f,  0.f, -1.f };
        }
    }


    inline glm::mat4 shadow_look_view(const glm::vec3& position, const glm::vec3& forward) {

        const glm::vec3 up = std::abs(forward.y) > 0.99f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
        return glm::lookAt(position, position + forward, up);
    }


    inline void shadow_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]) {

        const glm::mat4 t = glm::transpose(view_projection);
        planes[0] = t[3] + t[0];
        planes[1] = t[3] - t[0];
        planes[2] = t[3] + t[1];
        planes[3] = t[3] - t[1];
        planes[4] = t[3] + t[2];
        planes[5] = t[3] - t[2];

        for (auto& plane : planes) {
            const f32 length = glm::length(glm::vec3(plane));
            if (length > 0.f)
                plane /= length;
        }
    }


    inline bool shadow_view_visible(const glm::mat4& view, const glm::vec3& position, const f32 half_tan, const f32 near_d, const f32 far_d,
        const glm::vec4 (&camera_planes)[6]) {

        const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
        const glm::vec3 forward(-view[0][2], -view[1][2], -view[2][2]);

        glm::vec3 corners[8];
        for (u32 i = 0; i < 8; ++i) {
            const f32 d = (i & 4) ? far_d : near_d;
            const f32 x = (i & 1) ? 1.f : -1.f;
            const f32 y = (i & 2) ? 1.f : -1.f;
            corners[i] = position + forward * d + right * (x * half_tan * d) + up * (y * half_tan * d);
        }

        for (const auto& plane : camera_planes) {
            bool all_outside = true;
            for (const auto& corner : corners)
                if (glm::dot(glm::vec3(plane), corner) + plane.w >= 0.f) { all_outside = false; break; }
            if (all_outside)
                return false;
        }
        return true;
    }

    // CLASS IMPLEMENTATION: shadow_atlas_allocator ====================================================================

    inline void shadow_atlas_allocator::reset(const u32 atlas_size, const u32 min_tile) {

        m_atlas_size = std::bit_floor(std::max(atlas_size, 1u));
        m_min_tile = std::min(std::bit_floor(std::max(min_tile, 1u)), m_atlas_size);

        m_levels = 1;                                   // level 0 = the whole atlas, the last level = min_tile
        for (u32 size = m_atlas_size; size > m_min_tile; size >>= 1)
            ++m_levels;

        size_t nodes = 0;
        size_t level_nodes = 1;
        for (u32 level = 0; level < m_levels; ++level, level_nodes *= 4)
            nodes += level_nodes;

        m_state.assign(nodes, node_free);
        m_largest.assign(nodes, 0);
        m_largest[0] = static_cast<u8>(m_levels);
        m_used_texels = 0;
    }


    inline void shadow_atlas_allocator::refresh_largest(const u32 node) {

        const u32 child = node * 4 + 1;
        m_largest[node] = std::max(std::max(m_largest[child], m_largest[child + 1]), std::max(m_largest[child + 2], m_largest[child + 3]));
    }


    inline bool shadow_atlas_allocator::allocate(const u32 size, u32& x, u32& y) {

        if (m_levels == 0 || size < m_min_tile || size > m_atlas_size || !std::has_single_bit(size))
            return false;

        const u32 target = level_of(size);
        const u8 want = static_cast<u8>(m_levels - target);        // a free node of the target level has exactly this value
        if (m_largest[0] < want)
            return false;

        std::array<u32, 32> path{};
        u32 node = 0;
        u32 tile_x = 0;
        u32 tile_y = 0;
        for (u32 level = 0; level < target; ++level) {

            path[level] = node;
            const u32 first_child = node * 4 + 1;
            if (m_state[node] == node_free) {           // split: four free children
                m_state[node] = node_split;
                for (u32 child = 0; child < 4; ++child) {
                    m_state[first_child + child] = node_free;
                    m_largest[first_child + child] = static_cast<u8>(m_levels - (level + 1));
                }
            }

            u32 pick = 4;
            for (u32 child = 0; child < 4; ++child)
                if (m_largest[first_child + child] >= want) { pick = child; break; }
            if (pick == 4)
                return false;                           // cannot happen: m_largest of this node promised one

            const u32 half = m_atlas_size >> (level + 1);
            tile_x += (pick & 1u) * half;
            tile_y += (pick >> 1u) * half;
            node = first_child + pick;
        }

        m_state[node] = node_used;
        m_largest[node] = 0;
        for (u32 level = target; level-- > 0; )
            refresh_largest(path[level]);

        x = tile_x;
        y = tile_y;
        m_used_texels += static_cast<u64>(size) * size;
        return true;
    }


    inline void shadow_atlas_allocator::release(const u32 x, const u32 y, const u32 size) {

        if (m_levels == 0 || size < m_min_tile || size > m_atlas_size || !std::has_single_bit(size))
            return;

        const u32 target = level_of(size);
        std::array<u32, 32> path{};
        u32 node = 0;
        for (u32 level = 0; level < target; ++level) {

            path[level] = node;
            if (m_state[node] != node_split)
                return;                                 // not a tile of this allocator
            const u32 half = m_atlas_size >> (level + 1);
            const u32 child = ((x / half) & 1u) | (((y / half) & 1u) << 1u);
            node = node * 4 + 1 + child;
        }
        if (m_state[node] != node_used)
            return;

        m_state[node] = node_free;
        m_largest[node] = static_cast<u8>(m_levels - target);
        m_used_texels -= static_cast<u64>(size) * size;

        for (u32 level = target; level-- > 0; ) {       // merge free siblings back into their parent

            const u32 parent = path[level];
            const u32 child = parent * 4 + 1;
            if (m_state[child] == node_free && m_state[child + 1] == node_free && m_state[child + 2] == node_free && m_state[child + 3] == node_free) {
                m_state[parent] = node_free;
                m_largest[parent] = static_cast<u8>(m_levels - level);
            } else
                refresh_largest(parent);
        }
    }

    // CLASS IMPLEMENTATION: local_shadow_cache ========================================================================

    inline void local_shadow_cache::clear() {

        m_entries.clear();
        m_free_entries.clear();
        m_lookup.clear();
        m_candidates.clear();
        m_views.clear();
        m_render_tiles.clear();
        m_cull_view_count = 0;
        m_stats = {};
        if (m_allocator.atlas_size() != 0)
            m_allocator.reset(m_allocator.atlas_size(), SHADOW_TILE_MIN);
    }


    inline void local_shadow_cache::invalidate_all() {

        for (auto& item : m_entries)
            if (item.alive)
                for (auto& tile : item.faces)
                    tile.dirty = true;
    }


    inline void local_shadow_cache::invalidate(const std::span<const glm::vec4> spheres) {

        if (spheres.empty() || m_lookup.empty())
            return;

        // Most lights are nowhere near what moved: one box around all of it rejects them without looking at every sphere
        glm::vec3 box_min(std::numeric_limits<f32>::max());
        glm::vec3 box_max(std::numeric_limits<f32>::lowest());
        for (const auto& sphere : spheres) {
            box_min = glm::min(box_min, glm::vec3(sphere) - sphere.w);
            box_max = glm::max(box_max, glm::vec3(sphere) + sphere.w);
        }

        for (auto& item : m_entries) {

            if (!item.alive || item.size == 0)
                continue;

            const glm::vec3 light(item.position_radius);
            const f32 reach = item.position_radius.w;
            const glm::vec3 nearest = glm::clamp(light, box_min, box_max) - light;
            if (glm::dot(nearest, nearest) > reach * reach)
                continue;

            u32 clean = 0;
            for (u32 index = 0; index < item.face_count; ++index)
                if (item.faces[index].allocated && item.faces[index].rendered && !item.faces[index].dirty)
                    clean |= 1u << index;

            for (const auto& sphere : spheres) {

                if (clean == 0)
                    break;

                const glm::vec3 center(sphere);
                const glm::vec3 offset = center - light;
                const f32 range = reach + sphere.w;
                if (glm::dot(offset, offset) > range * range)
                    continue;

                for (u32 index = 0; index < item.face_count; ++index) {

                    if ((clean & (1u << index)) == 0)
                        continue;

                    bool inside = true;
                    for (const auto& plane : item.faces[index].view.planes)
                        if (glm::dot(glm::vec3(plane), center) + plane.w < -sphere.w) { inside = false; break; }
                    if (inside) {
                        item.faces[index].dirty = true;
                        clean &= ~(1u << index);
                    }
                }
            }
        }
    }


    inline void local_shadow_cache::discard_frame() {

        for (auto& item : m_entries)
            if (item.alive)
                for (auto& tile : item.faces)
                    if (tile.rendered && tile.rendered_frame == m_frame) {
                        tile.rendered = false;
                        tile.dirty = true;
                    }
    }


    inline void local_shadow_cache::atlas_lost() {

        for (auto& item : m_entries)
            if (item.alive)
                for (auto& tile : item.faces)
                    if (tile.rendered && tile.rendered_frame != m_frame) {
                        tile.rendered = false;
                        tile.dirty = true;
                    }
    }


    inline u32 local_shadow_cache::find(const u64 key) const {

        const auto it = m_lookup.find(key);
        return it != m_lookup.end() ? it->second : UINT32_MAX;
    }


    inline u32 local_shadow_cache::create(const u64 key) {

        u32 index = 0;
        if (!m_free_entries.empty()) {
            index = m_free_entries.back();
            m_free_entries.pop_back();
        } else {
            index = static_cast<u32>(m_entries.size());
            m_entries.emplace_back();
        }

        m_entries[index] = entry{};
        m_entries[index].key = key;
        m_entries[index].alive = true;
        m_lookup[key] = index;
        return index;
    }


    inline void local_shadow_cache::release_tiles(entry& item) {

        for (auto& tile : item.faces) {
            if (tile.allocated)
                m_allocator.release(tile.x, tile.y, item.size);
            tile = face{};
        }
        item.size = 0;
    }


    // All faces or none. The faces' previous tiles are not touched (see resize)
    inline bool local_shadow_cache::allocate_tiles(entry& item, const u32 size) {

        std::array<glm::uvec2, 6> tiles{};
        for (u32 index = 0; index < item.face_count; ++index) {
            if (!m_allocator.allocate(size, tiles[index].x, tiles[index].y)) {
                for (u32 undo = 0; undo < index; ++undo)
                    m_allocator.release(tiles[undo].x, tiles[undo].y, size);
                return false;
            }
        }

        for (u32 index = 0; index < item.face_count; ++index) {
            face& tile = item.faces[index];
            if (tile.allocated)
                m_allocator.release(tile.x, tile.y, item.size);
            tile = face{};
            tile.x = tiles[index].x;
            tile.y = tiles[index].y;
            tile.allocated = true;
        }
        item.size = size;
        return true;
    }


    // New tiles of [size] for every face. The old tiles are kept until the new ones exist. If the atlas is too full for
    // both, lights that are out of range give up theirs, and for a near light the far lights do too. Shrinking then
    // reuses the light's own space (always fits); growing is given up, and the light keeps what it has
    inline bool local_shadow_cache::resize(entry& item, const u32 size) {

        if (allocate_tiles(item, size))
            return true;

        while (evict_one(!item.far_range))
            if (allocate_tiles(item, size))
                return true;

        if (item.size == 0 || size > item.size)
            return false;

        release_tiles(item);
        return allocate_tiles(item, size);
    }


    // Frees the tiles of the light that has been out of range the longest. If there is none and [far_lights] is set:
    // of the least important light beyond settings::distance. False: nothing to free
    inline bool local_shadow_cache::evict_one(const bool far_lights) {

        entry* oldest = nullptr;
        for (auto& item : m_entries)
            if (item.alive && item.size != 0 && item.seen_frame != m_frame && (!oldest || item.seen_frame < oldest->seen_frame))
                oldest = &item;

        if (!oldest && far_lights)
            for (auto& item : m_entries)
                if (item.alive && item.size != 0 && item.far_range && (!oldest || item.importance < oldest->importance))
                    oldest = &item;

        if (!oldest)
            return false;

        release_tiles(*oldest);
        return true;
    }


    // Lights that were not submitted (or not in range) for a long time: gone for good
    inline void local_shadow_cache::collect_garbage() {

        constexpr u64 KEEP_FRAMES = 600;
        for (u32 index = 0; index < m_entries.size(); ++index) {

            entry& item = m_entries[index];
            if (!item.alive || item.seen_frame + KEEP_FRAMES >= m_frame)
                continue;

            release_tiles(item);
            m_lookup.erase(item.key);
            item = entry{};
            m_free_entries.push_back(index);
        }
    }


    // Bit per face whose frustum may be on screen (conservative)
    inline u32 local_shadow_cache::visible_faces(const entry& item, const glm::vec4 (&camera_planes)[6]) const {

        const glm::vec3 position(item.position_radius);
        const f32 radius = item.position_radius.w;
        for (const auto& plane : camera_planes)
            if (glm::dot(glm::vec3(plane), position) + plane.w < -radius)
                return 0;

        const f32 near_d = std::max(0.05f, radius * 0.02f);
        const f32 half_tan = (item.type == 1) ? 1.f : std::tan(std::min(std::acos(std::clamp(item.cos_outer, -1.f, 1.f)), glm::radians(80.f)));

        u32 mask = 0;
        for (u32 index = 0; index < item.face_count; ++index) {
            const glm::vec3 forward = (item.type == 1) ? shadow_face_direction(index) : item.direction;
            if (shadow_view_visible(shadow_look_view(position, forward), position, half_tan, near_d, radius, camera_planes))
                mask |= 1u << index;
        }
        return mask;
    }


    // The face is drawn this frame with the light as it is now
    inline void local_shadow_cache::render_face(entry& item, const u32 face_index, const settings& config) {

        face& tile = item.faces[face_index];
        const glm::vec3 position(item.position_radius);
        const f32 radius = item.position_radius.w;
        const f32 size = static_cast<f32>(item.size);
        const f32 atlas = static_cast<f32>(m_allocator.atlas_size());

        const f32 near_d = std::max(0.05f, radius * 0.02f);
        const f32 half_tan = (item.type == 1) ? 1.f : std::tan(std::min(std::acos(std::clamp(item.cos_outer, -1.f, 1.f)), glm::radians(80.f)));

        // Widen the frustum so taps near the tile edge still hit real depth instead of clamping onto the border.
        // Depth 0..1 (Vulkan)
        const f32 guard_tan = half_tan * size / (size - 2.f * static_cast<f32>(SHADOW_TILE_GUARD));
        glm::mat4 projection = glm::perspectiveRH_ZO(2.f * std::atan(guard_tan), 1.f, near_d, radius);
        projection[1][1] *= -1.0f;

        const glm::vec3 forward = (item.type == 1) ? shadow_face_direction(face_index) : item.direction;

        gpu_shadow_view view{};
        view.view_proj = projection * shadow_look_view(position, forward);
        view.atlas_rect = glm::vec4(static_cast<f32>(tile.x) / atlas, static_cast<f32>(tile.y) / atlas, size / atlas, size / atlas);
        view.params = glm::vec4(2.f * guard_tan / size, config.normal_bias, config.bias, 1.f / atlas);
        view.sphere = glm::vec4(position, radius);
        shadow_frustum_planes(view.view_proj, view.planes);

        tile.view = view;
        tile.rendered = true;
        tile.dirty = false;
        tile.rendered_frame = m_frame;
    }


    inline void local_shadow_cache::plan(const std::span<gpu_light> lights, const std::span<const u64> keys, const glm::vec3& camera_position,
        const glm::vec4 (&camera_planes)[6], const settings& config, const u32 view_budget) {

        ++m_frame;
        m_candidates.clear();
        m_views.clear();
        m_render_tiles.clear();
        m_cull_view_count = 0;
        m_stats = {};

        const u32 atlas_size = std::bit_floor(std::max(config.atlas_size, SHADOW_TILE_MIN));
        if (m_allocator.atlas_size() != atlas_size) {
            clear();
            m_allocator.reset(atlas_size, SHADOW_TILE_MIN);
        }

        if ((m_frame & 63u) == 0u)
            collect_garbage();

        // ---- 1. Candidates: local lights whose sphere is within range of the camera, most important first ------------
        for (u32 index = 0; index < lights.size(); ++index) {

            gpu_light& light = lights[index];
            const u32 type = static_cast<u32>(light.params.y);
            if (type != 1 && type != 2)
                continue;
            light.params.z = 0.f;                       // unshadowed unless it gets views below

            const glm::vec3 position(light.position_radius);
            const f32 radius = light.position_radius.w;
            if (!(radius > 0.f) || !std::isfinite(radius) || !std::isfinite(position.x + position.y + position.z))
                continue;

            const f32 distance = glm::distance(camera_position, position);
            if (!(distance - radius <= std::max(config.distance, config.far_distance)))
                continue;

            candidate item{};
            item.light = index;
            item.key = index < keys.size() ? keys[index] : (0xFFFFFFFF00000000ull | index);
            item.type = type;
            item.faces = (type == 1) ? 6u : 1u;
            item.ratio = radius / std::max(distance, 1.f);
            item.far_range = !(distance - radius <= config.distance);
            if (item.far_range && item.ratio < LOCAL_SHADOW_FAR_MIN_RATIO)
                continue;

            // Lights that already have tiles get a head start, so two lights of about the same importance do not swap
            // places in the list every time the camera moves a little
            const u32 existing = find(item.key);
            const bool has_tiles = existing != UINT32_MAX && m_entries[existing].size != 0;
            item.sort_key = item.ratio * std::sqrt(std::max(light.color_intensity.a, 0.01f)) * (has_tiles ? 1.25f : 1.f);
            m_candidates.push_back(item);
        }

        // Near lights first: the caps below then drop far lights before any near one
        std::sort(m_candidates.begin(), m_candidates.end(), [](const candidate& a, const candidate& b) {
            if (a.far_range != b.far_range)
                return !a.far_range;
            return a.sort_key != b.sort_key ? a.sort_key > b.sort_key : a.key < b.key;
        });

        const u64 capacity = static_cast<u64>(atlas_size) * atlas_size;
        const u64 limit_grow = capacity / 100 * 80;
        const u64 limit_keep = capacity / 100 * 92;
        constexpr u64 MIN_AREA = static_cast<u64>(SHADOW_TILE_MIN) * SHADOW_TILE_MIN;

        {   // caps: lights, views, and what the atlas holds at the smallest tile size
            const u32 max_lights = std::min(config.max_lights, MAX_SHADOW_LIGHTS);
            u32 kept = 0;
            u32 view_total = 0;
            for (; kept < m_candidates.size() && kept < max_lights; ++kept) {
                const u32 faces = m_candidates[kept].faces;
                if (view_total + faces > MAX_SHADOW_VIEWS || (view_total + faces) * MIN_AREA > limit_grow)
                    break;
                view_total += faces;
            }
            m_candidates.resize(kept);
        }
        m_stats.candidates = static_cast<u32>(m_candidates.size());
        const size_t near_count = static_cast<size_t>(std::find_if(m_candidates.begin(), m_candidates.end(),
            [](const candidate& item) { return item.far_range; }) - m_candidates.begin());
        m_stats.far_candidates = static_cast<u32>(m_candidates.size() - near_count);

        // ---- 2. Cache entries; a light that moved or changed shape draws all its faces again -------------------------
        for (auto& item : m_candidates) {

            const gpu_light& light = lights[item.light];

            u64 key = item.key;
            u32 index = find(key);
            while (index != UINT32_MAX && m_entries[index].seen_frame == m_frame) {     // two lights with one key: keep them apart
                key = key * 0x9E3779B97F4A7C15ull + item.light + 1u;
                index = find(key);
            }
            if (index == UINT32_MAX)
                index = create(key);
            item.entry = index;

            entry& cached = m_entries[index];
            cached.seen_frame = m_frame;
            cached.far_range = item.far_range;
            cached.importance = item.sort_key;

            if (cached.type != item.type) {
                release_tiles(cached);
                cached.type = item.type;
                cached.face_count = item.faces;
                cached.tier = -1;
            }

            const glm::vec3 direction = (item.type == 2) ? glm::vec3(light.direction_inner_cos) : glm::vec3(0.f, -1.f, 0.f);
            const f32 cos_outer = (item.type == 2) ? light.params.x : 0.f;
            if (cached.position_radius != light.position_radius || cached.direction != direction || cached.cos_outer != cos_outer) {
                cached.position_radius = light.position_radius;
                cached.direction = direction;
                cached.cos_outer = cos_outer;
                for (auto& tile : cached.faces)
                    tile.dirty = true;
            }
        }

        // ---- 3. Tile sizes: by distance, then cut down to what the atlas holds ----------------------------------------
        // Every light's size by distance is halved the same number of times (the pressure) until all of them fit; the
        // space that is left then gives the most important lights one halving back. A light keeps the size it has as
        // long as the atlas stays below the upper limit, so lights at the border do not flip back and forth
        for (auto& item : m_candidates) {

            entry& cached = m_entries[item.entry];

            // ratio >= 0.5: full size, then one halving per octave of distance. The step only changes once the light is a
            // quarter octave past the border
            const u32 max_tile = (item.type == 1) ? SHADOW_POINT_TILE_MAX : SHADOW_SPOT_TILE_MAX;
            const i32 max_tier = static_cast<i32>(std::countr_zero(max_tile) - std::countr_zero(SHADOW_TILE_MIN));
            const f32 step = std::log2(0.5f / std::max(item.ratio, 1e-4f));
            i32 tier = std::clamp(static_cast<i32>(std::floor(step)), 0, max_tier);
            if (cached.tier >= 0 && cached.tier <= max_tier && step > static_cast<f32>(cached.tier) - 0.25f && step < static_cast<f32>(cached.tier) + 1.25f)
                tier = cached.tier;
            cached.tier = tier;
            item.desired = std::max(SHADOW_TILE_MIN, max_tile >> tier);
        }

        const auto size_at = [](const u32 desired, const u32 pressure) { return std::max(SHADOW_TILE_MIN, desired >> pressure); };
        const auto area_of = [](const candidate& item, const u32 size) { return static_cast<u64>(item.faces) * size * size; };

        // The near lights alone decide the pressure, and get their halving back first
        u32 pressure = 0;
        for (; pressure < 8; ++pressure) {              // at 8 every tile is at the minimum, which fits (see the caps above)
            u64 total = 0;
            for (size_t index = 0; index < near_count; ++index)
                total += area_of(m_candidates[index], size_at(m_candidates[index].desired, pressure));
            if (total <= limit_grow)
                break;
        }

        m_reserve.assign(m_candidates.size() + 1, 0);   // texels the near candidates behind [i] need at this pressure
        for (size_t index = near_count; index-- > 0; )
            m_reserve[index] = m_reserve[index + 1] + area_of(m_candidates[index], size_at(m_candidates[index].desired, pressure));

        u64 used = 0;
        u64 far_used = 0;
        for (size_t index = 0; index < m_candidates.size(); ++index) {

            candidate& item = m_candidates[index];
            const entry& cached = m_entries[item.entry];

            item.target = size_at(item.desired, pressure);

            // Far lights: in the order of their importance, in the room above what the near lights may grow into, and
            // not more than their share of the atlas. One that has its tiles may stay a little longer than a new one
            // may come in, so the last ones that fit do not come and go with every change of the near lights
            if (item.far_range) {
                const u64 area = area_of(item, item.target);
                const bool keeps = cached.size == item.target;
                const u64 share = capacity / LOCAL_SHADOW_FAR_ATLAS_SHARE;
                if (far_used + area > (keeps ? share : share / 5 * 4) || used + area > (keeps ? limit_keep : (limit_grow + limit_keep) / 2)) {
                    item.target = 0;
                    continue;
                }
                far_used += area;
                used += area;
                continue;
            }
            if (pressure > 0) {
                const u32 larger = size_at(item.desired, pressure - 1);
                const u64 limit = (larger == cached.size) ? limit_keep : limit_grow;
                if (larger != item.target && used + area_of(item, larger) + m_reserve[index + 1] <= limit)
                    item.target = larger;
            }
            used += area_of(item, item.target);
        }

        // ---- 4. Work of this frame: resize, then draw what is out of date, most important light first ------------------
        u32 budget = view_budget;
        for (auto& item : m_candidates) {

            entry& cached = m_entries[item.entry];

            if (item.target == 0) {                     // a far light the atlas has no room for: its space goes to others
                if (cached.size != 0)
                    release_tiles(cached);
                continue;
            }

            const bool wants_resize = item.target != cached.size;
            bool outdated = wants_resize;
            for (u32 index = 0; index < cached.face_count && !outdated; ++index)
                outdated = cached.faces[index].allocated && cached.faces[index].dirty;
            if (!outdated)
                continue;

            const u32 visible = visible_faces(cached, camera_planes);

            if (wants_resize) {
                // A resize throws the old depth maps away, so it waits until the faces on screen can be drawn in the same
                // frame. Off screen, growing can wait too (shrinking frees space for others)
                const u32 visible_count = static_cast<u32>(std::popcount(visible));
                if (visible_count <= budget && !(cached.size != 0 && visible_count == 0 && item.target > cached.size)) {
                    if (cached.size != 0)
                        (void)resize(cached, item.target);          // false: no room to grow yet, the light keeps its tiles
                    else
                        for (u32 size = item.target; size >= SHADOW_TILE_MIN && !resize(cached, size); size >>= 1) {}
                }
            }
            if (cached.size == 0)
                continue;                               // no room in the atlas (yet): no shadow from this light for now

            for (u32 index = 0; index < cached.face_count && budget > 0; ++index) {
                face& tile = cached.faces[index];
                if (tile.allocated && tile.dirty && (visible & (1u << index)) != 0) {
                    render_face(cached, index, config);
                    item.renders = true;
                    --budget;
                }
            }
        }

        // Faces nobody sees right now: a few per frame, so they are ready before the camera turns to them
        u32 background = std::min(budget, SHADOW_BACKGROUND_VIEWS_PER_FRAME);
        for (auto& item : m_candidates) {

            if (background == 0)
                break;
            entry& cached = m_entries[item.entry];
            for (u32 index = 0; index < cached.face_count && background > 0; ++index) {
                face& tile = cached.faces[index];
                if (tile.allocated && tile.dirty) {
                    render_face(cached, index, config);
                    item.renders = true;
                    --background;
                }
            }
        }

        // ---- 5. Views: lights that draw this frame first (the cull shader only walks those), then the rest ------------
        for (u32 pass = 0; pass < 2; ++pass) {

            for (const auto& item : m_candidates) {

                if (item.renders != (pass == 0))
                    continue;

                const entry& cached = m_entries[item.entry];
                if (cached.size == 0)
                    ++m_stats.unplaced;
                bool usable = false;
                for (u32 index = 0; index < cached.face_count; ++index) {
                    usable |= cached.faces[index].rendered;
                    if (cached.faces[index].allocated && cached.faces[index].dirty)
                        ++m_stats.waiting;
                }
                if (!usable)
                    continue;

                const u32 first_view = static_cast<u32>(m_views.size());
                for (u32 index = 0; index < cached.face_count; ++index) {

                    const face& tile = cached.faces[index];
                    gpu_shadow_view view{};             // atlas_rect.z == 0: this face has no depth map (yet), fragments stay lit
                    if (tile.rendered) {
                        view = tile.view;
                        if (tile.rendered_frame == m_frame) {
                            view.cull.x = 1.f;
                            m_render_tiles.push_back({ first_view + index, tile.x, tile.y, cached.size });
                        }
                    }
                    m_views.push_back(view);
                }
                m_views[first_view].cull.y = static_cast<f32>(cached.face_count);
                m_views[first_view].group_sphere = cached.position_radius;

                lights[item.light].params.z = static_cast<f32>(first_view + 1);    // the shader adds the face for point lights
                ++m_stats.shadowed;
            }
            if (pass == 0)
                m_cull_view_count = static_cast<u32>(m_views.size());
        }

        m_stats.views = static_cast<u32>(m_views.size());
        m_stats.rendered = static_cast<u32>(m_render_tiles.size());
        m_stats.atlas_fill = static_cast<f32>(static_cast<f64>(m_allocator.used_texels()) / static_cast<f64>(capacity));
    }

}
