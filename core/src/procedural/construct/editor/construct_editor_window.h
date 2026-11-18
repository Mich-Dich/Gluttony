#pragma once

#include "editor/i_editor.h"
#include "asset/asset_ref.h"
#include "world/entity.h"

#include "procedural/construct/engine/construct_asset.h"
#include "procedural/construct/editor/construct_preview.h"

// The Construct editor: one window for every kind of construct (trees, spaceship frames, ...). Everything specific to a
// kind comes from the packs through the registry: stage types and their settings, guide roles (with their per guide and
// per control point attributes), "add guide" templates, presets, upgrade functions and self tests. A pack has no
// editor code.
//
// Workflow: the window is a working session. Tools > Construct > Construct editor opens an empty one (pick a preset),
// double clicking a construct asset opens it; edits stay in the window (undo / redo) until the user saves:
//
//   File > Save (Ctrl+S)                   the construct asset the window is bound to (asks for a place the first time)
//   File > Save as procedural asset ...    a construct asset (.glt_construct): generated where it is used
//   File > Export static mesh ...          a baked .glt_static_mesh of the current result (seed included)
//
// Layout: menu bar | toolbar (seed, pipeline step, what to show, what clicks pick, gizmo) | stages + guides | 3D view |
// properties.
//
//   Pipeline   which step of the pipeline to look at: Guides (Interpret), Generate, Expand (elements: centre lines,
//              points, relations), Realize, Finish (the raw products per stage), Output (the final meshes). The
//              evaluation stops after the chosen step, so the later, expensive steps do not run while debugging early ones
//   Stages     the construct's stage list in run order: visibility, statistics, add / reorder / duplicate / re-seed /
//              delete; the selected stage is the current one (show all / up to it / only it, limit picking to it)
//   Guides     what the user places (splines, planes, volumes): gizmo, handles, shape, edits, the role's attributes
//   Elements   anything generated can be clicked in the view: path, stage, attributes; hide it or jump to its stage



namespace GLT::world {
    class i_world;
}

namespace GLT::construct::editor {

    // CLASS DECLARATION ===============================================================================================

    class construct_editor_window : public GLT::editor::i_editor_window {
    public:

        // [asset_path] empty: a new, unsaved construct (the window offers the presets of the loaded packs)
        explicit construct_editor_window(std::filesystem::path asset_path = {}, GLT::asset::asset_ref asset = {});
        ~construct_editor_window() override;

        construct_editor_window(const construct_editor_window&) = delete;
        construct_editor_window& operator=(const construct_editor_window&) = delete;

        [[nodiscard]] std::string title() const override;
        [[nodiscard]] int window_flags() const override;
        [[nodiscard]] std::filesystem::path asset_path() const override { return m_path; }
        void draw_content(f32 delta_time) override;
        void update(f32 delta_time) override;

    private:

        enum class selection_kind : u8 { none, stage, guide, element };
        enum class handle_kind : u8 { none, origin, control_point, point };
        enum class stage_view : u8 { all, up_to_current, only_current };
        enum class pick_filter : u8 { everything, guides, current_stage };
        enum class gizmo_mode : u8 { translate, rotate, scale };
        enum class dialog : u8 { none, save_construct, export_mesh };
        enum class pipeline_step : u8 { interpret, generate, expand, realize, finish, output };

        struct selection {
            selection_kind                              kind = selection_kind::none;
            u32                                         stage = 0;
            i32                                         guide = -1;
            element_path                                element{};
        };
        struct handle { i32 guide = -1; handle_kind kind = handle_kind::none; i32 index = -1; };
        struct product_info { u32 stage = 0; glm::vec3 lo{ 0.0f }, hi{ 0.0f }; };

        // what the packs supply (registry)
        [[nodiscard]] std::shared_ptr<const registry_snapshot> registry_view() const;
        [[nodiscard]] bool pack_active(std::string_view pack) const;
        bool upgrade(construct_data& data) const;
        [[nodiscard]] generator_instance make_stage(const generator_desc& type, u32 id) const;
        [[nodiscard]] publish_settings preview_publish_settings() const;
        bool draw_attribute_row(attribute_set& set, attribute_id id);
        void draw_presets();

        // data / history
        void take(construct_data data, bool keep_history);
        void sync_working();
        [[nodiscard]] const guide* selected_guide() const noexcept;
        void undo();
        void redo();
        [[nodiscard]] bool interacting() const;
        [[nodiscard]] bool dirty() const;

        // evaluation / preview
        void request_evaluation();
        void poll_evaluation();
        void publish_preview();
        [[nodiscard]] bool stage_visible(u32 stage) const;
        [[nodiscard]] bool stage_pickable(u32 stage) const;
        [[nodiscard]] i32 stage_index(u32 stage) const;
        [[nodiscard]] const generator_desc* stage_type(const generator_instance& g) const;
        [[nodiscard]] glm::vec3 stage_color(u32 stage) const;
        [[nodiscard]] const element_summary* find_element(const element_path& path) const;
        [[nodiscard]] const stage_stats* find_stats(u32 stage) const;

