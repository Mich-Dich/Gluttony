#include "util/pch.h"
#include "mesh_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <util/util.h>
#include <util/core_config.h>
#include <util/io/serializer_yaml.h>
#include <application.h>
#include <config/imgui_config.h>
#include <render/i_scene_renderer.h>
#include <world/camera_snapshot.h>
#include <asset/type.h>
#include <asset/i_asset_registry.h>
#include <asset/mesh_lod.h>
#include <asset/mesh_cluster.h>

#include "util/ui/panel_collection.h"



// Rendering analysis of the mesh editor: the second view of the left panel ("Outliner | Rendering"), next to the
// inspector, so settings can be changed while their effect is in view
//
// It answers "what does this mesh cost to draw, and which detail system suits it" from three sources:
//   measured  the preview renderer's GPU statistics (i_scene_renderer::last_frame_stats), sampled every frame while the
//             mesh is looked at and sorted by the mesh's size on screen. Frames drawn as clusters and frames drawn
//             through the discrete levels are kept apart, so the two systems can be compared over distance
//   computed  what each system selects at every distance (LOD level / cluster cut), on the CPU, without culling
//   rules     triangle count thresholds, used while nothing better is known
//
// One view says little about a mesh, so the panel keeps track of the distances and directions seen so far and asks
// for the missing ones. "Collect Data" moves the camera through them

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // Samples that make a distance range / a view direction count as seen
    constexpr u32                                   RANGE_SEEN_SAMPLES = 8;

    constexpr u32                                   DIRECTION_SEEN_SAMPLES = 4;

    // Statistics skipped after the preview setup changed (locked level, render mode): GPU results arrive a few frames late and those still belong to the old setup
    constexpr u32                                   SETTLE_STATISTICS = 6;

    // "Collect Data" (analysis_sweep)
    constexpr f32                                   SWEEP_SECONDS_PER_RANGE = 2.8f;        // pace of the zoom: a distance range (half an octave) per half second

    constexpr f32                                   SWEEP_MIN_TURNS_PER_SECOND = 0.12f;     // auto rotate during the flight: at least this fast

    constexpr f32                                   SWEEP_PITCH_SECONDS = 9.0f;             // one up / down swing of the camera height

    constexpr f32                                   SWEEP_PITCH_AMPLITUDE = 0.85f;          // radians: reaches "from above" and "from below"

    // Collected samples are written at the latest this long after they were taken (and when the mesh closes, a measurement ends, the data is reset)
    constexpr f32                                   ANALYSIS_SAVE_INTERVAL = 10.0f;

    constexpr u32                                   ANALYSIS_FILE_VERSION = 2;              // 2: YAML (serializer::yaml)

    inline const std::filesystem::path              ANALYSIS_DATA_DIR("asset_stats");       // under <project>/metadata

    // Cluster selections computed per frame for the curves (each one visits every cluster: spread over frames)
    constexpr u32                                   CURVE_POINTS_PER_FRAME = 2;

    constexpr f32                                   CHART_HEIGHT = 132.0f;

    // Rules of thumb for the recommendation while no numbers exist
    constexpr u32                                   SMALL_MESH_TRIANGLES = 10'000;          // below: clusters cannot pay off

    constexpr u32                                   DENSE_MESH_TRIANGLES = 250'000;         // above: clusters usually do

    constexpr f64                                   DECISIVE_TIME_DIFFERENCE = 0.10;        // measured GPU time: smaller differences are a tie

    constexpr f32                                   NEAR_SCREEN_SIZE = 0.25f;               // "close up" for the near / far split

    constexpr const char*                           ANALYSIS_SECTION = "render_samples";

    // One colour per detail system, everywhere in the panel
    const ImVec4                                    COLOR_LODS{ 0.38f, 0.65f, 1.00f, 1.00f };

    const ImVec4                                    COLOR_CLUSTERS{ 1.00f, 0.62f, 0.30f, 1.00f };

    const ImVec4                                    COLOR_GOOD{ 0.47f, 0.80f, 0.52f, 1.00f };

    const ImVec4                                    COLOR_WARNING{ 0.90f, 0.78f, 0.43f, 1.00f };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct mesh_sphere {

        glm::vec3                               centre{ 0.0f };
        f32                                     radius = 1.0f;
    };


    // Rows of the file: flat records, which is what serializer::yaml::vector() handles
    struct analysis_series_row {

        u32                                         id = 0;
        std::string                                 signature{};            // hex
        u32                                         samples = 0;
    };


    struct analysis_range_row {

        u32                                         series = 0;
        u32                                         range = 0;
        u32                                         samples = 0;
        u32                                         timed_samples = 0;
        f64                                         geometry_ms = 0.0;
        f64                                         gpu_ms = 0.0;
        f64                                         triangles = 0.0;
        f64                                         draws = 0.0;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    std::string hex_text(const u64 value);

    bool parse_hex(const std::string& text, u64& value);
    // Both directions through one function: the serializer reads into or writes from the same variables

    void serialize_analysis(GLT::serializer::yaml& yaml, u32& version, std::string& asset, std::string& path, std::string& geometry,
        glm::uvec2& extent, std::vector<u32>& directions, std::vector<analysis_series_row>& series, std::vector<analysis_range_row>& ranges);

    // 1234 -> "1,234", 1234567 -> "1.23M"
    std::string count_text(const u64 value);

    // GPU times of single passes are small: three decimals below a tenth of a millisecond
    std::string ms_text(const f64 milliseconds);

    // Axis labels of the log scaled count axes: 1, 10, 100, 1k, 10k, 100k, 1M
    int count_axis_format(const double value, char* buffer, const int size, void* /*user_data*/);

    // The sphere both detail systems measure the mesh's screen size with
    mesh_sphere sphere_of(const GLT::asset::mesh::mesh_asset& mesh);

    // |P11| of the projection: cot(fov_y / 2)
    f32 projection_scale(const f32 fov_deg);

    u64 float_bits(const f32 value);

    // The source geometry. A change (reimport) makes everything collected worthless
    u64 geometry_signature(const GLT::asset::mesh::mesh_asset& mesh);

    // What the discrete levels draw at which size
    u64 lod_signature(const GLT::asset::mesh::mesh_asset& mesh);

    // What the cluster selection draws at which size
    u64 cluster_signature(const GLT::asset::mesh::mesh_asset& mesh);

    // Submeshes that have triangles in [level]: the draw commands of one instance in LOD mode
    u32 lod_draw_count(const GLT::asset::mesh::mesh_asset& mesh, const u32 level);

    u64 cluster_bytes(const size_t clusters, const size_t indices, const size_t vertices, const size_t submeshes);

    void dim_text(const char* text);

    // Bullet + wrapped text, the bullet in [color]
    void bullet_text(const ImVec4& color, const std::string& text);

    // Coloured dot + label: legend entry of a detail system
    void series_label(const ImVec4& color, const char* label);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::string hex_text(const u64 value) { return std::format("{:016x}", value); }


    bool parse_hex(const std::string& text, u64& value) {

        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        return error == std::errc{} && end == text.data() + text.size();
    }


    void serialize_analysis(GLT::serializer::yaml& yaml, u32& version, std::string& asset, std::string& path, std::string& geometry,
        glm::uvec2& extent, std::vector<u32>& directions, std::vector<analysis_series_row>& series, std::vector<analysis_range_row>& ranges) {

        yaml.entry("version", version)
            .entry("asset", asset)
            .entry("path", path)
            .entry("geometry", geometry)
            .entry("extent_width", extent.x)
            .entry("extent_height", extent.y)
            .entry("directions", directions)
            .vector("series", series, [&](GLT::serializer::yaml& row, const u64 index) {
                row.entry("id", series[index].id)
                    .entry("signature", series[index].signature)
                    .entry("samples", series[index].samples);
            })
            .vector("ranges", ranges, [&](GLT::serializer::yaml& row, const u64 index) {
                analysis_range_row& r = ranges[index];
                row.entry("series", r.series)
                    .entry("range", r.range)
                    .entry("samples", r.samples)
                    .entry("timed_samples", r.timed_samples)
                    .entry("geometry_ms", r.geometry_ms)
                    .entry("gpu_ms", r.gpu_ms)
                    .entry("triangles", r.triangles)
                    .entry("draws", r.draws);
            });
    }

    std::string count_text(const u64 value) {

        char buffer[32];
        if (value >= 10'000'000ull)
            std::snprintf(buffer, sizeof(buffer), "%.1fM", static_cast<f64>(value) / 1e6);
        else if (value >= 1'000'000ull)
            std::snprintf(buffer, sizeof(buffer), "%.2fM", static_cast<f64>(value) / 1e6);
        else if (value >= 100'000ull)
            std::snprintf(buffer, sizeof(buffer), "%.0fk", static_cast<f64>(value) / 1e3);
        else if (value >= 1'000ull)
            std::snprintf(buffer, sizeof(buffer), "%llu,%03llu", static_cast<unsigned long long>(value / 1000), static_cast<unsigned long long>(value % 1000));
        else
            std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        return buffer;
    }


    std::string ms_text(const f64 milliseconds) { return milliseconds < 0.1 ? std::format("{:.3f}", milliseconds) : std::format("{:.2f}", milliseconds); }


    int count_axis_format(const double value, char* buffer, const int size, void* /*user_data*/) {

        if (value >= 999'999.5)
            return std::snprintf(buffer, static_cast<size_t>(size), "%gM", value / 1e6);
        if (value >= 999.5)
            return std::snprintf(buffer, static_cast<size_t>(size), "%gk", value / 1e3);
        return std::snprintf(buffer, static_cast<size_t>(size), "%g", value);
    }


    mesh_sphere sphere_of(const GLT::asset::mesh::mesh_asset& mesh) {

        return { 0.5f * (mesh.bounds.min + mesh.bounds.max), std::max(GLT::asset::mesh::lod_sphere_radius(mesh.bounds), 1e-4f) };
    }


    f32 projection_scale(const f32 fov_deg) { return 1.0f / std::tan(glm::radians(fov_deg) * 0.5f); }


    u64 float_bits(const f32 value) { return std::bit_cast<u32>(value); }


    u64 geometry_signature(const GLT::asset::mesh::mesh_asset& mesh) {

        u64 hash = GLT::util::mix_u64(0x6d657368ull, mesh.vertices.size());
        hash = GLT::util::mix_u64(hash, mesh.indices.size());
        return GLT::util::mix_u64(hash, mesh.submeshes.size());
    }


    u64 lod_signature(const GLT::asset::mesh::mesh_asset& mesh) {

        u64 hash = GLT::util::mix_u64(0x6c6f64ull, mesh.lod_count());
        hash = GLT::util::mix_u64(hash, float_bits(mesh.lod.cull_screen_size));
        for (u32 level = 1; level < mesh.lod_count(); ++level) {
            hash = GLT::util::mix_u64(hash, mesh.lods[level].index_count);
            hash = GLT::util::mix_u64(hash, float_bits(mesh.lod.levels[level].screen_size));
        }
        return hash;
    }


    u64 cluster_signature(const GLT::asset::mesh::mesh_asset& mesh) {

        u64 hash = GLT::util::mix_u64(0x636c7573ull, mesh.clusters.size());
        hash = GLT::util::mix_u64(hash, mesh.cluster_indices.size());
        hash = GLT::util::mix_u64(hash, mesh.cluster_vertices.size());
        hash = GLT::util::mix_u64(hash, float_bits(mesh.virtual_geometry.error_scale));
        return GLT::util::mix_u64(hash, float_bits(mesh.lod.cull_screen_size));
    }


    u32 lod_draw_count(const GLT::asset::mesh::mesh_asset& mesh, const u32 level) {

        u32 draws = 0;
        for (u32 submesh = 0; submesh < mesh.submesh_count(); ++submesh)
            if (mesh.range(level, submesh).index_count > 0)
                draws++;
        return draws;
    }


    u64 cluster_bytes(const size_t clusters, const size_t indices, const size_t vertices, const size_t submeshes) {

        return clusters * sizeof(GLT::asset::mesh::cluster) + indices * sizeof(u32) + vertices * sizeof(GLT::asset::mesh::vertex)
            + submeshes * sizeof(GLT::asset::mesh::cluster_submesh);
    }


    void dim_text(const char* text) {

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }


    void bullet_text(const ImVec4& color, const std::string& text) {

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", text.c_str());
    }


    void series_label(const ImVec4& color, const char* label) {

        const f32 height = ImGui::GetTextLineHeight();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(position.x + 5.f, position.y + height * 0.5f), 4.f, ImGui::GetColorU32(color));
        ImGui::Dummy(ImVec2(12.f, height));
        ImGui::SameLine(0.f, 4.f);
        ImGui::TextDisabled("%s", label);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // distance ranges --------------------------------------------------------------------------------------------------

    f32 mesh_editor_window::analysis_range_screen(const f32 position) noexcept {

        return ANALYSIS_SCREEN_MAX * std::exp2(-0.5f * position);
    }


    u32 mesh_editor_window::analysis_range_of(const f32 screen_size) noexcept {

        if (!(screen_size > 0.0f))
            return ANALYSIS_RANGES - 1;
        const f32 position = 2.0f * std::log2(ANALYSIS_SCREEN_MAX / screen_size);
        return static_cast<u32>(std::clamp(position, 0.0f, static_cast<f32>(ANALYSIS_RANGES - 1)));
    }


    f32 mesh_editor_window::analysis_range_distance(const GLT::asset::mesh::mesh_asset& mesh, const f32 position) const {

        return sphere_of(mesh).radius * projection_scale(m_preview_camera.fov_deg) / analysis_range_screen(position);
    }


    bool mesh_editor_window::analysis_range_reachable(const GLT::asset::mesh::mesh_asset& mesh, const u32 range) const {

        const f32 distance = analysis_range_distance(mesh, static_cast<f32>(range) + 0.5f);
        return distance >= sphere_of(mesh).radius * 1.05f && distance >= MIN_CAM_DISTANCE && distance <= MAX_CAM_DISTANCE;
    }


    std::span<const GLT::asset::mesh::cluster> mesh_editor_window::analysis_clusters(const GLT::asset::mesh::mesh_asset& mesh) const {

        if (mesh.has_clusters())
            return mesh.clusters;
        if (m_analysis.has_estimate)
            return m_analysis.estimate.clusters;
        return {};
    }


    mesh_editor_window::analysis_coverage mesh_editor_window::analysis_coverage_of(const GLT::asset::mesh::mesh_asset& mesh) const {

        analysis_coverage coverage{};
        coverage.series = preview_uses_virtual_geometry(mesh) ? series_virtual_geometry : series_lods;
        const analysis_series& series = m_analysis.series[coverage.series];

        for (u32 range = 0; range < ANALYSIS_RANGES; ++range) {

            if (!analysis_range_reachable(mesh, range))
                continue;
            if (coverage.first_expected < 0)
                coverage.first_expected = static_cast<i32>(range);
            coverage.last_expected = static_cast<i32>(range);
            coverage.ranges_expected++;

            if (series.ranges[range].samples < RANGE_SEEN_SAMPLES)
                continue;
            if (coverage.nearest_seen < 0)
                coverage.nearest_seen = static_cast<i32>(range);
            coverage.farthest_seen = static_cast<i32>(range);
            coverage.ranges_seen++;
        }

        for (u32 pitch = 0; pitch < ANALYSIS_PITCH_BINS; ++pitch)
            for (u32 yaw = 0; yaw < ANALYSIS_YAW_BINS; ++yaw)
                if (m_analysis.directions[pitch * ANALYSIS_YAW_BINS + yaw] >= DIRECTION_SEEN_SAMPLES) {
                    coverage.directions_seen++;
                    coverage.seen_from_below |= pitch == 0;
                    coverage.seen_from_above |= pitch == ANALYSIS_PITCH_BINS - 1;
                }

        coverage.thin = coverage.ranges_seen * 2 < coverage.ranges_expected
            || coverage.directions_seen * 3 < ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS;
        return coverage;
    }

    // sampling ---------------------------------------------------------------------------------------------------------

    void mesh_editor_window::update_analysis(const f32 seconds) {
        GLT_PROFILE_SCOPE("mesh_editor: analysis update");

        poll_analysis_estimate();

        analysis_state& analysis = m_analysis;
        if (analysis.dirty && !analysis.sweep.active) {
            analysis.unsaved_seconds += std::max(seconds, 0.0f);
            if (analysis.unsaved_seconds >= ANALYSIS_SAVE_INTERVAL)
                save_analysis_data();
        }
        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = (m_has_mesh && registry) ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh || !m_preview_scene) {
            if (analysis.sweep.active)
                stop_analysis_sweep();
            return;
        }

        // ---- what was collected describes data that may be gone ----
        const u64 geometry = geometry_signature(*mesh);
        if (geometry != analysis.geometry_signature) {
            if (analysis.geometry_signature != 0)
                reset_analysis(false);
            analysis.geometry_signature = geometry;
        }

        const u64 lods = lod_signature(*mesh);
        if (analysis.series[series_lods].signature != lods) {
            analysis.series[series_lods] = {};
            analysis.series[series_lods].signature = lods;
        }

        // The cluster samples outlive a visit to LOD mode (which removes the clusters): they are only dropped when
        // clusters exist again and are different ones
        if (mesh->has_clusters()) {

            const u64 clusters = cluster_signature(*mesh);
            if (analysis.series[series_virtual_geometry].signature != clusters) {
                analysis.series[series_virtual_geometry] = {};
                analysis.series[series_virtual_geometry].signature = clusters;
            }

            if (analysis.has_estimate) {            // the real hierarchy replaces the estimate
                analysis.estimate = {};
                analysis.has_estimate = false;
                analysis.estimate_serial++;
            }
        }

        // ---- statistics in flight belong to the previous setup ----
        const u32 render_mode = m_preview_scene->get_render_mode();
        if (m_preview_lod != analysis.tracked_lod || render_mode != analysis.tracked_mode) {
            analysis.tracked_lod = m_preview_lod;
            analysis.tracked_mode = render_mode;
            analysis.settle = SETTLE_STATISTICS;
        }

        update_analysis_sweep(*mesh, seconds);      // may move the camera / lock a level for the frames to come

        const GLT::render::frame_stats stats = m_preview_scene->last_frame_stats();
        if (stats.serial == 0 || stats.serial == analysis.last_serial)
            return;
        analysis.last_serial = stats.serial;

        analysis.live = stats;
        analysis.live_geometry_ms = analysis.live_geometry_ms > 0.0f ? glm::mix(analysis.live_geometry_ms, stats.geometry_time_ms, 0.15f) : stats.geometry_time_ms;
        analysis.live_gpu_ms = analysis.live_gpu_ms > 0.0f ? glm::mix(analysis.live_gpu_ms, stats.gpu_time_ms, 0.15f) : stats.gpu_time_ms;

        if (analysis.settle > 0) {
            analysis.settle--;
            return;
        }
        if (stats.draw_calls == 0 || stats.extent.x == 0 || stats.extent.y == 0)
            return;                                 // nothing of the mesh in view
        if (stats.render_mode != default_preview_render_mode())
            return;                                 // debug views cost something else

        // The preview was resized: GPU time depends on the pixel count, the cluster selection on the height. Start over
        const auto resized = [](const u32 before, const u32 now) {
            return before == 0 || std::abs(static_cast<f32>(now) - static_cast<f32>(before)) > 0.1f * static_cast<f32>(before);
        };
        if (resized(analysis.data_extent.x, stats.extent.x) || resized(analysis.data_extent.y, stats.extent.y)) {
            for (analysis_series& series : analysis.series) {
                const u64 signature = series.signature;
                series = {};
                series.signature = signature;
            }
            analysis.directions = {};
            analysis.data_extent = stats.extent;
        }

        // ---- the view the statistics were measured in ----
        const mesh_sphere sphere = sphere_of(*mesh);
        const glm::vec3 offset = stats.camera_position - sphere.centre;
        const f32 distance = std::max(glm::length(offset), 1e-4f);
        const f32 screen_size = sphere.radius * projection_scale(m_preview_camera.fov_deg) / distance;
        const u32 range = analysis_range_of(screen_size);

        // A frame with cluster draws went through the virtual geometry path, any other one through the discrete levels.
        // With the preview locked to a level, that frame only tells something about LOD mode if LOD mode would pick the
        // same level at this size
        const bool clusters_drawn = stats.cluster_draws > 0;
        if (!clusters_drawn && m_preview_lod >= 0 && static_cast<u32>(m_preview_lod) != auto_lod_for(*mesh, screen_size))
            return;

        analysis_series& series = analysis.series[clusters_drawn ? series_virtual_geometry : series_lods];
        analysis_range& bucket = series.ranges[range];
        bucket.samples++;
        bucket.triangles += static_cast<f64>(stats.triangles);
        bucket.draws += static_cast<f64>(stats.draw_calls);
        if (stats.gpu_time_ms > 0.0f) {
            bucket.timed_samples++;
            bucket.geometry_ms += static_cast<f64>(stats.geometry_time_ms);
            bucket.gpu_ms += static_cast<f64>(stats.gpu_time_ms);
        }
        series.samples++;

        // View direction: sector around the mesh x (from below, level, from above)
        const glm::vec3 direction = offset / distance;
        const f32 yaw = std::atan2(direction.x, direction.z) + glm::pi<f32>();                                  // 0 .. 2 pi
        const u32 yaw_bin = std::min(static_cast<u32>(yaw / glm::two_pi<f32>() * static_cast<f32>(ANALYSIS_YAW_BINS)), ANALYSIS_YAW_BINS - 1);
        const f32 pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
        const u32 pitch_bin = pitch < glm::radians(-20.0f) ? 0u : (pitch > glm::radians(20.0f) ? 2u : 1u);
        analysis.directions[pitch_bin * ANALYSIS_YAW_BINS + yaw_bin]++;
        analysis.dirty = true;
    }


    void mesh_editor_window::reset_analysis(const bool keep_estimate) {

        analysis_state& analysis = m_analysis;
        if (analysis.sweep.active)
            stop_analysis_sweep();

        for (analysis_series& series : analysis.series) {
            const u64 signature = keep_estimate ? series.signature : 0;
            series = {};
            series.signature = signature;
        }
        analysis.directions = {};
        analysis.curves = {};
        analysis.live = {};
        analysis.live_geometry_ms = 0.0f;
        analysis.live_gpu_ms = 0.0f;
        analysis.last_serial = 0;
        analysis.settle = SETTLE_STATISTICS;        // statistics still in flight describe what was just dropped
        analysis.data_extent = { 0, 0 };
        analysis.dirty = true;                      // the saved file follows (removed when nothing is left)

        if (keep_estimate)
            return;

        if (analysis.estimate_job.valid())
            analysis.estimate_job.wait();           // works on copies; just make sure its result is not applied to the next mesh
        analysis.estimate_job = {};
        analysis.estimate = {};
        analysis.has_estimate = false;
        analysis.estimate_serial++;
        analysis.geometry_signature = 0;
    }

    // persistence ------------------------------------------------------------------------------------------------------
    //
    // <project>/metadata/asset_stats/<asset uuid>.yml, one per mesh, through GLT::serializer::yaml:
    //
    //      render_samples:
    //        version: 1
    //        asset: 5eed5eed5eed5eed                   UUID of the mesh asset (also the file name)
    //        path: meshes/rock.glt_asset               where it was when saved (informational)
    //        geometry: 22b1ab47abce6b89                source geometry the samples belong to (else all are dropped)
    //        extent_width: 900                         preview size they were taken at
    //        extent_height: 700
    //        directions:                               samples per view direction (yaw sector x height band)
    //          - 12
    //        series:                                   0 = discrete LODs, 1 = virtual geometry
    //        - id: 0
    //          signature: 421820359f446c90             LOD levels / cluster hierarchy they were measured with
    //          samples: 424
    //        ranges:                                   sums per distance range
    //        - series: 0
    //          range: 2
    //          samples: 22
    //          ...
    //
    // The signatures are the ones update_analysis() compares every frame, so data that no longer fits the mesh is
    // dropped on the first update after loading, exactly as in-session. GPU timings are only meaningful on the machine
    // that measured them: the folder belongs out of version control

    void mesh_editor_window::load_analysis_data(const GLT::UUID asset_id) {
        GLT_PROFILE_SCOPE("mesh_editor: load collected data");

        analysis_state& analysis = m_analysis;
        analysis.data_file.clear();
        analysis.dirty = false;
        analysis.unsaved_seconds = 0.0f;

        const std::filesystem::path project = GLT::application::get().get_project_path();
        if (asset_id == 0 || project.empty())
            return;

        analysis.data_file = project / GLT::config::METADATA_DIR / ANALYSIS_DATA_DIR
            / (hex_text(static_cast<u64>(asset_id)) + GLT::config::FILE_EXTENSION_CONFIG);

        std::error_code error{};
        if (!std::filesystem::is_regular_file(analysis.data_file, error))
            return;                                 // nothing collected for this asset yet

        u32 version = 0;
        std::string asset, path, geometry_text;
        glm::uvec2 extent{ 0, 0 };
        std::vector<u32> directions;
        std::vector<analysis_series_row> series_rows;
        std::vector<analysis_range_row> range_rows;
        bool success = true;
        {
            GLT::serializer::yaml yaml(analysis.data_file, ANALYSIS_SECTION, GLT::serializer::option::load, &success);
            serialize_analysis(yaml, version, asset, path, geometry_text, extent, directions, series_rows, range_rows);
        }

        // ---- validate before anything is used ----
        std::array<analysis_series, series_count> series{};
        std::array<u32, ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS> direction_counts{};
        u64 geometry = 0;
        bool valid = success && version == ANALYSIS_FILE_VERSION && parse_hex(geometry_text, geometry) && geometry != 0
            && directions.size() == direction_counts.size();
        for (const analysis_series_row& row : series_rows) {
            u64 signature = 0;
            valid &= row.id < series_count && parse_hex(row.signature, signature);
            if (valid) {
                series[row.id].signature = signature;
                series[row.id].samples = row.samples;
            }
        }
        for (const analysis_range_row& row : range_rows) {
            valid &= row.series < series_count && row.range < ANALYSIS_RANGES && row.timed_samples <= row.samples;
            if (valid)
                series[row.series].ranges[row.range] = analysis_range{ row.samples, row.timed_samples, row.geometry_ms, row.gpu_ms,
                    row.triangles, row.draws };
        }

        if (!valid) {
            LOG(warn, "mesh_editor: [{}] collected render data in [{}] is unreadable or from another version - starting over",
                m_mesh_name, analysis.data_file.generic_string());
            analysis.dirty = true;                  // overwritten / removed by the next save
            return;
        }

        std::copy(directions.begin(), directions.end(), direction_counts.begin());
        analysis.series = series;
        analysis.directions = direction_counts;
        analysis.geometry_signature = geometry;
        analysis.data_extent = extent;
        LOG(info, "mesh_editor: [{}] loaded {} + {} collected render samples", m_mesh_name, series[series_lods].samples,
            series[series_virtual_geometry].samples);
    }


    void mesh_editor_window::save_analysis_data() {
        GLT_PROFILE_SCOPE("mesh_editor: save collected data");

        analysis_state& analysis = m_analysis;
        if (!analysis.dirty || analysis.data_file.empty())
            return;
        analysis.dirty = false;
        analysis.unsaved_seconds = 0.0f;

        std::error_code error{};
        if (analysis.series[series_lods].samples + analysis.series[series_virtual_geometry].samples == 0) {
            std::filesystem::remove(analysis.data_file, error);         // nothing left (reset, or dropped as outdated)
            return;
        }

        u32 version = ANALYSIS_FILE_VERSION;
        std::string asset = analysis.data_file.stem().string();
        std::string path = m_mesh_path.generic_string();
        std::string geometry = hex_text(analysis.geometry_signature);
        glm::uvec2 extent = analysis.data_extent;
        std::vector<u32> directions(analysis.directions.begin(), analysis.directions.end());
        std::vector<analysis_series_row> series_rows;
        std::vector<analysis_range_row> range_rows;
        for (u32 id = 0; id < series_count; ++id) {

            const analysis_series& series = analysis.series[id];
            series_rows.push_back({ id, hex_text(series.signature), series.samples });
            for (u32 index = 0; index < ANALYSIS_RANGES; ++index) {
                const analysis_range& range = series.ranges[index];
                if (range.samples > 0)
                    range_rows.push_back({ id, index, range.samples, range.timed_samples, range.geometry_ms, range.gpu_ms, range.triangles, range.draws });
            }
        }

        // The serializer keeps other sections of an existing file; this file only holds this one: start from empty.
        // Also drop the plain-text file of the first version (<uuid>.txt)
        std::filesystem::remove(analysis.data_file, error);
        std::filesystem::remove(std::filesystem::path(analysis.data_file).replace_extension(".txt"), error);
        bool success = true;
        {
            GLT::serializer::yaml yaml(analysis.data_file, ANALYSIS_SECTION, GLT::serializer::option::save, &success);
            serialize_analysis(yaml, version, asset, path, geometry, extent, directions, series_rows, range_rows);
        }                                                               // written when the serializer goes out of scope
        if (!success) {
            LOG(warn, "mesh_editor: cannot write collected render data to [{}]", analysis.data_file.generic_string());
        }
    }

    // computed curves --------------------------------------------------------------------------------------------------

    void mesh_editor_window::update_analysis_curves(const GLT::asset::mesh::mesh_asset& mesh) {

        using namespace GLT::asset::mesh;
        analysis_curves& curves = m_analysis.curves;
        const mesh_sphere sphere = sphere_of(mesh);
        const GLT::world::camera_snapshot camera = build_camera_snapshot();
        const std::span<const cluster> clusters = analysis_clusters(mesh);

        // Everything but the direction: a change makes all points wrong at once
        u64 data_key = GLT::util::mix_u64(0x637572766573ull, mesh.revision);
        data_key = GLT::util::mix_u64(data_key, m_analysis.estimate_serial);
        data_key = GLT::util::mix_u64(data_key, clusters.size());
        data_key = GLT::util::mix_u64(data_key, float_bits(camera.fov));
        data_key = GLT::util::mix_u64(data_key, m_last_preview_size.y / 32u);

        const f32 projection = projection_scale(camera.fov);
        if (data_key != curves.data_key) {

            curves.data_key = data_key;
            curves.filled = 0;
            curves.has_clusters = !clusters.empty();
            const glm::vec3 offset = camera.position - sphere.centre;
            const f32 length = glm::length(offset);
            curves.direction = length > 1e-5f ? offset / length : glm::vec3(0.0f, 0.0f, 1.0f);

            for (u32 point = 0; point < ANALYSIS_CURVE_POINTS; ++point) {          // LOD points: a table lookup each
                const f32 screen_size = analysis_range_screen(0.5f * static_cast<f32>(point));
                const bool culled = mesh.lod.cull_screen_size > 0.0f && screen_size < mesh.lod.cull_screen_size;
                const u32 level = auto_lod_for(mesh, screen_size);
                curves.distance[point] = sphere.radius * projection / screen_size;
                curves.lod_triangles[point] = culled ? 0.0f : static_cast<f32>(mesh.lod_triangle_count(level));
                curves.lod_draws[point] = culled ? 0.0f : static_cast<f32>(lod_draw_count(mesh, level));
                curves.cluster_triangles[point] = 0.0f;
                curves.cluster_draws[point] = 0.0f;
            }
        }

        if (!curves.has_clusters) {
            curves.filled = ANALYSIS_CURVE_POINTS;
            return;
        }

        GLT_PROFILE_SCOPE("mesh_editor: analysis curves");
        for (u32 budget = CURVE_POINTS_PER_FRAME; curves.filled < ANALYSIS_CURVE_POINTS && budget > 0; ++curves.filled, --budget) {

            const u32 point = curves.filled;
            const f32 screen_size = analysis_range_screen(0.5f * static_cast<f32>(point));
            if (mesh.lod.cull_screen_size > 0.0f && screen_size < mesh.lod.cull_screen_size)
                continue;                                   // not drawn at all: stays 0

            const f32 distance = curves.distance[point];
            cluster_view view = preview_cluster_view(mesh);
            view.camera_position = sphere.centre + curves.direction * distance;
            view.near_plane = std::max(0.01f, distance * 0.01f);
            const cluster_cut cut = measure_cut(clusters, view);
            curves.cluster_triangles[point] = static_cast<f32>(cut.triangles);
            curves.cluster_draws[point] = static_cast<f32>(cut.clusters);
        }
    }

    // virtual geometry estimate ----------------------------------------------------------------------------------------

    void mesh_editor_window::start_analysis_estimate() {

        if (m_analysis.estimate_job.valid())
            return;

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh || mesh->has_clusters())
            return;

        GLT::asset::mesh::cluster_settings settings = mesh->virtual_geometry;
        GLT::asset::mesh::sanitize(settings);

        // Copies: the job runs on a worker thread while the asset stays live
        m_analysis.estimate_job = std::async(std::launch::async,
            [vertices = mesh->vertices, indices = mesh->indices, submeshes = mesh->submeshes, settings]() {
                return GLT::asset::mesh::build_clusters(vertices, indices, submeshes, settings);
            });
    }


    void mesh_editor_window::poll_analysis_estimate() {

        analysis_state& analysis = m_analysis;
        if (!analysis.estimate_job.valid() || analysis.estimate_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        GLT::asset::mesh::cluster_build_output result = analysis.estimate_job.get();
        analysis.estimate_job = {};

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh || mesh->has_clusters())
            return;                                 // the mode was switched meanwhile: the real hierarchy is used

        if (!GLT::asset::mesh::validate_clusters(result.clusters, result.indices, result.submeshes,
            mesh->vertices.size() + result.vertices.size(), mesh->submeshes.size())) {

            LOG(warn, "mesh_editor: [{}] virtual geometry estimate produced no usable hierarchy", m_mesh_name);
            return;
        }

        analysis.estimate = std::move(result);
        analysis.has_estimate = true;
        analysis.estimate_serial++;
    }

    // Collect Data ----------------------------------------------------------------------------------------------------

    void mesh_editor_window::start_analysis_sweep() {

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh || !m_preview_scene || m_analysis.sweep.active)
            return;

        analysis_sweep sweep{};
        bool found = false;
        for (u32 range = 0; range < ANALYSIS_RANGES; ++range) {
            if (!analysis_range_reachable(*mesh, range))
                continue;
            if (!found)
                sweep.first = static_cast<f32>(range);
            sweep.last = static_cast<f32>(range + 1);
            found = true;
        }
        if (!found)
            return;

        sweep.active = true;
        sweep.pass_count = (mesh->uses_virtual_geometry() && mesh->lod_count() > 1) ? 2u : 1u;
        sweep.position = sweep.first;
        sweep.saved_camera = m_preview_camera;
        sweep.saved_lod = m_preview_lod;
        sweep.saved_auto_rotate = m_auto_rotate;
        m_analysis.sweep = sweep;
        m_auto_rotate = true;                       // orbits during the flight (update_analysis_sweep turns the camera)

        // Timings are only collected in the default render mode
        if (m_preview_scene->get_render_mode() != default_preview_render_mode())
            set_preview_render_mode(default_preview_render_mode());
        set_preview_lod(-1);
        update_analysis_sweep(*mesh, 0.0f);         // camera to the start right away
    }


    void mesh_editor_window::stop_analysis_sweep() {

        analysis_sweep& sweep = m_analysis.sweep;
        if (!sweep.active)
            return;

        sweep.active = false;
        m_preview_camera = sweep.saved_camera;
        m_auto_rotate = sweep.saved_auto_rotate;
        set_preview_lod(sweep.saved_lod);
        save_analysis_data();
    }


    f32 mesh_editor_window::analysis_sweep_progress() const noexcept {

        const analysis_sweep& sweep = m_analysis.sweep;
        const f32 span = std::max(sweep.last - sweep.first, 1e-3f);
        const f32 in_pass = std::clamp(sweep.pass == 0 ? (sweep.position - sweep.first) / span : (sweep.last - sweep.position) / span, 0.0f, 1.0f);
        return (static_cast<f32>(sweep.pass) + in_pass) / static_cast<f32>(std::max(sweep.pass_count, 1u));
    }


    void mesh_editor_window::update_analysis_sweep(const GLT::asset::mesh::mesh_asset& mesh, const f32 seconds) {

        analysis_sweep& sweep = m_analysis.sweep;
        if (!sweep.active)
            return;

        // The mesh changed mode (or lost its levels) during the flight: the plan does not fit any more
        const u32 pass_count = (mesh.uses_virtual_geometry() && mesh.lod_count() > 1) ? 2u : 1u;
        if (pass_count < sweep.pass_count && sweep.pass >= pass_count) {
            stop_analysis_sweep();
            return;
        }

        // ---- pace: an even zoom over time (smooth, independent of the statistics) ----
        const f32 dt = std::clamp(seconds, 0.0f, 0.1f);    // a long hitch must not skip ranges
        sweep.elapsed += dt;
        const f32 step = dt / SWEEP_SECONDS_PER_RANGE;
        sweep.position += sweep.pass == 0 ? step : -step;

        if (sweep.pass == 0 && sweep.position >= sweep.last) {
            if (sweep.pass_count < 2) {
                stop_analysis_sweep();
                return;
            }
            sweep.pass = 1;                         // turn around: fly back in with the fallback levels
            sweep.position = sweep.last;
        } else if (sweep.pass == 1 && sweep.position <= sweep.first) {
            stop_analysis_sweep();
            return;
        }

        // ---- camera: distance from the axis position; auto rotate (the preview's speed, at least the sweep's) and a
        // slow swing of the height, so all sides are seen ----
        const f32 turns_per_second = std::max(m_auto_rotate_speed / 100.0f, SWEEP_MIN_TURNS_PER_SECOND);  // same unit as update()
        m_preview_camera.target = sphere_of(mesh).centre;
        m_preview_camera.distance = std::clamp(analysis_range_distance(mesh, std::clamp(sweep.position, sweep.first, sweep.last - 1e-3f)),
            MIN_CAM_DISTANCE, MAX_CAM_DISTANCE);
        m_preview_camera.yaw_rad += glm::two_pi<f32>() * turns_per_second * dt;
        m_preview_camera.pitch_rad = SWEEP_PITCH_AMPLITUDE * std::sin(glm::two_pi<f32>() * sweep.elapsed / SWEEP_PITCH_SECONDS);

        set_preview_lod(sweep.pass == 1 ? static_cast<i32>(auto_lod_for(mesh, analysis_range_screen(sweep.position))) : -1);
    }

    // the view ---------------------------------------------------------------------------------------------------------

    void mesh_editor_window::draw_rendering_analysis() {
        GLT_PROFILE_SCOPE("mesh_editor: rendering view");

        auto registry = GLT::asset::registry::get_ref();
        auto* mesh = registry ? registry->data_as<GLT::asset::mesh::mesh_asset>(m_asset_handle) : nullptr;
        if (!mesh)
            return;

        update_analysis_curves(*mesh);

        // ---- what is analysed (the panel switch above already says "Rendering") ----
        const std::string subtitle = std::format("{} · {} triangles · {}", m_mesh_name, count_text(mesh->indices.size() / 3),
            mesh->detail() == GLT::asset::mesh::detail_mode::virtual_geometry ? "Virtual geometry" : "Discrete LODs");
        ImGui::TextDisabled("%s", subtitle.c_str());
        ImGui::Dummy(ImVec2(0.f, 6.f));

        draw_analysis_recommendation(*mesh);
        draw_analysis_view_section(*mesh);
        draw_analysis_coverage_section(*mesh);
        draw_analysis_charts_section(*mesh);
        draw_analysis_memory_section(*mesh);
    }


    void mesh_editor_window::draw_analysis_recommendation(GLT::asset::mesh::mesh_asset& mesh) {

        using namespace GLT::asset::mesh;
        const analysis_state& analysis = m_analysis;
        const analysis_curves& curves = analysis.curves;
        const bool virtual_mode = mesh.detail() == detail_mode::virtual_geometry;
        const u32 source_triangles = static_cast<u32>(mesh.indices.size() / 3);
        const bool estimated = !mesh.has_clusters() && analysis.has_estimate;
        const analysis_coverage coverage = analysis_coverage_of(mesh);

        // ---- measured: GPU time of the geometry passes, in the ranges where both systems were measured ----
        struct timing {

            u32                                     ranges = 0;
            f64                                     lods = 0.0;             // sums of the range means
            f64                                     clusters = 0.0;
        };
        timing all{}, close_up{}, far_away{};
        for (u32 range = 0; range < ANALYSIS_RANGES; ++range) {

            const analysis_range& lods = analysis.series[series_lods].ranges[range];
            const analysis_range& clusters = analysis.series[series_virtual_geometry].ranges[range];
            if (lods.timed_samples < RANGE_SEEN_SAMPLES || clusters.timed_samples < RANGE_SEEN_SAMPLES)
                continue;

            const f64 lod_ms = lods.geometry_ms / static_cast<f64>(lods.timed_samples);
            const f64 cluster_ms = clusters.geometry_ms / static_cast<f64>(clusters.timed_samples);
            for (timing* sum : { &all, analysis_range_screen(static_cast<f32>(range) + 0.5f) >= NEAR_SCREEN_SIZE ? &close_up : &far_away }) {
                sum->ranges++;
                sum->lods += lod_ms;
                sum->clusters += cluster_ms;
            }
        }
        const auto clear = [](const timing& sum) {
            const f64 slower = std::max(sum.lods, sum.clusters);
            return sum.ranges > 0 && slower > 0.0 && std::abs(sum.lods - sum.clusters) / slower >= DECISIVE_TIME_DIFFERENCE;
        };
        const auto clusters_win = [](const timing& sum) { return sum.clusters < sum.lods; };
        const bool measured = all.ranges >= 3 && std::max(all.lods, all.clusters) > 0.0;
        const bool decisive = measured && clear(all);

        // One system is faster close up, the other far away: then there is nothing to recommend but to look at where
        // the mesh is used
        const bool mixed = decisive && clear(close_up) && clear(far_away) && clusters_win(close_up) != clusters_win(far_away);

        // ---- computed: triangles each system selects, averaged over the distance ranges ----
        f64 lod_triangles = 0.0, cluster_triangles = 0.0;
        u32 curve_ranges = 0;
        if (curves.has_clusters && curves.data_key != 0 && curves.filled == ANALYSIS_CURVE_POINTS)
            for (u32 range = 0; range < ANALYSIS_RANGES; ++range)
                if (analysis_range_reachable(mesh, range)) {
                    lod_triangles += static_cast<f64>(curves.lod_triangles[range * 2 + 1]);
                    cluster_triangles += static_cast<f64>(curves.cluster_triangles[range * 2 + 1]);
                    curve_ranges++;
                }
        const bool computed = curve_ranges > 0 && lod_triangles > 0.0;

        // ---- the verdict ----
        detail_mode recommended = detail_mode::discrete_lods;
        const char* basis = "Rule of thumb";
        std::vector<std::string> reasons;
        const auto name_of = [](const detail_mode mode) { return mode == detail_mode::virtual_geometry ? "virtual geometry" : "discrete LODs"; };

        if (decisive) {

            recommended = mixed ? mesh.detail() : (clusters_win(all) ? detail_mode::virtual_geometry : detail_mode::discrete_lods);
            basis = "Measured in this preview";
            reasons.push_back(std::format("The geometry passes take {} ms as virtual geometry and {} ms with discrete LODs "
                "(mean of the {} distance ranges measured with both).", ms_text(all.clusters / all.ranges), ms_text(all.lods / all.ranges), all.ranges));

            if (mixed)
                reasons.push_back(std::format("Close up {} is faster ({} vs {} ms), far away {} ({} vs {} ms). Go by where this mesh "
                    "is usually seen.",
                    name_of(clusters_win(close_up) ? detail_mode::virtual_geometry : detail_mode::discrete_lods),
                    ms_text(std::min(close_up.clusters, close_up.lods) / close_up.ranges), ms_text(std::max(close_up.clusters, close_up.lods) / close_up.ranges),
                    name_of(clusters_win(far_away) ? detail_mode::virtual_geometry : detail_mode::discrete_lods),
                    ms_text(std::min(far_away.clusters, far_away.lods) / far_away.ranges), ms_text(std::max(far_away.clusters, far_away.lods) / far_away.ranges)));
        } else {

            if (measured)
                reasons.push_back(std::format("Measured GPU time is a tie: {} ms as virtual geometry, {} ms with discrete LODs "
                    "(geometry passes, {} distance ranges).", ms_text(all.clusters / all.ranges), ms_text(all.lods / all.ranges), all.ranges));

            if (source_triangles < SMALL_MESH_TRIANGLES) {

                recommended = detail_mode::discrete_lods;
                reasons.push_back(std::format("Only {} triangles. A mesh this small is cheap either way; clusters would add memory and "
                    "draw commands without a visible gain.", count_text(source_triangles)));

            } else if (computed) {

                basis = estimated ? "Estimate from triangle counts" : "Triangle counts over distance";
                const f64 ratio = cluster_triangles / lod_triangles;
                const std::string averages = std::format("{} vs {} on average", count_text(static_cast<u64>(cluster_triangles / curve_ranges)),
                    count_text(static_cast<u64>(lod_triangles / curve_ranges)));

                if (ratio < 0.75) {
                    recommended = detail_mode::virtual_geometry;
                    reasons.push_back(std::format("From near to far, virtual geometry selects {:.0f}% fewer triangles than the LOD levels ({}).",
                        (1.0 - ratio) * 100.0, averages));
                } else if (ratio > 1.15) {
                    recommended = detail_mode::discrete_lods;
                    reasons.push_back(std::format("From near to far, the LOD levels select fewer triangles than virtual geometry would ({}).", averages));
                } else if (source_triangles >= DENSE_MESH_TRIANGLES) {
                    recommended = detail_mode::virtual_geometry;
                    reasons.push_back(std::format("Both select about the same number of triangles ({}). For a mesh this dense clusters still "
                        "win whenever only part of it is close or in view: a LOD level is always drawn whole.", averages));
                } else {
                    recommended = detail_mode::discrete_lods;
                    reasons.push_back(std::format("Both select about the same number of triangles ({}). Discrete LODs then need less memory "
                        "and one draw command per material.", averages));
                }

            } else if (source_triangles >= DENSE_MESH_TRIANGLES) {

                recommended = detail_mode::virtual_geometry;
                reasons.push_back(std::format("{} triangles. Dense meshes usually gain from clusters: close parts stay detailed while far "
                    "and hidden parts get cheap.", count_text(source_triangles)));
            } else {

                recommended = detail_mode::discrete_lods;
                reasons.push_back(std::format("{} triangles. At this size discrete LODs are usually enough and need less memory.",
                    count_text(source_triangles)));
            }

            if (!computed && !virtual_mode && source_triangles >= SMALL_MESH_TRIANGLES)
                reasons.push_back("That is a rule of thumb. 'Estimate virtual geometry' below replaces it with numbers for this mesh.");
        }

        // ---- what else stands out ----
        std::vector<std::string> notes;
        if (virtual_mode) {

            if (mesh.has_clusters() && mesh.cluster_normals() == cluster_normals_mode::keep)
                notes.push_back("Coarse clusters are shaded with the mesh's own normals ('Keep source'), which can look blotchy. "
                    "Cluster Build > Normals in the inspector has better methods.");
            if (mesh.lod_count() <= 1)
                notes.push_back(std::format("No fallback levels: shadows and translucent materials draw all {} triangles. "
                    "Generate them under Fallback Levels in the inspector.", count_text(source_triangles)));
            if (analysis.live.cluster_draws > 3000)
                notes.push_back(std::format("This view needs {} draw commands, one per cluster. A larger cluster size "
                    "(Cluster Build, inspector) lowers that.", count_text(analysis.live.cluster_draws)));
        } else if (mesh.lod_count() <= 1 && source_triangles >= 5000)
            notes.push_back(std::format("No LOD levels: all {} triangles are drawn at any distance. Add levels under "
                "Level of Detail in the inspector.", count_text(source_triangles)));

        // Far away, clusters cannot get coarser than their locked borders allow
        if (curves.has_clusters && curves.data_key != 0 && curves.filled == ANALYSIS_CURVE_POINTS
            && (virtual_mode || recommended == detail_mode::virtual_geometry)) {
            const u32 floor_triangles = static_cast<u32>(curves.cluster_triangles[ANALYSIS_CURVE_POINTS - 1]);
            const u32 lowest_level = mesh.lod_triangle_count(mesh.lod_count() - 1);
            if (mesh.lod_count() > 1 && floor_triangles > 2000 && floor_triangles > lowest_level * 4)
                notes.push_back(std::format("Far away the mesh still draws about {} triangles as virtual geometry (its lowest LOD level "
                    "has {}). That adds up with many distant instances; 'Lock material seams' off or larger clusters lower it.",
                    count_text(floor_triangles), count_text(lowest_level)));
        }

        // Triangles finer than the screen can show, in the current view. Only while the view stands still: the statistics
        // are a few frames old and must belong to it
        const glm::vec3 view_position = build_camera_snapshot().position;
        if (!analysis.sweep.active && analysis.live.serial != 0 && analysis.live.triangles > 2000 && analysis.live.extent.y > 0
            && glm::distance(analysis.live.camera_position, view_position) <= 0.01f * glm::distance(view_position, sphere_of(mesh).centre)) {

            const mesh_sphere sphere = sphere_of(mesh);
            const f32 distance = std::max(glm::distance(view_position, sphere.centre), 1e-4f);
            const f32 screen_size = sphere.radius * projection_scale(m_preview_camera.fov_deg) / distance;
            const f32 diameter = screen_size * static_cast<f32>(analysis.live.extent.y);
            const f32 covered = std::min(0.785f * diameter * diameter, static_cast<f32>(analysis.live.extent.x) * static_cast<f32>(analysis.live.extent.y));
            const f32 pixels_per_triangle = covered / (0.5f * static_cast<f32>(analysis.live.triangles));

            if (pixels_per_triangle < 2.0f) {
                const char* remedy = "A lower 'Detail scale' (Cluster Detail, inspector) draws fewer; if nothing changes the "
                    "clusters are at their coarsest.";
                if (analysis.live.cluster_draws == 0)
                    remedy = auto_lod_for(mesh, screen_size) + 1 >= mesh.lod_count()
                        ? "This is the lowest LOD level: add a coarser one, or set 'Cull below' so the mesh is not drawn this small."
                        : "A larger screen size for the next LOD level switches to it earlier.";
                notes.push_back(std::format("From here the triangles are {}: finer than the screen can show. {}",
                    pixels_per_triangle < 0.5f ? std::string("far smaller than a pixel") : std::format("about {:.1f} pixels each", pixels_per_triangle),
                    remedy));
            }
        }

        // ---- card ----
        const bool keep = recommended == mesh.detail();
        const bool busy = m_cluster_job.valid() || m_lod_job.valid();
        bool switch_mode = false;

        if (UI::begin_card("Recommendation", UI::card_options{ .description = "Which detail system suits this mesh" })) {

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
            ImGui::PushStyleColor(ImGuiCol_Text, (keep && !mixed) ? COLOR_GOOD : COLOR_WARNING);
            const std::string headline = mixed ? std::string("No clear winner: it depends on the distance")
                : (keep ? std::format("Keep {}", recommended == detail_mode::virtual_geometry ? "Virtual Geometry" : "Discrete LODs")
                        : std::format("{} would suit it better", recommended == detail_mode::virtual_geometry ? "Virtual Geometry" : "Discrete LODs"));
            ImGui::TextWrapped("%s", headline.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();

            const u32 samples = analysis.series[series_lods].samples + analysis.series[series_virtual_geometry].samples;
            ImGui::TextDisabled("Basis: %s · %s samples", basis, count_text(samples).c_str());
            ImGui::Dummy(ImVec2(0.f, 2.f));

            for (const std::string& reason : reasons)
                bullet_text(ImGui::GetStyleColorVec4(ImGuiCol_Text), reason);

            // The reminder: one view says little
            const bool few_distances = coverage.ranges_seen * 2 < coverage.ranges_expected;
            const bool few_directions = coverage.directions_seen * 3 < ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS;
            if (few_distances) {
                ImGui::Dummy(ImVec2(0.f, 2.f));
                bullet_text(COLOR_WARNING, std::format("Seen at few distances so far ({} of {} ranges{}). A mesh that is cheap from here can "
                    "be expensive close up, or the other way round: zoom in and out{}, or press 'Collect Data' below.",
                    coverage.ranges_seen, coverage.ranges_expected,
                    few_directions ? std::format(", {} of {} directions", coverage.directions_seen, ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS) : std::string(),
                    few_directions ? " and orbit it" : ""));
            } else if (few_directions) {
                ImGui::Dummy(ImVec2(0.f, 2.f));
                bullet_text(COLOR_WARNING, std::format("Seen from few sides so far ({} of {} directions). Orbit the mesh: back sides and "
                    "undersides can be denser.", coverage.directions_seen, ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS));
            } else if (!measured && virtual_mode && mesh.lod_count() > 1)
                bullet_text(COLOR_WARNING, "GPU time is only measured for virtual geometry so far. 'Collect Data' below also "
                    "draws the fallback levels and compares the two.");

            if (!keep) {
                ImGui::Dummy(ImVec2(0.f, 4.f));
                ImGui::BeginDisabled(busy);
                if (UI::accent_button(recommended == detail_mode::virtual_geometry ? "Switch to Virtual Geometry" : "Switch to Discrete LODs",
                    ImVec2(-FLT_MIN, 0.f)))
                    switch_mode = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", recommended == detail_mode::virtual_geometry
                        ? "Builds the cluster hierarchy and draws the mesh with it. What was measured with the LOD levels stays for comparison"
                        : "Removes the clusters and draws the mesh with its LOD levels. What was measured as virtual geometry stays for comparison");
                ImGui::EndDisabled();
            }

            // A mesh in LOD mode has nothing to compute virtual geometry numbers from
            if (!virtual_mode && !mesh.has_clusters() && source_triangles >= SMALL_MESH_TRIANGLES) {

                ImGui::Dummy(ImVec2(0.f, 4.f));
                const bool estimating = analysis.estimate_job.valid();
                ImGui::BeginDisabled(estimating);
                if (UI::subtle_button(estimating ? "Estimating virtual geometry..."
                    : (analysis.has_estimate ? "Estimate virtual geometry again" : "Estimate virtual geometry"), ImVec2(-FLT_MIN, 0.f)))
                    start_analysis_estimate();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Builds the cluster hierarchy in the background for this panel only: nothing is saved or drawn.\n"
                        "Gives the triangles, draw commands and memory virtual geometry would need. GPU time can only be\n"
                        "measured after switching the mode");
                ImGui::EndDisabled();
            }

            if (!notes.empty()) {
                ImGui::Dummy(ImVec2(0.f, 4.f));
                ImGui::TextDisabled("Also worth a look");
                for (const std::string& note : notes)
                    bullet_text(COLOR_WARNING, note);
            }
        }
        UI::end_card();

        if (switch_mode)
            apply_detail_mode(recommended);         // last: it removes or replaces the cluster data read above
    }


    void mesh_editor_window::draw_analysis_view_section(const GLT::asset::mesh::mesh_asset& mesh) {

        using namespace GLT::asset::mesh;
        const analysis_state& analysis = m_analysis;
        const mesh_sphere sphere = sphere_of(mesh);
        const GLT::world::camera_snapshot camera = build_camera_snapshot();
        const f32 distance = std::max(glm::distance(camera.position, sphere.centre), 1e-4f);
        const f32 screen_size = sphere.radius * projection_scale(camera.fov) / distance;

        const bool virtual_mode = mesh.detail() == detail_mode::virtual_geometry;
        const bool clusters_drawn = preview_uses_virtual_geometry(mesh);
        const u32 auto_level = auto_lod_for(mesh, screen_size);
        const u32 level = m_preview_lod >= 0 ? std::min(static_cast<u32>(m_preview_lod), mesh.lod_count() - 1) : auto_level;
        const bool have_stats = analysis.live.serial != 0;

        const std::span<const cluster> clusters = analysis_clusters(mesh);
        const cluster_cut cut = clusters.empty() ? cluster_cut{}
            : cached_cut(mesh.has_clusters() ? m_preview_cut : m_estimate_cut, clusters, preview_cluster_view(mesh),
                mesh.has_clusters() ? mesh.revision : static_cast<u32>(m_analysis.estimate_serial)).cut;

        if (UI::begin_card("This View", UI::card_options{ .description = "What the preview draws right now", .collapsible = true })) {

            // ---- what is drawn ----
            std::string drawn_as;
            if (clusters_drawn)
                drawn_as = std::format("Virtual geometry, {} clusters", count_text(have_stats ? analysis.live.cluster_draws : cut.clusters));
            else if (m_preview_lod >= 0)
                drawn_as = std::format("{} {} (locked)", virtual_mode ? "Fallback level" : "LOD", level);
            else
                drawn_as = std::format("LOD {} of {}", level, mesh.lod_count());
            UI::property_value("Drawn as", drawn_as);

            UI::property_value("Distance", std::format("{:.2f} · {:.0f}% of the view height", distance, std::min(screen_size, 9.99f) * 100.0f),
                "Camera to mesh centre, and the size of the mesh's bounding sphere on screen: what both detail systems select by");

            const u32 triangles = have_stats ? analysis.live.triangles : (clusters_drawn ? cut.triangles : mesh.lod_triangle_count(level));
            UI::property_value("Triangles", std::format("{} of {}", count_text(triangles), count_text(mesh.indices.size() / 3)),
                have_stats ? "Drawn in the main view after culling, as the renderer reports it"
                           : "Selected for this view (the renderer reports no statistics)");

            const u32 draws = have_stats ? analysis.live.draw_calls : (clusters_drawn ? cut.clusters : lod_draw_count(mesh, level));
            UI::property_value("Draw commands", count_text(draws),
                "Discrete LODs: one per material. Virtual geometry: one per cluster, all submitted in a few indirect calls");

            if (triangles > 0 && m_last_preview_size.y > 0) {
                const f32 diameter = screen_size * static_cast<f32>(m_last_preview_size.y);
                const f32 covered = std::min(0.785f * diameter * diameter, static_cast<f32>(m_last_preview_size.x) * static_cast<f32>(m_last_preview_size.y));
                UI::property_value("Pixels per triangle", std::format("about {:.1f}", covered / (0.5f * static_cast<f32>(triangles))),
                    "Rough: the screen area of the mesh's bounding sphere over its front facing triangles. Below about 2 the "
                    "geometry is finer than the screen can show");
            }

            if (have_stats && analysis.live.gpu_time_ms > 0.0f)
                UI::property_value("GPU time", std::format("{} ms geometry · {} ms frame", ms_text(analysis.live_geometry_ms), ms_text(analysis.live_gpu_ms)),
                    "Geometry: the passes that cull and draw the mesh (depth, shading, shadows). Frame: every pass of the preview. "
                    "One instance in an empty scene: compare the two systems with it, do not read it as the cost in a level");
            else
                UI::property_value("GPU time", have_stats ? "not reported by this device" : "no statistics yet");

            // ---- the same view with the other system ----
            if (virtual_mode)
                UI::property_value("As discrete LODs", mesh.lod_count() > 1
                    ? std::format("LOD {}: {} triangles, {} draws", auto_level, count_text(mesh.lod_triangle_count(auto_level)), lod_draw_count(mesh, auto_level))
                    : std::string("no levels: the full mesh"),
                    "What LOD mode would draw from here: the fallback level picked by the screen size");
            else if (!clusters.empty())
                UI::property_value("As virtual geometry", std::format("{} triangles in {} clusters", count_text(cut.triangles), count_text(cut.clusters)),
                    "What the cluster selection would pick from here at 1 pixel error, before culling (from the estimate)");
            else
                UI::property_value("As virtual geometry", "not estimated", "Press 'Estimate virtual geometry' in the recommendation above");

            // ---- why nothing is being collected ----
            if (m_preview_scene && m_preview_scene->get_render_mode() != default_preview_render_mode())
                dim_text("A debug view is active: this view is not added to the collected data.");
            else if (m_preview_lod >= 0 && !analysis.sweep.active && (clusters_drawn || level != auto_level))
                dim_text("The preview is locked to a level LOD mode would not pick here: this view is not added to the collected data.");
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_analysis_coverage_section(const GLT::asset::mesh::mesh_asset& mesh) {

        const analysis_state& analysis = m_analysis;
        const analysis_coverage coverage = analysis_coverage_of(mesh);
        const mesh_sphere sphere = sphere_of(mesh);
        const GLT::world::camera_snapshot camera = build_camera_snapshot();
        const glm::vec3 offset = camera.position - sphere.centre;
        const f32 distance = std::max(glm::length(offset), 1e-4f);
        const u32 current_range = analysis_range_of(sphere.radius * projection_scale(camera.fov) / distance);

        const std::string summary = std::format("{} of {} distances", coverage.ranges_seen, coverage.ranges_expected);

        // While "Collect Data" flies the camera the content of the card is inactive and a Stop button floats over
        // it. Cards are child windows: the overlay is drawn inside the card, over its content, so nothing covers it
        const bool measuring = analysis.sweep.active;
        if (UI::begin_card("Collected Data", UI::card_options{ .description = "Sampled while you look at the mesh in the preview",
            .summary = summary.c_str(), .collapsible = true })) {

            const ImVec2 content_min = ImGui::GetCursorScreenPos();
            const f32 content_width = ImGui::GetContentRegionAvail().x;
            ImGui::BeginDisabled(measuring);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImU32 empty_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
            const ImU32 outline_color = ImGui::GetColorU32(ImGuiCol_Text);
            const bool window_hovered = !measuring && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            // ---- distances: one cell per range, near to far ----
            const auto range_strip = [&](const char* label, const analysis_series& series, const ImVec4& color) {

                series_label(color, label);
                ImGui::SameLine();
                const std::string count = std::format("{} samples", count_text(series.samples));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(count.c_str()).x));
                ImGui::TextDisabled("%s", count.c_str());

                const f32 width = ImGui::GetContentRegionAvail().x;
                const f32 height = 12.f;
                const f32 gap = 2.f;
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(width, height));
                if (coverage.ranges_expected == 0)
                    return;

                const f32 cell = (width - gap * static_cast<f32>(coverage.ranges_expected - 1)) / static_cast<f32>(coverage.ranges_expected);
                u32 column = 0;
                for (u32 range = 0; range < ANALYSIS_RANGES; ++range) {

                    if (!analysis_range_reachable(mesh, range))
                        continue;
                    const ImVec2 min(origin.x + static_cast<f32>(column) * (cell + gap), origin.y);
                    const ImVec2 max(min.x + cell, min.y + height);
                    column++;

                    const u32 samples = series.ranges[range].samples;
                    ImVec4 fill = color;
                    fill.w = samples >= RANGE_SEEN_SAMPLES ? 1.0f : 0.35f;
                    draw_list->AddRectFilled(min, max, samples > 0 ? ImGui::GetColorU32(fill) : empty_color, 2.f);
                    if (range == current_range)
                        draw_list->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), outline_color, 2.f);

                    if (window_hovered && ImGui::IsMouseHoveringRect(min, max))
                        ImGui::SetTooltip("Distance %.2f - %.2f (mesh %.0f%% - %.0f%% of the view height)\n%u samples%s",
                            analysis_range_distance(mesh, static_cast<f32>(range)), analysis_range_distance(mesh, static_cast<f32>(range + 1)),
                            analysis_range_screen(static_cast<f32>(range)) * 100.0f, analysis_range_screen(static_cast<f32>(range + 1)) * 100.0f,
                            samples, range == current_range ? "\nThe preview is here now" : "");
                }
            };

            const analysis_series& lods = analysis.series[series_lods];
            const analysis_series& clusters = analysis.series[series_virtual_geometry];
            if (coverage.series == series_virtual_geometry || clusters.samples > 0)
                range_strip("Virtual geometry", clusters, COLOR_CLUSTERS);
            if (coverage.series == series_lods || lods.samples > 0)
                range_strip("Discrete LODs", lods, COLOR_LODS);

            ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
            ImGui::TextDisabled("near");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("far").x));
            ImGui::TextDisabled("far");
            ImGui::PopFont();

            // ---- directions: 8 sectors around the mesh x (above, level, below) ----
            ImGui::Dummy(ImVec2(0.f, 2.f));
            ImGui::TextDisabled("View directions");
            {
                const glm::vec3 direction = offset / distance;
                const f32 yaw = std::atan2(direction.x, direction.z) + glm::pi<f32>();
                const u32 current_yaw = std::min(static_cast<u32>(yaw / glm::two_pi<f32>() * static_cast<f32>(ANALYSIS_YAW_BINS)), ANALYSIS_YAW_BINS - 1);
                const f32 pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
                const u32 current_pitch = pitch < glm::radians(-20.0f) ? 0u : (pitch > glm::radians(20.0f) ? 2u : 1u);

                static constexpr const char* ROWS[ANALYSIS_PITCH_BINS] = { "from below", "level", "from above" };
                ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::small));
                const f32 label_width = ImGui::CalcTextSize("from above").x + 8.f;
                const f32 height = ImGui::GetTextLineHeight();
                const f32 gap = 2.f;
                const f32 cell = (ImGui::GetContentRegionAvail().x - label_width - gap * static_cast<f32>(ANALYSIS_YAW_BINS - 1)) / static_cast<f32>(ANALYSIS_YAW_BINS);

                for (u32 row = ANALYSIS_PITCH_BINS; row-- > 0;) {

                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    ImGui::TextDisabled("%s", ROWS[row]);
                    for (u32 column = 0; column < ANALYSIS_YAW_BINS; ++column) {

                        const ImVec2 min(origin.x + label_width + static_cast<f32>(column) * (cell + gap), origin.y + 1.f);
                        const ImVec2 max(min.x + cell, origin.y + height - 1.f);
                        const u32 samples = analysis.directions[row * ANALYSIS_YAW_BINS + column];
                        ImVec4 fill = COLOR_GOOD;
                        fill.w = samples >= DIRECTION_SEEN_SAMPLES ? 1.0f : 0.35f;
                        draw_list->AddRectFilled(min, max, samples > 0 ? ImGui::GetColorU32(fill) : empty_color, 2.f);
                        if (row == current_pitch && column == current_yaw)
                            draw_list->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), outline_color, 2.f);
                    }
                }
                ImGui::PopFont();
            }

            // ---- the reminder (not while the measurement moves the camera: it changes with every step) ----
            ImGui::Dummy(ImVec2(0.f, 4.f));
            bool complete = true;
            if (analysis.sweep.active)
                complete = false;
            else if (coverage.ranges_expected > 0 && coverage.ranges_seen == 0) {
                bullet_text(COLOR_WARNING, "Nothing collected yet. Look at the mesh in the preview: zoom in and out, orbit around it.");
                complete = false;
            } else {
                if (coverage.nearest_seen > coverage.first_expected) {
                    bullet_text(COLOR_WARNING, std::format("Zoom in: nothing collected closer than {:.1f}. Close up is where dense "
                        "meshes get expensive.", analysis_range_distance(mesh, static_cast<f32>(coverage.nearest_seen))));
                    complete = false;
                }
                if (coverage.farthest_seen >= 0 && coverage.farthest_seen < coverage.last_expected) {
                    bullet_text(COLOR_WARNING, std::format("Zoom out: nothing collected beyond {:.1f} (the mesh smaller than {:.0f}% of the "
                        "view). Far away is where most instances of a mesh are.",
                        analysis_range_distance(mesh, static_cast<f32>(coverage.farthest_seen + 1)),
                        analysis_range_screen(static_cast<f32>(coverage.farthest_seen + 1)) * 100.0f));
                    complete = false;
                }
                const u32 between = coverage.farthest_seen >= coverage.nearest_seen && coverage.nearest_seen >= 0
                    ? static_cast<u32>(coverage.farthest_seen - coverage.nearest_seen + 1) - coverage.ranges_seen : 0u;
                if (between > 0) {
                    bullet_text(COLOR_WARNING, std::format("{} distance range{} in between {} missing (the empty cells above).",
                        between, between == 1 ? "" : "s", between == 1 ? "is" : "are"));
                    complete = false;
                }
            }
            if (!analysis.sweep.active
                && (coverage.directions_seen * 2 < ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS || !coverage.seen_from_above || !coverage.seen_from_below)) {
                bullet_text(COLOR_WARNING, std::format("Orbit the mesh: seen from {} of {} directions{}. Back sides and undersides often "
                    "have a different triangle density.", coverage.directions_seen, ANALYSIS_YAW_BINS * ANALYSIS_PITCH_BINS,
                    !coverage.seen_from_above && !coverage.seen_from_below ? ", never from above or below"
                        : (!coverage.seen_from_above ? ", never from above" : (!coverage.seen_from_below ? ", never from below" : ""))));
                complete = false;
            }
            if (complete)
                bullet_text(COLOR_GOOD, "Near, far and all around: the numbers describe the whole mesh, not one view.");

            // ---- actions ----
            ImGui::Dummy(ImVec2(0.f, 4.f));
            const f32 half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (UI::accent_button("Collect Data", ImVec2(half, 0.f)))
                start_analysis_sweep();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Zooms the preview camera smoothly from close up to far away with auto rotate on, collecting samples\n"
                    "on the way, then puts the camera back. A virtual geometry mesh with fallback levels is flown back in\n"
                    "drawn with those levels (what discrete LODs would draw), which measures both systems in one go");
            ImGui::SameLine();
            if (UI::subtle_button("Reset data", ImVec2(half, 0.f)))
                reset_analysis(true);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Forget the collected samples. They are also dropped on their own when the data they describe changes:\n"
                    "rebuilt clusters, regenerated LOD levels, a resized preview");

            ImGui::EndDisabled();
            if (measuring)
                draw_analysis_sweep_overlay(content_min, ImVec2(content_min.x + content_width, ImGui::GetCursorScreenPos().y));
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_analysis_sweep_overlay(const ImVec2& card_min, const ImVec2& card_max) {

        const analysis_sweep& sweep = m_analysis.sweep;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const f32 bottom = std::max(card_min.y, card_max.y - ImGui::GetStyle().ItemSpacing.y);

        ImVec4 veil = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
        veil.w = 0.6f;
        draw_list->AddRectFilled(ImVec2(card_min.x - 4.f, card_min.y - 4.f), ImVec2(card_max.x + 4.f, bottom + 4.f), ImGui::GetColorU32(veil), 6.f);

        // Centre of the visible part of the card (the panel may have scrolled it partly out of view)
        const ImRect clip = ImGui::GetCurrentWindow()->ClipRect;
        const f32 visible_top = std::max(card_min.y, clip.Min.y);
        const f32 visible_bottom = std::min(bottom, clip.Max.y);
        const f32 centre_x = 0.5f * (card_min.x + card_max.x);
        const f32 centre_y = visible_bottom > visible_top ? 0.5f * (visible_top + visible_bottom) : 0.5f * (card_min.y + bottom);

        const std::string status = std::format("{} {:.0f}%", sweep.pass == 0 ? (sweep.pass_count > 1 ? "Measuring, flying out..." : "Measuring...")
            : "Fallback levels, flying back in...", analysis_sweep_progress() * 100.0f);
        const f32 line_height = ImGui::GetTextLineHeightWithSpacing();
        const ImVec2 button_size(std::min(160.0f, card_max.x - card_min.x - 20.0f), ImGui::GetFrameHeight() * 1.3f);
        const f32 bar_width = button_size.x;
        const f32 block_height = line_height + 6.0f + 4.0f + 6.0f + button_size.y;
        const f32 top = centre_y - 0.5f * block_height;

        // panel behind the block, so it reads over the cells and text of the card
        const ImVec2 panel_min(centre_x - 0.5f * bar_width - 14.0f, top - 10.0f);
        const ImVec2 panel_max(centre_x + 0.5f * bar_width + 14.0f, top + block_height + 10.0f);
        draw_list->AddRectFilled(panel_min, panel_max, ImGui::GetColorU32(ImGuiCol_PopupBg), 8.f);
        draw_list->AddRect(panel_min, panel_max, ImGui::GetColorU32(ImGuiCol_Border), 8.f);

        const ImVec2 text_size = ImGui::CalcTextSize(status.c_str());
        draw_list->AddText(ImVec2(centre_x - 0.5f * text_size.x, top), ImGui::GetColorU32(ImGuiCol_Text), status.c_str());

        const ImVec2 bar_min(centre_x - 0.5f * bar_width, top + line_height + 4.0f);
        draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width, bar_min.y + 4.0f), ImGui::GetColorU32(ImGuiCol_FrameBg), 2.f);
        draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width * analysis_sweep_progress(), bar_min.y + 4.0f),
            ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 2.f);

        ImGui::SetCursorScreenPos(ImVec2(centre_x - 0.5f * button_size.x, bar_min.y + 4.0f + 6.0f));
        if (UI::accent_button("Stop##analysis_sweep", button_size))
            stop_analysis_sweep();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("End the measurement and put the camera back. What was collected so far is kept");

        ImGui::SetCursorScreenPos(cursor);          // the overlay takes no space in the layout
        ImGui::Dummy(ImVec2(0.f, 0.f));
    }


    void mesh_editor_window::draw_analysis_charts_section(const GLT::asset::mesh::mesh_asset& mesh) {

        const analysis_state& analysis = m_analysis;
        const analysis_curves& curves = analysis.curves;
        const bool estimated = !mesh.has_clusters() && analysis.has_estimate;

        if (UI::begin_card("Over Distance", UI::card_options{ .description = "Lines: what each system selects. Dots: measured in the preview, after culling",
            .collapsible = true })) {

            if (!ImPlot::GetCurrentContext() || curves.data_key == 0) {
                dim_text("Graphs are not available.");
                UI::end_card();
                return;
            }

            const mesh_sphere sphere = sphere_of(mesh);
            const GLT::world::camera_snapshot camera = build_camera_snapshot();
            const f32 projection = projection_scale(camera.fov);
            const f32 view_distance = std::max(glm::distance(camera.position, sphere.centre), 1e-4f);

            // ---- x axis: distance, log scaled, the ranges from near to far ----
            const f64 x_min = static_cast<f64>(curves.distance.front());
            const f64 x_max = std::max(static_cast<f64>(std::min(curves.distance.back(), MAX_CAM_DISTANCE * 2.0f)), x_min * 2.0);

            // ---- measured means per range ----
            struct measured_series {

                std::array<f32, ANALYSIS_RANGES>    distance{}, triangles{}, draws{}, geometry_ms{}, timed_distance{};
                int                                 count = 0;
                int                                 timed_count = 0;
            };
            std::array<measured_series, series_count> measured{};
            f32 max_ms = 0.0f;
            for (u32 id = 0; id < series_count; ++id)
                for (u32 range = 0; range < ANALYSIS_RANGES; ++range) {

                    const analysis_range& bucket = analysis.series[id].ranges[range];
                    if (bucket.samples == 0)
                        continue;
                    measured_series& out = measured[id];
                    const f32 centre = analysis_range_distance(mesh, static_cast<f32>(range) + 0.5f);
                    out.distance[static_cast<size_t>(out.count)] = centre;
                    out.triangles[static_cast<size_t>(out.count)] = std::max(1.0f, static_cast<f32>(bucket.triangles / bucket.samples));
                    out.draws[static_cast<size_t>(out.count)] = std::max(1.0f, static_cast<f32>(bucket.draws / bucket.samples));
                    out.count++;
                    if (bucket.timed_samples >= 3) {
                        out.timed_distance[static_cast<size_t>(out.timed_count)] = centre;
                        out.geometry_ms[static_cast<size_t>(out.timed_count)] = static_cast<f32>(bucket.geometry_ms / bucket.timed_samples);
                        max_ms = std::max(max_ms, out.geometry_ms[static_cast<size_t>(out.timed_count)]);
                        out.timed_count++;
                    }
                }

            // ---- computed lines, clamped for the log scale ----
            std::array<f32, ANALYSIS_CURVE_POINTS> lod_triangles{}, cluster_triangles{}, lod_draws{}, cluster_draws{};
            f32 max_triangles = 1.0f, min_triangles = 1e9f, max_draws = 1.0f;
            for (u32 point = 0; point < ANALYSIS_CURVE_POINTS; ++point) {
                lod_triangles[point] = std::max(curves.lod_triangles[point], 1.0f);
                lod_draws[point] = std::max(curves.lod_draws[point], 1.0f);
                cluster_triangles[point] = std::max(curves.cluster_triangles[point], 1.0f);
                cluster_draws[point] = std::max(curves.cluster_draws[point], 1.0f);
                const bool cluster_point = curves.has_clusters && point < curves.filled;
                max_triangles = std::max({ max_triangles, lod_triangles[point], cluster_point ? cluster_triangles[point] : 1.0f });
                min_triangles = std::min({ min_triangles, lod_triangles[point], cluster_point ? cluster_triangles[point] : 1e9f });
                max_draws = std::max({ max_draws, lod_draws[point], cluster_point ? cluster_draws[point] : 1.0f });
            }
            for (const measured_series& series : measured)
                for (int i = 0; i < series.count; ++i) {
                    max_triangles = std::max(max_triangles, series.triangles[static_cast<size_t>(i)]);
                    min_triangles = std::min(min_triangles, series.triangles[static_cast<size_t>(i)]);
                    max_draws = std::max(max_draws, series.draws[static_cast<size_t>(i)]);
                }

            const char* cluster_label = estimated ? "Virtual geometry (estimate)" : "Virtual geometry";
            const ImPlotFlags plot_flags = ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoTitle;
            const ImPlotAxisFlags axis_flags = ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoMenus;
            const ImPlotSpec view_line(ImPlotProp_LineColor, ImVec4(1.0f, 1.0f, 1.0f, 0.45f), ImPlotProp_LineWeight, 1.0f,
                ImPlotProp_Flags, static_cast<ImPlotItemFlags>(ImPlotItemFlags_NoLegend));
            const auto dots = [](const ImVec4& color) {
                return ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle, ImPlotProp_MarkerSize, 3.0f, ImPlotProp_MarkerFillColor, color,
                    ImPlotProp_MarkerLineColor, color, ImPlotProp_Flags, static_cast<ImPlotItemFlags>(ImPlotItemFlags_NoLegend));
            };

            // Numbers of the range under the mouse, the same for every graph
            const auto range_tooltip = [&](const f64 at_distance) {

                const u32 range = analysis_range_of(sphere.radius * projection / static_cast<f32>(std::max(at_distance, 1e-4)));
                const u32 point = range * 2 + 1;
                ImGui::BeginTooltip();
                ImGui::Text("Distance %.2f - %.2f", analysis_range_distance(mesh, static_cast<f32>(range)), analysis_range_distance(mesh, static_cast<f32>(range + 1)));
                ImGui::TextDisabled("mesh %.0f%% - %.0f%% of the view height", analysis_range_screen(static_cast<f32>(range)) * 100.0f,
                    analysis_range_screen(static_cast<f32>(range + 1)) * 100.0f);

                const auto row = [&](const u32 id, const char* label, const ImVec4& color, const f32 triangles, const f32 draws, const bool has_line) {

                    const analysis_range& bucket = analysis.series[id].ranges[range];
                    ImGui::Separator();
                    ImGui::TextColored(color, "%s", label);
                    if (has_line)
                        ImGui::Text("selects %s triangles, %s draw commands", count_text(static_cast<u64>(triangles)).c_str(),
                            count_text(static_cast<u64>(draws)).c_str());
                    if (bucket.samples == 0)
                        ImGui::TextDisabled("not measured here");
                    else {
                        ImGui::Text("drawn %s triangles, %s draw commands", count_text(static_cast<u64>(bucket.triangles / bucket.samples)).c_str(),
                            count_text(static_cast<u64>(bucket.draws / bucket.samples)).c_str());
                        if (bucket.timed_samples > 0)
                            ImGui::Text("%.3f ms geometry, %.3f ms frame", bucket.geometry_ms / bucket.timed_samples, bucket.gpu_ms / bucket.timed_samples);
                        ImGui::TextDisabled("%u samples", bucket.samples);
                    }
                };
                row(series_lods, "Discrete LODs", COLOR_LODS, curves.lod_triangles[point], curves.lod_draws[point], true);
                row(series_virtual_geometry, cluster_label, COLOR_CLUSTERS, curves.cluster_triangles[point], curves.cluster_draws[point], curves.has_clusters && point < curves.filled);
                ImGui::EndTooltip();
            };

            // ---- legend (shared by the graphs) ----
            series_label(COLOR_LODS, "Discrete LODs");
            if (curves.has_clusters || measured[series_virtual_geometry].count > 0) {
                ImGui::SameLine(0.f, 14.f);
                series_label(COLOR_CLUSTERS, cluster_label);
            }

            // ---- triangles ----
            ImGui::TextDisabled("Triangles");
            if (ImPlot::BeginPlot("##analysis_triangles", ImVec2(-1.0f, CHART_HEIGHT), plot_flags | ImPlotFlags_NoLegend)) {

                ImPlot::SetupAxes(nullptr, nullptr, axis_flags, axis_flags);
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::SetupAxisFormat(ImAxis_Y1, count_axis_format);
                ImPlot::SetupAxesLimits(x_min, x_max, std::max(1.0, static_cast<f64>(min_triangles) * 0.5), static_cast<f64>(max_triangles) * 2.0, ImPlotCond_Always);

                ImPlot::PlotInfLines("##view", &view_distance, 1, view_line);
                ImPlot::PlotStairs("Discrete LODs", curves.distance.data(), lod_triangles.data(), static_cast<int>(ANALYSIS_CURVE_POINTS),
                    ImPlotSpec(ImPlotProp_LineColor, COLOR_LODS, ImPlotProp_LineWeight, 1.5f));
                if (curves.has_clusters)
                    ImPlot::PlotLine(cluster_label, curves.distance.data(), cluster_triangles.data(), static_cast<int>(curves.filled),
                        ImPlotSpec(ImPlotProp_LineColor, COLOR_CLUSTERS, ImPlotProp_LineWeight, 1.5f));
                if (measured[series_lods].count > 0)
                    ImPlot::PlotScatter("##measured_lods", measured[series_lods].distance.data(), measured[series_lods].triangles.data(),
                        measured[series_lods].count, dots(COLOR_LODS));
                if (measured[series_virtual_geometry].count > 0)
                    ImPlot::PlotScatter("##measured_clusters", measured[series_virtual_geometry].distance.data(),
                        measured[series_virtual_geometry].triangles.data(), measured[series_virtual_geometry].count, dots(COLOR_CLUSTERS));

                if (ImPlot::IsPlotHovered())
                    range_tooltip(ImPlot::GetPlotMousePos().x);
                ImPlot::EndPlot();
            }

            // ---- draw commands ----
            ImGui::TextDisabled("Draw commands");
            if (ImPlot::BeginPlot("##analysis_draws", ImVec2(-1.0f, CHART_HEIGHT), plot_flags | ImPlotFlags_NoLegend)) {

                ImPlot::SetupAxes(nullptr, nullptr, axis_flags, axis_flags);
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::SetupAxisFormat(ImAxis_Y1, count_axis_format);
                ImPlot::SetupAxesLimits(x_min, x_max, 0.7, std::max(static_cast<f64>(max_draws) * 2.0, 10.0), ImPlotCond_Always);

                ImPlot::PlotInfLines("##view", &view_distance, 1, view_line);
                ImPlot::PlotStairs("Discrete LODs", curves.distance.data(), lod_draws.data(), static_cast<int>(ANALYSIS_CURVE_POINTS),
                    ImPlotSpec(ImPlotProp_LineColor, COLOR_LODS, ImPlotProp_LineWeight, 1.5f));
                if (curves.has_clusters)
                    ImPlot::PlotLine(cluster_label, curves.distance.data(), cluster_draws.data(), static_cast<int>(curves.filled),
                        ImPlotSpec(ImPlotProp_LineColor, COLOR_CLUSTERS, ImPlotProp_LineWeight, 1.5f));
                if (measured[series_lods].count > 0)
                    ImPlot::PlotScatter("##measured_lods", measured[series_lods].distance.data(), measured[series_lods].draws.data(),
                        measured[series_lods].count, dots(COLOR_LODS));
                if (measured[series_virtual_geometry].count > 0)
                    ImPlot::PlotScatter("##measured_clusters", measured[series_virtual_geometry].distance.data(),
                        measured[series_virtual_geometry].draws.data(), measured[series_virtual_geometry].count, dots(COLOR_CLUSTERS));

                if (ImPlot::IsPlotHovered())
                    range_tooltip(ImPlot::GetPlotMousePos().x);
                ImPlot::EndPlot();
            }

            // ---- GPU time (measured only) ----
            ImGui::TextDisabled("GPU time of the geometry passes (ms)");
            if (measured[series_lods].timed_count + measured[series_virtual_geometry].timed_count == 0)
                dim_text(analysis.live.serial != 0 && analysis.live.gpu_time_ms <= 0.0f
                    ? "This device reports no GPU timings. Triangles and draw commands are still measured."
                    : "No timings yet. Look at the mesh from different distances, or press 'Collect Data' above.");
            else if (ImPlot::BeginPlot("##analysis_time", ImVec2(-1.0f, CHART_HEIGHT), plot_flags | ImPlotFlags_NoLegend)) {

                ImPlot::SetupAxes(nullptr, nullptr, axis_flags, axis_flags);
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                ImPlot::SetupAxisFormat(ImAxis_Y1, "%.2f");
                ImPlot::SetupAxesLimits(x_min, x_max, 0.0, std::max(static_cast<f64>(max_ms) * 1.25, 0.01), ImPlotCond_Always);

                ImPlot::PlotInfLines("##view", &view_distance, 1, view_line);
                const auto timed = [](const char* label, const measured_series& series, const ImVec4& color) {
                    if (series.timed_count > 0)
                        ImPlot::PlotLine(label, series.timed_distance.data(), series.geometry_ms.data(), series.timed_count,
                            ImPlotSpec(ImPlotProp_LineColor, color, ImPlotProp_LineWeight, 1.5f, ImPlotProp_Marker, ImPlotMarker_Circle,
                                ImPlotProp_MarkerSize, 3.0f, ImPlotProp_MarkerFillColor, color, ImPlotProp_MarkerLineColor, color));
                };
                timed("Discrete LODs", measured[series_lods], COLOR_LODS);
                timed(cluster_label, measured[series_virtual_geometry], COLOR_CLUSTERS);

                if (ImPlot::IsPlotHovered())
                    range_tooltip(ImPlot::GetPlotMousePos().x);
                ImPlot::EndPlot();
            }

            dim_text("X axis: distance to the mesh, log scaled. The vertical line is the preview camera. Hover a graph for the numbers.");
        }
        UI::end_card();
    }


    void mesh_editor_window::draw_analysis_memory_section(const GLT::asset::mesh::mesh_asset& mesh) {

        using namespace GLT::asset::mesh;
        const bool virtual_mode = mesh.detail() == detail_mode::virtual_geometry;
        const bool estimated = !mesh.has_clusters() && m_analysis.has_estimate;

        const u64 source = mesh.vertices.size() * sizeof(vertex) + mesh.indices.size() * sizeof(u32);
        const u64 levels = mesh.lod_vertices.size() * sizeof(vertex) + mesh.lod_indices.size() * sizeof(u32)
            + mesh.lods.size() * sizeof(lod_level) + mesh.lod_ranges.size() * sizeof(lod_range);
        const bool have_clusters = mesh.has_clusters() || estimated;
        const u64 clusters = mesh.has_clusters()
            ? cluster_bytes(mesh.clusters.size(), mesh.cluster_indices.size(), mesh.cluster_vertices.size(), mesh.cluster_submeshes.size())
            : (estimated ? cluster_bytes(m_analysis.estimate.clusters.size(), m_analysis.estimate.indices.size(),
                m_analysis.estimate.vertices.size(), m_analysis.estimate.submeshes.size()) : 0ull);

        const u64 as_lods = source + levels;
        const u64 as_clusters = source + levels + clusters;         // the levels stay as the fallback
        const std::string summary = GLT::util::format_bytes(virtual_mode ? as_clusters : as_lods);

        if (UI::begin_card("Memory", UI::card_options{ .description = "Geometry data of this mesh. The GPU holds the same again",
            .summary = summary.c_str(), .collapsible = true })) {

            const auto percent = [&](const u64 bytes) { return 100.0 * static_cast<f64>(bytes) / static_cast<f64>(std::max<u64>(source, 1)); };

            UI::property_value("Source mesh", GLT::util::format_bytes(source), "Vertices and indices at full detail");
            UI::property_value(virtual_mode ? "Fallback levels" : "LOD levels", std::format("{} (+{:.0f}%)", GLT::util::format_bytes(levels), percent(levels)),
                "Index data of the simplified levels and the vertices their normals method added");
            if (have_clusters)
                UI::property_value(estimated ? "Clusters (estimate)" : "Clusters", std::format("{} (+{:.0f}%)", GLT::util::format_bytes(clusters), percent(clusters)),
                    "Cluster records, the index data of every hierarchy level and the vertices the normals method added");
            else
                UI::property_value("Clusters", "not estimated", "Press 'Estimate virtual geometry' in the recommendation above");

            // ---- the two totals as bars ----
            ImGui::Dummy(ImVec2(0.f, 4.f));
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const u64 largest = std::max<u64>(have_clusters ? as_clusters : as_lods, 1);
            const auto bar = [&](const char* label, const u64 bytes, const ImVec4& color, const bool active, const bool known) {

                const std::string text = known ? GLT::util::format_bytes(bytes) : std::string("?");
                const f32 height = ImGui::GetTextLineHeight();
                const f32 width = ImGui::GetContentRegionAvail().x;
                const f32 label_width = ImGui::CalcTextSize("Virtual geometry").x + 10.f;
                const f32 value_width = ImGui::CalcTextSize("000.00 MB").x + 8.f;
                const ImVec2 origin = ImGui::GetCursorScreenPos();

                if (active)
                    ImGui::TextUnformatted(label);
                else
                    ImGui::TextDisabled("%s", label);

                const f32 track = std::max(width - label_width - value_width, 8.f);
                const ImVec2 min(origin.x + label_width, origin.y + 3.f);
                const ImVec2 max(min.x + track, origin.y + height - 3.f);
                draw_list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 2.f);
                if (known)
                    draw_list->AddRectFilled(min, ImVec2(min.x + track * static_cast<f32>(static_cast<f64>(bytes) / static_cast<f64>(largest)), max.y),
                        ImGui::GetColorU32(color), 2.f);
                draw_list->AddText(ImVec2(max.x + 8.f, origin.y), ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), text.c_str());
            };
            bar("Discrete LODs", as_lods, COLOR_LODS, !virtual_mode, true);
            bar("Virtual geometry", as_clusters, COLOR_CLUSTERS, virtual_mode, have_clusters);

            if (have_clusters && as_lods > 0) {
                ImGui::Dummy(ImVec2(0.f, 2.f));
                dim_text(std::format("Virtual geometry needs {:.0f}% more memory for this mesh: it keeps the levels as its fallback and "
                    "adds the cluster hierarchy.", 100.0 * static_cast<f64>(clusters) / static_cast<f64>(as_lods)).c_str());
            }
        }
        UI::end_card();
    }

}
