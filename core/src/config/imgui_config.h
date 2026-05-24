
#pragma once

#include <imgui.h>
// #include <implot.h>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::imgui_config {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

	// Enum representing available UI theme options.
	enum class theme_selection : u8 {
		dark = 0, 				// Dark theme.
		light 					// Light theme.
	};


	enum class font_type {
		regular,
		regular_big,
		bold,
		bold_big,
		italic,
		italic_big,
		small,
		header0,
		header1,
		header2,
		giant,
		giant_thin,
		monospace_regular,
		monospace_regular_big,
	};
    
    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

	ImVec4& get_main_color_ref();
	ImVec4& get_main_titlebar_color_ref();

	ImVec4& get_action_color00_faded_ref();
	ImVec4& get_action_color00_weak_ref();
	ImVec4& get_action_color00_default_ref();
	ImVec4& get_action_color00_hover_ref();
	ImVec4& get_action_color00_active_ref();

	ImVec4& get_default_gray_ref();
	ImVec4& get_default_gray1_ref();

	ImVec4& get_action_color_gray_default_ref();
	ImVec4& get_action_color_gray_hover_ref();
	ImVec4& get_action_color_gray_active_ref();

    ImGuiContext* get_context_imgui();


	void init();


	void shutdown();

	// Sets the current UI theme.
	// @param theme_selection New theme to apply.
	void set_ui_theme_selection(const theme_selection theme_selection);


	// Enables or disables window borders globally.
	// @param enable Whether to enable borders.
	void enable_window_border(bool enable);


	// Applies the currently selected UI theme.
	void update_ui_theme();


	// Updates the main UI color and saves the change to config.
	// @param new_color New main color to apply.
	void update_ui_colors(ImVec4 new_color);


    // Retrieves a font by name.
    // @param type Font identifier (e.g., font_type::regular, font_type::bold).
    // @return Pointer to the requested ImFont, or nullptr if not found.
    ImFont* get_font(const font_type type = font_type::regular);


    // Resizes fonts based on a single base size.
    // @note !! IMPORTANT !! - Do not call during rendering. Call it during update
    // @param font_size New base font size.
    void resize_fonts(const f32 font_size);


    // Resizes fonts based on multiple custom sizes.
    // @note !! IMPORTANT !! - Do not call during rendering. Call it during update
    // @param font_size Base font size.
    // @param big_font_size Larger font size for emphasis.
    // @param font_size_header_0 Font size for header level 0.
    // @param font_size_header_1 Font size for header level 1.
    // @param font_size_header_2 Font size for header level 2.
    void resize_fonts(const f32 regular, const f32 small, const f32 big,
        const f32 header0, const f32 header1, const f32 header2);


    void serialize(GLT::serializer::option option);

    // TEMPLATE DECLARATION ============================================================================================

	FORCE_INLINE_R static u32 convert_color_to_int(const ImVec4& color);

    // CLASS DECLARATION ===============================================================================================

}

#include "imgui_config.inl"