        // saving
        void save();
        bool save_construct_to(const std::filesystem::path& content_path);
        bool export_static_mesh_to(const std::filesystem::path& content_path);
        void open_dialog(dialog d);
        void draw_dialogs();
        void message(std::string text, bool error = false);

        // world link, tests
        void update_world_link();
        void remove_from_world();
        void run_self_test();
        void frame_all();
        void make_unique(guide& g) const;
        [[nodiscard]] glm::vec3 to_view(const guide& g, const glm::vec3& local) const;

        // layout
        void draw_menu_bar();
        void draw_toolbar();
        void draw_stage_list();
        void draw_guide_list();
        void draw_view(ImVec2 size);
        void draw_overlay(ImDrawList& draw);
        void draw_gizmo(ImDrawList& draw);
        void draw_properties();
        void draw_stage_properties(generator_instance& g);
        bool draw_setting(const setting_desc& desc, generator_instance& g);
        void draw_guide_section(guide& g);
        void draw_element_properties();
        void draw_construct_section();
        void draw_diagnostics();
        bool draw_shape(guide& g);
        bool draw_edits(guide& g);
        bool draw_attributes(guide& g);
        void submit_debug_lines();

        // picking / selection
        [[nodiscard]] handle pick_guide(ImVec2 mouse, bool edges) const;
        [[nodiscard]] std::optional<element_path> pick_element(ImVec2 mouse) const;
        [[nodiscard]] std::optional<element_path> pick_skeleton(ImVec2 mouse) const;
        void drag(const handle& h, ImVec2 delta);
        [[nodiscard]] glm::vec3 handle_position(const handle& h) const;
        void select_stage(u32 stage);
        void select_guide(i32 guide);
        void select_element(const element_path& path);
        void add_stage(const generator_desc& type);
        void remove_stage(u32 stage);

        std::filesystem::path                           m_path;
        GLT::asset::asset_ref                           m_asset;

        // the session
        construct_data                                  m_working{};
        construct_preview_settings                      m_settings{};
        std::shared_ptr<const construct_data>           m_committed{};      // last settled state (undo point)
        u64                                             m_committed_hash = 0;
        std::shared_ptr<const construct_data>           m_snapshot{};       // what evaluations get
        u64                                             m_snapshot_hash = 0;
        std::vector<std::shared_ptr<const construct_data>> m_undo{}, m_redo{};
        u64                                             m_saved_hash = 0;
        u64                                             m_saved_seed = 0;
        std::string                                     m_message{};
        bool                                            m_message_error = false;
        f64                                             m_message_time = 0.0;

        // evaluation
        std::future<evaluation_result>                  m_job{};
        u64                                             m_job_key = 0;
        u64                                             m_result_key = 0;
        evaluation_result                               m_result{};
        bool                                            m_has_result = false;
        std::unordered_map<element_path, u32>           m_element_index{};
        std::vector<product_info>                       m_products{};
        std::vector<published_mesh>                     m_meshes{};
        std::vector<published_mesh>                     m_visible_meshes{};

        // view state
        construct_preview                               m_preview{};
        pipeline_step                                   m_step = pipeline_step::output;
        selection                                       m_selection{};
        u32                                             m_current_stage = 0;
        i32                                             m_point = -1;       // selected control point of the selected guide
        std::unordered_set<u32>                         m_hidden_stages{};
        stage_view                                      m_stage_view = stage_view::all;
        pick_filter                                     m_pick = pick_filter::everything;
        gizmo_mode                                      m_gizmo = gizmo_mode::translate;
        bool                                            m_gizmo_local = false;
        bool                                            m_gizmo_hot = false;
        bool                                            m_gizmo_using = false;
        bool                                            m_show_skeleton = false;
        bool                                            m_show_advanced = false;
        bool                                            m_framed = false;
        bool                                            m_started = false;
        bool                                            m_visible = false;
        handle                                          m_drag{};

        // dialogs
        dialog                                          m_dialog = dialog::none;
        bool                                            m_dialog_open_request = false;
        std::filesystem::path                           m_dialog_dir{};
        std::string                                     m_dialog_name{};
        std::string                                     m_dialog_error{};
        std::vector<std::string>                        m_export_outputs{};

        // world link / tests
        GLT::world::i_world*                            m_world = nullptr;
        u64                                             m_world_instance = 0;
        std::vector<GLT::world::entity_id>              m_world_entities{};
        bool                                            m_world_linked = false;
        std::string                                     m_test_summary{};
        bool                                            m_test_failed = false;

        f32                                             m_outline_width = 290.0f;
        f32                                             m_properties_width = 390.0f;
    };

}
