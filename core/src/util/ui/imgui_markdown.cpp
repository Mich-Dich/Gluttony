
#include "util/pch.h"
#include "imgui_markdown.h"

#include <imgui.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <sstream>

#include "config/imgui_config.h"
#include "panel_collection.h"

#include "imgui_markdown.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::UI {

    // CONSTANTS =======================================================================================================

    const f32                                                   INDENTATION_SPACING = 6;

    static const bool                                           skip_empty_lines = false;

    static const ImU32                                          code_bg_color = ImColor(60, 60, 60);

    static const ImVec2                                         padding(15, 15);

    // MACROS ==========================================================================================================

    #define FONT_ITALIC                                         GLT::imgui_config::get_font(GLT::imgui_config::font_type::italic)

    #define FONT_BOLD                                           GLT::imgui_config::get_font(GLT::imgui_config::font_type::bold)

    #define FONT_HEADER_0                                       GLT::imgui_config::get_font(GLT::imgui_config::font_type::header0)

    #define FONT_HEADER_1                                       GLT::imgui_config::get_font(GLT::imgui_config::font_type::header1)

    #define FONT_HEADER_2                                       GLT::imgui_config::get_font(GLT::imgui_config::font_type::header2)

    #define FONT_HEADER_DEFAULT                                 GLT::imgui_config::get_font(GLT::imgui_config::font_type::header0)

    #define FONT_MONOSPACE_DEFAULT                              GLT::imgui_config::get_font(GLT::imgui_config::font_type::monospace_regular)

    #define VALIDATE_IF_POINTER_AT_TARGET_CHAR(pointer, target_char)                                                \
        if (markdown_context.markdown_text[pointer] != target_char)                                                 \
            return;


    #define MOVE_POINTER_TO_TARGET_CHAR(pointer, target_char)                                                       \
        while (pointer < markdown_context.markdown_length &&                                                        \
            markdown_context.markdown_text[pointer] != target_char &&                                               \
            markdown_context.markdown_text[pointer] != '\n')                                                        \
            pointer++;                                                                                              \
        if (pointer >= markdown_context.markdown_length ||                                                          \
            markdown_context.markdown_text[pointer] != target_char)                                                 \
            return

    // TYPES ===========================================================================================================

    enum class line_purpose {

        empty = 0,
        normal,
        bullet,
        headline,
        highlight_block,
        speratom,
        table,
    };


    struct link {

        link(u64 text_start = 0)
            : text_start(text_start), text_end(text_start) {}

        u64                                                     text_start = 0, text_end = 0;
        u64                                                     content_start = 0, content_end = 0;
        void*                                                   user_data{};
    };


    struct markdown_context {

        markdown_context(const char* markdown_text, size_t markdown_length)
            : markdown_text(markdown_text), markdown_length(markdown_length) {}

        const char*                                             markdown_text;
        size_t                                                  markdown_length;
        u64                                                     indentation_last_line = 0;
        u64                                                     indentation = 0;
        u64                                                     line_start = 0;
        u64                                                     line_end = 0;
    };

    // STATIC VARIABLES ================================================================================================

    static ImVec2                                               button_size{};

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Helper function to split a line into table cells
    static std::vector<std::string> split_table_line(const char* line_start, const char* line_end);


    // Helper function to check if a line is a table separator
    static bool is_table_separator(const char* line_start, const char* line_end);


    // Helper function to parse and render a table
    static bool parse_and_render_table(markdown_context& context, u64& current_pos);


    FORCE_INLINE static void render_text_wrapped(const char* text, const char* text_end);


    FORCE_INLINE static f32 calculate_text_height(const char* text, const char* text_end);


    // Helper function to check for and skip comments
    FORCE_INLINE static bool check_for_comment(markdown_context& markdown_context, u64& start_position);


    FORCE_INLINE static void check_for_link(markdown_context& markdown_context, u64& start_position);

    // Text `highlight` Text
    FORCE_INLINE static void check_for_highlight(markdown_context& markdown_context, u64& start_position, const ImU32 background_color);


    FORCE_INLINE static void check_for_emphasis(markdown_context& markdown_context, u64& start_position);


    // Helper function to render text with a background
    void RenderTextWithBackground(const char* text_start, const char* text_end, const ImU32 background_color);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static std::vector<std::string> split_table_line(const char* line_start, const char* line_end) {

        std::vector<std::string> cells;
        const char* cell_start = line_start;

        while (cell_start < line_end) {
            // Skip leading spaces
            while (cell_start < line_end && (*cell_start == ' ' || *cell_start == '\t')) {
                cell_start++;
            }

            // Find cell end (either '|' or end of line)
            const char* cell_end = cell_start;
            while (cell_end < line_end && *cell_end != '|') {
                cell_end++;
            }

            // Trim trailing spaces
            const char* trimmed_end = cell_end;
            while (trimmed_end > cell_start && (trimmed_end[-1] == ' ' || trimmed_end[-1] == '\t')) {
                trimmed_end--;
            }

            // Extract cell content
            if (cell_start < trimmed_end) {
                cells.push_back(std::string(cell_start, trimmed_end));
            } else {
                // cells.push_back(""); // Empty cell
            }

            // Move to next cell
            cell_start = (cell_end < line_end) ? cell_end + 1 : line_end;
        }

        return cells;
    }


    static bool is_table_separator(const char* line_start, const char* line_end) {

        bool has_colon = false;
        bool has_dash = false;
        for (const char* p = line_start; p < line_end; p++) {
            if (*p == ':') {
                has_colon = true;
            } else if (*p == '-') {
                has_dash = true;
            } else if (*p != '|' && *p != ' ' && *p != '\t') {
                return false; // Invalid character in separator
            }
        }

        return has_dash; // Must have at least one dash
    }


    static bool parse_and_render_table(markdown_context& context, u64& current_pos) {

        u64 start_pos = current_pos;

        // Store the first line (header)
        u64 header_start = start_pos;
        while (header_start < context.markdown_length &&
               context.markdown_text[header_start] != '|') {
            header_start++;
        }

        u64 header_end = header_start;
        while (header_end < context.markdown_length &&
               context.markdown_text[header_end] != '\n') {
            header_end++;
        }

        // Parse header cells
        std::vector<std::string> header_cells = split_table_line(context.markdown_text + header_start,
            context.markdown_text + header_end);

        if (header_cells.empty()) {
            return false;
        }

        // Move to next line (separator)
        u64 sep_start = header_end + 1;
        if (sep_start >= context.markdown_length) {
            return false;
        }

        u64 sep_end = sep_start;
        while (sep_end < context.markdown_length &&
               context.markdown_text[sep_end] != '\n') {
            sep_end++;
        }

        // Check if this is a valid separator
        if (!is_table_separator(context.markdown_text + sep_start, context.markdown_text + sep_end)) {
            return false;
        }

        // Parse separator cells to get column count
        std::vector<std::string> sep_cells = split_table_line(context.markdown_text + sep_start,
            context.markdown_text + sep_end);

        if (sep_cells.size() != header_cells.size()) {
            return false;
        }

        // Collect all data rows
        std::vector<std::vector<std::string>> data_rows;
        u64 row_start = sep_end + 1;

        while (row_start < context.markdown_length) {
            // Skip empty lines
            while (row_start < context.markdown_length
                && (context.markdown_text[row_start] == '\n' || context.markdown_text[row_start] == '\r')) {
                row_start++;
            }

            // Check if this is still part of the table
            if (row_start >= context.markdown_length
                || context.markdown_text[row_start] != '|') {
                break;
            }

            u64 row_end = row_start;
            while (row_end < context.markdown_length &&
                   context.markdown_text[row_end] != '\n') {
                row_end++;
            }

            // Parse row cells
            std::vector<std::string> row_cells = split_table_line(context.markdown_text + row_start,
                context.markdown_text + row_end);

            // If row doesn't have the right number of cells, stop parsing table
            if (row_cells.size() != header_cells.size()) {
                break;
            }

            data_rows.push_back(row_cells);
            row_start = row_end + 1;
        }

        // Now render the table
        ImGui::NewLine();

        // Create a table with borders
        if (ImGui::BeginTable("##MarkdownTable", header_cells.size(),
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {

            // Setup columns
            for (size_t i = 0; i < header_cells.size(); i++) {
                ImGui::TableSetupColumn(header_cells[i].c_str());
            }
            ImGui::TableHeadersRow();

            // Add data rows
            for (const auto& row : data_rows) {
                ImGui::TableNextRow();
                for (size_t col = 0; col < row.size(); col++) {
                    ImGui::TableSetColumnIndex(col);

                    // Get column width for wrapping
                    const float col_width = ImGui::GetContentRegionAvail().x;
                    const float wrap_pos = ImGui::GetCursorPosX() + col_width - ImGui::GetStyle().CellPadding.x * 2;
                    ImGui::PushTextWrapPos(wrap_pos);

                    // Check if cell contains inline code
                    std::string cell_text = row[col];
                    size_t code_start = cell_text.find('`');
                    if (code_start != std::string::npos) {
                        // Parse inline code in cell
                        size_t pos = 0;
                        while (pos < cell_text.length()) {
                            if (cell_text[pos] == '`') {
                                size_t code_end = cell_text.find('`', pos + 1);
                                if (code_end != std::string::npos) {
                                    // Text before code
                                    if (pos > 0) {
                                        ImGui::TextUnformatted(cell_text.substr(0, pos).c_str());
                                        ImGui::SameLine();
                                    }

                                    // Code with background
                                    std::string code = cell_text.substr(pos + 1, code_end - pos - 1);
                                    ImGui::PushFont(FONT_MONOSPACE_DEFAULT);
                                    ImVec2 text_pos = ImGui::GetCursorScreenPos();
                                    ImVec2 text_size = ImGui::CalcTextSize(code.c_str());

                                    ImVec2 code_padding(4, 2);
                                    ImGui::GetWindowDrawList()->AddRectFilled(
                                        text_pos - code_padding,
                                        ImVec2(text_pos.x + text_size.x + code_padding.x,
                                               text_pos.y + text_size.y + code_padding.y),
                                        code_bg_color,
                                        ImGui::GetStyle().FrameRounding
                                    );

                                    ImGui::TextUnformatted(code.c_str());
                                    ImGui::SameLine();
                                    ImGui::PopFont();

                                    cell_text = cell_text.substr(code_end + 1);
                                    pos = 0;
                                } else {
                                    pos++;
                                }
                            } else {
                                pos++;
                            }
                        }

                        // Remaining text
                        if (!cell_text.empty()) {
                            ImGui::TextUnformatted(cell_text.c_str());
                        }
                    } else {
                        // Regular cell text
                        ImGui::TextUnformatted(row[col].c_str());
                    }
                    ImGui::PopTextWrapPos();
                }
            }

            ImGui::EndTable();
        }

        ImGui::NewLine();

        // Update current position to after the table
        current_pos = row_start - 1;
        return true;
    }


    FORCE_INLINE static void render_text_wrapped(const char* text, const char* text_end) {

        f32 scale = ImGui::GetIO().FontGlobalScale;
        f32 width_left = ImGui::GetContentRegionAvail().x -20;
        const char* endLine = ImGui::GetFont()->CalcWordWrapPositionA(scale, text, text_end, width_left);
        ImGui::TextUnformatted(text, endLine);
        width_left = ImGui::GetContentRegionAvail().x;
        while (endLine < text_end) {
            text = endLine;
            if (*text == ' ') ++text;
            endLine = ImGui::GetFont()->CalcWordWrapPositionA(scale, text, text_end, width_left);
            if (text == endLine) endLine++;
            ImGui::TextUnformatted(text, endLine);
        }
    }


    FORCE_INLINE static f32 calculate_text_height(const char* text, const char* text_end) {

        f32 scale = ImGui::GetIO().FontGlobalScale;
        f32 line_height = ImGui::GetTextLineHeight();
        f32 spacing = ImGui::GetTextLineHeightWithSpacing() - line_height;
        f32 width_left = ImGui::GetContentRegionAvail().x;

        f32 total_height = line_height + spacing;
        const char* endLine = ImGui::GetFont()->CalcWordWrapPositionA(scale, text, text_end, width_left);
        while (endLine < text_end) {
            text = endLine;
            if (*text == ' ') ++text;
            endLine = ImGui::GetFont()->CalcWordWrapPositionA(scale, text, text_end, width_left);
            if (text == endLine) endLine++;
            total_height += line_height + spacing;
        }
        return total_height;
    }


    FORCE_INLINE static bool check_for_comment(markdown_context& markdown_context, u64& start_position) {

        // Check if we have at least 4 characters for "<!--"
        if (start_position + 3 >= markdown_context.markdown_length)
            return false;

        // Check if we have the comment opening sequence
        if (markdown_context.markdown_text[start_position] == '<' &&
            markdown_context.markdown_text[start_position + 1] == '!' &&
            markdown_context.markdown_text[start_position + 2] == '-' &&
            markdown_context.markdown_text[start_position + 3] == '-') {

            u64 comment_end = start_position + 4; // Skip past "<!--"

            // Find the closing "-->"
            while (comment_end + 2 < markdown_context.markdown_length) {
                if (markdown_context.markdown_text[comment_end] == '-' &&
                    markdown_context.markdown_text[comment_end + 1] == '-' &&
                    markdown_context.markdown_text[comment_end + 2] == '>') {
                    // Found the closing "-->"
                    start_position = comment_end + 3; // Skip past "-->"
                    markdown_context.line_start = start_position; // Update line start
                    return true;
                }
                comment_end++;
            }

            // If we reach here, we didn't find a closing "-->"
            // Skip to the end of the line or file
            while (comment_end < markdown_context.markdown_length &&
                   markdown_context.markdown_text[comment_end] != '\n') {
                comment_end++;
            }
            start_position = comment_end;
            markdown_context.line_start = start_position;
            return true;
        }

        return false;
    }


    FORCE_INLINE static void check_for_link(markdown_context& markdown_context, u64& start_position) {

        link loc_link(start_position + 1);

        // ------------------------------------ locate link text ------------------------------------
        MOVE_POINTER_TO_TARGET_CHAR(loc_link.text_end, ']');

        // ------------------------------------ locate link content ------------------------------------
        VALIDATE_IF_POINTER_AT_TARGET_CHAR(loc_link.text_end + 1, '(')
        loc_link.content_start = loc_link.content_end = loc_link.text_end + 2;
        MOVE_POINTER_TO_TARGET_CHAR(loc_link.content_end, ')');

        #ifdef MARKDOWN_LINK_DEBUG
            ImGui::Text("TEXT: [");
            ImGui::SameLine();

            render_text_wrapped(markdown_context.markdown_text + markdown_context.line_start + markdown_context.indentation, markdown_context.markdown_text + loc_link.text_start-1);

            ImGui::SameLine();
            ImGui::Text("] LINK-TEXT: [");
            ImGui::SameLine();

            render_text_wrapped(markdown_context.markdown_text + loc_link.text_start, markdown_context.markdown_text + loc_link.text_end);

            ImGui::SameLine();
            ImGui::Text("] LINK-CONTENT: [");
            ImGui::SameLine();

            render_text_wrapped(markdown_context.markdown_text + loc_link.content_start, markdown_context.markdown_text + loc_link.content_end);

            ImGui::SameLine();
            ImGui::Text("]");
        #else
            render_text_wrapped(markdown_context.markdown_text + markdown_context.line_start + markdown_context.indentation, markdown_context.markdown_text + loc_link.text_start - 1);
            ImGui::SameLine();
            render_text_wrapped(markdown_context.markdown_text + loc_link.text_start, markdown_context.markdown_text + loc_link.text_end);
            ImGui::SameLine();
        #endif

        markdown_context.line_start = loc_link.content_end;
        start_position = loc_link.content_end;
    }


    FORCE_INLINE static void check_for_highlight(markdown_context& markdown_context, u64& start_position, const ImU32 background_color) {

        u64 highlight_start = start_position;
        u64 highlight_end = start_position + 1;
        MOVE_POINTER_TO_TARGET_CHAR(highlight_end, '`');

        // Render text before the highlight
        render_text_wrapped(markdown_context.markdown_text + markdown_context.line_start + markdown_context.indentation, markdown_context.markdown_text + highlight_start);
        ImGui::SameLine();

        const char* START = markdown_context.markdown_text + highlight_start + 1;
        const char* END = markdown_context.markdown_text + highlight_end;

        // Use monospace font for code
        ImGui::PushFont(FONT_MONOSPACE_DEFAULT);

        // Calculate text size and draw background
        ImVec2 text_pos = ImGui::GetCursorScreenPos();
        ImVec2 text_size = ImGui::CalcTextSize(START, END);

        // Add some padding similar to code blocks
        ImVec2 padding(4, 2);
        ImGui::GetWindowDrawList()->AddRectFilled(
            text_pos - padding,
            ImVec2(text_pos.x + text_size.x + padding.x, text_pos.y + text_size.y + padding.y),
            background_color,
            ImGui::GetStyle().FrameRounding
        );

        // Render the highlighted text
        ImGui::TextUnformatted(START, END);
        ImGui::SameLine();

        ImGui::PopFont();

        // CORRECTED: Skip the closing backtick by setting line_start to after the highlight
        markdown_context.line_start = highlight_end + 1;  // +1 to skip the closing backtick
        start_position = highlight_end;  // This will be incremented in the calling loop
    }


    FORCE_INLINE static void check_for_emphasis(markdown_context& markdown_context, u64& start_position) {

        u64 emphasis_start = start_position;
        VALIDATE_IF_POINTER_AT_TARGET_CHAR(emphasis_start + 1, '*');                    // emphasis needs 2 starts

        u64 emphasis_end = start_position + 2;
        MOVE_POINTER_TO_TARGET_CHAR(emphasis_end, '*');
        VALIDATE_IF_POINTER_AT_TARGET_CHAR(emphasis_end + 1, '*');                      // emphasis needs 2 starts


        render_text_wrapped(markdown_context.markdown_text + markdown_context.line_start + markdown_context.indentation, markdown_context.markdown_text + emphasis_start);
        ImGui::SameLine();

        ImGui::PushFont(FONT_BOLD);
        render_text_wrapped(markdown_context.markdown_text + emphasis_start +2, markdown_context.markdown_text + emphasis_end);
        ImGui::SameLine();
        ImGui::PopFont();

        markdown_context.line_start = emphasis_end + 2;
        start_position = emphasis_end + 1;
    }


    void RenderTextWithBackground(const char* text_start, const char* text_end, const ImU32 background_color) {

        if (text_start >= text_end)
            return;

        ImVec2 text_pos = ImGui::GetCursorScreenPos();
        ImGui::PushFont(FONT_MONOSPACE_DEFAULT);

        // Calculate the total size needed
        std::string code_text(text_start, text_end);
        std::stringstream ss(code_text);
        std::string line;
        float max_width = 0.0f;
        int line_count = 0;

        // First pass: calculate dimensions
        while (std::getline(ss, line)) {
            ImVec2 line_size = ImGui::CalcTextSize(line.c_str());
            max_width = std::max(max_width, line_size.x);
            line_count++;
        }

        float line_height = ImGui::GetTextLineHeight() * 1.25f;
        float total_height = line_count * line_height + padding.y * 2;
        float total_width = max_width + padding.x * 2;

        // Draw background
        ImGui::GetWindowDrawList()->AddRectFilled(
            text_pos,
            ImVec2(text_pos.x + total_width, text_pos.y + total_height),
            background_color,
            ImGui::GetStyle().FrameRounding
        );

        // Copy button
        ImGui::SetCursorScreenPos(ImVec2(text_pos.x + 5, text_pos.y + 5));
        std::string button_id = "C##" + std::to_string(reinterpret_cast<uintptr_t>(text_start));
        if (ImGui::Button(button_id.c_str(), button_size)) {
            std::string text_to_copy(text_start, text_end);
            ImGui::SetClipboardText(text_to_copy.c_str());
        }

        // Render code lines
        ImGui::SetCursorScreenPos(ImVec2(text_pos.x + padding.x, text_pos.y + padding.y));

        std::stringstream ss2(code_text);
        while (std::getline(ss2, line)) {
            ImGui::TextUnformatted(line.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding.x);
        }

        ImGui::PopFont();

        // Move cursor down after the code block
        ImGui::SetCursorScreenPos(ImVec2(text_pos.x, text_pos.y + total_height));
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void markdown(const std::string_view& markdown_text, const bool new_line_at_end) {

        markdown(markdown_text.data(), markdown_text.size(), new_line_at_end);
    }


    void markdown(const char* markdown_text, size_t markdown_length, const bool new_line_at_end) {

        //SCOPED_PROFILER(700, "markdown imgui conversion", AT::duration_precision::microseconds);

        const f32 line_height = ImGui::GetTextLineHeight();
        const f32 height = line_height + ((ImGui::GetTextLineHeightWithSpacing() - line_height) * 2);
        button_size = ImVec2(height, height);
        const char* sub_section_begin = nullptr;
        line_purpose last_line_purpose = line_purpose::empty;
        markdown_context loc_context{ markdown_text, markdown_length };
        char c = 0;

        ImGui::BeginGroup();

        for (u64 x = 0; x < (u64)markdown_length; x++) {

            loc_context.line_start = x;

            c = markdown_text[x];          // get the character at index
            if (c == 0) { break; }         // shouldn't happen but don't go beyond 0.

            loc_context.indentation = 0;

            {   // ------------------------------ handle indentation ------------------------------
                while (markdown_text[x] == '\t' || markdown_text[x] == ' ') {

                    x++;
                    loc_context.indentation++;
                }

                if (loc_context.indentation > loc_context.indentation_last_line)
                    ImGui::Indent((f32)(loc_context.indentation - loc_context.indentation_last_line) * INDENTATION_SPACING);
                else if (loc_context.indentation_last_line > loc_context.indentation)
                    ImGui::Unindent((f32)(loc_context.indentation_last_line - loc_context.indentation) * INDENTATION_SPACING);

                loc_context.indentation_last_line = loc_context.indentation;
            }

            // Check for comments at the start of the line (after indentation)
            if (check_for_comment(loc_context, x)) {
                last_line_purpose = line_purpose::empty;
                continue;
            }

            // Check for table at the start of the line
            if (markdown_text[x] == '|') {
                if (parse_and_render_table(loc_context, x)) {
                    last_line_purpose = line_purpose::table;
                    continue;
                }
            }

            switch (markdown_text[x]) {         // check start of line
                case '#': {                     // line is a header

                    u32 heading_counter = 1;
                    while (markdown_text[x + heading_counter] == '#')
                        heading_counter++;

                    if (markdown_text[x + heading_counter] != ' ')        // not real headline (headline '#' needs to be followed be ' ')
                        break;

                    x += (u64)heading_counter;
                    while (markdown_text[x] != '\n' && x < markdown_length)     // move pointer to end of line
                        x++;

                    ImGui::NewLine();
                    ImVec4 header_color = GLT::imgui_config::get_main_color_ref();
                    ImGui::PushStyleColor(ImGuiCol_Text, header_color);

                    switch (heading_counter) {
                        case 1:  ImGui::PushFont(FONT_HEADER_0); break;
                        case 2:  ImGui::PushFont(FONT_HEADER_1); break;
                        case 3:  ImGui::PushFont(FONT_HEADER_2); break;
                        default: ImGui::PushFont(FONT_HEADER_DEFAULT); break;
                    }
                    render_text_wrapped(markdown_text + loc_context.line_start + loc_context.indentation + heading_counter, markdown_text + x);

                    if (heading_counter == 1)
                        ImGui::Separator();

                    ImGui::PopFont();
                    ImGui::PopStyleColor();

                    last_line_purpose = line_purpose::headline;
                    continue;

                } break;

                case '-': {                     // Line begins with bullet point

                    if (markdown_text[x + 1] == '-' && markdown_text[x + 2] == '-') {

                        ImGui::NewLine();
                        ImGui::Separator();
                        ImGui::NewLine();
                        x += 3;
                        last_line_purpose = line_purpose::speratom;
                        continue;

                    } else {

                        loc_context.line_start++;
                        //x++;
                        ImGui::Bullet();
                        ImGui::SameLine();
                    }

                }break;

                case '\n': {

                    if (loc_context.line_start + loc_context.indentation == x) {

                        if (last_line_purpose != line_purpose::empty && last_line_purpose != line_purpose::headline)
                            ImGui::NewLine();

                        last_line_purpose = line_purpose::empty;
                        continue;
                    }

                } break;

                case '`': {
                    if (markdown_text[x + 1] == '`' && markdown_text[x + 2] == '`') {
                        // Found start of code block ```
                        u64 code_block_start = x + 3; // Skip the opening ```

                        // Find the end of the first line (after language identifier)
                        u64 y = code_block_start;
                        while (y < markdown_length && markdown_text[y] != '\n') {
                            y++;
                        }

                        // Now find the closing ```
                        u64 code_block_end = y + 1; // Start after the first newline
                        bool found_end = false;

                        while (code_block_end + 2 < markdown_length) {
                            if (markdown_text[code_block_end] == '`' &&
                                markdown_text[code_block_end + 1] == '`' &&
                                markdown_text[code_block_end + 2] == '`') {
                                found_end = true;
                                break;
                            }
                            code_block_end++;
                        }

                        if (!found_end) {
                            // Code block never closed, render what we have
                            code_block_end = markdown_length;
                        }

                        // Extract code content (from after first newline to before closing ```)
                        const char* code_content_start = markdown_text + y + 1;
                        const char* code_content_end = markdown_text + code_block_end;

                        // Trim trailing whitespace and newlines
                        while (code_content_end > code_content_start &&
                            (code_content_end[-1] == '\n' || code_content_end[-1] == '\r' || code_content_end[-1] == ' ')) {
                            code_content_end--;
                        }

                        // Render the code block
                        if (code_content_end > code_content_start) {
                            RenderTextWithBackground(code_content_start, code_content_end, ImColor(60, 60, 60));
                        }

                        // Move x to the end of the code block
                        x = code_block_end + 2; // Position after closing ```
                        last_line_purpose = line_purpose::highlight_block;
                        continue;

                    } else {
                        // Single backtick for inline code - use existing check_for_highlight
                        check_for_highlight(loc_context, x, code_bg_color);
                    }
                } break;

                default: break;
            }

            while (markdown_text[x] != '\n' && x < markdown_length) {
                // Check for comments in the middle of the line
                if (check_for_comment(loc_context, x)) {
                    // Continue processing the rest of the line after the comment
                    continue;
                }

                switch (markdown_text[x]) {                                 // check for spatial characters in line

                    case '[':                                               // check if '[' starts a link
                        check_for_link(loc_context, x);
                        break;

                    case '`':                                               // check if '`' is used as emphasis
                        check_for_highlight(loc_context, x, code_bg_color);
                        break;

                    case '*':                                               // check if '*' is used as emphasis
                        check_for_emphasis(loc_context, x);
                        break;

                    default: break;
                }
                x++;
            }

            // normal text - but check if we have anything left to render after comments
            if (loc_context.line_start + loc_context.indentation < x) {
                last_line_purpose = line_purpose::normal;
                render_text_wrapped(markdown_text + loc_context.line_start + loc_context.indentation, markdown_text + x);
            }
        }

        if (new_line_at_end)
        {
            ImGui::NewLine();
            ImGui::NewLine();
        }

        ImGui::EndGroup();
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
