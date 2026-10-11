
#include "util/pch.h"
#include "world_topdown_editor.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <config/imgui_config.h>
#include <asset/i_asset_registry.h>
#include <asset/mesh.h>
#include <plugin_system/plugin_manager.h>
#include <world/i_world.h>
#include <world/i_world_scene.h>

#include "util/ui/pannel_collection.h"
#include "resource_manager/icon_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                           SIDEBAR_WIDTH = 260.0f;

    constexpr f32                           MIN_PPM = 0.01f;

    constexpr f32                           MAX_PPM = 64.0f;

    constexpr f32                           ZOOM_STEP = 1.15f;

    constexpr int                           CELL_SIZE_MIN = 64;

    constexpr int                           CELL_SIZE_MAX = 4096;

    constexpr f32                           DOT_RADIUS = 3.0f;

    constexpr f32                           DOT_HOVER_RADIUS = 5.0f;

    // Floating button strip (top-right overlay)
    constexpr f32                           FLOATING_BUTTON_SIZE = 32.0f;

    constexpr f32                           FLOATING_BUTTON_GAP = 6.0f;

    constexpr f32                           FLOATING_BUTTON_EDGE_PAD = 10.0f;

    constexpr f32                           FLOATING_STRIP_PAD = 6.0f;

    constexpr ImU32                         BG_COLOR = IM_COL32( 24,  24,  28, 255);

    constexpr ImU32                         GRID_COLOR = IM_COL32( 90,  90, 100, 140);

    constexpr ImU32                         GRID_EDGE_COLOR = IM_COL32(140, 140, 160, 200);

    constexpr ImU32                         AXIS_X_COLOR = IM_COL32(200,  90,  90, 180);

    constexpr ImU32                         AXIS_Z_COLOR = IM_COL32( 80, 130, 200, 180);

    constexpr ImU32                         ENTITY_DOT_COLOR = IM_COL32(220, 220, 230, 255);

    constexpr ImU32                         OCCUPIED_CELL_FILL = IM_COL32(100, 160, 240,  40);

    constexpr ImU32                         OCCUPIED_CELL_BORDER = IM_COL32(100, 160, 240, 170);

    constexpr ImU32                         STREAMING_RADIUS_FILL = IM_COL32(120, 200, 120,  28);

    constexpr ImU32                         STREAMING_RADIUS_LINE = IM_COL32(120, 200, 120, 190);

    constexpr ImU32                         STREAMING_ANCHOR_COLOR = IM_COL32(190, 240, 190, 235);

    constexpr ImU32                         HUD_TEXT_COLOR = IM_COL32(180, 180, 190, 255);

    constexpr ImU32                         FLOATING_STRIP_BG = IM_COL32( 18,  18,  22, 140);

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Dashed line, since ImGui has no built-in for one. Length units are pixels; caller chooses dash/gap.
    void add_dashed_line(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, f32 thickness = 1.0f, f32 dash = 5.0f, f32 gap = 5.0f);


    // The world plugin currently exposes entity transforms through i_world_scene::gather_scene(), which
    // only walks entities that own a mesh component. That's a reasonable starting subset for a top-down
    // view - if you want literally every transform-bearing entity, i_world_scene would need a new
    // method (e.g. gather_transforms). Keeping the lookup here so it's the only place to change when
    // the interface grows.
    GLT::ref<GLT::world::i_world_scene> get_world_scene();

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void add_dashed_line(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, f32 thickness, f32 dash, f32 gap) {

        const f32 dx = b.x - a.x;
        const f32 dy = b.y - a.y;
        const f32 length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-4f)
            return;

        const f32 ux = dx / length;
        const f32 uy = dy / length;
        const f32 step = dash + gap;

        for (f32 t = 0.0f; t < length; t += step) {

            const f32 e = std::min(t + dash, length);
            draw->AddLine({ a.x + ux * t, a.y + uy * t }, { a.x + ux * e, a.y + uy * e }, color, thickness);
        }
    }


    GLT::ref<GLT::world::i_world_scene> get_world_scene() {

        return GLT::plugin_manager::get_plugin_ref<GLT::world::i_world_scene>(GLT::plugin_manager::interface::custom);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    world_topdown_editor::world_topdown_editor(const std::filesystem::path& path)
        : m_asset_path(path) {

        cache_window_state();
        make_window_name((std::string("WORLD: ") + path.stem().string()).c_str());

        auto registry = GLT::asset::registry::get_ref();
        if (!registry) {
            m_load_failed = true;
            return;
        }

        // load() hands back a raw handle with refcount 0; wrapping it in asset_ref takes ownership of one
        // reference so the world stays resident for as long as this editor is alive. On destruction the
        // asset_ref releases that reference and the registry is free to unload the world.
        if (auto loaded = registry->load(path); loaded)
            m_world = GLT::asset::asset_ref{ *loaded };
        else
            m_load_failed = true;
    }

    // CLASS PUBLIC ====================================================================================================

    void world_topdown_editor::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();

        ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 420.0f), ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));
        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            GLT::UI::custom_frame(SIDEBAR_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()), 
                [this] { draw_sidebar(); },
                [this] { draw_view(); });
        }
        ImGui::End();
    }


    void world_topdown_editor::update(const f32 /*delta_time*/) {

        m_entities.clear();

        if (m_load_failed || !m_world.valid())
            return;

        auto world = GLT::world::manager::get_ref();
        if (!world)
            return;

        // Only show markers when the active world is the one this editor owns. Otherwise we'd display a completely different world's contents
        if (world->world_handle() != m_world.get())
            return;

        auto* scene = world->as<GLT::world::i_world_scene>();
        if (!scene)
            return;

        std::vector<GLT::world::entity_position> positions;
        scene->gather_entity_positions(positions);

        m_entities.reserve(positions.size());
        for (const auto& p : positions)
            m_entities.push_back(editor_entity{ { p.world_position.x, p.world_position.z }, p.id.index, p.id.generation });
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void world_topdown_editor::refresh_entities() {

        m_entities.clear();

        if (m_load_failed || !m_world.valid())
            return;

        auto scene = get_world_scene();
        if (!scene)
            return;

        std::vector<GLT::asset::mesh::instance> instances;
        scene->gather_scene(instances);

        m_entities.reserve(instances.size());
        for (const auto& inst : instances) {

            // inst.transform is a world-space mat4; column 3 is translation. Top-down means we drop Y.
            const glm::vec3 pos = glm::vec3(inst.transform[3]);
            m_entities.push_back(editor_entity{ .xz = { pos.x, pos.z } });
        }
    }


    void world_topdown_editor::draw_sidebar() {

        // --- fetch current world state once after open / after apply ----------------------
        auto world = GLT::world::manager::get_ref();

        if (m_sync_sliders_pending && world && world->world_handle() == m_world.get()) {
            const glm::vec3 sz = world->get_region_cell_size();
            m_cell_x = static_cast<int>(std::round(sz.x));
            m_cell_y = static_cast<int>(std::round(sz.y));
            m_cell_z = static_cast<int>(std::round(sz.z));
            m_cell_size_from_world = sz;
            m_sync_sliders_pending = false;
        }

        GLT::UI::big_text("Region scale (m)");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (GLT::UI::begin_table("##region_scale_table", false)) {
            GLT::UI::table_row_slider_int("cell X", m_cell_x, CELL_SIZE_MIN, CELL_SIZE_MAX);
            GLT::UI::table_row_slider_int("cell Y", m_cell_y, CELL_SIZE_MIN, CELL_SIZE_MAX);
            GLT::UI::table_row_slider_int("cell Z", m_cell_z, CELL_SIZE_MIN, CELL_SIZE_MAX);
            GLT::UI::end_table();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Button("Presets: 64##x")) { m_cell_x = m_cell_y = m_cell_z = 64; } ImGui::SameLine();
        if (ImGui::Button("256")) { m_cell_x = m_cell_y = m_cell_z = 256;  } ImGui::SameLine();
        if (ImGui::Button("1024")) { m_cell_x = m_cell_y = m_cell_z = 1024; } ImGui::SameLine();
        if (ImGui::Button("4096")) { m_cell_x = m_cell_y = m_cell_z = 4096; }

        const bool dirty =
            m_cell_x != static_cast<int>(std::round(m_cell_size_from_world.x)) ||
            m_cell_y != static_cast<int>(std::round(m_cell_size_from_world.y)) ||
            m_cell_z != static_cast<int>(std::round(m_cell_size_from_world.z));

        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        ImGui::BeginDisabled(!world || world->world_handle() != m_world.get());

        if (ImGui::Button("Apply to world", ImVec2(-1.0f, 0.0f)) && dirty) {

            world->set_region_cell_size(glm::vec3{static_cast<f32>(m_cell_x), static_cast<f32>(m_cell_y), static_cast<f32>(m_cell_z) });
            m_cell_size_from_world = glm::vec3{ m_cell_x, m_cell_y, m_cell_z };
            m_last_action = "applied cell size";
        }

        if (ImGui::Button("Reassign entities", ImVec2(-1.0f, 0.0f))) {
            world->reassign_all_entities();
            m_last_action = "reassigned entities";
        }

        if (ImGui::Button("Save world", ImVec2(-1.0f, 0.0f))) {
            if (auto r = world->save_world(); !r)
                m_last_action = "save failed";
            else
                m_last_action = "saved";
        }

        ImGui::EndDisabled();

        if (!m_last_action.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled("%s", m_last_action.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        ImGui::Checkbox("Show grid", &m_show_grid);
        ImGui::Checkbox("Show axes", &m_show_axes);
        ImGui::Checkbox("Show entity dots", &m_show_entity_dots);
        ImGui::Checkbox("Highlight occupied cells", &m_show_occupied_cells);
        ImGui::Checkbox("Show streaming radius", &m_show_streaming_radius);

        // NOTE: "Fit to entities" moved out of the sidebar. See draw_floating_buttons().

        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        if (m_load_failed) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to load world.");
            ImGui::PopStyleColor();
            return;
        }

        ImGui::TextDisabled("Entities: %zu", m_entities.size());
        ImGui::TextDisabled("World grid: %.0f x %.0f x %.0f",
            m_cell_size_from_world.x, m_cell_size_from_world.y, m_cell_size_from_world.z);
    }


    void world_topdown_editor::draw_view() {

        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        const ImVec2 canvas_max = { canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y };
        const ImVec2 canvas_center = { (canvas_min.x + canvas_max.x) * 0.5f,
                                       (canvas_min.y + canvas_max.y) * 0.5f };

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(canvas_min, canvas_max, BG_COLOR);

        // Reserve the canvas for interaction on all three primary mouse buttons. SetNextItemAllowOverlap()
        // lets the floating buttons (submitted later, on top) win hover/clicks where they overlap.
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##world_view", canvas_size,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        // ---- input: zoom ---------------------------------------------------------------------------------
        if (hovered) {

            const f32 wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {

                const f32 before = m_pixels_per_meter;
                m_pixels_per_meter = std::clamp(m_pixels_per_meter * std::pow(ZOOM_STEP, wheel), MIN_PPM, MAX_PPM);

                // zoom toward the cursor so the point under it stays put
                const glm::vec2 world_before{
                    m_view_center.x + (mouse.x - canvas_center.x) / before,
                    m_view_center.y + (mouse.y - canvas_center.y) / before,
                };
                const glm::vec2 world_after{
                    m_view_center.x + (mouse.x - canvas_center.x) / m_pixels_per_meter,
                    m_view_center.y + (mouse.y - canvas_center.y) / m_pixels_per_meter,
                };
                m_view_center += world_before - world_after;
            }
        }

        // ---- input: pan ----------------------------------------------------------------------------------
        // Middle mouse OR right mouse drags the view. Right mouse is the "primary" pan for users
        // coming from a 3D viewport, middle mouse is the classic fallback.
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                       ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {

            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            m_view_center.x -= delta.x / m_pixels_per_meter;
            m_view_center.y -= delta.y / m_pixels_per_meter;
        }

        // ---- deferred fit --------------------------------------------------------------------------------
        if (m_fit_pending) {
            fit_to_entities(canvas_size);
            m_fit_pending = false;
        }

        // world -> screen helper (X/Z plane, +Z points down on screen)
        auto w2s = [&](const glm::vec2& w) -> ImVec2 {
            return ImVec2(
                canvas_center.x + (w.x - m_view_center.x) * m_pixels_per_meter,
                canvas_center.y + (w.y - m_view_center.y) * m_pixels_per_meter);
        };

        draw->PushClipRect(canvas_min, canvas_max, true);

        // ---- visible world AABB --------------------------------------------------------------------------
        const glm::vec2 world_min{
            m_view_center.x - canvas_size.x * 0.5f / m_pixels_per_meter,
            m_view_center.y - canvas_size.y * 0.5f / m_pixels_per_meter,
        };
        const glm::vec2 world_max{
            m_view_center.x + canvas_size.x * 0.5f / m_pixels_per_meter,
            m_view_center.y + canvas_size.y * 0.5f / m_pixels_per_meter,
        };

        // ---- dashed grid at cell size --------------------------------------------------------------------
        if (m_show_grid && m_cell_x > 0 && m_cell_z > 0) {

            const f32 cx = static_cast<f32>(m_cell_x);
            const f32 cz = static_cast<f32>(m_cell_z);

            const f32 half_x = cx * 0.5f;
            const f32 half_z = cz * 0.5f;

            // cell k spans [k*size - size/2, k*size + size/2]
            // boundary positions are at k*size - size/2
            const int first_col = static_cast<int>(std::floor((world_min.x + half_x) / cx));
            const int last_col  = static_cast<int>(std::ceil ((world_max.x + half_x) / cx));
            for (int col = first_col; col <= last_col; ++col) {

                const f32 wx = static_cast<f32>(col) * cx - half_x;
                const f32 sx = canvas_center.x + (wx - m_view_center.x) * m_pixels_per_meter;

                add_dashed_line(draw, { sx, canvas_min.y }, { sx, canvas_max.y }, GRID_COLOR, 1.0f, 4.0f, 4.0f);
            }

            const int first_row = static_cast<int>(std::floor((world_min.y + half_z) / cz));
            const int last_row  = static_cast<int>(std::ceil ((world_max.y + half_z) / cz));
            for (int row = first_row; row <= last_row; ++row) {

                const f32 wz = static_cast<f32>(row) * cz - half_z;
                const f32 sy = canvas_center.y + (wz - m_view_center.y) * m_pixels_per_meter;

                add_dashed_line(draw, { canvas_min.x, sy }, { canvas_max.x, sy }, GRID_COLOR, 1.0f, 4.0f, 4.0f);
            }
        }

        // ---- streaming radius ----------------------------------------------------------------------------
        // Only meaningful when THIS editor's world is the one currently bound to the world plugin;
        // otherwise we'd render some other world's anchor.
        if (m_show_streaming_radius && !m_load_failed && m_world.valid()) {

            auto world = GLT::world::manager::get_ref();
            if (world && world->world_handle() == m_world.get()) {

                const glm::vec3 anchor = world->get_streaming_anchor();
                const f32 streaming_radius = world->get_streaming_radius();

                const ImVec2 anchor_screen = w2s({ anchor.x, anchor.z });

                if (streaming_radius > 0.0f) {

                    const f32 radius_pixels = streaming_radius * m_pixels_per_meter;

                    // Filled disc first (subtle tint), then the outline on top so the edge reads clearly.
                    draw->AddCircleFilled(anchor_screen, radius_pixels, STREAMING_RADIUS_FILL, 96);
                    draw->AddCircle(anchor_screen, radius_pixels, STREAMING_RADIUS_LINE, 96, 1.5f);
                }

                // Anchor marker: a small filled dot with a thin outer ring so it stays visible
                // against both the grid and the streaming disc.
                draw->AddCircleFilled(anchor_screen, 4.0f, STREAMING_ANCHOR_COLOR);
                draw->AddCircle(anchor_screen, 7.0f, STREAMING_ANCHOR_COLOR, 0, 1.5f);
            }
        }

        // ---- axes ----------------------------------------------------------------------------------------
        if (m_show_axes) {

            const f32 axis_x_screen = canvas_center.x + (0.0f - m_view_center.x) * m_pixels_per_meter;
            const f32 axis_y_screen = canvas_center.y + (0.0f - m_view_center.y) * m_pixels_per_meter;

            if (axis_x_screen >= canvas_min.x && axis_x_screen <= canvas_max.x)
                draw->AddLine({ axis_x_screen, canvas_min.y }, { axis_x_screen, canvas_max.y }, AXIS_X_COLOR, 1.5f);

            if (axis_y_screen >= canvas_min.y && axis_y_screen <= canvas_max.y)
                draw->AddLine({ canvas_min.x, axis_y_screen }, { canvas_max.x, axis_y_screen }, AXIS_Z_COLOR, 1.5f);
        }

        // ---- occupied cells ------------------------------------------------------------------------------
        if (m_show_occupied_cells && m_cell_x > 0 && m_cell_z > 0 && !m_entities.empty()) {

            const f32 cx = static_cast<f32>(m_cell_x);
            const f32 cz = static_cast<f32>(m_cell_z);

            const f32 half_x = cx * 0.5f;
            const f32 half_z = cz * 0.5f;

            // bucket entities into cells, draw once per occupied cell
            std::vector<u64> keys;
            keys.reserve(m_entities.size());
            for (const auto& e : m_entities) {
                const i64 col = static_cast<i64>(std::floor((e.xz.x + half_x) / cx));
                const i64 row = static_cast<i64>(std::floor((e.xz.y + half_z) / cz));
                keys.push_back((static_cast<u64>(col) << 32) ^ static_cast<u32>(row));
            }
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

            for (u64 key : keys) {

                const i32 col = static_cast<i32>(key >> 32);
                const i32 row = static_cast<i32>(static_cast<u32>(key));

                const f32 wx0 = static_cast<f32>(col) * cx - half_x;
                const f32 wz0 = static_cast<f32>(row) * cz - half_z;

                const ImVec2 a = w2s({ wx0,       wz0       });
                const ImVec2 b = w2s({ wx0 + cx,  wz0 + cz  });

                draw->AddRectFilled(a, b, OCCUPIED_CELL_FILL);
                draw->AddRect(a, b, OCCUPIED_CELL_BORDER, 0.0f, 0, 1.0f);
            }
        }

        // ---- entity dots ---------------------------------------------------------------------------------
        if (m_show_entity_dots) {

            const bool draw_hover = hovered;
            i32 hovered_index = -1;

            for (size_t i = 0; i < m_entities.size(); ++i) {

                const ImVec2 sp = w2s(m_entities[i].xz);

                if (sp.x < canvas_min.x - DOT_RADIUS || sp.x > canvas_max.x + DOT_RADIUS ||
                    sp.y < canvas_min.y - DOT_RADIUS || sp.y > canvas_max.y + DOT_RADIUS)
                    continue;

                draw->AddCircleFilled(sp, DOT_RADIUS, ENTITY_DOT_COLOR);

                if (draw_hover) {
                    const f32 dx = sp.x - mouse.x;
                    const f32 dy = sp.y - mouse.y;
                    if (dx * dx + dy * dy <= DOT_HOVER_RADIUS * DOT_HOVER_RADIUS)
                        hovered_index = static_cast<i32>(i);
                }
            }

            if (hovered_index >= 0) {
                const ImVec2 sp = w2s(m_entities[static_cast<size_t>(hovered_index)].xz);
                draw->AddCircle(sp, DOT_HOVER_RADIUS, ENTITY_DOT_COLOR, 0, 1.5f);
            }
        }

        draw->PopClipRect();

        // ---- HUD -----------------------------------------------------------------------------------------
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "center X %.1f m   Z %.1f m      %.2f px/m      cells %d x %d x %d",
                m_view_center.x, m_view_center.y, m_pixels_per_meter,
                m_cell_x, m_cell_y, m_cell_z);

            draw->AddText({ canvas_min.x + 8.0f, canvas_min.y + 8.0f }, HUD_TEXT_COLOR, buf);
        }

        if (m_load_failed) {

            const char* msg = "World could not be loaded.";
            const ImVec2 sz = ImGui::CalcTextSize(msg);
            draw->AddText(
                { (canvas_min.x + canvas_max.x - sz.x) * 0.5f,
                  (canvas_min.y + canvas_max.y - sz.y) * 0.5f },
                IM_COL32(230, 90, 90, 255), msg);
        }
        else if (m_entities.empty()) {

            const char* msg = "No entities with a transform in the current world.";
            const ImVec2 sz = ImGui::CalcTextSize(msg);
            draw->AddText(
                { (canvas_min.x + canvas_max.x - sz.x) * 0.5f,
                  (canvas_min.y + canvas_max.y - sz.y) * 0.5f },
                IM_COL32(140, 140, 140, 255), msg);
        }

        // ---- floating overlay (fit / center-on-origin) ---------------------------------------------------
        draw_floating_buttons(canvas_min, canvas_max);
    }


    void world_topdown_editor::draw_floating_buttons(const ImVec2& canvas_min, const ImVec2& canvas_max) {

        constexpr int button_count = 2;

        const f32 strip_width  = FLOATING_BUTTON_SIZE * static_cast<f32>(button_count) +
            FLOATING_BUTTON_GAP  * static_cast<f32>(button_count - 1);

        const ImVec2 strip_min(
            canvas_max.x - FLOATING_BUTTON_EDGE_PAD - strip_width - FLOATING_STRIP_PAD,
            canvas_min.y + FLOATING_BUTTON_EDGE_PAD - FLOATING_STRIP_PAD);

        const ImVec2 strip_max(
            canvas_max.x - FLOATING_BUTTON_EDGE_PAD + FLOATING_STRIP_PAD,
            canvas_min.y + FLOATING_BUTTON_EDGE_PAD + FLOATING_BUTTON_SIZE + FLOATING_STRIP_PAD);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(strip_min, strip_max, FLOATING_STRIP_BG, 8.0f);

        // Half-transparency for the whole strip.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.65f);

        const ImVec4 button_bg(0.15f, 0.15f, 0.18f, 0.85f);
        const ImVec4 button_tint(1.0f, 1.0f, 1.0f, 0.95f);
        const ImVec2 icon_size(FLOATING_BUTTON_SIZE, FLOATING_BUTTON_SIZE);

        f32 cursor_x = canvas_max.x - FLOATING_BUTTON_EDGE_PAD - strip_width;
        const f32 cursor_y = canvas_min.y + FLOATING_BUTTON_EDGE_PAD;

        auto place_icon_button = [&](const char* id, const icon_manager::icon icon_type) -> bool {

            const icon_manager::icon_data icon = icon_manager::get(icon_type);
            if (icon.image_size.x <= 0.0f) {
                cursor_x += FLOATING_BUTTON_SIZE + FLOATING_BUTTON_GAP;
                return false;
            }

            ImGui::SetCursorScreenPos(ImVec2(cursor_x, cursor_y));
            const bool clicked = ImGui::ImageButton(id, icon.tex_ref, icon_size,
                icon.uv0, icon.uv1, button_bg, button_tint);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", id);      // pass a friendlier label in if you like

            cursor_x += FLOATING_BUTTON_SIZE + FLOATING_BUTTON_GAP;
            return clicked;
        };

        // Top-right, left-to-right: Fit to entities, then Center on origin.
        if (place_icon_button("##world_fit_to_entities", icon_manager::icon::world))
            m_fit_pending = true;

        if (place_icon_button("##world_center_on_origin", icon_manager::icon::transfrom_translation))
            m_view_center = { 0.0f, 0.0f };

        ImGui::PopStyleVar();
    }


    void world_topdown_editor::fit_to_entities(const ImVec2& canvas_size) {

        if (m_entities.empty() || canvas_size.x <= 1.0f || canvas_size.y <= 1.0f) {
            m_view_center = { 0.0f, 0.0f };
            m_pixels_per_meter = 2.0f;
            return;
        }

        f32 min_x = std::numeric_limits<f32>::max();
        f32 min_z = std::numeric_limits<f32>::max();
        f32 max_x = -std::numeric_limits<f32>::max();
        f32 max_z = -std::numeric_limits<f32>::max();

        for (const auto& e : m_entities) {
            min_x = std::min(min_x, e.xz.x);
            max_x = std::max(max_x, e.xz.x);
            min_z = std::min(min_z, e.xz.y);
            max_z = std::max(max_z, e.xz.y);
        }

        m_view_center = { (min_x + max_x) * 0.5f, (min_z + max_z) * 0.5f };

        // a little headroom so the outermost dots don't touch the edge
        const f32 span_x = std::max(1.0f, (max_x - min_x));
        const f32 span_z = std::max(1.0f, (max_z - min_z));

        const f32 ppm_x = canvas_size.x * 0.85f / span_x;
        const f32 ppm_z = canvas_size.y * 0.85f / span_z;

        m_pixels_per_meter = std::clamp(std::min(ppm_x, ppm_z), MIN_PPM, MAX_PPM);
    }

}
