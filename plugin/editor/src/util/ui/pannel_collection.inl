#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::UI {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<typename T>
	bool table_row(std::string_view label, T& value, f32 drag_speed, T min_value, T max_value, ImGuiInputTextFlags flags) {

		ImGuiStyle& style = ImGui::GetStyle();
		flags |= ImGuiInputTextFlags_AllowTabInput;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		size_t pos = label.find("##");
		std::string_view displayLabel = (pos != std::string_view::npos) ? label.substr(0, pos) : label;
		ImGui::Text("%.*s", static_cast<int>(displayLabel.length()), displayLabel.data());

		ImGui::TableSetColumnIndex(1);
		std::string loc_label = "##";
		loc_label += label.data();
		ImGui::SetNextItemWidth(ImGui::GetColumnWidth());

		if constexpr (std::is_same_v<T, bool>) {

			ImGui::Text("%s", util::bool_to_str(value));
			return false;
		}

		else if constexpr (std::is_integral_v<T>) {
			if constexpr (std::is_unsigned_v<T>) {
				switch (sizeof(T)) {
				case 1: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_U8, &value, drag_speed, &min_value, &max_value, "%u", flags);		// u8
				case 2: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_U16, &value, drag_speed, &min_value, &max_value, "%u", flags);	// u16
				case 4: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_U32, &value, drag_speed, &min_value, &max_value, "%u", flags);	// u32
				case 8: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_U64, &value, drag_speed, &min_value, &max_value, "%llu", flags);	// u64
				default:
					ImGui::Text("Could not display variable of type unsigned int [size: %zu]", sizeof(T));
					return false;
				}
			} else {
				switch (sizeof(T)) {
				case 1: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_S8, &value, drag_speed, &min_value, &max_value, "%d", flags);		// i8
				case 2: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_S16, &value, drag_speed, &min_value, &max_value, "%d", flags);	// i16
				case 4: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_S32, &value, drag_speed, &min_value, &max_value, "%d", flags);	// i32
				case 8: return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_S64, &value, drag_speed, &min_value, &max_value, "%lld", flags);	// i64
				default:
					ImGui::Text("Could not display var of type signed int [size: %zu]", sizeof(T));
					return false;
				}
			}
		} else if constexpr (std::is_floating_point_v<T>) {
			if constexpr (sizeof(T) <= 4)
				return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_Float, &value, drag_speed, &min_value, &max_value, "%.3f", flags);
			else 
				return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_Double, &value, drag_speed, &min_value, &max_value, "%.3f", flags);
		}

		else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, ImVec2>)
			return ImGui::DragFloat2(loc_label.c_str(), &value[0], drag_speed, min_value[0], max_value[0], "%.2f", flags);

		else if constexpr (std::is_same_v<T, glm::vec3>)
			return ImGui::DragFloat3(loc_label.c_str(), &value[0], drag_speed, min_value[0], max_value[0], "%.2f", flags);

		else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, ImVec4>)
			return ImGui::DragFloat4(loc_label.c_str(), &value[0], drag_speed, min_value[0], max_value[0], "%.2f", flags);

		else if constexpr (std::is_same_v<T, std::string>) {

			ImGui::Text("%s", value.c_str());
			return false;

		} else if constexpr (std::is_convertible_v<T, std::string>) {

			ImGui::Text("%s", std::to_string(value).c_str());
			return false;
		}

		else
			ImGui::Text("Could not display variable");

		return false;
	}


	template<typename T>
	bool table_row(std::string_view label, T& current_value, const std::vector<std::string>& options, const char* desc, 
		std::function<void(T)> on_changed) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		if (desc) {

			ImGui::SameLine();
			UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
			help_marker(desc);
		}

		ImGui::TableSetColumnIndex(1);
		std::string loc_label = "##";
		loc_label += label.data();

		// Convert options to C strings for ImGui
		std::vector<const char*> option_cstrings;
		option_cstrings.reserve(options.size());
		for (const auto& option : options)
			option_cstrings.push_back(option.c_str());

		// Find current index
		int current_index = static_cast<int>(current_value);

		// Ensure the index is within bounds
		if (current_index < 0 || current_index >= static_cast<int>(options.size())) {

			current_index = 0;
			current_value = static_cast<T>(0);
		}

		bool changed = false;
		ImGui::SetNextItemWidth(ImGui::GetColumnWidth());
		if (ImGui::Combo(loc_label.c_str(), &current_index, option_cstrings.data(), static_cast<int>(option_cstrings.size()))) {

			T new_value = static_cast<T>(current_index);
			if (new_value != current_value) {

				current_value = new_value;
				changed = true;

				// Call callback if provided
				if (on_changed)
					on_changed(current_value);
			}
		}

		return changed;
	}


    template<typename T, typename Container>
	bool table_row(std::string_view label, T& current_value, const Container& options, const char* desc, std::function<void(T)> on_changed) {

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		if (desc) {

			ImGui::SameLine();
			UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
			help_marker(desc);
		}

		ImGui::TableSetColumnIndex(1);
		std::string loc_label = "##";
		loc_label += label.data();

		// Helper lambda to get size and convert elements to const char*
		auto prepare_options = [&]() -> std::pair<std::vector<const char*>, size_t> {

			std::vector<const char*> option_cstrings;
			if constexpr (requires { options.size(); }) {

				// For containers with size() method (std::vector, std::array)
				option_cstrings.reserve(options.size());
				for (const auto& option : options) {

					if constexpr (std::is_same_v<std::decay_t<decltype(option)>, std::string>)
						option_cstrings.push_back(option.c_str());
                    else
						option_cstrings.push_back(option);
				}
				return {std::move(option_cstrings), options.size()};
			
			} else {

				// For C-style arrays
				constexpr size_t array_size = std::size(options);
				option_cstrings.reserve(array_size);
				for (size_t i = 0; i < array_size; ++i)
					option_cstrings.push_back(options[i]);
				
				return {std::move(option_cstrings), array_size};
			}
		};

		auto [option_cstrings, options_size] = prepare_options();
		int current_index = static_cast<int>(current_value);                        // Find current index
		if (current_index < 0 || current_index >= static_cast<int>(options_size)) { // Ensure the index is within bounds
			
			current_index = 0;
			current_value = static_cast<T>(0);
		}

		bool changed = false;
		ImGui::SetNextItemWidth(ImGui::GetColumnWidth() - 10);
		if (ImGui::Combo(loc_label.c_str(), &current_index, option_cstrings.data(), static_cast<int>(options_size))) {

			T new_value = static_cast<T>(current_index);
			if (new_value != current_value) {

				current_value = new_value;
				changed = true;

				if (on_changed)                                                     // Call callback if provided
					on_changed(current_value);
			}
		}

		return changed;
	}


    template<typename T, typename Container>
    bool table_row(std::string_view label, T& current_value, const Container& options, const char* desc, bool* p_removed, 
		std::function<void(T)> on_changed) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", label.data());

		if (desc) {

			ImGui::SameLine();
			UI::shift_cursor_pos(ImGui::GetContentRegionAvail().x - 12.f, 0.f);
			help_marker(desc);
		}

        ImGui::TableSetColumnIndex(1);
        std::string loc_label = "##";
        loc_label += label.data();

        // Prepare option strings (same as before)
        auto prepare_options = [&]() -> std::pair<std::vector<const char*>, size_t> {

            std::vector<const char*> option_cstrings;
            if constexpr (requires { options.size(); }) {

                option_cstrings.reserve(options.size());
                for (const auto& option : options) {

                    if constexpr (std::is_same_v<std::decay_t<decltype(option)>, std::string>)
                        option_cstrings.push_back(option.c_str());
                    else
                        option_cstrings.push_back(option);
                }
                return {std::move(option_cstrings), options.size()};
            
			} else {

				constexpr size_t array_size = std::size(options);
                option_cstrings.reserve(array_size);
                for (size_t i = 0; i < array_size; ++i)
                    option_cstrings.push_back(options[i]);
                
				return {std::move(option_cstrings), array_size};
            }
        };

        auto [option_cstrings, options_size] = prepare_options();

        // Current index
        int current_index = static_cast<int>(current_value);
        if (current_index < 0 || current_index >= static_cast<int>(options_size)) {

            current_index = 0;
            current_value = static_cast<T>(0);
        }

        bool changed = false;

        // --- Layout: combo + optional remove button ---
        f32 column_width = ImGui::GetColumnWidth();
        f32 combo_width = column_width;
        if (p_removed) {

            // Reserve space for button (≈ 30px) and ImGui::SameLine spacing
            combo_width -= 30.0f;
            if (combo_width < 50.0f) combo_width = 50.0f;
        }

        ImGui::SetNextItemWidth(combo_width);
        if (ImGui::Combo(loc_label.c_str(), &current_index, option_cstrings.data(), static_cast<int>(options_size))) {

            T new_value = static_cast<T>(current_index);
            if (new_value != current_value) {

                current_value = new_value;
                changed = true;
                if (on_changed) on_changed(current_value);
            }
        }

        // Remove button (if requested)
        if (p_removed) {

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button(("X##" + loc_label).c_str()))
                *p_removed = true;   // signal removal
            
			ImGui::PopStyleColor(3);
        }

        return changed;
    }


	template<typename T>
	bool table_row_slider(std::string_view label, T& value, f32 min_value, f32 max_value, f32 draw_speed, ImGuiInputTextFlags flags) {

		ImGuiStyle& style = ImGui::GetStyle();
		flags |= ImGuiInputTextFlags_AllowTabInput;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);
		std::string loc_label = "##";
		loc_label += label.data();
		ImGui::SetNextItemWidth(ImGui::GetColumnWidth());

		if constexpr (std::is_same_v<T, int>)
			return ImGui::SliderInt(loc_label.c_str(), &value, static_cast<int>(min_value), static_cast<int>(max_value), "%d", flags);
		
		// if constexpr (std::is_same_v<T, u32>)
		// 	return ImGui::SliderInt(loc_label.c_str(), &value, static_cast<int>(min_value), static_cast<int>(max_value), "%d", flags);

		if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>)
			return ImGui::SliderFloat(loc_label.c_str(), &value, min_value, max_value, "%.2f", flags);

		if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, ImVec2>)
			return ImGui::SliderFloat2(loc_label.c_str(), &value[0], min_value, max_value, "%.2f", flags);

		if constexpr (std::is_same_v<T, glm::vec3>)
			return ImGui::SliderFloat3(loc_label.c_str(), &value[0], min_value, max_value, "%.2f", flags);

		if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, ImVec4>)
			return ImGui::SliderFloat4(loc_label.c_str(), &value[0], min_value, max_value, "%.2f", flags);

		else
			ImGui::Text("unsuported data type");

		return false;
	}
	
	
	template<typename T>
	bool table_row_drag_scalar(std::string_view label, T& value, const char* format, T min_value, T max_value, f32 draw_speed, 
        ImGuiInputTextFlags flags) {

		ImGuiStyle& style = ImGui::GetStyle();
		flags |= ImGuiInputTextFlags_AllowTabInput;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("%s", label.data());

		ImGui::TableSetColumnIndex(1);
		std::string loc_label = "##";
		loc_label += label.data();
		ImGui::SetNextItemWidth(ImGui::GetColumnWidth());

		if constexpr (std::is_same_v<T, int>)
			return ImGui::SliderInt(loc_label.c_str(), &value, static_cast<int>(min_value), static_cast<int>(max_value), "%d", flags);

		if constexpr (std::is_same_v<T, f32> || std::is_same_v<T, f64>)
			return ImGui::SliderFloat(loc_label.c_str(), &value, min_value, max_value, "%.2f", flags);

		if constexpr (std::is_same_v<T, u32>)
			return ImGui::DragScalar(loc_label.c_str(), ImGuiDataType_U32, &value, 0.2f, (const void*)&min_value, (const void*)&max_value, format);

		return false;
	}

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
