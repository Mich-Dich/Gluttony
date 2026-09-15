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
		ImVec2 current_item_spacing = style.ItemSpacing;
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

	/*
		else if constexpr (std::is_same_v<T, int32> || std::is_same_v<T, u32> || std::is_same_v<T, int64> || std::is_same_v<T, u64>) {

			ImGui::SetNextItemWidth(ImGui::GetColumnWidth());
			return ImGui::DragInt(loc_label.c_str(), &value, drag_speed, min_value, max_value, "%.2f", flags);
		}
	*/


	template<typename T>
	bool table_row_slider(std::string_view label, T& value, f32 min_value, f32 max_value, f32 draw_speed, ImGuiInputTextFlags flags) {

		ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 current_item_spacing = style.ItemSpacing;
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
		ImVec2 current_item_spacing = style.ItemSpacing;
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
