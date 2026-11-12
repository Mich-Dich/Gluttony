
#include "util/pch.h"
#include "panel_collection.h"

#include <emmintrin.h>    // SSE2
#include <immintrin.h>    // for future‑proofing (AVX)

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::UI {

	// CONSTANTS =======================================================================================================

	// MACROS ==========================================================================================================

	#define GRAY(value)											IM_COL32(value, value, value, 255);

	// TYPES ===========================================================================================================

	struct PickerState {
		bool 													open = false;
		bool 													just_opened = false;
	};

	// STATIC VARIABLES ================================================================================================

	static std::unordered_map<std::string, PickerState> 		g_picker_states{};

	// INTERNAL TEMPLATE DECLARATION ===================================================================================

	// INTERNAL FUNCTION DECLARATION ===================================================================================

	FORCE_INLINE bool is_delim_char(char c);

	FORCE_INLINE ImVec4 get_background_8_color(int index);

	FORCE_INLINE ImVec4 get_background_16_color(int index);

	FORCE_INLINE ImVec4 get_8_color(int index);

	FORCE_INLINE ImVec4 get_16_color(int index);

	FORCE_INLINE ImVec4 get_256_color(int index);

	void render_text_segment(const char* text, int length, ImVec4 fg_color, ImVec4 bg_color);

    std::vector<std::filesystem::path> list_subdirs(const std::filesystem::path& dir);

	// INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

	// INTERNAL FUNCTION IMPLEMENTATION ================================================================================
	
	FORCE_INLINE bool is_delim_char(char c) { return c==' '||c=='_'||c=='-'||c=='/'||c=='\\'||c=='.'; }

	
	FORCE_INLINE ImVec4 get_background_8_color(int index) {
		const static ImVec4 colors[] = {
			ImVec4(0.0f, 0.0f, 0.0f, 1.0f),         // 40: Black
			ImVec4(0.5f, 0.0f, 0.0f, 1.0f),         // 41: Red
			ImVec4(0.0f, 0.5f, 0.0f, 1.0f),         // 42: Green
			ImVec4(0.5f, 0.5f, 0.0f, 1.0f),         // 43: Yellow
			ImVec4(0.0f, 0.0f, 0.5f, 1.0f),         // 44: Blue
			ImVec4(0.5f, 0.0f, 0.5f, 1.0f),         // 45: Magenta
			ImVec4(0.0f, 0.5f, 0.5f, 1.0f),         // 46: Cyan
			ImVec4(0.75f, 0.75f, 0.75f, 1.0f),      // 47: White
		};
		if (index >= 0 && index < 8)
			return colors[index];
		return ImVec4(0.0f, 0.0f, 0.0f, 0.0f);      // Transparent default
	}


	FORCE_INLINE ImVec4 get_background_16_color(int index) {
		const static ImVec4 colors[] = {
			ImVec4(0.5f, 0.5f, 0.5f, 1.0f),        // 100: Bright Black (Gray)
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),        // 101: Bright Red
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f),        // 102: Bright Green
			ImVec4(1.0f, 1.0f, 0.0f, 1.0f),        // 103: Bright Yellow
			ImVec4(0.0f, 0.0f, 1.0f, 1.0f),        // 104: Bright Blue
			ImVec4(1.0f, 0.0f, 1.0f, 1.0f),        // 105: Bright Magenta
			ImVec4(0.0f, 1.0f, 1.0f, 1.0f),        // 106: Bright Cyan
			ImVec4(1.0f, 1.0f, 1.0f, 1.0f),        // 107: Bright White
		};
		if (index >= 0 && index < 8)
			return colors[index];
		return ImVec4(0.0f, 0.0f, 0.0f, 0.0f);      // Transparent default
	}


	FORCE_INLINE ImVec4 get_8_color(int index) {
		const static ImVec4 colors[] = {
			ImVec4(0.0f, 0.0f, 0.0f, 1.0f),         // 0: Black
			ImVec4(0.9f, 0.1f, 0.1f, 1.0f),         // 1: Red
			ImVec4(0.0f, 0.5f, 0.0f, 1.0f),         // 2: Green
			ImVec4(0.9f, 0.9f, 0.1f, 1.0f),         // 3: Yellow
			ImVec4(0.0f, 0.0f, 0.5f, 1.0f),         // 4: Blue
			ImVec4(0.5f, 0.0f, 0.5f, 1.0f),         // 5: Magenta
			ImVec4(0.0f, 0.5f, 0.5f, 1.0f),         // 6: Cyan
			ImVec4(0.75f, 0.75f, 0.75f, 1.0f),      // 7: White
		};
		if (index >= 0 && index < 8)
			return colors[index];
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}


	FORCE_INLINE ImVec4 get_16_color(int index) {
		const static ImVec4 colors[] = {
			ImVec4(0.5f, 0.5f, 0.5f, 1.0f),         // 8: Bright Black (Gray)
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f),         // 9: Bright Red
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f),         // 10: Bright Green
			ImVec4(1.0f, 1.0f, 0.0f, 1.0f),         // 11: Bright Yellow
			ImVec4(0.0f, 0.0f, 1.0f, 1.0f),         // 12: Bright Blue
			ImVec4(1.0f, 0.0f, 1.0f, 1.0f),         // 13: Bright Magenta
			ImVec4(0.0f, 1.0f, 1.0f, 1.0f),         // 14: Bright Cyan
			ImVec4(1.0f, 1.0f, 1.0f, 1.0f),         // 15: Bright White
		};
		if (index >= 8 && index < 16)
			return colors[index - 8];
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}


	FORCE_INLINE ImVec4 get_256_color(int index) {

		index = math::clamp(index, 0, 255);
		if (index < 16)
			return index < 8 ? get_8_color(index) : get_16_color(index);

		else if (index < 232) {

			const int cube_index = index - 16;
			const int r = cube_index / 36;
			const int g = (cube_index % 36) / 6;
			const int b = cube_index % 6;
			const int values[] = { 0, 95, 135, 175, 215, 255 };
			return ImVec4(values[r] / 255.0f, values[g] / 255.0f, values[b] / 255.0f, 1.0f);

		}
		else {

			const int gray = 8 + (index - 232) * 10;
			const float intensity = gray / 255.0f;
			return ImVec4(intensity, intensity, intensity, 1.0f);
		}
	}


	void render_text_segment(const char* text, int length, ImVec4 fg_color, ImVec4 bg_color) {

		ImVec2 pos = ImGui::GetCursorScreenPos();
		const ImVec2 text_size = ImGui::CalcTextSize(text, text + length);

		if (bg_color.w > 0.0f)                                                                                                                  // Draw background if needed
			ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + text_size.x, pos.y + text_size.y), ImGui::GetColorU32(bg_color));

		ImGui::TextColored(fg_color, "%.*s", length, text);
		ImGui::SameLine(0, 0);
	}


    std::vector<std::filesystem::path> list_subdirs(const std::filesystem::path& dir) {

        std::vector<std::filesystem::path> out;
        std::error_code error{};
        auto it = GLT::vfs::directory_iterator(dir, error);
        if (error)
            return out;

        for (auto& e : it) {
            if (!e.is_directory(error))
                continue;

            const auto name = e.path().filename().string();
            if (!name.empty() && name.front() == '.')
                continue;

            out.push_back(e.path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

	// TEMPLATE IMPLEMENTATION =========================================================================================

	// FUNCTION IMPLEMENTATION =========================================================================================

	bool is_hovering_window() {

		const ImVec2 mouse_pos = ImGui::GetMousePos();
		const ImVec2 popup_pos = ImGui::GetWindowPos();
		const ImVec2 popup_size = ImGui::GetWindowSize();
		return (mouse_pos.x >= popup_pos.x && mouse_pos.x <= popup_pos.x + popup_size.x && mouse_pos.y >= popup_pos.y && mouse_pos.y <= popup_pos.y + popup_size.y);
	}


	bool is_item_double_clicked() {

		const ImVec2 item_pos = ImGui::GetItemRectMin();
		const ImVec2 item_max{item_pos.x + ImGui::GetItemRectSize().x, item_pos.y + ImGui::GetItemRectSize().y};
		return ImGui::IsMouseHoveringRect(item_pos, item_max) && ImGui::IsMouseDoubleClicked(0);
	}


	void set_mouse_interaction_state(const ImGuiMouseButton_ mouse_button, mouse_interation& state, bool& is_button_down) {

		u8 offset = 0;
		if (mouse_button == ImGuiMouseButton_Right)
			offset = 4;

		if (mouse_button == ImGuiMouseButton_Middle)
			offset = 8;

		// Check for left mouse button interactions
		if (ImGui::IsMouseDoubleClicked(mouse_button))
			state = static_cast<mouse_interation>(static_cast<u8>(mouse_interation::left_double_clicked) + offset);
			
		else if (ImGui::IsMouseClicked(mouse_button))
			state = static_cast<mouse_interation>(static_cast<u8>(mouse_interation::left_clicked) + offset);

		else if (ImGui::IsMouseDragging(mouse_button))
			state = mouse_interation::dragged;

		else if (ImGui::IsMouseDown(mouse_button)) {
			is_button_down = true;
			state = static_cast<mouse_interation>(static_cast<u8>(mouse_interation::left_pressed) + offset);
		
		} else if (ImGui::IsMouseReleased(mouse_button) && is_button_down) {
			is_button_down = false;
			state = static_cast<mouse_interation>(static_cast<u8>(mouse_interation::left_released) + offset);
		}
	}


	mouse_interation get_mouse_interation_on_item(const bool block_input) {

		if (block_input)
			return mouse_interation::none;

		// Must be called right after widget to test. IsItemHovered() respects window z-order and popups
		if (!ImGui::IsItemHovered())
			return mouse_interation::none;

		static bool is_middle_button_down = false;
		static bool is_right_button_down = false;
		static bool is_left_button_down = false;

		mouse_interation state = mouse_interation::hovered;

		if (ImGui::IsItemFocused())
			state = mouse_interation::focused;

		if (ImGui::IsItemActive())
			state = mouse_interation::active;

		if (ImGui::IsItemDeactivated())
			state = mouse_interation::deactivated;

		if (ImGui::IsItemDeactivatedAfterEdit())
			state = mouse_interation::deactivated_after_edit;

		set_mouse_interaction_state(ImGuiMouseButton_Middle, state, is_middle_button_down);
		set_mouse_interaction_state(ImGuiMouseButton_Right, state, is_right_button_down);
		set_mouse_interaction_state(ImGuiMouseButton_Left, state, is_left_button_down);

		return state;
	}


	mouse_interation get_mouse_interation_on_window() {

		static bool is_middle_button_down = false;
		static bool is_right_button_down = false;
		static bool is_left_button_down = false;

		// If the mouse is not hovering over the item, return none
		const ImVec2 item_pos = ImGui::GetWindowPos();
		const ImVec2 item_max{item_pos.x + ImGui::GetWindowSize().x, item_pos.y + ImGui::GetWindowSize().y};
		if (!ImGui::IsMouseHoveringRect(item_pos, item_max))
			return mouse_interation::none;

		mouse_interation state = mouse_interation::hovered;								// default to hover state

		if (ImGui::IsItemFocused())
			state = mouse_interation::focused;

		if (ImGui::IsItemActive())
			state = mouse_interation::active;

		if (ImGui::IsItemDeactivated())
			state = mouse_interation::deactivated;

		if (ImGui::IsItemDeactivatedAfterEdit())
			state = mouse_interation::deactivated_after_edit;
			
		set_mouse_interaction_state(ImGuiMouseButton_Middle, state, is_middle_button_down);			// least importent one first
		set_mouse_interaction_state(ImGuiMouseButton_Right, state, is_right_button_down);
		set_mouse_interaction_state(ImGuiMouseButton_Left, state, is_left_button_down);

		return state;
	}


	void wrap_text(std::string& string, f32 wrap_width, int max_lines) {

		size_t orig = string.size();
		string.reserve(orig * 2 + 16);
		size_t read_pos = 0, write_pos = 0;
		size_t line_start = 0;
		size_t last_delim = std::string::npos;
		int    line_count = 0;
	
		// // SIMD constants
		// constexpr __m128i space = _mm_set1_epi8(' ');
		// constexpr __m128i uscore = _mm_set1_epi8('_');
		// constexpr __m128i dash = _mm_set1_epi8('-');
		// constexpr __m128i slash = _mm_set1_epi8('/');
		// constexpr __m128i bslash = _mm_set1_epi8('\\');
		// constexpr __m128i dot = _mm_set1_epi8('.');
	
		auto insert_char = [&](size_t pos, char c) {

			string.insert(string.begin() + pos, c);
			write_pos++;
		};
	
		while (read_pos < string.size()) {
			size_t rem = string.size() - read_pos;
			size_t chunk = rem >= 16 ? 16 : rem;
			// __m128i block = _mm_loadu_si128((__m128i*)(string.data()+read_pos));
			// __m128i m = _mm_or_si128(
			// 	_mm_or_si128(_mm_cmpeq_epi8(block, space),  _mm_cmpeq_epi8(block, uscore)),
			// 	_mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(block, dash),
			// 							  _mm_cmpeq_epi8(block, slash)),
			// 				 _mm_or_si128(_mm_cmpeq_epi8(block, bslash),
			// 							  _mm_cmpeq_epi8(block, dot)))
			// );
	
			for (size_t i = 0; i < chunk; i++) {

				char c = string[read_pos + i];
				if (write_pos != read_pos + i) string[write_pos] = c;
					write_pos++;
				if (is_delim_char(c))
					last_delim = write_pos;
	
				// Measure only when potential overflow:
				ImVec2 string_size = ImGui::CalcTextSize(string.c_str() + line_start, string.c_str() + write_pos);
				if (string_size.x > wrap_width) {
	
					// If next line would exceed max_lines, ellipsize THIS line instead of breaking:
					if (max_lines > 0 && line_count + 1 == max_lines) {
						size_t end = write_pos;															// find end-of-line = write_pos
						while (end > line_start && ImGui::CalcTextSize(string.c_str()+line_start, string.c_str()+end).x + ImGui::CalcTextSize("...").x > wrap_width)			// back up so we can fit "..."
							--end;

						string.replace(end, std::string::npos, "...");									// replace tail with "..."
						string.resize(end + 3);															// truncate everything after
						return;
					}
	
					if (last_delim != std::string::npos && last_delim > line_start) {					// Otherwise do normal break
						insert_char(last_delim, '\n');
						line_start = last_delim + 1;
					} else {																			// forced break: replace last char with '-' then newline
						size_t brk = write_pos - 1;
						string[brk] = '-';
						insert_char(brk+1, '\n');
						line_start = brk + 2;
					}
					line_count++;
					last_delim = std::string::npos;
					break;
				}
			}
			read_pos += chunk;
		}
		string.resize(write_pos);
	}


	std::string wrap_text_at_underscore(const std::string& text, float wrap_width) {

		std::stringstream ss(text);
		std::string segment;
		std::string wrapped_text;
		float text_width = 0.0f;

		// Split the text at underscores
		while (std::getline(ss, segment, '_')) {
			// Check if adding this segment exceeds the wrap width
			if (text_width + ImGui::CalcTextSize(segment.c_str()).x > wrap_width) {
				wrapped_text += "\n"; // Insert a line break if needed
				text_width = 0.0f; // Reset the line width counter
			}
			wrapped_text += segment + "_"; // Add the segment with underscore
			text_width += ImGui::CalcTextSize((wrapped_text + "_").c_str()).x; // Update the line width
		}

		// Remove the trailing underscore
		if (!wrapped_text.empty() && wrapped_text.back() == '_') {
			wrapped_text.pop_back();
		}

		return wrapped_text;
	}


	void set_next_window_pos(window_pos location, f32 padding) {

		if (location == window_pos::center)
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		else if (static_cast<u32>(location) >= 2) {

			const f32 title_bar_height = 60.f;
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImVec2 work_pos = { viewport->WorkPos.x , viewport->WorkPos.y + title_bar_height }; // Use work area to avoid menu-bar/task-bar, if any!
			ImVec2 work_size = viewport->WorkSize;
			ImVec2 window_pos, window_pos_pivot;

			window_pos.x = (location == window_pos::top_right || location == window_pos::bottom_right) ? (work_pos.x + work_size.x - padding) : (work_pos.x + padding);
			window_pos.y = (location == window_pos::bottom_right || location == window_pos::bottom_left) ? (work_pos.y + work_size.y - padding - title_bar_height) : (work_pos.y + padding);
			window_pos_pivot.x = (location == window_pos::top_right || location == window_pos::bottom_right) ? 1.0f : 0.0f;
			window_pos_pivot.y = (location == window_pos::bottom_right || location == window_pos::bottom_left) ? 1.0f : 0.0f;
			ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
			ImGui::SetNextWindowViewport(viewport->ID);
		}
	}


	void set_next_window_pos_in_window(window_pos location, f32 padding) {

		ImVec2 pos = ImGui::GetWindowPos();
		ImVec2 size = ImGui::GetWindowSize();

		if (location == window_pos::center)
			ImGui::SetNextWindowPos(ImVec2(pos.x + (size.x / 2), pos.y + (size.y / 2)), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

		else if (static_cast<u32>(location) >= 2) {

			bool has_tab_bar = !(GImGui->CurrentWindow->Flags & ImGuiWindowFlags_NoTitleBar);

			const ImGuiViewport* viewport = ImGui::GetWindowViewport();
			ImVec2 window_pos, window_pos_pivot;

			window_pos.x = (location == window_pos::top_right || location == window_pos::bottom_right) ? (pos.x + size.x - padding) : (pos.x + padding);
			window_pos.y = (location == window_pos::bottom_right || location == window_pos::bottom_left) ? (pos.y + size.y - padding) : (pos.y + padding + (has_tab_bar ? ImGui::GetFrameHeight() : 0.f));
			window_pos_pivot.x = (location == window_pos::top_right || location == window_pos::bottom_right) ? 1.0f : 0.0f;
			window_pos_pivot.y = (location == window_pos::bottom_right || location == window_pos::bottom_left) ? 1.0f : 0.0f;
			ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
			ImGui::SetNextWindowViewport(viewport->ID);
		}
	}


	void next_window_position_selector(window_pos& position, bool& show_window) {

		if (ImGui::MenuItem("custom", NULL, position == window_pos::custom))
			position = window_pos::custom;

		if (ImGui::MenuItem("center", NULL, position == window_pos::center))
			position = window_pos::center;

		if (ImGui::MenuItem("top-left", NULL, position == window_pos::top_left))
			position = window_pos::top_left;

		if (ImGui::MenuItem("top-right", NULL, position == window_pos::top_right))
			position = window_pos::top_right;

		if (ImGui::MenuItem("bottom-left", NULL, position == window_pos::bottom_left))
			position = window_pos::bottom_left;

		if (ImGui::MenuItem("bottom-right", NULL, position == window_pos::bottom_right))
			position = window_pos::bottom_right;

		if (show_window && ImGui::MenuItem("close"))
			show_window = false;
	}


	void next_window_position_selector_popup(window_pos& position, bool& show_window) {

		if (ImGui::BeginPopupContextWindow()) {

			next_window_position_selector(position, show_window);

			ImGui::EndPopup();
		}
	}


	void adjust_popup_to_window_bounds(const ImVec2 expected_popup_size) {

		ImVec2 popup_pos = ImGui::GetMousePos();
		// ImVec2 mouse_pos = ImGui::GetMousePos();
		// ImVec2 expected_size = ImVec2(20, 50);					// TODO: Adjust based on content

		// if (file_corrupted) 
		// 	switch (loc_file_corruption_source) {					// sprocimate size bycorruption type
		// 		default:
		// 		case file_corruption_source::unknown:				expected_size = ImVec2(280, 250); break;	// should display everything to help user
		// 		case file_corruption_source::header_incorrect:		expected_size = ImVec2(280, 250); break;	// should display header
		// 		case file_corruption_source::empty_file:			expected_size = ImVec2(180, 150); break;	// doesn't need to display anything other than size
		// 	}
		
		// popup_pos = mouse_pos;
		ImVec2 window_pos = ImGui::GetWindowPos();
		ImVec2 window_size = ImGui::GetWindowSize();
		
		if ((popup_pos.x + expected_popup_size.x) > (window_pos.x + window_size.x))				// Check if popup would go out of bounds horizontally
			popup_pos.x = window_pos.x + window_size.x - expected_popup_size.x;
		
		if ((popup_pos.y + expected_popup_size.y) > (window_pos.y + window_size.y))				// Check vertical bounds
			popup_pos.y = window_pos.y + window_size.y - expected_popup_size.y;
		
		ImGui::SetNextWindowPos(popup_pos, ImGuiCond_Appearing);
	}


	void separation_vertical() {

		ImGui::SameLine();
		shift_cursor_pos(5, 0);
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);

		ImGui::SameLine();
		shift_cursor_pos(5, 0);
	}


	bool gray_button(const char* label, const ImVec2& size) {

		ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_default_gray_ref());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color_gray_hover_ref());
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, GLT::imgui_config::get_action_color_gray_active_ref());

		const bool result = ImGui::Button(label, size);

		ImGui::PopStyleColor(3);
		return result;
	}


	bool transparent_button(const char* label, const ImVec2& size) {

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{1.f, 1.f, 1.f, 0.f});
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{1.f, 1.f, 1.f, .05f});
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{1.f, 1.f, 1.f, .1f});

		const bool result = ImGui::Button(label, size);

		ImGui::PopStyleColor(3);
		return result;
	}


	bool toggle_button(const bool bool_var) {

		// show weaker color if toggle_bool is false
		if (!bool_var) {

			ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_action_color00_faded_ref());
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color00_weak_ref());
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, GLT::imgui_config::get_action_color00_default_ref());
		}

		const bool result = ImGui::Button("show Markdown", ImVec2(100, 21));

		if (!bool_var)
			ImGui::PopStyleColor(3);

		return result;
	}


	void big_text(const char* text, bool wrapped) {

		ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::regular_big));

		if (wrapped)
			ImGui::TextWrapped("%s", text);
		else
			ImGui::Text("%s", text);

		ImGui::PopFont();
	}


	void text_bold(const char* text, bool wrapped) {

		ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));

		if (wrapped)
			ImGui::TextWrapped("%s", text);
		else
			ImGui::Text("%s", text);

		ImGui::PopFont();
	}


	void text_italic(const char* text, bool wrapped) {

		ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::italic));

		if (wrapped)
			ImGui::TextWrapped("%s", text);
		else
			ImGui::Text("%s", text);

		ImGui::PopFont();
	}


	void ansi_text(std::string_view text) {

		size_t current_position = 0;
		ImVec4 color = ImVec4(1.f, 1.f, 1.f, 1.f);
		ImVec4 bg_color = ImVec4(0.f, 0.f, 0.f, 0.f);

		while (current_position < text.length()) {

			size_t ansi_start = text.find('\033', current_position);

			if (ansi_start >= text.length() || ansi_start == std::string::npos) {																// No more ANSI codes, print the remaining text
				render_text_segment(text.data() + current_position, static_cast<int>(text.length() - current_position), color, bg_color);
				break;
			}

			if (ansi_start > current_position)
				render_text_segment(text.data() + current_position, static_cast<int>(ansi_start - current_position), color, bg_color);

			current_position = ansi_start;
			const size_t ansi_end = text.find('m', ansi_start);
			VALIDATE(ansi_end != std::string::npos, break, "", "Detected ANSI escape code is not closed [{}] at pos [{}]", (text, ansi_start));

			if (ansi_start + 1 >= text.length() || text[ansi_start + 1] != '[')                                                                 // Check if ANSI code starts with '['
				current_position = ansi_start + 1;

			// Parse ANSI code
			const std::string_view code = text.substr(ansi_start, ansi_end - ansi_start);
			if (code.size() > 2 && code[0] == '\033' && code[1] == '[') {
				std::vector<int> params;
				size_t pos = 2;

				while (pos < code.size()) {
					const size_t end = code.find(';', pos);
					const auto sub = code.substr(pos, (end == std::string::npos ? code.size() : end) - pos);

					int value = 0;
					auto result = std::from_chars(sub.data(), sub.data() + sub.size(), value);
					if (result.ec == std::errc())
						params.push_back(value);

					pos = end != std::string::npos ? end + 1 : code.size();
				}

				// Process parameters
				for (size_t i = 0; i < params.size();) {
					const int p = params[i++];

					if (p == 0) {                                               // Reset
						color = ImVec4(1.f, 1.f, 1.f, 1.f);
						bg_color = ImVec4(0.f, 0.f, 0.f, 0.f);
					}
					else if (p == 1) {                                          // Bold
						color.x = std::min(color.x * 1.2f, 1.0f);
						color.y = std::min(color.y * 1.2f, 1.0f);
						color.z = std::min(color.z * 1.2f, 1.0f);
					}

					// Foreground colors
					else if (p >= 30 && p <= 37)
						color = get_8_color(p - 30);

					else if (p >= 90 && p <= 97)
						color = get_16_color(p - 90 + 8);

					else if (p == 38) {                                         // Extended foreground
						if (i < params.size()) {

							const int type = params[i++];
							if (type == 5 && i < params.size())
								color = get_256_color(params[i++]);

							else if (type == 2 && i + 2 < params.size()) {

								const f32 r = params[i++] / 255.0f;
								const f32 g = params[i++] / 255.0f;
								const f32 b = params[i++] / 255.0f;
								color = ImVec4(r, g, b, 1.0f);
							}
						}
					}

					// Background colors
					else if (p >= 40 && p <= 47)
						bg_color = get_background_8_color(p - 40);

					else if (p >= 100 && p <= 107)
						bg_color = get_background_16_color(p - 100);

					else if (p == 48) {                                         // Extended background
						if (i < params.size()) {

							const int type = params[i++];
							if (type == 5 && i < params.size())
								bg_color = get_256_color(params[i++]);

							else if (type == 2 && i + 2 < params.size()) {
								const f32 r = params[i++] / 255.0f;
								const f32 g = params[i++] / 255.0f;
								const f32 b = params[i++] / 255.0f;
								bg_color = ImVec4(r, g, b, 1.0f);
							}
						}
					}
					else if (p == 49)                                           // Reset background
						bg_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
				}
			}

			current_position = ansi_end + 1;
		}

		ImGui::NewLine();
	}


	void help_marker(const char* desc) {

		ImGui::TextDisabled(" ? ");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}


	void shift_cursor_pos(const f32 shift_x, const f32 shift_y) {

		auto current_pos = ImGui::GetCursorPos();
		ImGui::SetCursorPos({ current_pos.x + shift_x, current_pos.y + shift_y });
	}


	void shift_cursor_pos(const ImVec2 shift) {

		auto current_pos = ImGui::GetCursorPos();
		ImGui::SetCursorPos({ current_pos.x + shift.x, current_pos.y + shift.y });
	}


	void progressbar_with_text(const char* label, const char* progress_bar_text, f32 percent, f32 label_size, 
		f32 progressbar_size_x, f32 progressbar_size_y) {

		ImVec2 curser_pos;
		ImVec2 progressbar_size;
		ImVec2 text_size;
		f32 loc_label_size;

		ImGuiStyle* style = &ImGui::GetStyle();
		curser_pos = ImGui::GetCursorPos();

		loc_label_size = ImGui::CalcTextSize(label).x;
		loc_label_size = std::max<f32>(loc_label_size, label_size);
		ImGui::Text("%s", label);
		ImGui::SetCursorPos({ curser_pos.x + loc_label_size, curser_pos.y });

		text_size = ImGui::CalcTextSize(progress_bar_text);
		progressbar_size = text_size;
		progressbar_size.x = std::max<f32>(progressbar_size.x, progressbar_size_x);
		progressbar_size.y = std::max<f32>(progressbar_size.y, progressbar_size_y);

		curser_pos = ImGui::GetCursorPos();
		ImGui::ProgressBar(percent, progressbar_size, "");
		ImGui::SetCursorPos({ curser_pos.x + progressbar_size.x - (text_size.x + style->ItemSpacing.x), curser_pos.y });
		ImGui::Text("%s", progress_bar_text);
	}


    bool draw_dir_tree(const std::filesystem::path& dir, const std::filesystem::path& current, std::filesystem::path& out_selected) {

        bool picked = false;
        const auto sub = list_subdirs(dir);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        if (dir == current)
            flags |= ImGuiTreeNodeFlags_Selected;

        if (sub.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        if (!current.empty() && current != dir) {
            for (auto p = current.parent_path(); !p.empty(); p = p.parent_path()) {
                if (p == dir) {
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                    break;
                }
                if (p == p.root_path())
                    break;
            }
        }

        ImGui::PushID(dir.string().c_str());

        const std::string label = dir.filename().empty() ? dir.generic_string() : dir.filename().string();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            out_selected = dir;
            picked = true;
        }

        if (open && !sub.empty()) {
            for (const auto& s : sub)
                if (draw_dir_tree(s, current, out_selected))
                    picked = true;
            ImGui::TreePop();
        }

        ImGui::PopID();
        return picked;
    }


	bool draw_directory_picker(const char* id, std::filesystem::path& in_out, const std::filesystem::path& root) {

        bool changed = false;
        ImGui::PushID(id);

        const ImGuiStyle& style = ImGui::GetStyle();

        // ---- display: content-relative when under `root`, otherwise the full path ----
        std::string display;
        if (!root.empty()) {

            std::filesystem::path rel = in_out.lexically_relative(root);

            // Empty -> different root name (e.g. relative vs. absolute). Leading ".."
            // -> the path escapes root; show the full path instead of a ../../ chain.
            if (!rel.empty() && *rel.begin() != "..")
                display = rel.generic_string();
            else
                display = in_out.generic_string();
        }
        else
            display = in_out.generic_string();

        if (display.empty())
            display = ".";

        // ---- button: fills its column / parent width ----
        const f32 width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button(display.c_str(), ImVec2(width, 0)))
            ImGui::OpenPopup("##picker");

        // Capture the button rect now - the item will be gone by the time we draw the popup.
        const ImVec2 btn_min = ImGui::GetItemRectMin();
        const ImVec2 btn_max = ImGui::GetItemRectMax();
        const f32 btn_w = btn_max.x - btn_min.x;

        // ---- popup: dropdown anchored beneath the button, same width ----
        if (ImGui::IsPopupOpen("##picker")) {

            // Flip above if there's not enough room below the button.
            const ImGuiViewport* vp = ImGui::GetWindowViewport();
            const f32 popup_h = 300.0f;
            const bool flip_up = (vp->Pos.y + vp->Size.y - btn_max.y) < popup_h;

            ImGui::SetNextWindowPos(flip_up ? ImVec2(btn_min.x, btn_min.y) : ImVec2(btn_min.x, btn_max.y),
                ImGuiCond_Always, flip_up ? ImVec2(0.f, 1.f) : ImVec2(0.f, 0.f));

            // Force exactly the button width; let the height auto-fit up to 400px.
            ImGui::SetNextWindowSizeConstraints(ImVec2(btn_w, 0.f), ImVec2(btn_w, 400.f));
        }

        if (ImGui::BeginPopup("##picker", ImGuiWindowFlags_NoMove)) {

            if (root.empty()) {
                ImGui::TextDisabled("(no content root configured)");
            } else {

                if (ImGui::BeginChild("##tree_scroll", ImVec2(0, 300), true)) {

                    std::filesystem::path selected;
                    if (draw_dir_tree(root, in_out, selected)) {

						in_out = selected;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndChild();
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
        return changed;
    }


	bool begin_table(std::string_view label, bool display_name, ImVec2 size, f32 inner_width, bool set_columns_width, f32 columns_width_percentage) {

		if (display_name)
			ImGui::Text("%s:", label.data());

		ImGuiTableFlags flags = ImGuiTableFlags_Resizable;
		if (ImGui::BeginTable(label.data(), 2, flags, size, inner_width)) {

			// setup table and columns
			if (size.x > 0.0f && set_columns_width) {

				float column_width = size.x * columns_width_percentage;
				ImGui::TableSetupColumn("##one", ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_WidthFixed, column_width);
				ImGui::TableSetupColumn("##two", ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_WidthStretch);
			} else {

				ImGui::TableSetupColumn("##one", ImGuiTableColumnFlags_NoHeaderLabel);
				ImGui::TableSetupColumn("##two", ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_NoResize);
			}

			return true;
		}
		return false;
	}


	void end_table() { ImGui::EndTable(); }


	void custom_frame(const f32 width_left_side, const bool can_resize, const ImU32 color_left_side, std::function<void()> left_side, std::function<void()> right_side) {

		static ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
		auto content_region = ImGui::GetContentRegionAvail();
		const ImVec2 start_pos = ImGui::GetCursorScreenPos();

		// ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		// ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, color_left_side);
		ImGui::BeginChild("LEFT_SIDE", ImVec2(width_left_side, content_region.y), can_resize ? child_flags | ImGuiChildFlags_ResizeX : child_flags, ImGuiWindowFlags_None);
		ImGui::PopStyleColor();
		// ImGui::PopStyleVar(2);

		left_side();

		{
			const auto width = ImGui::GetContentRegionAvail().x;

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			ImU32 begin_col = IM_COL32(0, 0, 0, 0);
			ImU32 end_col = IM_COL32(0, 0, 0, 60);
			const f32 padding = ImGui::GetStyle().WindowPadding.x;

			draw_list->AddRectFilledMultiColor(
				ImVec2(start_pos.x + width + (padding * 2) - 40, start_pos.y-20),
				ImVec2(start_pos.x + width + (padding * 2), start_pos.y + content_region.y),
				begin_col, end_col, end_col, begin_col);
		}

		ImGui::EndChild();

		ImGui::SameLine();
		content_region = ImGui::GetContentRegionAvail();
		ImGui::BeginChild("RIGHT_SIDE", content_region, child_flags, ImGuiWindowFlags_None);

		right_side();

		ImGui::EndChild();
	}


	bool search_input(const char* label, std::string& search_text) {

		std::string buffer = search_text;
		buffer.resize(256);

		ImGui::SetNextItemAllowOverlap();
		if (ImGui::InputTextWithHint(label, "Search", buffer.data(), 255, ImGuiInputTextFlags_EnterReturnsTrue)) {

			buffer.resize(strlen(buffer.c_str()));
			search_text = buffer;
			return true;
		}

		if (!search_text.empty()) {

			ImGui::SameLine();
			UI::shift_cursor_pos(-ImGui::CalcTextSize(" X ").x - (ImGui::GetStyle().ItemSpacing.x * 3), 0);

			std::string button_text = " X ##Clear_search_query__" + std::string(label);
			if (ImGui::Button(button_text.c_str())) {

				LOG(trace, "Trigger clear button")
				search_text = "";
			}
		}

		return false;
	}


	bool table_row_slider_int(std::string_view label, int& value, int min_value, int max_value, ImGuiInputTextFlags flags) {

		flags |= ImGuiInputTextFlags_AllowTabInput;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);

		// Copy non-space characters from label to loc_label
		std::string loc_label = "##";
		loc_label.reserve(label.size() + 2);
		std::remove_copy_if(label.begin(), label.end(), std::back_inserter(loc_label), [](char c) {
			return std::isspace(static_cast<unsigned char>(c));
			});

		ImGui::SetNextItemWidth(ImGui::GetColumnWidth());
		return ImGui::SliderInt(loc_label.c_str(), &value, static_cast<int>(min_value), static_cast<int>(max_value), "%d", flags);
	}


	bool table_row_slider_color(std::string_view label, glm::vec4& value, f32 min_value, f32 max_value, ImGuiInputTextFlags flags) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());
		ImGui::TableSetColumnIndex(1);

		std::string key = std::string("picker_") + label.data();
		auto&      state = g_picker_states[key];
		const char* popup_id = (key + "_popup").c_str();
		std::string btn_id = "Pick##btn_" + key;

		if (ImGui::Button(btn_id.c_str())) {
			state.open = true;
			state.just_opened = true;
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetColumnWidth());
		bool changed = ImGui::SliderFloat4(("##slider_" + key).c_str(), &value.x, min_value, max_value, "%.2f", flags);

		if (state.open)
			ImGui::OpenPopup(popup_id);

		if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoMove)) {

			ImGui::ColorPicker4(("##picker_" + key).c_str(), &value.x, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview );
			if (state.just_opened)
				state.just_opened = false;

			else {
				bool hover_popup = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
				bool hover_button = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
				if (!hover_popup && !hover_button)
					state.open = false;
			}

			ImGui::EndPopup();
		}

		return changed;
	}


	void color_picker(ImVec4& color) {
		
		static ImVec4 backup_color{};
		static bool saved_palette_init = true;
		static ImVec4 saved_palette[35] = {};

		if (saved_palette_init) {
			for (size_t n = 0; n < ARRAY_SIZE(saved_palette); n++) {
				ImGui::ColorConvertHSVtoRGB((n / 34.f), .8f, .8f,
					saved_palette[n].x, saved_palette[n].y, saved_palette[n].z);
				saved_palette[n].w = 1.0f; // Alpha
			}
			saved_palette_init = false;
		}

		ImGui::ColorPicker4("##picker", &color.x, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);

		ImGui::SameLine();
		ImGui::BeginGroup();
		{
			ImGui::BeginGroup();
			{
				ImGui::Text("Current");
				ImGui::ColorButton("##current", GLT::imgui_config::get_main_color_ref(), 
					ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(60, 40));
			}
			ImGui::EndGroup();

			ImGui::SameLine();
			{
				ImGui::BeginGroup();
				ImGui::Text("Previous");
				if (ImGui::ColorButton("##previous", backup_color, 
					ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(60, 40))) {

					GLT::imgui_config::update_ui_colors(backup_color);
				}
			}
			ImGui::EndGroup();

			ImGui::Separator();
			ImGui::Text("Palette");
			for (size_t n = 0; n < ARRAY_SIZE(saved_palette); n++) {
				ImGui::PushID(n);
				if ((n % 5) != 0)
					ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.y);

				ImGuiColorEditFlags palette_button_flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip;
				if (ImGui::ColorButton("##palette", saved_palette[n], palette_button_flags, ImVec2(21, 21)))
					GLT::imgui_config::update_ui_colors(ImVec4(
							saved_palette[n].x, 
							saved_palette[n].y, 
							saved_palette[n].z, 
							GLT::imgui_config::get_main_color_ref().w)
						);

				// Allow user to drop colors into each palette entry. Note that ColorButton() is already a
				// drag source by default, unless specifying the ImGuiColorEditFlags_NoDragDrop flag.
				if (ImGui::BeginDragDropTarget()) {

					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
						memcpy((float*)&saved_palette[n], payload->Data, sizeof(float) * 3);

					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F))
						memcpy((float*)&saved_palette[n], payload->Data, sizeof(float) * 4);
					ImGui::EndDragDropTarget();
				}

				ImGui::PopID();
			}
		}
		ImGui::EndGroup();

	}


	void table_row(std::function<void()> first_column, std::function<void()> second_column) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		first_column();
		ImGui::TableSetColumnIndex(1);
		second_column();
	}


	bool table_row(std::string_view label, std::string& text, bool& enable_input, const bool allow_space_as_input, const char* desc) {
		
		bool confirmed = false;
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		if (desc) {

			ImGui::SameLine();
			UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
			help_marker(desc);
		}

		ImGui::TableSetColumnIndex(1);

		ImGui::PushID(label.data());							// Push a unique ID scope based on the label and text pointer
		ImGui::PushID(&text);

		f32 column_width = ImGui::GetColumnWidth();				// Get the available width for the column
		if (enable_input) {

			std::string loc_label = "##Input";
			std::string buffer = text;
			buffer.resize(256);

			ImGui::SetNextItemWidth(column_width);
			ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll
				| ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_AutoSelectAll;

			if (!allow_space_as_input)
				flags |= ImGuiInputTextFlags_CharsNoBlank;

			// Store if the input text was active last frame
			static bool was_text_active = false;
			bool is_text_active = ImGui::InputText(loc_label.c_str(), buffer.data(), 256, flags);

			if (is_text_active) {
				buffer.resize(strlen(buffer.c_str()));
				if (!buffer.empty()) {
					text = buffer;
					enable_input = false;
					confirmed = true;
				}
			}

			// Handle focus loss
			if (enable_input) {
				bool current_active = ImGui::IsItemActive();

				// If we were active last frame but not this frame, we lost focus
				if (was_text_active && !current_active) {
					enable_input = false;
				}

				// Also check for mouse clicks outside the item
				if (current_active && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()) {
					enable_input = false;
				}

				// Escape key to cancel
				if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
					enable_input = false;
				}

				was_text_active = current_active;
			} else {
				was_text_active = false;
			}
		}
		else
		{
			// Create a button that fills the entire column width
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));

			// Create a button with the full column width
			ImVec2 button_size = ImVec2(column_width, 0);

			// Use a dummy label that won't conflict
			std::string button_label = text.empty() ? " " : text;

			if (ImGui::Button(button_label.c_str(), button_size)) {
				// Button clicked - we'll handle double click below
			}

			ImGui::PopStyleColor(3);

			// Check for double click on the button
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				enable_input = true;
			}

			// Alternative: Use your existing double click function if you prefer
			// if (getMouseInterationOnItem() == mouseInteration::leftDoubleClicked)
			//     enable_input = true;
		}

		ImGui::PopID();
		ImGui::PopID();

		return confirmed;
	}


	void table_row_text(std::string_view label, const char* format, ...) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);

		va_list args;
		va_start(args, format);
		ImGui::TextV(format, args);
		va_end(args);
	}


	bool table_row(std::string_view label, bool& value) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);

		return ImGui::Checkbox((std::string("##") + label.data()).c_str(), &value);
	}


	void table_row(std::string_view label, std::string_view value) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);

		ImGui::Text("%s", value.data());
	}


	bool table_row(glm::mat4& value, const bool display_in_degree) {
		
		glm::vec3 translation, rotation, scale;
		math::decompose_transform(value, translation, rotation, scale);

		if (display_in_degree)
			rotation = glm::degrees(rotation);

		const bool changed_0 = UI::table_row("translation", translation);
		const bool changed_1 = UI::table_row("rotation", rotation, display_in_degree ? 0.1f : 0.01f, 0.f, 0.f);
		const bool changed_2 = UI::table_row("scale", scale);

		if (changed_0 || changed_1 || changed_2) {

			if (display_in_degree)
				rotation = glm::radians(rotation);

			math::compose_transform(value, translation, rotation, scale);
		}

		return changed_0 || changed_1 || changed_2;
	}


	bool table_row(std::string_view label, std::filesystem::path& in_out, const std::filesystem::path& root, const char* desc) {

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", label.data());

        if (desc) {
            ImGui::SameLine();
            UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
            help_marker(desc);
        }

        ImGui::TableSetColumnIndex(1);
        return draw_directory_picker("##dir", in_out, root);
    }


	void table_row_progressbar(std::string_view label, const char* progress_bar_text, const f32 percent, const bool auto_resize, const f32 progressbar_size_x, const f32 progressbar_size_y) {


		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);

		float column_width = progressbar_size_x;
		if (auto_resize)
			if (ImGuiTable* table = ImGui::GetCurrentTable())
				column_width = table->Columns[1].WidthGiven;
		
		UI::progressbar_with_text("", progress_bar_text, percent, 0.0f, column_width, progressbar_size_y);
	}


	bool table_row_color(std::string_view label, glm::vec4& color, const f32 min_value, const f32 max_value) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label.data());
		ImGui::TableSetColumnIndex(1);

		std::string loc_label = "##";
		loc_label += label.data();

		const f32 avail = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(avail);
		return ImGui::ColorEdit4(loc_label.c_str(), &color.x,
			ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float);
	}


	bool table_row_color(std::string_view label, glm::vec3& color, const f32 min_value, const f32 max_value) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label.data());
		ImGui::TableSetColumnIndex(1);

		std::string loc_label = "##";
		loc_label += label.data();

		const f32 avail = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(avail);
		return ImGui::ColorEdit3(loc_label.c_str(), &color.x, ImGuiColorEditFlags_Float);
	}


	bool begin_collapsing_header_section(const char* label) {

		ImGui::Indent();
		bool buffer = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);

		if (!buffer)
			ImGui::Unindent();

		return buffer;
	}


	void end_collapsing_header_section() { ImGui::Unindent(); }

	// ---- modern controls ----------------------------------------------------------------------------------------------

	namespace {

		ImVec4 lighten(ImVec4 color, const f32 amount) {

			color.x = std::min(1.f, color.x + amount);
			color.y = std::min(1.f, color.y + amount);
			color.z = std::min(1.f, color.z + amount);
			return color;
		}

	}


	bool toggle_switch(const char* id, bool& value) {

		const f32 height = ImGui::GetFrameHeight();
		const f32 width = height * 1.8f;
		const f32 radius = height * 0.5f;

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
		if (clicked)
			value = !value;

		const bool hovered = ImGui::IsItemHovered();

		// knob position, animated towards its target
		const ImGuiID anim_id = ImGui::GetItemID();
		f32* t = ImGui::GetStateStorage()->GetFloatRef(anim_id, value ? 1.f : 0.f);
		const f32 target = value ? 1.f : 0.f;
		const f32 step = ImGui::GetIO().DeltaTime * 10.f;
		*t = (*t < target) ? std::min(target, *t + step) : std::max(target, *t - step);

		const ImVec4 on_color = hovered ? GLT::imgui_config::get_action_color00_hover_ref() : GLT::imgui_config::get_action_color00_default_ref();
		const ImVec4 off_color = hovered ? GLT::imgui_config::get_action_color_gray_hover_ref() : GLT::imgui_config::get_action_color_gray_active_ref();
		const ImVec4 bg = ImLerp(off_color, on_color, *t);

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(bg), radius);

		const f32 knob_radius = radius - 3.f;
		const f32 knob_x = ImLerp(pos.x + radius, pos.x + width - radius, *t);
		draw_list->AddCircleFilled(ImVec2(knob_x, pos.y + radius), knob_radius, IM_COL32(240, 240, 240, 255));

		return clicked;
	}


	bool segmented_control(const char* id, i32& index, std::span<const char* const> options, f32 width) {

		if (options.empty())
			return false;

		if (width <= 0.f)
			width = ImGui::CalcItemWidth();

		const f32 height = ImGui::GetFrameHeight();
		const f32 rounding = ImGui::GetStyle().FrameRounding + 2.f;
		const f32 segment_w = width / static_cast<f32>(options.size());
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);

		bool changed = false;
		ImGui::PushID(id);
		ImGui::BeginGroup();
		for (size_t x = 0; x < options.size(); ++x) {

			ImGui::PushID(static_cast<int>(x));
			const ImVec2 seg_min = ImVec2(pos.x + segment_w * static_cast<f32>(x), pos.y);
			const ImVec2 seg_max = ImVec2(seg_min.x + segment_w, pos.y + height);

			ImGui::SetCursorScreenPos(seg_min);
			if (ImGui::InvisibleButton("##segment", ImVec2(segment_w, height)) && index != static_cast<i32>(x)) {
				index = static_cast<i32>(x);
				changed = true;
			}

			const bool selected = index == static_cast<i32>(x);
			const bool hovered = ImGui::IsItemHovered();
			if (selected)
				draw_list->AddRectFilled(ImVec2(seg_min.x + 2.f, seg_min.y + 2.f), ImVec2(seg_max.x - 2.f, seg_max.y - 2.f),
					ImGui::GetColorU32(hovered ? GLT::imgui_config::get_action_color00_hover_ref() : GLT::imgui_config::get_action_color00_default_ref()), rounding - 1.f);
			else if (hovered)
				draw_list->AddRectFilled(ImVec2(seg_min.x + 2.f, seg_min.y + 2.f), ImVec2(seg_max.x - 2.f, seg_max.y - 2.f),
					ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref()), rounding - 1.f);

			const ImVec2 text_size = ImGui::CalcTextSize(options[x]);
			const ImVec2 text_pos = ImVec2(seg_min.x + (segment_w - text_size.x) * 0.5f, seg_min.y + (height - text_size.y) * 0.5f);
			draw_list->AddText(text_pos, ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled), options[x]);

			ImGui::PopID();
		}
		ImGui::EndGroup();
		ImGui::PopID();

		return changed;
	}


	bool color_palette(ImVec4& color) {

		constexpr int COUNT = 16;
		constexpr int PER_ROW = 8;
		const f32 swatch = ImGui::GetFrameHeight();

		bool changed = false;
		ImGui::PushID("##palette");
		for (int x = 0; x < COUNT; ++x) {

			ImVec4 preset{ 0.f, 0.f, 0.f, 1.f };
			ImGui::ColorConvertHSVtoRGB(static_cast<f32>(x) / static_cast<f32>(COUNT), .75f, .8f, preset.x, preset.y, preset.z);

			ImGui::PushID(x);
			if ((x % PER_ROW) != 0)
				ImGui::SameLine(0.f, 4.f);

			if (ImGui::ColorButton("##swatch", preset, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch))) {
				color = ImVec4(preset.x, preset.y, preset.z, color.w);
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		return changed;
	}


	bool reset_button(const char* id, const f32 size, const char* tooltip) {

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
		const bool hovered = ImGui::IsItemHovered();

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		const ImVec2 center = ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f);
		const f32 radius = size * 0.26f;

		if (hovered)
			draw_list->AddCircleFilled(center, size * 0.48f, ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.35f));

		// 3/4 circle + arrow head at its open end
		constexpr f32 PI = 3.14159265f;
		const f32 a_min = -PI * 0.25f;
		const f32 a_max = PI * 1.25f;
		draw_list->PathArcTo(center, radius, a_min, a_max, 16);
		draw_list->PathStroke(color, ImDrawFlags_None, 1.6f);

		const ImVec2 tip = ImVec2(center.x + std::cos(a_min) * radius, center.y + std::sin(a_min) * radius);
		const f32 head = size * 0.16f;
		draw_list->AddTriangleFilled(
			ImVec2(tip.x - head, tip.y - head * 0.9f),
			ImVec2(tip.x + head * 0.9f, tip.y - head * 0.2f),
			ImVec2(tip.x - head * 0.1f, tip.y + head),
			color);

		if (hovered && tooltip)
			ImGui::SetTooltip("%s", tooltip);

		return clicked;
	}

	bool accent_button(const char* label, const ImVec2& size) {

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_action_color00_default_ref());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color00_hover_ref());
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, GLT::imgui_config::get_action_color00_active_ref());
		const bool clicked = ImGui::Button(label, size);
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		return clicked;
	}


	bool subtle_button(const char* label, const ImVec2& size) {

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_action_color_gray_hover_ref());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color_gray_active_ref());
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, lighten(GLT::imgui_config::get_action_color_gray_active_ref(), 0.06f));
		const bool clicked = ImGui::Button(label, size);
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(2);
		return clicked;
	}


	bool search_field(const char* id, char* buffer, const size_t buffer_size, const char* hint, f32 width) {

		if (width <= 0.f)
			width = ImGui::GetContentRegionAvail().x;

		const f32 height = ImGui::GetFrameHeight();
		const f32 icon_space = height * 1.1f;
		const ImVec2 pos = ImGui::GetCursorScreenPos();

		ImGui::PushID(id);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, height * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(icon_space, ImGui::GetStyle().FramePadding.y));
		ImGui::SetNextItemWidth(width);
		bool changed = ImGui::InputTextWithHint("##search", hint, buffer, buffer_size);
		ImGui::PopStyleVar(2);

		// magnifier: circle + handle
		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImU32 icon_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const f32 r = height * 0.2f;
		const ImVec2 c = ImVec2(pos.x + icon_space * 0.5f, pos.y + height * 0.46f);
		draw_list->AddCircle(c, r, icon_color, 12, 1.5f);
		draw_list->AddLine(ImVec2(c.x + r * 0.7f, c.y + r * 0.7f), ImVec2(c.x + r * 1.6f, c.y + r * 1.6f), icon_color, 1.5f);

		// clear button
		if (buffer[0] != '\0') {

			const f32 size = height * 0.7f;
			const ImVec2 clear_pos = ImVec2(pos.x + width - size - height * 0.2f, pos.y + (height - size) * 0.5f);
			ImGui::SetCursorScreenPos(clear_pos);
			if (ImGui::InvisibleButton("##clear", ImVec2(size, size))) {
				buffer[0] = '\0';
				changed = true;
			}
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 cc = ImVec2(clear_pos.x + size * 0.5f, clear_pos.y + size * 0.5f);
			if (hovered)
				draw_list->AddCircleFilled(cc, size * 0.5f, ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref()));
			const f32 x = size * 0.18f;
			const ImU32 cross = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
			draw_list->AddLine(ImVec2(cc.x - x, cc.y - x), ImVec2(cc.x + x, cc.y + x), cross, 1.5f);
			draw_list->AddLine(ImVec2(cc.x - x, cc.y + x), ImVec2(cc.x + x, cc.y - x), cross, 1.5f);

			ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
			ImGui::Dummy(ImVec2(0.f, 0.f));
		}
		ImGui::PopID();

		return changed;
	}


	bool drag_axes(const char* id, f32* values, const int components, const f32 speed, const f32 min_value, const f32 max_value,
		const char* format, f32 width) {

		static constexpr ImU32 AXIS_COLORS[4] = {
			IM_COL32(222, 72, 72, 255),         // X
			IM_COL32(110, 190, 70, 255),        // Y
			IM_COL32(70, 120, 230, 255),        // Z
			IM_COL32(160, 160, 160, 255),       // W
		};
		static constexpr const char* AXIS_NAMES[4] = { "X", "Y", "Z", "W" };

		if (width <= 0.f)
			width = ImGui::CalcItemWidth();

		const f32 spacing = 4.f;
		const f32 field_w = std::max(10.f, (width - spacing * static_cast<f32>(components - 1)) / static_cast<f32>(components));
		const ImGuiSliderFlags flags = (min_value < max_value) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;

		bool changed = false;
		ImGui::PushID(id);
		ImGui::BeginGroup();
		for (int x = 0; x < components; ++x) {

			ImGui::PushID(x);
			if (x > 0)
				ImGui::SameLine(0.f, spacing);

			ImGui::SetNextItemWidth(field_w);
			changed |= ImGui::DragFloat("##axis", &values[x], speed, min_value, max_value, format, flags);

			// colored stripe on the left edge of the field
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x + 3.f, max.y), AXIS_COLORS[x & 3],
				ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
				ImGui::SetTooltip("%s", AXIS_NAMES[x & 3]);
			ImGui::PopID();
		}
		ImGui::EndGroup();
		ImGui::PopID();

		return changed;
	}


	bool menu_button(const char* id, f32 size, const char* tooltip) {

		if (size <= 0.f)
			size = ImGui::GetFrameHeight();

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImVec2 center = ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f);
		if (hovered || active)
			draw_list->AddCircleFilled(center, size * 0.45f, ImGui::GetColorU32(active
				? GLT::imgui_config::get_action_color_gray_active_ref() : GLT::imgui_config::get_action_color_gray_hover_ref()));

		const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		const f32 gap = size * 0.18f;
		const f32 dot = std::max(1.2f, size * 0.06f);
		for (int x = -1; x <= 1; ++x)
			draw_list->AddCircleFilled(ImVec2(center.x + gap * static_cast<f32>(x), center.y), dot, color);

		if (hovered && tooltip)
			ImGui::SetTooltip("%s", tooltip);

		return clicked;
	}


	void empty_state(const char* title, const char* hint) {

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const f32 wrap = std::max(80.f, avail.x - 32.f);
		const ImVec2 title_size = ImGui::CalcTextSize(title, nullptr, false, wrap);
		const ImVec2 hint_size = hint ? ImGui::CalcTextSize(hint, nullptr, false, wrap) : ImVec2(0.f, 0.f);
		const f32 block_h = title_size.y + (hint ? hint_size.y + 4.f : 0.f);

		const ImVec2 start = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(start.x + std::max(0.f, (avail.x - title_size.x) * 0.5f), start.y + std::max(0.f, (avail.y - block_h) * 0.4f)));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
		ImGui::TextUnformatted(title);
		ImGui::PopTextWrapPos();

		if (hint) {
			ImGui::SetCursorPosX(start.x + std::max(0.f, (avail.x - hint_size.x) * 0.5f));
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
			ImGui::TextDisabled("%s", hint);
			ImGui::PopTextWrapPos();
		}
	}


	std::string pretty_name(std::string_view identifier) {

		std::string out;
		out.reserve(identifier.size());
		bool new_word = true;
		for (const char c : identifier) {

			if (c == '_' || c == ' ' || c == '-') {
				if (!out.empty() && out.back() != ' ')
					out.push_back(' ');
				new_word = true;
				continue;
			}
			out.push_back(new_word ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
			new_word = false;
		}
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		return out;
	}

	// ---- cards + property rows ----------------------------------------------------------------------------------------

	namespace {

		// Row context. A stack so property blocks can nest (a settings page inside a popup inside a panel, ...)
		struct property_state {

			property_layout                                     layout = property_layout::compact;
			std::string                                         filter{};               // lower case
			bool                                                reset = false;
			u32                                                 visible_rows = 0;

			// settings pages: page header and sections are drawn lazily, so filtered-out sections disappear
			bool                                                is_page = false;
			const char*                                         page_title = nullptr;
			const char*                                         page_desc = nullptr;
			bool                                                page_header_pending = false;
			bool                                                page_matches = true;
			const char*                                         section_title = nullptr;
			const char*                                         section_desc = nullptr;
			bool                                                section_pending = false;
			bool                                                section_matches = true;
			bool                                                section_card_open = false;
		};

		struct card_frame {

			u32                                                 rows = 0;               // rows drawn in this card (for dividers)
		};

		std::vector<property_state>                             s_property_stack{};
		property_state                                          s_default_state{};
		std::vector<card_frame>                                 s_card_stack{};


		property_state& props()                                 { return s_property_stack.empty() ? s_default_state : s_property_stack.back(); }


		bool contains_ci(const char* haystack, const std::string& needle_lower) {

			if (needle_lower.empty())
				return true;
			if (!haystack)
				return false;

			std::string lower(haystack);
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lower.find(needle_lower) != std::string::npos;
		}


		ImVec4 card_color()                                     { return lighten(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), 0.035f); }


		// Longest prefix of [text] that fits [max_width] with "..." appended
		std::string ellipsize(const char* text, const f32 max_width) {

			if (ImGui::CalcTextSize(text).x <= max_width)
				return text;

			std::string out = text;
			while (!out.empty()) {
				do { out.pop_back(); } while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);
				if (ImGui::CalcTextSize((out + "...").c_str()).x <= max_width)
					return out + "...";
			}
			return "...";
		}


		void draw_page_header(const property_state& state) {

			if (!state.page_title)
				return;

			ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::header1));
			ImGui::TextUnformatted(state.page_title);
			ImGui::PopFont();

			if (state.page_desc && *state.page_desc) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", state.page_desc);
				ImGui::PopStyleColor();
			}
			ImGui::Dummy(ImVec2(0.f, 6.f));
		}


		// Draws a lazily pending page header / settings section card before the first visible row
		void flush_pending() {

			auto& state = props();
			if (state.page_header_pending) {
				draw_page_header(state);
				state.page_header_pending = false;
			}

			if (state.section_pending) {
				state.section_pending = false;
				state.section_card_open = true;
				begin_card(state.section_title, card_options{ .description = state.section_desc, .title_above = true });
			}
		}


		// The shared row: label (+ description), reset arrow, control. [control_width] > 0 = fixed size control (switch)
		bool property_row(const char* label, const char* desc, bool is_default, const std::function<void()>& reset,
			const std::function<bool(f32)>& control, const f32 control_width = 0.f) {

			auto& state = props();
			const bool visible = state.filter.empty() || state.section_matches || contains_ci(label, state.filter) || contains_ci(desc, state.filter);
			if (!visible)
				return false;

			flush_pending();

			bool changed = false;
			if (state.reset && !is_default && reset) {
				reset();
				is_default = true;
				changed = true;
			}

			ImGui::PushID(label);

			const ImGuiStyle& style = ImGui::GetStyle();
			const f32 frame_h = ImGui::GetFrameHeight();
			const f32 full_w = ImGui::GetContentRegionAvail().x;
			const bool described = state.layout == property_layout::described;
			card_frame* card = s_card_stack.empty() ? nullptr : &s_card_stack.back();

			// thin divider between the rows of one card (described layout only, compact rows stay dense)
			if (described && card && card->rows > 0) {
				ImGui::Dummy(ImVec2(0.f, 3.f));
				const ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + full_w, p.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.35f));
				ImGui::Dummy(ImVec2(0.f, 4.f));
			}

			const ImVec2 start = ImGui::GetCursorScreenPos();
			f32 label_bottom = 0.f;
			f32 ctrl_x = 0.f;
			f32 ctrl_w = 0.f;

			if (described) {

				const f32 area_w = std::clamp(full_w * 0.42f, 120.f, 340.f);
				ctrl_w = control_width > 0.f ? std::min(control_width, area_w) : area_w;
				ctrl_x = start.x + full_w - ctrl_w;
				const f32 text_w = std::max(40.f, full_w - area_w - frame_h - style.ItemSpacing.x * 3.f);

				ImGui::BeginGroup();
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_w);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
				ImGui::TextUnformatted(label);
				if (desc && *desc) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					ImGui::TextUnformatted(desc);
					ImGui::PopStyleColor();
				}
				ImGui::PopTextWrapPos();
				ImGui::EndGroup();
				label_bottom = ImGui::GetItemRectMax().y;

				ImGui::SetCursorScreenPos(ImVec2(ctrl_x - frame_h - style.ItemSpacing.x, start.y));
				if (!is_default) {
					if (reset_button("##reset", frame_h)) {
						reset();
						changed = true;
					}
				} else
					ImGui::Dummy(ImVec2(frame_h, frame_h));

			} else {

				// compact: fixed label column, the control fills the rest
				const f32 label_w = std::clamp(full_w * 0.36f, 60.f, 170.f);
				const f32 reset_size = frame_h * 0.8f;
				const f32 text_max = label_w - style.ItemSpacing.x - (is_default ? 0.f : reset_size);
				ctrl_x = start.x + label_w;
				ctrl_w = std::max(20.f, full_w - label_w);

				const std::string shown = ellipsize(label, text_max);
				const ImVec2 text_size = ImGui::CalcTextSize(shown.c_str());
				ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + style.FramePadding.y));
				ImGui::TextUnformatted(shown.c_str());
				if ((desc && *desc) || shown.size() != std::strlen(label)) {
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
						ImGui::BeginTooltip();
						ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.f);
						ImGui::TextUnformatted(label);
						if (desc && *desc)
							ImGui::TextDisabled("%s", desc);
						ImGui::PopTextWrapPos();
						ImGui::EndTooltip();
					}
				}
				label_bottom = start.y + text_size.y + style.FramePadding.y * 2.f;

				if (!is_default) {
					ImGui::SetCursorScreenPos(ImVec2(ctrl_x - reset_size - style.ItemSpacing.x * 0.5f, start.y + (frame_h - reset_size) * 0.5f));
					if (reset_button("##reset", reset_size)) {
						reset();
						changed = true;
					}
				}
			}

			// control
			ImGui::SetCursorScreenPos(ImVec2(ctrl_x, start.y));
			ImGui::BeginGroup();
			ImGui::SetNextItemWidth(ctrl_w);
			changed |= control(ctrl_w);
			ImGui::EndGroup();
			const f32 bottom = std::max(label_bottom, ImGui::GetItemRectMax().y);

			// accent marker for values that differ from their default
			if (!is_default) {
				const f32 marker_x = start.x - (described ? 9.f : 6.f);
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(marker_x, start.y + 2.f), ImVec2(marker_x + 3.f, bottom - 2.f),
					ImGui::GetColorU32(GLT::imgui_config::get_action_color00_hover_ref()), 1.5f);
			}

			ImGui::SetCursorScreenPos(ImVec2(start.x, bottom));
			ImGui::Dummy(ImVec2(0.f, 0.f));
			ImGui::PopID();

			if (card)
				card->rows++;
			state.visible_rows++;
			return changed;
		}


		bool drag_floats(const char* label, const char* desc, f32* value, const f32* default_value, const int components,
			const f32 speed, const f32 min_value, const f32 max_value, const char* format) {

			const bool is_default = !default_value || std::equal(value, value + components, default_value);
			return property_row(label, desc, is_default,
				[&]() { std::copy(default_value, default_value + components, value); },
				[&](f32 width) {
					if (components == 1)
						return ImGui::DragScalar("##value", ImGuiDataType_Float, value, speed, &min_value, &max_value, format,
							(min_value < max_value) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
					return drag_axes("##value", value, components, speed, min_value, max_value, format, width);
				});
		}


		int string_resize_callback(ImGuiInputTextCallbackData* data) {

			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
				auto* str = static_cast<std::string*>(data->UserData);
				str->resize(static_cast<size_t>(data->BufTextLen));
				data->Buf = str->data();
			}
			return 0;
		}

	}

	// ---- cards ----

	bool begin_card(const char* title, const card_options& options) {

		ImGui::PushID(title ? title : "##card");

		if (options.title_above && title) {

			ImGui::Dummy(ImVec2(0.f, 4.f));
			ImGui::PushFont(GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold));
			ImGui::TextUnformatted(title);
			ImGui::PopFont();

			if (options.description && *options.description) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("%s", options.description);
				ImGui::PopStyleColor();
			}
			ImGui::Dummy(ImVec2(0.f, 2.f));
		}

		ImGui::PushStyleColor(ImGuiCol_ChildBg, card_color());
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, options.title_above ? ImVec2(14.f, 10.f) : ImVec2(10.f, 7.f));
		ImGui::BeginChild("##card", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		s_card_stack.push_back({});

		if (options.title_above || !title)
			return true;

		// ---- header row: chevron, icon, title, summary, actions ----
		const f32 frame_h = ImGui::GetFrameHeight();
		const f32 width = ImGui::GetContentRegionAvail().x;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* draw_list = ImGui::GetWindowDrawList();

		bool open = true;
		if (options.collapsible) {

			ImGuiStorage* storage = ImGui::GetStateStorage();
			const ImGuiID open_id = ImGui::GetID("##open");
			open = storage->GetBool(open_id, options.default_open);

			const f32 hit_w = std::max(10.f, width - options.header_actions_width);
			if (ImGui::InvisibleButton("##header", ImVec2(hit_w, frame_h))) {
				open = !open;
				storage->SetBool(open_id, open);
			}
			if (ImGui::IsItemHovered())
				draw_list->AddRectFilled(ImVec2(pos.x - 4.f, pos.y), ImVec2(pos.x + hit_w, pos.y + frame_h),
					ImGui::GetColorU32(GLT::imgui_config::get_action_color_gray_hover_ref()), 4.f);
		} else
			ImGui::Dummy(ImVec2(std::max(10.f, width - options.header_actions_width), frame_h));

		f32 x = pos.x;
		const f32 text_y = pos.y + (frame_h - ImGui::GetTextLineHeight()) * 0.5f;

		if (options.collapsible) {
			ImGui::RenderArrow(draw_list, ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), open ? ImGuiDir_Down : ImGuiDir_Right, 0.8f);
			x += ImGui::GetFontSize() * 0.9f + 2.f;
		}

		if (options.has_icon) {
			const f32 icon = ImGui::GetTextLineHeight();
			draw_list->AddImage(options.icon, ImVec2(x, text_y), ImVec2(x + icon, text_y + icon), options.icon_uv0, options.icon_uv1);
			x += icon + 6.f;
		}

		ImFont* bold = GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold);
		ImGui::PushFont(bold);
		draw_list->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), title);
		x += ImGui::CalcTextSize(title).x;
		ImGui::PopFont();

		if (options.summary && *options.summary) {
			const f32 space = pos.x + width - options.header_actions_width - (x + 10.f) - 6.f;
			if (space > 20.f) {
				const std::string summary = ellipsize(options.summary, space);
				draw_list->AddText(ImVec2(x + 10.f, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), summary.c_str());
			}
		}

		if (options.header_actions && options.header_actions_width > 0.f) {
			ImGui::SetCursorScreenPos(ImVec2(pos.x + width - options.header_actions_width, pos.y));
			ImGui::BeginGroup();
			options.header_actions();
			ImGui::EndGroup();
		}

		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + frame_h));
		ImGui::Dummy(ImVec2(0.f, 0.f));

		if (open && options.description && *options.description) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", options.description);
			ImGui::PopStyleColor();
		}

		if (open) {
			const ImVec2 p = ImGui::GetCursorScreenPos();
			draw_list->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + width, p.y), ImGui::GetColorU32(ImGuiCol_Separator, 0.3f));
			ImGui::Dummy(ImVec2(0.f, 3.f));
		}

		return open;
	}


	void end_card() {

		if (!s_card_stack.empty())
			s_card_stack.pop_back();

		ImGui::EndChild();
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0.f, 6.f));
	}

	// ---- property blocks ----

	void begin_properties(const property_layout layout, std::string_view filter) {

		property_state state{};
		state.layout = layout;
		state.filter.assign(filter.begin(), filter.end());
		std::transform(state.filter.begin(), state.filter.end(), state.filter.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		s_property_stack.push_back(std::move(state));
	}


	u32 end_properties() {

		if (s_property_stack.empty())
			return 0;

		const u32 rows = s_property_stack.back().visible_rows;
		s_property_stack.pop_back();
		return rows;
	}


	void reset_properties()                     { props().reset = true; }

	// ---- property rows ----

	bool property_toggle(const char* label, bool& value, const char* desc, const bool* default_value) {

		const f32 switch_w = ImGui::GetFrameHeight() * 1.8f;
		return property_row(label, desc, !default_value || value == *default_value,
			[&]() { value = *default_value; },
			[&](f32) { return toggle_switch("##value", value); },
			switch_w);
	}


	bool property_slider(const char* label, f32& value, const f32 min_value, const f32 max_value, const char* format,
		const char* desc, const f32* default_value, ImGuiSliderFlags flags) {

		return property_row(label, desc, !default_value || value == *default_value,
			[&]() { value = *default_value; },
			[&](f32) { return ImGui::SliderFloat("##value", &value, min_value, max_value, format, flags | ImGuiSliderFlags_AlwaysClamp); });
	}


	bool property_slider(const char* label, i32& value, const i32 min_value, const i32 max_value, const char* format,
		const char* desc, const i32* default_value, ImGuiSliderFlags flags) {

		return property_row(label, desc, !default_value || value == *default_value,
			[&]() { value = *default_value; },
			[&](f32) { return ImGui::SliderInt("##value", &value, min_value, max_value, format, flags | ImGuiSliderFlags_AlwaysClamp); });
	}


	bool property_drag(const char* label, f32& value, const f32 speed, const f32 min_value, const f32 max_value, const char* format,
		const char* desc, const f32* default_value) {

		return drag_floats(label, desc, &value, default_value, 1, speed, min_value, max_value, format);
	}


	bool property_drag(const char* label, glm::vec2& value, const f32 speed, const f32 min_value, const f32 max_value, const char* format,
		const char* desc, const glm::vec2* default_value) {

		return drag_floats(label, desc, &value.x, default_value ? &default_value->x : nullptr, 2, speed, min_value, max_value, format);
	}


	bool property_drag(const char* label, glm::vec3& value, const f32 speed, const f32 min_value, const f32 max_value, const char* format,
		const char* desc, const glm::vec3* default_value) {

		return drag_floats(label, desc, &value.x, default_value ? &default_value->x : nullptr, 3, speed, min_value, max_value, format);
	}


	bool property_combo(const char* label, i32& index, std::span<const char* const> options, const char* desc, const i32* default_index) {

		return property_row(label, desc, !default_index || index == *default_index,
			[&]() { index = *default_index; },
			[&](f32) {
				bool changed = false;
				const char* preview = (index >= 0 && index < static_cast<i32>(options.size())) ? options[static_cast<size_t>(index)] : "";
				ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_action_color_gray_hover_ref());
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color_gray_active_ref());
				const bool combo_open = ImGui::BeginCombo("##value", preview);
				ImGui::PopStyleColor(2);
				if (combo_open) {
					for (size_t x = 0; x < options.size(); ++x) {
						const bool selected = static_cast<i32>(x) == index;
						if (ImGui::Selectable(options[x], selected) && !selected) {
							index = static_cast<i32>(x);
							changed = true;
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				return changed;
			});
	}


	bool property_segmented(const char* label, i32& index, std::span<const char* const> options, const char* desc, const i32* default_index) {

		return property_row(label, desc, !default_index || index == *default_index,
			[&]() { index = *default_index; },
			[&](f32 width) { return segmented_control("##value", index, options, width); });
	}


	bool property_color(const char* label, ImVec4& color, const bool alpha, const char* desc, const ImVec4* default_color) {

		const bool is_default = !default_color || (color.x == default_color->x && color.y == default_color->y && color.z == default_color->z
			&& (!alpha || color.w == default_color->w));

		return property_row(label, desc, is_default,
			[&]() { color = alpha ? *default_color : ImVec4(default_color->x, default_color->y, default_color->z, color.w); },
			[&](f32 width) {

				const ImVec2 pos = ImGui::GetCursorScreenPos();
				const f32 height = ImGui::GetFrameHeight();
				const ImGuiColorEditFlags swatch_flags = ImGuiColorEditFlags_NoTooltip
					| (alpha ? ImGuiColorEditFlags_AlphaPreviewHalf : ImGuiColorEditFlags_NoAlpha);

				if (ImGui::ColorButton("##swatch", color, swatch_flags, ImVec2(width, height)))
					ImGui::OpenPopup("##color_popup");

				// hex code on top of the swatch, in black or white depending on the brightness
				const f32 luminance = 0.299f * color.x + 0.587f * color.y + 0.114f * color.z;
				const std::string hex = std::format("#{:02X}{:02X}{:02X}",
					static_cast<int>(std::clamp(color.x, 0.f, 1.f) * 255.f + .5f),
					static_cast<int>(std::clamp(color.y, 0.f, 1.f) * 255.f + .5f),
					static_cast<int>(std::clamp(color.z, 0.f, 1.f) * 255.f + .5f));
				const ImVec2 text_size = ImGui::CalcTextSize(hex.c_str());
				if (text_size.x < width - 4.f)
					ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + (width - text_size.x) * 0.5f, pos.y + (height - text_size.y) * 0.5f),
						luminance > 0.55f ? IM_COL32(20, 20, 20, 255) : IM_COL32(240, 240, 240, 255), hex.c_str());

				bool changed = false;
				ImGui::SetNextWindowPos(ImVec2(pos.x + width, pos.y + height + 4.f), ImGuiCond_Appearing, ImVec2(1.f, 0.f));
				if (ImGui::BeginPopup("##color_popup")) {

					ImGui::SetNextItemWidth(230.f);
					changed |= ImGui::ColorPicker4("##picker", &color.x, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview
						| ImGuiColorEditFlags_PickerHueWheel | (alpha ? ImGuiColorEditFlags_AlphaBar : ImGuiColorEditFlags_NoAlpha));

					ImGui::Dummy(ImVec2(0.f, 2.f));
					ImGui::TextDisabled("Presets");
					changed |= color_palette(color);
					ImGui::EndPopup();
				}
				return changed;
			});
	}


	bool property_color(const char* label, glm::vec4& color, const bool alpha, const char* desc, const glm::vec4* default_color) {

		ImVec4 buffer{ color.x, color.y, color.z, color.w };
		const ImVec4 def = default_color ? ImVec4(default_color->x, default_color->y, default_color->z, default_color->w) : ImVec4();
		const bool changed = property_color(label, buffer, alpha, desc, default_color ? &def : nullptr);
		if (changed)
			color = glm::vec4(buffer.x, buffer.y, buffer.z, buffer.w);
		return changed;
	}


	bool property_color(const char* label, glm::vec3& color, const char* desc, const glm::vec3* default_color) {

		ImVec4 buffer{ color.x, color.y, color.z, 1.f };
		const ImVec4 def = default_color ? ImVec4(default_color->x, default_color->y, default_color->z, 1.f) : ImVec4();
		const bool changed = property_color(label, buffer, false, desc, default_color ? &def : nullptr);
		if (changed)
			color = glm::vec3(buffer.x, buffer.y, buffer.z);
		return changed;
	}


	bool property_text(const char* label, std::string& value, const char* desc, const char* hint) {

		return property_row(label, desc, true, {},
			[&](f32) {
				return ImGui::InputTextWithHint("##value", hint ? hint : "", value.data(), value.capacity() + 1,
					ImGuiInputTextFlags_CallbackResize, &string_resize_callback, &value);
			});
	}


	void property_value(const char* label, std::string_view value, const char* desc) {

		property_row(label, desc, true, {},
			[&](f32) {
				ImGui::AlignTextToFramePadding();
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextUnformatted(value.data(), value.data() + value.size());
				ImGui::PopStyleColor();
				return false;
			});
	}


	bool property_custom(const char* label, const std::function<bool(f32 width)>& control, const char* desc, const bool is_default,
		const std::function<void()>& reset) {

		return property_row(label, desc, is_default, reset, control);
	}

	// ---- settings pages ----

	void settings_begin_page(const char* title, std::string_view filter, const char* description) {

		begin_properties(property_layout::described, filter);
		auto& state = props();
		state.is_page = true;
		state.page_title = title;
		state.page_desc = description;
		state.page_matches = state.filter.empty() || (title && contains_ci(title, state.filter));
		state.section_matches = state.page_matches;

		ImGui::PushID(title ? title : "##settings_page");

		if (state.filter.empty())
			draw_page_header(state);
		else
			state.page_header_pending = title != nullptr;
	}


	u32 settings_end_page() {

		settings_end_section();
		ImGui::PopID();
		return end_properties();
	}


	void settings_reset_page()                  { reset_properties(); }


	void settings_begin_section(const char* title, const char* description) {

		settings_end_section();
		auto& state = props();
		state.section_title = title;
		state.section_desc = description;
		state.section_pending = true;
		state.section_matches = state.page_matches || contains_ci(title, state.filter) || contains_ci(description, state.filter);
	}


	void settings_end_section() {

		auto& state = props();
		if (state.section_card_open) {
			end_card();
			ImGui::Dummy(ImVec2(0.f, 2.f));
			state.section_card_open = false;
		}
		state.section_pending = false;
		state.section_title = nullptr;
		state.section_desc = nullptr;
		state.section_matches = state.page_matches;
	}


	bool settings_toggle(const char* label, const char* desc, bool& value, const bool default_value) {
		return property_toggle(label, value, desc, &default_value);
	}


	bool settings_slider(const char* label, const char* desc, f32& value, const f32 default_value, const f32 min_value, const f32 max_value,
		const char* format, ImGuiSliderFlags flags) {
		return property_slider(label, value, min_value, max_value, format, desc, &default_value, flags);
	}


	bool settings_slider(const char* label, const char* desc, i32& value, const i32 default_value, const i32 min_value, const i32 max_value,
		const char* format, ImGuiSliderFlags flags) {
		return property_slider(label, value, min_value, max_value, format, desc, &default_value, flags);
	}


	bool settings_drag(const char* label, const char* desc, f32& value, const f32 default_value, const f32 speed,
		const f32 min_value, const f32 max_value, const char* format) {
		return property_drag(label, value, speed, min_value, max_value, format, desc, &default_value);
	}


	bool settings_drag(const char* label, const char* desc, glm::vec2& value, const glm::vec2& default_value, const f32 speed,
		const f32 min_value, const f32 max_value, const char* format) {
		return property_drag(label, value, speed, min_value, max_value, format, desc, &default_value);
	}


	bool settings_drag(const char* label, const char* desc, glm::vec3& value, const glm::vec3& default_value, const f32 speed,
		const f32 min_value, const f32 max_value, const char* format) {
		return property_drag(label, value, speed, min_value, max_value, format, desc, &default_value);
	}


	bool settings_combo(const char* label, const char* desc, i32& index, const i32 default_index, std::span<const char* const> options) {
		return property_combo(label, index, options, desc, &default_index);
	}


	bool settings_segmented(const char* label, const char* desc, i32& index, const i32 default_index, std::span<const char* const> options) {
		return property_segmented(label, index, options, desc, &default_index);
	}


	bool settings_color(const char* label, const char* desc, ImVec4& color, const ImVec4& default_color, const bool alpha) {
		return property_color(label, color, alpha, desc, &default_color);
	}


	bool settings_color(const char* label, const char* desc, glm::vec4& color, const glm::vec4& default_color, const bool alpha) {
		return property_color(label, color, alpha, desc, &default_color);
	}


	bool settings_custom(const char* label, const char* desc, const bool is_default, const std::function<void()>& reset,
		const std::function<bool()>& control) {
		return property_custom(label, [&](f32) { return control(); }, desc, is_default, reset);
	}


	// CLASS IMPLEMENTATION ============================================================================================

	// CLASS PUBLIC ====================================================================================================

	// CLASS PROTECTED =================================================================================================

	// CLASS PRIVATE ===================================================================================================

}
