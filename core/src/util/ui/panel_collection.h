
#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

#include "application.h"
#include "util/data_structures/UUID.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::UI {

	// CONSTANTS =======================================================================================================

    // Drag-and-drop payload type used whenever an asset path (or several) is dragged between editor widgets
	// Producers and consumers both match on this ID
    //
    // Payload layout: a newline-separated list of absolute filesystem paths. The trailing byte is NOT guaranteed to be
	// a newline; consumers must tolerate an unterminated final line
    inline constexpr const char* ASSET_DRAG_PAYLOAD = "ASSET_DRAG_PAYLOAD";

	// MACROS ==========================================================================================================

	// TYPES ===========================================================================================================

	enum class window_pos : u8 {

		center = 0,
		custom = 1,
		top_left = 2,
		top_right = 3,
		bottom_left = 4,
		bottom_right = 5,
	};


	enum class mouse_interation : u8 {									

		none,
		hovered,						// The mouse cursor is over the item.

		// DONT CHANGE ORDER OF: LEFT / RIGHT / MIDDLE ENTRYS   else change refrence in: util/ui/panel_collection/set_mouse_interaction_state()
		left_clicked,					// The mouse button was pressed and released over the item.
		left_double_clicked,			// The mouse button was clicked twice in quick succession over the item.
		left_pressed,					// The mouse button is currently held down over the item.
		left_released,					// The mouse button was released over the item.
		right_clicked,					// The right mouse button was clicked over the item.
		right_double_clicked,			// The right mouse button was clicked over the item.
		right_pressed,					// The mouse button is currently held down over the item.
		right_released,					// The mouse button was released over the item.
		middle_clicked,					// The middle mouse button was clicked over the item.
		middle_double_clicked,			// The middle mouse button was clicked over the item.
		middle_pressed,					// The mouse button is currently held down over the item.
		middle_release,					// The mouse button is currently held down over the item.
		
		dragged,						// The item is being dragged with the mouse.
		focused,						// The item has keyboard focus(can be set with mouse clicks).
		active,							// The item is currently being interacted with
		deactivated,					// The item was previously active but is no longer being interacted with.
		deactivated_after_edit,			// The item was active and edited, but the interaction has ended.
	};

	// STATIC VARIABLES ================================================================================================

	// FUNCTION DECLARATION ============================================================================================

	// @brief Checks if the mouse is currently hovering over the current ImGui window.
	// @return True if the mouse is hovering over the window, false otherwise.
	bool is_hovering_window();


	// @brief Checks if the current ImGui item (e.g., button, text) is double-clicked.
	// @return True if the item is double-clicked, false otherwise.
	bool is_item_double_clicked();


	// @brief Determines the mouse interaction state (e.g., hovered, clicked) on the current ImGui item.
	// @return The mouse interaction state (e.g., hovered, clicked, held).
	mouse_interation get_mouse_interation_on_item(const bool block_input = false);


	// @brief Determines the mouse interaction state (e.g., hovered, clicked) on the current ImGui window.
	// @return The mouse interaction state (e.g., hovered, clicked, held).
	mouse_interation get_mouse_interation_on_window();


	void wrap_text(std::string& text, f32 wrap_width, int max_lines = -1);


	// @brief Wraps text at underscores to fit within a specified width.
	// @param [text] The text to wrap.
	// @param [wrap_width] The maximum width before wrapping occurs.
	// @return The wrapped text as a string.
	std::string wrap_text_at_underscore(const std::string& text, float wrap_width);


	// @brief Sets the position of the next ImGui window based on a predefined location.
	// @param [location] The desired position of the window (e.g., center, top-left).
	// @param [padding] The padding to apply around the window.
	void set_next_window_pos(window_pos location, f32 padding = 10.f);


	// @brief Sets the position of the next ImGui window relative to the current window.
	// @param [location] The desired position of the window (e.g., center, top-left).
	// @param [padding] The padding to apply around the window.
	void set_next_window_pos_in_window(window_pos location, f32 padding = 10.f);


	// @brief Displays a menu to select the position of the next ImGui window.
	// @param [position] The current position of the window, which can be modified.
	// @param [show_window] A boolean flag to control the visibility of the window.
	void next_window_position_selector(window_pos& position, bool& show_window);


	// @brief Displays a popup menu to select the position of the next ImGui window.
	// @param [position] The current position of the window, which can be modified.
	// @param [show_window] A boolean flag to control the visibility of the window.
	void next_window_position_selector_popup(window_pos& position, bool& show_window);

	
	void adjust_popup_to_window_bounds(const ImVec2 expected_popup_size);


	// @brief Draws a vertical separation line.
	void separation_vertical();


	// @brief Creates a button with a gray color scheme.
	// @param [label] The label displayed on the button.
	// @param [size] The size of the button. If {0, 0}, the size is automatically calculated.
	// @return True if the button is clicked, false otherwise.
	bool gray_button(const char* label, const ImVec2& size = { 0, 0 });


	bool transparent_button(const char* label, const ImVec2& size = { 0, 0 });


	// @brief Creates a toggle button that changes its appearance based on a boolean variable.
	// @param [bool_var] The boolean variable that controls the button's state.
	// @return True if the button is clicked, false otherwise.
	bool toggle_button(const bool bool_var);


	// @brief Draws text using a larger font.
	// @param [text] The text to be drawn.
	void big_text(const char* text, bool wrapped = false);


	// @brief Displays text in a bold font.
	// @param [text] The text to display.
	// @param [wrapped] Whether the text should be wrapped if it exceeds the available width.
	void text_bold(const char* text, bool wrapped = false);


	// @brief Displays text in an italic font.
	// @param [text] The text to display.
	// @param [wrapped] Whether the text should be wrapped if it exceeds the available width.
	void text_italic(const char* text, bool wrapped = false);


	// @brief Displays text with a specific style
	// @param [text] The text to display.
	void ansi_text(std::string_view text);


	// @brief Displays a help marker with tooltip containing the provided description.
	// @param [desc] The description text to be displayed in the tooltip.
	void help_marker(const char* desc);


	// @brief Adjusts the current ImGui cursor position by adding the specified horizontal and vertical shift offsets.
	// @param [shift_x] The horizontal shift offset.
	// @param [shift_y] The vertical shift offset.
	void shift_cursor_pos(const f32 shift_x, const f32 shift_y);


	// @brief Adjusts the current ImGui cursor position by adding the specified horizontal and vertical shift offsets.
	// @param [shift_x] The horizontal shift offset.
	// @param [shift_y] The vertical shift offset.
	void shift_cursor_pos(const ImVec2 shift);


    bool draw_dir_tree(const std::filesystem::path& dir, const std::filesystem::path& current, std::filesystem::path& out_selected);


	void progressbar_with_text(const char* label, const char* progress_bar_text, f32 percent, f32 label_size = 50.f, 
		f32 progressbar_size_x = 50.f, f32 progressbar_size_y = 1.f);


	bool draw_directory_picker(const char* id, std::filesystem::path& in_out, const std::filesystem::path& root);


	// @brief This function sets up an ImGui table with two columns, where the first column is resizable and the second column fills the remaining available area
	// @brief CAUTION - you need to call UI::end_table() at the end of the table;
	// @param [label] Is used to identify the table
	bool begin_table(std::string_view label, bool display_name = true, ImVec2 size = ImVec2(0,0), f32 inner_width = 0.0f, 
		bool set_columns_width = true, f32 columns_width_percentage = 0.5f);


	// @brief Ends the table started with UI::begin_table().
	void end_table();


	// @brief Creates a custom frame with a left and right side, allowing for resizing and custom coloring.
	// @param [width_left_side] The width of the left side panel.
	// @param [can_resize] Whether the left side panel can be resized.
	// @param [color_left_side] The background color of the left side panel.
	// @param [left_side] A function to render the content of the left side panel.
	// @param [right_side] A function to render the content of the right side panel.
	void custom_frame(const f32 width_left_side, const bool can_resize, const ImU32 color_left_side, 
		std::function<void()> left_side, std::function<void()> right_side);


	// @brief Creates a search input field with a clear button.
	// @param [label] The label for the search input field.
	// @param [search_text] A reference to the string that holds the search text.
	// @return true if the search text was changed, false otherwise.
	bool search_input(const char* label, std::string& search_text);


	// @brief Renders an integer slider within a table row in an ImGui interface.
	// 
	// This function creates a row in an ImGui table, sets the label in the first column,
	// and places an integer slider in the second column. The slider modifies the provided
	// integer value within the specified range.
	// 
	// @param label The text label for the slider, displayed in the first column of the table.
	// @param value A reference to the integer value to be modified by the slider.
	// @param min_value The minimum value for the slider.
	// @param max_value The maximum value for the slider.
	// @param flags Optional ImGui input text flags.
	// 
	// @return true if the value was changed by the slider, false otherwise.
	bool table_row_slider_int(std::string_view label, int& value, int min_value = 0, int max_value = 1, 
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
	
	
	bool table_row_slider_color(std::string_view label, glm::vec4& value, f32 min_value = 0.f, f32 max_value = 1.f, 
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);

	
	void color_picker(ImVec4& color);


	// @brief Renders a table row with two columns, each containing custom content.
	// @param [first_column] A function to render the content of the first column.
	// @param [second_column] A function to render the content of the second column.
	void table_row(std::function<void()> first_column, std::function<void()> second_column);


	// @brief Renders a table row with a label and an editable text field.
	// @param [label] The label for the row.
	// @param [text] A reference to the string that holds the text.
	// @param [enable_input] A reference to a boolean that controls whether the text field is editable.
	bool table_row(std::string_view label, std::string& text, bool& enable_input, const bool allow_space_as_input = true, 
		const char* desc = nullptr);


	// @brief Renders a table row with a label and formatted text.
	// @param [label] The label for the row.
	// @param [format] The format string for the text.
	// @param [...] Variable arguments for the format string.
	void table_row_text(std::string_view label, const char* format, ...);


	// @brief Renders a table row with a label and a checkbox.
	// @param [label] The label for the row.
	// @param [value] A reference to the boolean value controlled by the checkbox.
	bool table_row(std::string_view label, bool& value);


	// @brief Renders a table row with a label and a non-editable text value.
	// @param [label] The label for the row.
	// @param [value] The text value to display.
	void table_row(std::string_view label, std::string_view value);


	// @brief Renders a table row with a label and a 4x4 matrix, allowing for editing of translation, rotation, and scale.
	// @param [label] The label for the row.
	// @param [value] A reference to the 4x4 matrix to be edited.
	// @return true if any component of the matrix was changed, false otherwise.
	bool table_row(glm::mat4& value, const bool display_in_degree = false);


	// @brief Renders a table row with a label and a directory picker.
	// The picker displays `in_out` relative to `root` when possible, and opens its popup as a dropdown anchored beneath the button (matching its width)
	// @returns true if the selection changed this frame.
	bool table_row(std::string_view label, std::filesystem::path& in_out, const std::filesystem::path& root, const char* desc = nullptr);


	// @brief Renders a table row with a label and a progress bar.
	// @param [label] The label for the row.
	// @param [progress_bar_text] The text to display alongside the progress bar.
	// @param [percent] The percentage value of the progress bar.
	// @param [auto_resize] Whether the progress bar should automatically resize to fit the column width.
	// @param [progressbar_size_x] The width of the progress bar.
	// @param [progressbar_size_y] The height of the progress bar.
	void table_row_progressbar(std::string_view label, const char* progress_bar_text, const f32 percent, 
		const bool auto_resize = true, const f32 progressbar_size_x = 50.f, const f32 progressbar_size_y = 1.f);


	bool table_row_color(std::string_view label, glm::vec4& color, const f32 min_value = 0.f, const f32 max_value = 0.f);


	bool table_row_color(std::string_view label, glm::vec3& color, const f32 min_value = 0.f, const f32 max_value = 0.f);


	// @brief Begins a collapsible header section with an indent.
	// @param [label] The label for the collapsible header.
	// @return true if the header is open, false otherwise.
	bool begin_collapsing_header_section(const char* label);


	// @brief Ends a collapsible header section and removes the indent.
	void end_collapsing_header_section();

	// TEMPLATE DECLARATION ============================================================================================

	// @brief Renders a table row with a label and a combo box for enum selection.
	// @param [label] The label for the row.
	// @param [current_value] A reference to the current enum value (will be used to find the current index).
	// @param [options] A vector of string options for the enum values.
	// @param [on_changed] Optional callback function that gets called when the selection changes.
	// @return true if the value was changed, false otherwise.
	template<typename T>
	bool table_row(std::string_view label, T& current_value, const std::vector<std::string>& options, const char* desc = nullptr, 
		std::function<void(T)> on_changed = nullptr);


	// @brief Renders a table row with a label and a combo box for enum selection with custom value mapping.
	// @param [label] The label for the row.
	// @param [current_value] A reference to the current enum value.
	// @param [options] A vector of string options for the enum values.
	// @param [value_getter] Function to convert enum value to index.
	// @param [value_setter] Function to convert index to enum value.
	// @param [on_changed] Optional callback function that gets called when the selection changes.
	// @return true if the value was changed, false otherwise.
	template<typename T, typename Container>
	bool table_row(std::string_view label, T& current_value, const Container& options, const char* desc = nullptr, 
		std::function<void(T)> on_changed = nullptr);


    template<typename T, typename Container>
    bool table_row(std::string_view label, T& current_value, const Container& options, bool* p_removed, const char* desc = nullptr, 
		std::function<void(T)> on_changed = nullptr);


	// @brief Adds a row to an ImGui table with a label and corresponding value input field.
	// @tparam [T] The type of the value.
	// @param [label] The label for the row.
	// @param [value] The value to be displayed or edited.
	// @param [flags] Flags controlling the behavior of the input field.
	template<typename T>
	bool table_row(std::string_view label, T& value, f32 drag_speed = .05f, std::optional<f32> min_value = std::nullopt,
		std::optional<f32> max_value = std::nullopt, ImGuiSliderFlags flags = ImGuiSliderFlags_None);


	template <typename E, std::size_t N>
	requires std::is_enum_v<E>
	bool table_row(std::string_view label, E& current_value, const std::array<E, N>& options, const char* desc = nullptr,
		std::function<void(E)> on_changed = nullptr);


	// @brief Renders a slider within a table row in an ImGui interface.
	// 
	// This function creates a row in an ImGui table, sets the label in the first column,
	// and places a slider in the second column. The type of the slider is determined by
	// the type of the value parameter.
	// 
	// @tparam T The type of the value to be modified by the slider. Supported types are:
	// @tparam      - int
	// @tparam      - f32 (float)
	// @tparam      - f64 (double)
	// @tparam      - glm::vec2
	// @tparam      - glm::vec3
	// @tparam      - glm::vec4
	// @tparam      - ImVec2
	// @tparam      - ImVec4
	// 
	// @param label The text label for the slider, displayed in the first column of the table.
	// @param value A reference to the value to be modified by the slider.
	// @param min_value The minimum value for the slider. Defaults to 0.f.
	// @param max_value The maximum value for the slider. Defaults to 1.f.
	// @param flags Optional ImGui input text flags. Defaults to ImGuiInputTextFlags_None.
	// 
	// @return true if the value was changed by the slider, false otherwise.
	template<typename T>
	bool table_row_slider(std::string_view label, T& value, f32 min_value = 0.f, f32 max_value = 0.f,
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
	
	
	template<typename T>
	bool table_row_drag_scalar(std::string_view label, T& value, const char* format, T min_value = (T)0, T max_value = (T)1, 
	f32 draw_speed = 0.2f, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);

	// ---- modern controls ----------------------------------------------------------------------------------------------

	// Pill shaped on/off switch (animated). Returns true when toggled
	bool toggle_switch(const char* id, bool& value);


	// Row of mutually exclusive buttons ("Dark | Light"). [width] <= 0 uses SetNextItemWidth / the available width
	bool segmented_control(const char* id, i32& index, std::span<const char* const> options, f32 width = 0.f);


	// Grid of preset colors; clicking one assigns it to [color] (alpha is kept). Returns true on change
	bool color_palette(ImVec4& color);


	// Small circular-arrow button (used by settings rows to reset a value)
	bool reset_button(const char* id, const f32 size, const char* tooltip = "Reset to default");

	// Accent-colored button for the main action of a panel ("+ Add Component")
	bool accent_button(const char* label, const ImVec2& size = { 0, 0 });


	// Quiet gray button for secondary actions ("Reset", "Clear")
	bool subtle_button(const char* label, const ImVec2& size = { 0, 0 });


	// Rounded search box with a magnifier and a clear button. Returns true when the text changed. [width] <= 0 = available width
	bool search_field(const char* id, char* buffer, const size_t buffer_size, const char* hint = "Search", f32 width = 0.f);


	// X / Y / Z / W drag fields side by side, each marked with its axis color. [width] <= 0 uses CalcItemWidth()
	bool drag_axes(const char* id, f32* values, const int components, const f32 speed, const f32 min_value = 0.f, const f32 max_value = 0.f,
		const char* format = "%.2f", f32 width = 0.f);


	// Round "..." button for per-item menus. Returns true when clicked
	bool menu_button(const char* id, const f32 size = 0.f, const char* tooltip = nullptr);


	// Centered hint for empty panels ("No entity selected")
	void empty_state(const char* title, const char* hint = nullptr);


	// "ambient_light" / "audio source" -> "Ambient Light" / "Audio Source"
	[[nodiscard]] std::string pretty_name(std::string_view identifier);

	// ---- cards --------------------------------------------------------------------------------------------------------
	//
	// A card is a rounded panel that groups related content. Always pair begin_card() with end_card(), even when
	// begin_card() returns false (collapsed).
	//
	//      UI::card_options options{ .collapsible = true, .summary = "3 overrides" };
	//      if (UI::begin_card("Mesh", options)) {
	//          UI::property_toggle("Visible", mesh.visible);
	//      }
	//      UI::end_card();

	struct card_options {

		const char*							description = nullptr;		// dim line under the title
		const char*							summary = nullptr;			// dim text right of the title (useful when collapsed)
		bool								collapsible = false;		// header toggles the content, state is remembered
		bool								default_open = true;
		bool								title_above = false;		// title + description above the card instead of in a header
		ImTextureRef						icon{};						// optional icon in the header (has_icon = true)
		ImVec2								icon_uv0{ 0.f, 0.f };
		ImVec2								icon_uv1{ 1.f, 1.f };
		bool								has_icon = false;
		f32									header_actions_width = 0.f;	// space reserved on the right of the header
		std::function<void()>				header_actions{};			// draws buttons into that space ("...")
	};

	// @return true if the content is visible (not collapsed)
	bool begin_card(const char* title, const card_options& options = {});
	void end_card();

	// ---- property rows ------------------------------------------------------------------------------------------------
	//
	// One row = label + control. Two layouts:
	//
	//   compact    (default, inspectors / popups)      | Label      [ control                    ] |
	//                                                  description shows as tooltip on the label
	//
	//   described  (settings pages)                    | Label                    (reset) [ control ] |
	//                                                  | Description, dimmer and wrapped            |
	//
	// Every row takes an optional default value. When the value differs from it the row gets an accent marker and a
	// reset arrow, and reset_properties() puts it back. Rows work anywhere; begin_properties() only changes the layout /
	// adds a search filter for the rows until end_properties().

	enum class property_layout : u8 {
		compact = 0,
		described,
	};

	void begin_properties(const property_layout layout = property_layout::compact, std::string_view filter = {});

	// @return number of rows that were drawn (rows hidden by the filter don't count)
	u32 end_properties();

	// Every row drawn until end_properties() is set back to its default value (rows report this as a change)
	void reset_properties();


	bool property_toggle(const char* label, bool& value, const char* desc = nullptr, const bool* default_value = nullptr);

	bool property_slider(const char* label, f32& value, const f32 min_value, const f32 max_value, const char* format = "%.2f",
		const char* desc = nullptr, const f32* default_value = nullptr, ImGuiSliderFlags flags = ImGuiSliderFlags_None);

	bool property_slider(const char* label, i32& value, const i32 min_value, const i32 max_value, const char* format = "%d",
		const char* desc = nullptr, const i32* default_value = nullptr, ImGuiSliderFlags flags = ImGuiSliderFlags_None);

	// min == max = unbounded
	bool property_drag(const char* label, f32& value, const f32 speed = 0.1f, const f32 min_value = 0.f, const f32 max_value = 0.f,
		const char* format = "%.2f", const char* desc = nullptr, const f32* default_value = nullptr);

	bool property_drag(const char* label, glm::vec2& value, const f32 speed = 0.1f, const f32 min_value = 0.f, const f32 max_value = 0.f,
		const char* format = "%.2f", const char* desc = nullptr, const glm::vec2* default_value = nullptr);

	bool property_drag(const char* label, glm::vec3& value, const f32 speed = 0.1f, const f32 min_value = 0.f, const f32 max_value = 0.f,
		const char* format = "%.2f", const char* desc = nullptr, const glm::vec3* default_value = nullptr);

	bool property_combo(const char* label, i32& index, std::span<const char* const> options, const char* desc = nullptr,
		const i32* default_index = nullptr);

	// Best for 2-4 short options
	bool property_segmented(const char* label, i32& index, std::span<const char* const> options, const char* desc = nullptr,
		const i32* default_index = nullptr);

	// Swatch with hex code; click opens a picker + palette
	bool property_color(const char* label, ImVec4& color, const bool alpha = false, const char* desc = nullptr, const ImVec4* default_color = nullptr);
	bool property_color(const char* label, glm::vec4& color, const bool alpha = false, const char* desc = nullptr, const glm::vec4* default_color = nullptr);
	bool property_color(const char* label, glm::vec3& color, const char* desc = nullptr, const glm::vec3* default_color = nullptr);

	bool property_text(const char* label, std::string& value, const char* desc = nullptr, const char* hint = nullptr);

	// Read-only value
	void property_value(const char* label, std::string_view value, const char* desc = nullptr);

	// Anything else: [control] draws ONE widget group of the given width and returns true on change
	bool property_custom(const char* label, const std::function<bool(f32 width)>& control, const char* desc = nullptr,
		const bool is_default = true, const std::function<void()>& reset = {});

	// Enum combo with prettified enumerator names ("static_body" -> "Static Body")
	template<typename E> requires std::is_enum_v<E>
	bool property_enum(const char* label, E& value, const char* desc = nullptr, const E* default_value = nullptr);

	template<typename E> requires std::is_enum_v<E>
	bool property_combo(const char* label, E& value, std::span<const char* const> options, const char* desc = nullptr, const E* default_value = nullptr);

	template<typename E> requires std::is_enum_v<E>
	bool property_segmented(const char* label, E& value, std::span<const char* const> options, const char* desc = nullptr, const E* default_value = nullptr);

	// ---- settings pages -----------------------------------------------------------------------------------------------
	//
	// Thin layer over cards + described property rows, with search over pages and sections:
	//
	//      UI::settings_begin_page("Viewport", filter, "How the 3D viewport behaves");
	//          UI::settings_begin_section("Camera");
	//              changed |= UI::settings_slider("Fly speed", "Units per frame", cfg.speed, defaults.speed, 0.01f, 5.f);
	//          UI::settings_end_section();
	//      const u32 hits = UI::settings_end_page();
	//
	// Sections only appear when one of their rows passes the filter.

	// [title] null = no page header (e.g. settings drawn inside a popup)
	void settings_begin_page(const char* title, std::string_view filter = {}, const char* description = nullptr);

	// @return number of rows that were drawn
	u32 settings_end_page();

	void settings_reset_page();

	void settings_begin_section(const char* title, const char* description = nullptr);
	void settings_end_section();

	bool settings_toggle(const char* label, const char* desc, bool& value, const bool default_value);

	bool settings_slider(const char* label, const char* desc, f32& value, const f32 default_value, const f32 min_value, const f32 max_value,
		const char* format = "%.2f", ImGuiSliderFlags flags = ImGuiSliderFlags_None);

	bool settings_slider(const char* label, const char* desc, i32& value, const i32 default_value, const i32 min_value, const i32 max_value,
		const char* format = "%d", ImGuiSliderFlags flags = ImGuiSliderFlags_None);

	bool settings_drag(const char* label, const char* desc, f32& value, const f32 default_value, const f32 speed,
		const f32 min_value = 0.f, const f32 max_value = 0.f, const char* format = "%.2f");

	bool settings_drag(const char* label, const char* desc, glm::vec2& value, const glm::vec2& default_value, const f32 speed,
		const f32 min_value = 0.f, const f32 max_value = 0.f, const char* format = "%.2f");

	bool settings_drag(const char* label, const char* desc, glm::vec3& value, const glm::vec3& default_value, const f32 speed,
		const f32 min_value = 0.f, const f32 max_value = 0.f, const char* format = "%.2f");

	bool settings_combo(const char* label, const char* desc, i32& index, const i32 default_index, std::span<const char* const> options);

	bool settings_segmented(const char* label, const char* desc, i32& index, const i32 default_index, std::span<const char* const> options);

	bool settings_color(const char* label, const char* desc, ImVec4& color, const ImVec4& default_color, const bool alpha = false);

	bool settings_color(const char* label, const char* desc, glm::vec4& color, const glm::vec4& default_color, const bool alpha = false);

	bool settings_custom(const char* label, const char* desc, const bool is_default, const std::function<void()>& reset,
		const std::function<bool()>& control);

	template<typename E> requires std::is_enum_v<E>
	bool settings_combo(const char* label, const char* desc, E& value, const E default_value, std::span<const char* const> options);

	template<typename E> requires std::is_enum_v<E>
	bool settings_segmented(const char* label, const char* desc, E& value, const E default_value, std::span<const char* const> options);

	// CLASS DECLARATION ===============================================================================================

}

#include "panel_collection.inl"
