
#pragma once

#include <imgui.h>
#include <TextEditor.h>

#include <asset/type.h>
#include <asset/asset_ref.h>
#include <asset/material.h>

#include "window/base/asset_editor.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable editor window for a material asset.
    //
    // The material format is dynamic: parameters are an opaque packed blob whose layout is described by the reflection schema
    // carried inside the asset. The editor therefore does not know any parameter names or types at compile time — it walks the
    // reflection table every frame and renders widgets on demand
    //
    // Edits never touch the asset directly. The window keeps parallel edit and baseline copies of every mutable field; save()
    // flushes the edit copy back through the registry
    class material_viewer_window : public asset_editor {
    public:

        material_viewer_window();
        material_viewer_window(const std::filesystem::path& path);
        ~material_viewer_window();

        DEFAULT_GETTER(GLT::asset::handle,                      asset_handle)
        DEFAULT_GETTER_C(std::filesystem::path,                 material_path)
        DEFAULT_GETTER(bool,                                    dirty)
        DEFAULT_GETTER_C(std::string,                           compile_error_message)

        // from asset_editor
        FORCE_INLINE_R const std::filesystem::path& get_asset_path() const noexcept override { return m_material_path; }


        void open(const std::filesystem::path& path);


        void close_material();


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, GLT::serializer::option option) override;


        bool save();


        bool compile();         // runs the shader pipeline against the current edit buffers


        void revert();

    private:


        static constexpr std::size_t TEXTURE_SLOT_COUNT = static_cast<std::size_t>(GLT::asset::material::texture_slot::count);

        using texture_handles = std::array<GLT::asset::handle, TEXTURE_SLOT_COUNT>;

        enum class compile_log_level : u8 {
            info,
            warning,
            error,
            success
        };

        struct compile_log_entry {

            compile_log_level   level{ compile_log_level::info };
            std::string         text{};
        };

        // panels -----------------------------------------------------------------------------------------------------

        void draw_details_panel();

        void draw_source_panel();

        void draw_compile_terminal();   // bottom pane of the source panel

        // details sections --------------------------------------------------------------------------------------------

        void draw_blend_section();

        void draw_parameters_section();

        void draw_textures_section();

        void draw_derived_section();

        void draw_preview_canvas();

        // parameter widgets -------------------------------------------------------------------------------------------

        bool draw_param_widget(const char* label, const GLT::asset::material::reflection_param& p);

        void mark_dirty();

        // Push the current edit params into the live asset and invalidate its bytecode hash so the renderer rebuilds anything keyed
        // on it (descriptor sets, pipeline cache entries). Parameter edits don't require a shader recompile — the bytecode layout is
        // unchanged, only the values it reads at runtime
        //
        // No-op when the material has never been saved (m_asset_handle == INVALID_HANDLE): the change will flush through the normal save() path
        void push_live_params();

        void build_new_material();

        bool load_from_handle(GLT::asset::handle h);

        void handle_preview_input();

        void draw_preview_fallback(ImDrawList* draw, const ImVec2& min, const ImVec2& max);



        void append_compile_log(compile_log_level level, std::string text);

        void clear_compile_log();

        // ---- state -------------------------------------------------------------------------------------------------

        std::filesystem::path                                   m_material_path{};
        GLT::asset::asset_ref                                   m_asset_handle = INVALID_HANDLE;
        std::string                                             m_material_name{};

        bool                                                    m_has_material = false;
        bool                                                    m_load_failed = false;
        bool                                                    m_dirty = false;

        // Compile diagnostics surface in the source panel's terminal section
        std::string                                             m_compile_error_message{};

        // Rolling log shown in the terminal. Bounded; oldest entries are dropped first
        std::vector<compile_log_entry>                          m_compile_log{};
        bool                                                    m_scroll_terminal_to_bottom{ false };

        // Persisted height (px) of the terminal section. The editor takes whatever is left. Never reset on
        // material close - it's a UI preference, not material state
        f32                                                     m_terminal_height{ 160.0f };

        // Edit buffers. The window edits these, never the asset directly.
        std::string                                             m_edit_slang{};
        std::string                                             m_baseline_slang{};

        std::vector<std::byte>                                  m_edit_params{};
        std::vector<std::byte>                                  m_baseline_params{};

        GLT::asset::material::blend_mode                        m_edit_blend{ GLT::asset::material::blend_mode::opaque };
        GLT::asset::material::blend_mode                        m_baseline_blend{ GLT::asset::material::blend_mode::opaque };

        texture_handles                                         m_edit_textures{};
        texture_handles                                         m_baseline_textures{};

        // Read-only mirrors of derived artifacts.
        std::vector<std::byte>                                  m_reflection_blob{};
        std::vector<std::byte>                                  m_bytecode_blob{};
        u64                                                     m_compiler_hash{ 0 };
        bool                                                    m_compiler_hash_matches{ true };

        TextEditor                                              m_slang_editor{};

        glm::vec3                                               m_preview_camera_pos{ 0.0f, 0.0f, 3.0f };
        glm::vec3                                               m_preview_target{ 0.0f, 0.0f, 0.0f };
    };

}
