
#pragma once

#include <plugin_system/plugin_manager.h>
#include "window/base/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class plugin_manager_window : public base_window {
    public:

        plugin_manager_window();
        ~plugin_manager_window();

        void window(const f32 delta_time) override;
        void update(const f32 delta_time) override;
        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

        // Open the window and select a specific plugin by name. No-op if the plugin
        // has not been discovered.
        void focus_plugin(std::string_view plugin_name);

    private:

        enum class group_mode : u8 {

            category = 0,
            interface_
        };


        enum class state_filter : u8 {

            all = 0,
            enabled,
            disabled,
            blocked
        };


        enum class plugin_state : u8 {

            loaded = 0,
            pending,
            blocked,
            disabled
        };


        struct category_node {

            std::string                             name{};           // "UI"
            std::string                             full_path{};      // "Core|UI" or "interface:physics"
            std::vector<category_node>              children{};
        };

        // per-frame pipeline
        void refresh_plugin_data();                     // rebuild trees from get_all_plugins()
        void rebuild_category_tree();
        void rebuild_interface_tree();
        void append_into_category(const std::string& full_path);

        // drawing
        void draw_toolbar();
        void draw_filter_group();
        void draw_restart_banner();
        void draw_tree_panel();
        void draw_tree_node(category_node& node);
        void draw_cards_panel();
        void render_category_section(const category_node& node);
        void render_card_grid(const std::vector<size_t>& indices);
        void draw_plugin_card(const GLT::plugin_manager::plugin_info& info, f32 card_width);
        void draw_details_panel();
        void draw_details_header(const GLT::plugin_manager::plugin_info& info);
        void draw_details_metadata(const GLT::plugin_manager::plugin_info& info);
        void draw_details_dependencies(const GLT::plugin_manager::plugin_info& info);
        void draw_details_actions(const GLT::plugin_manager::plugin_info& info);

        void draw_state_pill(plugin_state state);
        void draw_logo_placeholder(const GLT::plugin_manager::plugin_info& info, f32 size);

        // helpers
        plugin_state classify(const GLT::plugin_manager::plugin_info& info) const;
        bool matches_category(const GLT::plugin_manager::plugin_info& info, const std::string& sel) const;
        bool matches_search(const GLT::plugin_manager::plugin_info& info) const;
        bool passes_filter(const GLT::plugin_manager::plugin_info& info) const;
        const GLT::plugin_manager::plugin_info* find_plugin(std::string_view name) const;
        const category_node* find_node_by_path(const category_node& root, const std::string& path) const;
        bool plugin_in_section(const GLT::plugin_manager::plugin_info& info, const category_node& node) const;
        bool plugin_is_direct(const GLT::plugin_manager::plugin_info& info, const category_node& node) const;



        std::vector<GLT::plugin_manager::plugin_info>           m_plugins{};            // snapshot of get_all_plugins()
        category_node                                           m_category_root{};
        category_node                                           m_interface_root{};
        std::vector<size_t>                                     m_filtered_indices{};   // into m_plugins

        std::string                                             m_selected_category{};  // "" = all; "Core|UI"; "interface:physics"
        std::string                                             m_selected_plugin{};    // empty = no details shown

        char                                                    m_search_buffer[256] = {};
        group_mode                                              m_group_mode = group_mode::category;
        state_filter                                            m_filter = state_filter::all;

        u64                                                     m_cached_generation = 0;
        bool                                                    m_config_dirty = false;
        bool                                                    m_first_frame = true;

        // layout
        static constexpr f32                                    TREE_PANEL_WIDTH = 240.0f;
        static constexpr f32                                    CARD_MIN_WIDTH = 300.0f;    // was CARD_WIDTH
        static constexpr f32                                    CARD_HEIGHT = 92.0f;     // grew to fit 4 lines comfortably
        static constexpr f32                                    CARD_SPACING = 8.0f;
        static constexpr f32                                    DETAILS_HEIGHT = 180.0f;
        static constexpr f32                                    LOGO_SIZE = 56.0f;
    };
}
