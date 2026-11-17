
#pragma once

#include <editor/i_editor.h>
#include <asset/asset_ref.h>

#include "graph/evaluator.h"
#include "graph_canvas.h"
#include "terrain_map.h"
#include "terrain_preview_3d.h"



namespace GLT::terrain {

    class terrain_graph_asset;

    // @brief The asset editor of .glt_terrain_graph: node canvas | sidebar (2D map on top, details below)
    //
    // Both splitters (canvas | sidebar, map / details) are draggable.
    // Edits go straight into the asset's graph (new immutable snapshot per change), so the map follows while a parameter
    // is dragged. Save (button or Ctrl+S) writes the graph, the node positions and the preview settings
    class terrain_editor_window final : public GLT::editor::i_editor_window {
    public:

        terrain_editor_window(std::filesystem::path asset_path, GLT::asset::asset_ref asset, const i_node_registry& registry, evaluator& eval);
        ~terrain_editor_window() override;

        terrain_editor_window(const terrain_editor_window&) = delete;
        terrain_editor_window& operator=(const terrain_editor_window&) = delete;

        [[nodiscard]] std::string title() const override;
        [[nodiscard]] int window_flags() const override;
        [[nodiscard]] std::filesystem::path asset_path() const override { return m_path; }

        void draw_content(f32 delta_time) override;

    private:

        [[nodiscard]] terrain_graph_asset* asset() const;
        void apply(terrain_graph graph);
        void save();

        void draw_toolbar(terrain_graph_asset& graph_asset);
        void draw_sidebar(terrain_graph_asset& graph_asset);
        void draw_map_header(terrain_graph_asset& graph_asset);
        // Returns a changed graph when a parameter / label was edited
        [[nodiscard]] std::optional<terrain_graph> draw_details(const terrain_graph& graph);
        void draw_graph_overview(const terrain_graph& graph);
        [[nodiscard]] terrain_map::target map_target(const terrain_graph_asset& graph_asset) const;
        // The 3D view shows heights: the map's target when that is a heightfield, the surface otherwise
        [[nodiscard]] terrain_preview_3d::target preview_3d_target(const terrain_graph_asset& graph_asset) const;

        std::filesystem::path                           m_path;
        GLT::asset::asset_ref                           m_asset;
        const i_node_registry&                          m_registry;
        evaluator&                                      m_eval;

        graph_canvas                                    m_canvas;
        terrain_map                                     m_map;
        terrain_preview_3d                              m_preview_3d;
        i32                                             m_view_mode = 0;        // preview: 0 = map (2D), 1 = 3D
        bool                                            m_loaded = false;
        bool                                            m_dirty = false;
        std::string                                     m_save_status{};
        std::string                                     m_apply_status{};       // "Apply to world"
        std::string                                     m_saved_layout{};       // node positions as last loaded / saved

        f32                                             m_sidebar_width = 420.0f;
        f32                                             m_map_height = 0.0f;                    // 0 = square on first draw

        u32                                             m_inspected_node = INVALID_NODE_ID;     // details + map follow the selection
        i32                                             m_inspected_pin = 0;
        std::map<std::string, f32>                      m_overrides{};                         // exposed parameters (preview only)
    };

}
