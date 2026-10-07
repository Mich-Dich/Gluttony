#include "util/pch.h"
#include "material_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <asset/type.h>
#include <asset/i_asset_registry.h>
#include <plugin_system/plugin_manager.h>
#include <render/i_renderer.h>

#include "util/ui/pannel_collection.h"
#include "util/ui/asset_picker.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr f32                                   DETAILS_PANEL_WIDTH = 430.0f;

    // Maximum edge length of the preview square. The actual size is min(available_width, PREVIEW_MAX_SIDE) so the canvas
    // grows with the panel but never dominates the entire window on very wide layouts
    constexpr f32                                   PREVIEW_MAX_SIDE = 420.0f;

    // Every table in this window uses the same name
    // Tables live inside distinct collapsing headers, so the effective ImGui IDs remain unique
    constexpr const char*                           TABLE_NAME = "material_viewer_table";

    // Column split for every table. The label column is a fixed fraction; the value column takes whatever remains
    // Kept low so texture pickers have room
    constexpr f32                                   TABLE_LABEL_RATIO = 0.38f;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Bit flags understood by the material editor. These are placeholders until the renderer starts consuming them;
    // the shaders currently ignore material_params::flags.
    enum material_flag : u32 {

        MAT_FLAG_DOUBLE_SIDED                       = BIT(0),
        MAT_FLAG_ALPHA_TEST                         = BIT(1),
        MAT_FLAG_ALPHA_BLEND                        = BIT(2),
        MAT_FLAG_UNLIT                              = BIT(3),
        MAT_FLAG_CAST_SHADOW                        = BIT(4),
        MAT_FLAG_RECEIVE_SHADOW                     = BIT(5),
    };


    struct flag_entry {

        u32                                         mask;
        const char*                                 label;
        const char*                                 tooltip;
    };


    constexpr flag_entry kMaterialFlags[] = {

        { MAT_FLAG_DOUBLE_SIDED,   "Double Sided",   "Render both faces. Currently a no-op in the RT pipeline." },
        { MAT_FLAG_ALPHA_TEST,     "Alpha Test",     "Discard fragments below an alpha cutoff. Not wired yet." },
        { MAT_FLAG_ALPHA_BLEND,    "Alpha Blend",    "Sort and blend. Not implemented yet." },
        { MAT_FLAG_UNLIT,          "Unlit",          "Skip lighting, output albedo + emissive directly." },
        { MAT_FLAG_CAST_SHADOW,    "Cast Shadow",    "Contributes to shadow rays / AO." },
        { MAT_FLAG_RECEIVE_SHADOW, "Receive Shadow", "Lit by direct lighting." },
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static bool params_equal(const GLT::asset::material::material_params& a, const GLT::asset::material::material_params& b);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static bool params_equal(const GLT::asset::material::material_params& a, const GLT::asset::material::material_params& b) {

        // memcmp would almost work since the struct is trivially copyable, but padding bytes could differ between two
        // logically-equal structs. Compare field by field.
        return a.base_color.x           == b.base_color.x
            && a.base_color.y           == b.base_color.y
            && a.base_color.z           == b.base_color.z
            && a.base_color.w           == b.base_color.w
            && a.emissive.x             == b.emissive.x
            && a.emissive.y             == b.emissive.y
            && a.emissive.z             == b.emissive.z
            && a.roughness              == b.roughness
            && a.metallic               == b.metallic
            && a.reflectance            == b.reflectance
            && a.normal_scale           == b.normal_scale
            && a.occlusion_strength     == b.occlusion_strength
            && a.flags                  == b.flags;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    material_viewer_window::material_viewer_window() { open(std::filesystem::path{}); }


    material_viewer_window::material_viewer_window(const std::filesystem::path& path) { open(path); }


    material_viewer_window::~material_viewer_window() { close_material(); }

    // CLASS PUBLIC ====================================================================================================

    void material_viewer_window::open(const std::filesystem::path& path) {

        close_material();

        m_material_path = path;
        m_material_name = path.empty()
            ? std::string("New Material")
            : path.filename().replace_extension("").string();

        m_load_failed = false;

        cache_window_state();
        make_window_name((std::string("MAT: ") + m_material_name).c_str());

        if (path.empty()) {
            build_new_material();
            m_show_window = true;
            return;
        }

        std::error_code error{};
        VALIDATE(GLT::vfs::exists(PROJECT_CONTENT_DIR / path, error) && !error,
            m_load_failed = true; m_has_material = false; m_show_window = true; return, "",
            "file not found [{}]", path.generic_string())

        VALIDATE(!GLT::vfs::is_directory(PROJECT_CONTENT_DIR / path, error) && !error,
            m_load_failed = true; m_has_material = false; m_show_window = true; return, "",
            "path is a directory [{}]", path.generic_string())

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry,
            m_load_failed = true; m_show_window = true; return, "",
            "no asset registry")

        auto loaded = registry->load(path);
        VALIDATE(loaded,
            m_load_failed = true; m_show_window = true; return, "",
            "failed to load material [{}]", path.generic_string())

        if (!load_from_handle(*loaded)) {
            registry->unload(*loaded);
            m_load_failed = true;
        }

        m_show_window = true;
    }


    void material_viewer_window::close_material() {

        if (m_asset_handle != INVALID_HANDLE) {
            if (auto registry = GLT::asset::registry::get_ref())
                registry->unload(m_asset_handle);
            m_asset_handle = INVALID_HANDLE;
        }

        m_has_material = false;
        m_dirty = false;
        m_edit = {};
        m_baseline = {};
        m_edit_textures.fill(INVALID_HANDLE);
        m_baseline_textures.fill(INVALID_HANDLE);
    }


    void material_viewer_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;

        apply_pending_dock();
        ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 460.0f),
            ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));

        if (ImGui::Begin(m_window_id.c_str(), &m_show_window)) {

            GLT::UI::custom_frame(DETAILS_PANEL_WIDTH, true, ImGui::GetColorU32(GLT::imgui_config::get_default_gray1_ref()),
                [this]() {
                    draw_details_panel();
                },
                [this]() {
                    draw_material_panel();
                });
        }
        ImGui::End();
    }


    void material_viewer_window::update(const f32 /*delta_time*/) {

        // Entirely event-driven; nothing to tick per frame.
    }


    bool material_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) {

        // The material owns its own persistence through the registry. The
        // window itself holds no state that belongs in the project file.
        return false;
    }


    bool material_viewer_window::save() {

        if (!m_has_material)
            return false;

        if (m_asset_handle == INVALID_HANDLE) {
            // Unsaved in-memory material. Registering it requires a destination
            // path the window doesn't have yet; a "Save As" flow will handle it.
            LOG(warn, "material_viewer: no destination path for [{}] - cannot save", m_material_name);
            return false;
        }

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(m_asset_handle);
        if (!asset) {
            LOG(error, "material_viewer: asset for [{}] disappeared before save", m_material_name);
            return false;
        }

        asset->params = m_edit;
        for (std::size_t i = 0; i < TEXTURE_SLOT_COUNT; ++i)
            if (m_edit_textures[i] != INVALID_HANDLE) {
                asset->textures[i] = m_edit_textures[i];
                registry->add_dependency(m_asset_handle, m_edit_textures[i]);
            }

        auto result = registry->save(m_asset_handle);
        if (!result) {
            LOG(error, "material_viewer: save failed for [{}] (error {})",
                m_material_name, static_cast<int>(result.error()));
            return false;
        }

        m_baseline = m_edit;
        m_baseline_textures = m_edit_textures;
        m_dirty = false;
        return true;
    }


    void material_viewer_window::revert() {

        m_edit = m_baseline;
        m_edit_textures = m_baseline_textures;
        m_dirty = false;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void material_viewer_window::draw_details_panel() {

        const std::string title = m_material_name.empty() ? std::string("(no material)") : m_material_name;
        ImGui::TextUnformatted(title.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (m_material_path.empty())
            ImGui::TextUnformatted("(unsaved)");
        else
            ImGui::TextWrapped("%s", m_material_path.generic_string().c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        if (m_load_failed) {

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 90, 90, 255));
            ImGui::TextUnformatted("Failed to load material.");
            ImGui::PopStyleColor();
            return;
        }

        if (!m_has_material) {

            ImGui::TextDisabled("No material loaded.");
            return;
        }

        draw_preview_canvas();
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        draw_identity_section();
        draw_flags_section();

        // save / revert -----------------------------------------------------------------------------------------------
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        if (m_dirty) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 200, 90, 255));
            ImGui::TextUnformatted("Unsaved changes");
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("No changes");
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Save", ImVec2(80.0f, 0.0f)))
            save();
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(80.0f, 0.0f)))
            revert();
        ImGui::EndDisabled();
    }


    void material_viewer_window::draw_material_panel() {

        if (!m_has_material) {

            ImGui::TextDisabled("Open a .glt_material, or create a new one, to edit it here.");
            return;
        }

        draw_pbr_section();
        draw_emission_section();
        draw_surface_section();
    }


    void material_viewer_window::draw_identity_section() {

        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO)) {

            GLT::UI::table_row("name", m_material_name.c_str());

            const std::string path_str = m_material_path.empty()
                ? std::string("(unsaved)")
                : m_material_path.generic_string();
            GLT::UI::table_row("path", path_str.c_str());

            GLT::UI::end_table();
        }
    }

    // -----------------------------------------------------------------------------------------------------------------
    // PBR Parameters
    // -----------------------------------------------------------------------------------------------------------------
    //
    // Rows are laid out so that a texture picker and its scalar/colour partner
    // sit on the same line, separated by a muted 'x'. This communicates the
    // shader convention: sampled_value * uniform_value.
    //
    //   base colour        [ texture picker ]  x  [ colour swatch ]
    //   metallic/roughness [ texture picker ]  x  [ sliders ]
    //   reflectance                                [ slider ]
    //
    void material_viewer_window::draw_pbr_section() {

        if (!ImGui::CollapsingHeader("PBR Parameters", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (!GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO))
            return;

        using slot = GLT::asset::material::texture_slot;

        if (GLT::UI::table_row_asset_picker("Albedo", m_edit_textures[static_cast<std::size_t>(slot::base_color)], GLT::asset::core_types::texture2d))
            mark_dirty();

        if (GLT::UI::table_row_color("Base color", m_edit.base_color, 0.f, 1.f))
            mark_dirty();

        if (GLT::UI::table_row("metallic", m_edit.metallic, 0.02f, 0.f, 1.f))
            mark_dirty();

        if (GLT::UI::table_row("roughness", m_edit.roughness, 0.02f, 0.f, 1.f))
            mark_dirty();

        if (GLT::UI::table_row("reflectance", m_edit.reflectance, 0.02f, 0.f, 1.f))
            mark_dirty();

        GLT::UI::end_table();
    }

    // Emission --------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_emission_section() {

        if (!ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (!GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO))
            return;

        using slot = GLT::asset::material::texture_slot;

        if (GLT::UI::table_row_asset_picker("Emissive", m_edit_textures[static_cast<std::size_t>(slot::emissive)], GLT::asset::core_types::texture2d))
            mark_dirty();

        if (GLT::UI::table_row_color("Emissive color", m_edit.emissive, 0.f, 1.f))
            mark_dirty();

        GLT::UI::end_table();
    }

    // Surface ---------------------------------------------------------------------------------------------------------

    void material_viewer_window::draw_surface_section() {

        if (!ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        if (!GLT::UI::begin_table(TABLE_NAME, false, ImVec2(0, 0), 0.0f, true, TABLE_LABEL_RATIO))
            return;

        using slot = GLT::asset::material::texture_slot;

        if (GLT::UI::table_row_asset_picker("Normal", m_edit_textures[static_cast<std::size_t>(slot::normal)], GLT::asset::core_types::texture2d))
            mark_dirty();

        if (GLT::UI::table_row("Normal scale", m_edit.normal_scale, 0.02f, 0.f, 4.f))
            mark_dirty();

        if (GLT::UI::table_row_asset_picker("Occlusion", m_edit_textures[static_cast<std::size_t>(slot::occlusion)], GLT::asset::core_types::texture2d))
            mark_dirty();

        if (GLT::UI::table_row("Occlusion strength", m_edit.occlusion_strength, 0.02f, 0.f, 1.f))
            mark_dirty();

        if (GLT::UI::table_row_asset_picker("Height", m_edit_textures[static_cast<std::size_t>(slot::height)], GLT::asset::core_types::texture2d))
            mark_dirty();

        GLT::UI::end_table();
    }


    void material_viewer_window::draw_flags_section() {

        if (!ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        for (const auto& f : kMaterialFlags) {

            bool active = (m_edit.flags & f.mask) != 0;
            if (ImGui::Checkbox(f.label, &active)) {

                if (active) m_edit.flags |=  f.mask;
                else        m_edit.flags &= ~f.mask;
                mark_dirty();
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f.tooltip);
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("raw: 0x%08X", m_edit.flags);
    }


    void material_viewer_window::draw_preview_canvas() {

        const ImVec2 avail = ImGui::GetContentRegionAvail();

        // Square preview that grows with the panel but never exceeds PREVIEW_MAX_SIDE. On narrow layouts it falls back to the full width
        const f32 preview_side = std::min(avail.x, PREVIEW_MAX_SIDE);

        const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max(canvas_min.x + preview_side, canvas_min.y + preview_side);

        ImGui::InvisibleButton("##mat_preview", ImVec2(preview_side, preview_side));
        handle_preview_input();

        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Renderer preview --------------------------------------------------------------------------------------------
        void* tex = nullptr;
        if (m_has_material && m_asset_handle != INVALID_HANDLE) {
            if (auto renderer = GLT::render::renderer::get_ref())
                tex = renderer->render_material_preview(m_asset_handle, m_edit, m_edit_textures, m_preview_camera_pos);
        }

        if (tex) {

            draw->AddImage(reinterpret_cast<ImTextureID>(tex), canvas_min, canvas_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
            draw->AddRect(canvas_min, canvas_max, IM_COL32(60, 60, 70, 255), 0.0f, 0, 1.0f);
        } else {
            draw_preview_fallback(draw, canvas_min, canvas_max);
        }

        // Readout -----------------------------------------------------------------------------------------------------
        char buf[128];
        std::snprintf(buf, sizeof(buf), "R %.2f   M %.2f   Refl %.2f", m_edit.roughness, m_edit.metallic, m_edit.reflectance);
        draw->AddText(ImVec2(canvas_min.x + 10.0f, canvas_min.y + 8.0f), IM_COL32(220, 220, 220, 210), buf);

        // Control hint ------------------------------------------------------------------------------------------------
        draw->AddText(ImVec2(canvas_min.x + 10.0f, canvas_max.y - 20.0f), IM_COL32(180, 180, 190, 150),
            "drag = translate     scroll = zoom     double-click = reset");
    }


    void material_viewer_window::mark_dirty() { m_dirty = !params_equal(m_edit, m_baseline) || m_edit_textures != m_baseline_textures; }


    void material_viewer_window::build_new_material() {

        m_edit = {};
        m_edit.base_color = { 0.8f, 0.8f, 0.8f, 1.0f };
        m_edit.roughness  = 0.5f;
        m_edit.metallic   = 0.0f;
        m_edit.reflectance = 0.5f;
        m_edit.normal_scale = 1.0f;
        m_edit.occlusion_strength = 1.0f;
        m_edit.flags = 0;

        m_baseline = m_edit;

        m_edit_textures.fill(INVALID_HANDLE);
        m_baseline_textures.fill(INVALID_HANDLE);

        m_dirty = false;
        m_has_material = true;
        m_load_failed = false;
        m_asset_handle = INVALID_HANDLE;    // not registered with the registry

        m_preview_camera_pos = { 0.0f, 0.0f, 3.0f };
        m_preview_target     = { 0.0f, 0.0f, 0.0f };
    }


    bool material_viewer_window::load_from_handle(GLT::asset::handle h) {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;

        auto* asset = registry->data_as<GLT::asset::material::material_asset>(h);
        if (!asset)
            return false;

        m_asset_handle = h;
        m_edit = asset->params;
        m_baseline = m_edit;

        for (std::size_t i = 0; i < TEXTURE_SLOT_COUNT; ++i)
            m_edit_textures[i] = asset->textures[i];
        m_baseline_textures = m_edit_textures;

        m_dirty = false;
        m_has_material = true;
        m_load_failed = false;

        m_preview_camera_pos = { 0.0f, 0.0f, 3.0f };
        m_preview_target     = { 0.0f, 0.0f, 0.0f };
        return true;
    }


    // Camera manipulation
    //
    // The renderer is handed a single world-space position and is expected to
    // look at the origin; we keep that contract by translating the position in
    // the plane perpendicular to the view direction, and zooming by scaling
    // the position along that direction.
    //
    //   drag     -> translate the camera in screen-space (feels like panning)
    //   scroll   -> zoom in/out along the view axis
    //   dblclick -> reset the camera to its default framing
    void material_viewer_window::handle_preview_input() {

        ImGuiIO& io = ImGui::GetIO();

        // Camera sits at m_preview_camera_pos and looks at the origin.
        const f32 dist = glm::length(m_preview_camera_pos);
        if (dist < 1e-4f)
            return;

        const glm::vec3 forward = -m_preview_camera_pos / dist;          // camera -> origin

        // Build an orthonormal basis. Fall back to a stable right vector if the
        // camera is looking straight down the world Y axis.
        glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(right, right) < 1e-6f)
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // Pan: move along the view plane. Speed scales with distance so the
        // cursor feels "glued" to the surface regardless of zoom.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {

            const f32 pan_speed = dist * 0.0035f;
            const glm::vec3 delta = (-right * io.MouseDelta.x + up * io.MouseDelta.y) * pan_speed;
            m_preview_camera_pos += delta;
        }

        // Zoom: scale the distance from the origin.
        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {

            const f32 factor = std::pow(0.9f, io.MouseWheel);
            const f32 new_dist = std::clamp(dist * factor, 0.5f, 50.0f);
            m_preview_camera_pos = glm::normalize(m_preview_camera_pos) * new_dist;
        }

        // Reset.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_preview_camera_pos = { 0.0f, 0.0f, 3.0f };
            m_preview_target     = { 0.0f, 0.0f, 0.0f };
        }
    }


    void material_viewer_window::draw_preview_fallback(ImDrawList* draw, const ImVec2& min, const ImVec2& max) {

        draw->AddRectFilled(min, max, IM_COL32(28, 28, 32, 255));
        draw->AddRect(min, max, IM_COL32(80, 80, 90, 255), 0.0f, 0, 1.0f);

        const char* msg = m_has_material
            ? "Preview unavailable (material not registered)"
            : "No material loaded";
        const ImVec2 s = ImGui::CalcTextSize(msg);
        draw->AddText(ImVec2((min.x + max.x - s.x) * 0.5f, (min.y + max.y - s.y) * 0.5f),
                    IM_COL32(180, 180, 180, 220), msg);
    }

}
