
#include "util/pch.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "config.h"
#include "util/io/serializer_yaml.h"
#include "util/system.h"

#include "imgui_config.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::imgui_config {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #define UI_ACTIVE_THEME                         UI::THEME::current_theme

	#define LERP_GRAY(value)					    { value, value, value, 1.f }

    #define LERP_GRAY_A(value, alpha)               {value, value, value, alpha}

	#define IMCOLOR_GRAY(value)					    ImColor{ value, value, value, 255 }

    #define LERP_MAIN_COLOR_DARK(value)             {main_color.x * value, main_color.y * value, main_color.z * value, 1.f }

    #define LERP_MAIN_COLOR_LIGHT(value)                                                                \
        {	(1.f - value) * 1.f + value * main_color.x,                                                  \
            (1.f - value) * 1.f + value * main_color.y,                                                  \
            (1.f - value) * 1.f + value * main_color.z,                                                  \
            1.f }						// Set [w] to be [1.f] to disable accidental transparency

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

	static std::unordered_map<font_type, ImFont*>   s_fonts{}; 				// Loaded fonts mapped by name.

    ImGuiContext* 								    s_context_imgui{}; 		// Pointer to the ImGui context.

    // ImPlotContext* 								    s_context_implot{}; 	    // Pointer to the ImPlot context.

    // Path to the ImGui .ini file.
	static std::filesystem::path                    g_ini_file_location = GLT::util::get_executable_path() /
        GLT::config::get_filepath_from_config_type_ini(GLT::config::type::imgui);

    static f32                                      g_font_size = 15.f; 					                            // Default UI font size.
	
    static f32                                      g_font_size_header0 = 19.f; 			                            // Font size for small headers.
	
    static f32                                      g_font_size_header1 = 23.f; 			                            // Font size for medium headers.
	
    static f32                                      g_font_size_header2 = 27.f; 			                            // Font size for large headers.
	
    static f32                                      g_big_font_size = 18.f; 				                            // Font size for emphasized text.
	
    static f32                                      g_font_size_small = 14.4f;				                            // Font size for emphasized text.
	
    static theme_selection                          g_ui_theme = theme_selection::dark; 	                            // Currently selected UI theme.
	
    static bool                                     g_window_border = false; 				                            // Whether window borders are enabled.
	
    static ImVec4                                   g_highlighted_window_bg = {0.5700f, 0.5700f, 0.5700f, 1.0000f};    // Highlighted background color for selected windows.
	
    static ImVec4                                   main_color = {0.0000f, 0.4609f, 0.7382f, 1.0000f};
	
    static ImVec4                                   main_titlebar_color = {};
	
    static ImVec4                                   action_color00_faded = {};
	
    static ImVec4                                   action_color00_weak = {};
	
    static ImVec4                                   action_color00_default = {};
	
    static ImVec4                                   action_color00_hover = {};
	
    static ImVec4                                   action_color00_active = {};
	
    static ImVec4                                   default_gray = IMCOLOR_GRAY(30);
	
    static ImVec4                                   default_gray1 = IMCOLOR_GRAY(35);
	
    static ImVec4                                   action_color_gray_default = LERP_GRAY(0.2f);
	
    static ImVec4                                   action_color_gray_hover = LERP_GRAY(0.27f);
	
    static ImVec4                                   actionColorGrayActive = LERP_GRAY(0.35f);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

	[[maybe_unused]] static ImVec4 vector_multi(const ImVec4& vec_0, const ImVec4& vec_1) {

		return ImVec4{ vec_0.x * vec_1.x, vec_0.y * vec_1.y, vec_0.z * vec_1.z, vec_0.w * vec_1.w };
	}


	void load_fonts() {

		ImGui::SetCurrentContext(s_context_imgui);
		// ImPlot::SetCurrentContext(s_context_implot);

		auto& io = ImGui::GetIO();
		io.Fonts->Clear();			// Clear the font atlas before adding new fonts
		s_fonts.clear();

		std::filesystem::path basePath = util::get_executable_path() / "assets" / "fonts";
		std::filesystem::path fontPath = basePath / "Open_Sans" / "static";
		std::filesystem::path inconsolataPath = basePath / "Inconsolata" / "static";

		io.FontAllowUserScaling = true;

		#define ADD_FONT(font_type, fontPath, fontSize) \
			s_fonts[font_type] = io.Fonts->AddFontFromFileTTF((fontPath).string().c_str(), fontSize);

		ADD_FONT(font_type::regular, fontPath/ "OpenSans-Regular.ttf", g_font_size)
		ADD_FONT(font_type::bold, fontPath/ "OpenSans-Bold.ttf", g_font_size)
		ADD_FONT(font_type::italic, fontPath/ "OpenSans-Italic.ttf", g_font_size)

		ADD_FONT(font_type::regular_big, fontPath / "OpenSans-Regular.ttf", g_big_font_size)
		ADD_FONT(font_type::bold_big, fontPath / "OpenSans-Bold.ttf", g_big_font_size)
		ADD_FONT(font_type::italic_big, fontPath / "OpenSans-Italic.ttf", g_big_font_size)

		ADD_FONT(font_type::small, fontPath / "OpenSans-Regular.ttf", g_font_size_small)

		ADD_FONT(font_type::header0, fontPath / "OpenSans-Regular.ttf", g_font_size_header2)
		ADD_FONT(font_type::header1, fontPath / "OpenSans-Regular.ttf", g_font_size_header1)
		ADD_FONT(font_type::header2, fontPath / "OpenSans-Regular.ttf", g_font_size_header0)

		ADD_FONT(font_type::giant_thin, fontPath / "OpenSans-Regular.ttf", 38.f)
		ADD_FONT(font_type::giant, fontPath / "OpenSans-Bold.ttf", 38.f)

		ADD_FONT(font_type::monospace_regular, inconsolataPath / "Inconsolata-Regular.ttf", g_font_size * 0.92f)
		ADD_FONT(font_type::monospace_regular_big, inconsolataPath / "Inconsolata-Regular.ttf", g_big_font_size * 1.92f)
		
		#undef ADD_FONT

		io.FontDefault = s_fonts[font_type::regular];
	}


	void update_ui_theme() {

		//LOG(debug, "updating UI theme");

		ImGuiStyle* style = &ImGui::GetStyle();
		ImVec4* colors = style->Colors;

		// main sizes
		style->WindowPadding = ImVec2(10.f, 4.f);
		style->FramePadding = ImVec2(4.f, 4.f);
		style->CellPadding = ImVec2(4.f, 4.f);
		style->ItemSpacing = ImVec2(4.f, 4.f);
		style->ItemInnerSpacing = ImVec2(4.f, 4.f);
		style->TouchExtraPadding = ImVec2(4.f, 4.f);
		style->IndentSpacing = 10.f;
		style->ScrollbarSize = 14.f;
		style->GrabMinSize = 14.f;
		style->WindowMenuButtonPosition = ImGuiDir_Right;
        style->ButtonTextAlign = ImVec2(.0f, .5f);

		// border
		style->WindowBorderSize = 1.0f;
		style->ChildBorderSize = 0.0f;
		style->PopupBorderSize = 0.0f;
		style->FrameBorderSize = 0.0f;
		style->TabBorderSize = 0.0f;
		style->TabBarBorderSize = 0.0f;

		// padding
		style->WindowRounding = 2.f;
		style->ChildRounding = 2.f;
		style->FrameRounding = 2.f;
		style->PopupRounding = 2.f;
		style->ScrollbarRounding = 2.f;
		style->GrabRounding = 2.f;
		style->TabRounding = 2.f;

		switch (g_ui_theme) {

			case theme_selection::dark: {

				action_color00_faded = LERP_MAIN_COLOR_DARK(0.5f);
				action_color00_weak = LERP_MAIN_COLOR_DARK(0.6f);
				action_color00_default = LERP_MAIN_COLOR_DARK(0.7f);
				action_color00_hover = LERP_MAIN_COLOR_DARK(0.85f);
				action_color00_active = LERP_MAIN_COLOR_DARK(0.92f);

				action_color_gray_default = LERP_GRAY(0.15f);
				action_color_gray_hover = LERP_GRAY(0.2f);
				actionColorGrayActive = LERP_GRAY(0.25f);

				g_highlighted_window_bg = LERP_GRAY(0.57f);
				main_titlebar_color = LERP_MAIN_COLOR_DARK(.5f);

				colors[ImGuiCol_Text]					= IMCOLOR_GRAY(255);
				colors[ImGuiCol_TextDisabled]			= IMCOLOR_GRAY(180);
				colors[ImGuiCol_WindowBg]				= default_gray;
				colors[ImGuiCol_ChildBg]				= default_gray;
				colors[ImGuiCol_PopupBg]				= IMCOLOR_GRAY(20);
				colors[ImGuiCol_Border]					= LERP_GRAY_A(.43f, .5f);
				colors[ImGuiCol_BorderShadow]			= LERP_GRAY_A(.12f, .5f);
				colors[ImGuiCol_FrameBg]				= LERP_GRAY_A(.06f, .54f);
				colors[ImGuiCol_FrameBgHovered]			= LERP_GRAY_A(.19f, .4f);
				colors[ImGuiCol_FrameBgActive]			= LERP_GRAY_A(.3f, .67f);
				colors[ImGuiCol_TitleBg]				= IMCOLOR_GRAY(22);
				colors[ImGuiCol_TitleBgActive]			= IMCOLOR_GRAY(22);
				colors[ImGuiCol_TitleBgCollapsed]		= IMCOLOR_GRAY(22);
				colors[ImGuiCol_MenuBarBg]				= LERP_GRAY(.1f);
				colors[ImGuiCol_ScrollbarBg]			= LERP_GRAY(0.23f);
				colors[ImGuiCol_ScrollbarGrab]			= action_color00_default;
				colors[ImGuiCol_ScrollbarGrabHovered]	= action_color00_hover;
				colors[ImGuiCol_ScrollbarGrabActive]	= action_color00_active;
				colors[ImGuiCol_CheckMark]				= action_color00_active;
				colors[ImGuiCol_SliderGrab]				= action_color00_default;
				colors[ImGuiCol_SliderGrabActive]		= action_color00_active;
				colors[ImGuiCol_Button]					= action_color00_default;
				colors[ImGuiCol_ButtonHovered]			= action_color00_hover;
				colors[ImGuiCol_ButtonActive]			= action_color00_active;
				colors[ImGuiCol_Header]					= LERP_GRAY(.3f);
				colors[ImGuiCol_HeaderHovered]			= LERP_GRAY(.4f);
				colors[ImGuiCol_HeaderActive]			= LERP_GRAY(.5f);
				colors[ImGuiCol_Separator]				= LERP_GRAY(.45f);
				colors[ImGuiCol_SeparatorHovered]		= LERP_GRAY(.45f);
				colors[ImGuiCol_SeparatorActive]		= LERP_GRAY(.45f);
				colors[ImGuiCol_ResizeGrip]				= action_color00_default;
				colors[ImGuiCol_ResizeGripHovered]		= action_color00_hover;
				colors[ImGuiCol_ResizeGripActive]		= action_color00_active;
				colors[ImGuiCol_Tab]					= LERP_MAIN_COLOR_DARK(0.4f);
				colors[ImGuiCol_TabHovered]				= LERP_MAIN_COLOR_DARK(0.5f);
				colors[ImGuiCol_TabActive]				= LERP_MAIN_COLOR_DARK(0.6f);
				colors[ImGuiCol_TabUnfocused]			= LERP_MAIN_COLOR_DARK(0.5f);
				colors[ImGuiCol_TabUnfocusedActive]		= LERP_MAIN_COLOR_DARK(0.6f);
				colors[ImGuiCol_TabSelectedOverline]	= ImVec4(0.f, 0.f, 0.f, 0.f);
				colors[ImGuiCol_DockingPreview]			= action_color00_active;
				colors[ImGuiCol_DockingEmptyBg]			= LERP_GRAY(0.2f);
				colors[ImGuiCol_PlotLines]				= ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
				colors[ImGuiCol_PlotLinesHovered]		= ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
				colors[ImGuiCol_PlotHistogram]			= LERP_MAIN_COLOR_DARK(.75f);
				colors[ImGuiCol_PlotHistogramHovered]	= action_color00_active;
				colors[ImGuiCol_TableHeaderBg]			= ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
				colors[ImGuiCol_TableBorderStrong]		= ImVec4(0.31f, 0.31f, 0.35f, 1.00f);   // Prefer using Alpha=1.0 here
				colors[ImGuiCol_TableBorderLight]		= ImVec4(0.23f, 0.23f, 0.25f, 1.00f);   // Prefer using Alpha=1.0 here
				colors[ImGuiCol_TableRowBg]				= ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
				colors[ImGuiCol_TableRowBgAlt]			= ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
				colors[ImGuiCol_TextSelectedBg]			= LERP_MAIN_COLOR_DARK(.4f);
				colors[ImGuiCol_DragDropTarget]			= LERP_MAIN_COLOR_DARK(.6f);
				colors[ImGuiCol_NavHighlight]			= ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
				colors[ImGuiCol_NavWindowingHighlight]	= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
				colors[ImGuiCol_NavWindowingDimBg]		= ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
				colors[ImGuiCol_ModalWindowDimBg]		= ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

			} break;

			case theme_selection::light: {

				action_color00_default = LERP_MAIN_COLOR_LIGHT(0.7f);
				action_color00_hover = LERP_MAIN_COLOR_LIGHT(0.85f);
				action_color00_active = LERP_MAIN_COLOR_LIGHT(1.f);

				action_color_gray_default = LERP_GRAY(0.2f);
				action_color_gray_hover = LERP_GRAY(0.27f);
				actionColorGrayActive = LERP_GRAY(0.35f);

				g_highlighted_window_bg = LERP_GRAY(0.8f);
				main_titlebar_color = LERP_MAIN_COLOR_LIGHT(.5f);

				colors[ImGuiCol_Text]					= LERP_GRAY(1.0f);
				colors[ImGuiCol_TextDisabled]			= LERP_GRAY(.7f);
				colors[ImGuiCol_WindowBg]				= LERP_GRAY(.25f);
				colors[ImGuiCol_ChildBg]				= LERP_GRAY(.25f);
				colors[ImGuiCol_PopupBg]				= LERP_GRAY(.25f);
				colors[ImGuiCol_Border]					= LERP_GRAY_A(0.2f, .50f);
				colors[ImGuiCol_BorderShadow]			= LERP_GRAY(.12f);
				colors[ImGuiCol_FrameBg]				= LERP_GRAY_A(.75f, .75f);
				colors[ImGuiCol_FrameBgHovered]			= LERP_GRAY_A(.70f, .75f);
				colors[ImGuiCol_FrameBgActive]			= LERP_GRAY_A(.65f, .75f);
				colors[ImGuiCol_TitleBg]				= action_color00_default;
				colors[ImGuiCol_TitleBgActive]			= action_color00_active;
				colors[ImGuiCol_TitleBgCollapsed]		= action_color00_default;
				colors[ImGuiCol_MenuBarBg]				= LERP_GRAY(.58f);
				colors[ImGuiCol_ScrollbarBg]			= LERP_GRAY(.75f);
				colors[ImGuiCol_ScrollbarGrab]			= action_color00_default;
				colors[ImGuiCol_ScrollbarGrabHovered]	= action_color00_hover;
				colors[ImGuiCol_ScrollbarGrabActive]	= action_color00_active;
				colors[ImGuiCol_CheckMark]				= action_color00_active;
				colors[ImGuiCol_SliderGrab]				= action_color00_default;
				colors[ImGuiCol_SliderGrabActive]		= action_color00_active;
				colors[ImGuiCol_Button]					= action_color00_default;
				colors[ImGuiCol_ButtonHovered]			= action_color00_hover;
				colors[ImGuiCol_ButtonActive]			= action_color00_active;
				colors[ImGuiCol_Header]					= LERP_GRAY(.75f);
				colors[ImGuiCol_HeaderHovered]			= LERP_GRAY(.7f);
				colors[ImGuiCol_HeaderActive]			= LERP_GRAY(.65f);
				colors[ImGuiCol_Separator]				= LERP_GRAY(.45f);
				colors[ImGuiCol_SeparatorHovered]		= LERP_GRAY(.45f);
				colors[ImGuiCol_SeparatorActive]		= LERP_GRAY(.45f);
				colors[ImGuiCol_ResizeGrip]				= action_color00_default;
				colors[ImGuiCol_ResizeGripHovered]		= action_color00_hover;
				colors[ImGuiCol_ResizeGripActive]		= action_color00_active;
				colors[ImGuiCol_Tab]					= action_color00_default;
				colors[ImGuiCol_TabHovered]				= action_color00_hover;
				colors[ImGuiCol_TabActive]				= action_color00_active;
				colors[ImGuiCol_TabUnfocused]			= LERP_MAIN_COLOR_LIGHT(0.5f);
				colors[ImGuiCol_TabUnfocusedActive]		= LERP_MAIN_COLOR_LIGHT(0.6f);
				colors[ImGuiCol_TabSelectedOverline]	= ImVec4(0.f, 0.f, 0.f, 0.f);
				colors[ImGuiCol_DockingPreview]			= action_color00_active;
				colors[ImGuiCol_DockingEmptyBg]			= LERP_GRAY(.2f);
				colors[ImGuiCol_PlotLines]				= ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
				colors[ImGuiCol_PlotLinesHovered]		= ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
				colors[ImGuiCol_PlotHistogram]			= LERP_MAIN_COLOR_LIGHT(.75f);
				colors[ImGuiCol_PlotHistogramHovered]	= action_color00_active;
				colors[ImGuiCol_TableHeaderBg]			= ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
				colors[ImGuiCol_TableBorderStrong]		= ImVec4(0.31f, 0.31f, 0.35f, 1.00f);	// Prefer using Alpha=1.0 here
				colors[ImGuiCol_TableBorderLight]		= ImVec4(0.23f, 0.23f, 0.25f, 1.00f);	// Prefer using Alpha=1.0 here
				colors[ImGuiCol_TableRowBg]				= ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
				colors[ImGuiCol_TableRowBgAlt]			= ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
				colors[ImGuiCol_TextSelectedBg]			= LERP_MAIN_COLOR_LIGHT(.4f);
				colors[ImGuiCol_DragDropTarget]			= ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
				colors[ImGuiCol_NavHighlight]			= ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
				colors[ImGuiCol_NavWindowingHighlight]	= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
				colors[ImGuiCol_NavWindowingDimBg]		= ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
				colors[ImGuiCol_ModalWindowDimBg]		= ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

			} break;

			default:
				break;
		}

	}

    // FUNCTION IMPLEMENTATION =========================================================================================

	
    ImVec4& get_main_color_ref()                        { return main_color; }
	
    ImVec4& get_main_titlebar_color_ref()               { return main_titlebar_color; }
	
    ImVec4& get_action_color00_faded_ref()              { return action_color00_faded; }
	
    ImVec4& get_action_color00_weak_ref()               { return action_color00_weak; }
	
    ImVec4& get_action_color00_default_ref()            { return action_color00_default; }
	
    ImVec4& get_action_color00_hover_ref()              { return action_color00_hover; }
	
    ImVec4& get_action_color00_active_ref()             { return action_color00_active; }
	
    ImVec4& get_default_gray_ref()                      { return default_gray; }
	
    ImVec4& get_default_gray1_ref()                     { return default_gray1; }
	
    ImVec4& get_action_color_gray_default_ref()         { return action_color_gray_default; }
	
    ImVec4& get_action_color_gray_hover_ref()           { return action_color_gray_hover; }
	
    ImVec4& get_action_color_gray_active_ref()          { return actionColorGrayActive; }

    ImGuiContext* get_context_imgui()                   { return s_context_imgui; }
    
    // ImPlotContext* get_context_implot()                 { return s_context_implot; }

	void init()
    {
		IMGUI_CHECKVERSION();
		s_context_imgui = ImGui::CreateContext();
		// s_context_implot = ImPlot::CreateContext();

        std::filesystem::path ini_path = GLT::util::get_executable_path() /
            GLT::config::get_filepath_from_config_type_ini(GLT::config::type::imgui);

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = ImStrdup(ini_path.string().c_str());
		io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
		// Viewport enable flags (require both ImGuiBackendFlags_PlatformHasViewports + ImGuiBackendFlags_RendererHasViewports set by the respective backends)
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;		// Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;		// Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking

        // Initialize the backend for ImGui
        ImGui::StyleColorsDark();

		g_highlighted_window_bg = LERP_GRAY(0.57f);

        serialize(GLT::serializer::option::load);
		main_titlebar_color = LERP_MAIN_COLOR_DARK(.5f);			// lerp after loading main color
		action_color00_faded = LERP_MAIN_COLOR_DARK(0.5f);
		action_color00_weak = LERP_MAIN_COLOR_DARK(0.6f);
		action_color00_default = LERP_MAIN_COLOR_DARK(0.7f);
		action_color00_hover = LERP_MAIN_COLOR_DARK(0.85f);
		action_color00_active = LERP_MAIN_COLOR_DARK(1.f);

		load_fonts();
		update_ui_theme();
		LOG_INIT
	}


	void shutdown()
    {
		ImGui::DestroyContext(s_context_imgui);
		// ImPlot::DestroyContext(s_context_implot);

		serialize(GLT::serializer::option::save);
		LOG_SHUTDOWN
	}


	void set_ui_theme_selection(theme_selection theme_selection) { g_ui_theme = theme_selection; }


	void enable_window_border(bool enable) {

		g_window_border = enable;
		//serialize(serializer::option::save);

		ImGuiStyle* style = &ImGui::GetStyle();
		style->WindowBorderSize = enable ? 1.f : 0.f;
	}


	void update_ui_colors(ImVec4 new_color)
    {
		main_color = new_color;
		serialize(GLT::serializer::option::save);
		update_ui_theme();
	}


	void resize_fonts(const f32 font_size)
    {
		g_font_size = font_size;
		g_font_size_small = font_size * 0.8f;
		g_big_font_size = font_size * 1.2f;
		g_font_size_header0 = font_size * 	1.2666666666f;
		g_font_size_header1 = font_size * 	1.5333333333f;
		g_font_size_header2 = font_size * 	1.8f;
		load_fonts();
	}


	void resize_fonts(const f32 regular, const f32 small, const f32 big, const f32 header_0,
        const f32 header_1, const f32 header_2)
    {
		g_font_size = regular;
		g_font_size_small = small;
		g_big_font_size = big;
		g_font_size_header0 = header_0;
		g_font_size_header1 = header_1;
		g_font_size_header2 = header_2;
		load_fonts();
	}


	ImFont* get_font(const font_type type)
    {
		if (s_fonts.contains(type))
        {
            return s_fonts.at(type);
        }
		return nullptr;
	}


	void serialize(const GLT::serializer::option option) {

        const auto confip_path = GLT::util::get_executable_path() / config_type_to_filepath(GLT::config::type::ui);
		GLT::serializer::yaml(confip_path, "theme", option)
			.entry(KEY_VALUE(g_font_size))
			.entry(KEY_VALUE(g_font_size_header0))
			.entry(KEY_VALUE(g_font_size_header1))
			.entry(KEY_VALUE(g_font_size_header2))
			.entry(KEY_VALUE(g_big_font_size))
			.entry(KEY_VALUE(g_font_size_small))
			.entry(KEY_VALUE(g_ui_theme))
			.entry(KEY_VALUE(g_window_border))
			.entry(KEY_VALUE(g_highlighted_window_bg))

			// color
			.entry(KEY_VALUE(main_color))
			.entry(KEY_VALUE(main_titlebar_color))

			// gray
			.entry(KEY_VALUE(action_color_gray_default))
			.entry(KEY_VALUE(action_color_gray_hover))
			.entry(KEY_VALUE(actionColorGrayActive));
	}

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
