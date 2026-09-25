
#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

#include "application.h"
#include "util/data_structures/UUID.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::UI {

	// CONSTANTS =======================================================================================================

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

		// DONT CHANGE ORDER OF: LEFT / RIGHT / MIDDLE ENTRYS   else change refrence in: util/ui/pannel_collection/set_mouse_interaction_state()
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


	void progressbar_with_text(const char* label, const char* progress_bar_text, f32 percent, f32 label_size = 50.f, 
		f32 progressbar_size_x = 50.f, f32 progressbar_size_y = 1.f);


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
	void table_row(std::string_view label, bool& value);


	// @brief Renders a table row with a label and a non-editable text value.
	// @param [label] The label for the row.
	// @param [value] The text value to display.
	void table_row(std::string_view label, std::string_view value);


	// @brief Renders a table row with a label and a 4x4 matrix, allowing for editing of translation, rotation, and scale.
	// @param [label] The label for the row.
	// @param [value] A reference to the 4x4 matrix to be edited.
	// @return true if any component of the matrix was changed, false otherwise.
	bool table_row(glm::mat4& value, const bool display_in_degree = false);


	// @brief Renders a table row with a label and a progress bar.
	// @param [label] The label for the row.
	// @param [progress_bar_text] The text to display alongside the progress bar.
	// @param [percent] The percentage value of the progress bar.
	// @param [auto_resize] Whether the progress bar should automatically resize to fit the column width.
	// @param [progressbar_size_x] The width of the progress bar.
	// @param [progressbar_size_y] The height of the progress bar.
	void table_row_progressbar(std::string_view label, const char* progress_bar_text, const f32 percent, 
		const bool auto_resize = true, const f32 progressbar_size_x = 50.f, const f32 progressbar_size_y = 1.f);


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
	bool table_row(std::string_view label, T& value, f32 drag_speed = 0.01f, T min_value = T{0}, T max_value = T{1}, 
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);

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
	bool table_row_slider(std::string_view label, T& value, f32 min_value = 0.f, f32 max_value = 0.f, f32 draw_speed = 0.2f, 
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
	
	
	template<typename T>
	bool table_row_drag_scalar(std::string_view label, T& value, const char* format, T min_value = (T)0, T max_value = (T)1, 
	f32 draw_speed = 0.2f, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);

	// CLASS DECLARATION ===============================================================================================

}

#include "pannel_collection.inl"
