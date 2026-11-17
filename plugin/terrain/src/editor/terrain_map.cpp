
#include "util/pch.h"
#include "terrain_map.h"



namespace GLT::terrain {

    namespace {

        [[nodiscard]] u32 pack(f32 r, f32 g, f32 b) {
            auto c = [](f32 v) { return static_cast<u32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return IM_COL32(c(r), c(g), c(b), 255);
        }

        // Height tint: deep blue-green lowlands, green, brown, grey rock, snow
        [[nodiscard]] glm::vec3 height_tint(f32 t) {
            static const glm::vec3 stops[] = {
                { 0.16f, 0.28f, 0.20f }, { 0.30f, 0.45f, 0.22f }, { 0.55f, 0.50f, 0.30f }, { 0.55f, 0.52f, 0.50f }, { 0.95f, 0.95f, 0.97f } };
            constexpr i32 last = static_cast<i32>(std::size(stops)) - 1;
            const f32 x = std::clamp(t, 0.0f, 1.0f) * static_cast<f32>(last);
            const i32 i = std::min(static_cast<i32>(x), last - 1);
            return glm::mix(stops[i], stops[i + 1], x - static_cast<f32>(i));
        }

        [[nodiscard]] glm::vec3 heat(f32 t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return glm::vec3(std::min(t * 3.0f, 1.0f), std::clamp(t * 3.0f - 1.0f, 0.0f, 1.0f), std::clamp(t * 3.0f - 2.0f, 0.0f, 1.0f));
        }

        [[nodiscard]] u64 hash_f64(u64 h, f64 v) { return hash_combine(h, std::bit_cast<u64>(v)); }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    terrain_map::terrain_map(evaluator& eval) : m_eval(eval) {}


    terrain_map::~terrain_map() {

        // The task references only the evaluator (outlives every editor window): no need to wait
        if (m_cancel)
            m_cancel->store(true);
    }


    void terrain_map::frame(glm::dvec2 min, glm::dvec2 max) {

        m_frame_request = std::make_pair(min, max);
    }


    void terrain_map::collect() {

        if (!m_pending.valid() || m_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        eval_outcome outcome = m_pending.get();
        if (!outcome) {
            if (outcome.error() != "cancelled") {
                m_status = outcome.error();
                m_failed = true;
                m_failed_key = m_pending_key;       // e.g. a cycle: retrying the same state would fail again, forever
            }
            m_pending_key = 0;
            return;
        }

        m_field = outcome->data;
        m_shown_key = m_pending_key;
        m_shown_center = m_pending_center;
        m_shown_view = m_pending_view;
        m_pending_key = 0;
        colorize(*m_field, m_pending_mask);

        m_failed = false;
        m_stats = std::format("{:.0f} ms, {} node(s) run, {} cached", outcome->milliseconds, outcome->nodes_evaluated, outcome->cache_hits);
        m_status.clear();
        for (const auto& warning : outcome->warnings)
            m_status += (m_status.empty() ? "" : "\n") + warning;
    }


    void terrain_map::start(const target& what, u64 key) {

        if (m_cancel)
            m_cancel->store(true);

        // RESOLUTION samples along the longer side, the shorter one in proportion
        constexpr f64 RES = static_cast<f64>(RESOLUTION);
        const u32 width = m_aspect <= 1.0 ? RESOLUTION : std::max(16u, static_cast<u32>(std::lround(RES / m_aspect)));
        const u32 height = m_aspect <= 1.0 ? std::max(16u, static_cast<u32>(std::lround(RES * m_aspect))) : RESOLUTION;
        const f64 view_height = m_view * m_aspect;
        const f64 spacing = m_view / static_cast<f64>(width);

        eval_request request{};
        request.graph = what.graph;
        request.node = what.node;
        request.pin = what.pin;
        request.seed = what.seed;
        request.parameters = what.parameters;
        request.area.spacing = spacing;
        request.area.x0 = static_cast<i64>(std::floor((m_center.x - m_view * 0.5) / spacing));
        request.area.y0 = static_cast<i64>(std::floor((m_center.y - view_height * 0.5) / spacing));
        request.area.width = width;
        request.area.height = height;

        m_cancel = std::make_shared<std::atomic<bool>>(false);
        m_pending = m_eval.evaluate_async(std::move(request), m_cancel);
        m_pending_key = key;
        m_pending_start = std::chrono::steady_clock::now();
        m_pending_mask = what.mask;
        m_pending_center = m_center;
        m_pending_view = m_view;
    }


    void terrain_map::colorize(const field& data, bool mask) {

        const region& r = data.area();
        m_texture.ensure(r.width, r.height);
        m_pixels.resize(static_cast<size_t>(r.width) * r.height);

        const auto values = data.values();
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        m_min = values.empty() ? 0.0f : *lo;
        m_max = values.empty() ? 0.0f : *hi;
        const f32 range = m_max - m_min > 1e-6f ? m_max - m_min : 1.0f;

        const glm::vec3 light = glm::normalize(glm::vec3(-1.0f, 1.2f, -1.0f));     // from the upper left
        const f32 inv = static_cast<f32>(0.5 / r.spacing);

        size_t i = 0;
        for (i64 gy = r.y0; gy < r.y1(); ++gy)
            for (i64 gx = r.x0; gx < r.x1(); ++gx, ++i) {
                const f32 v = data.at(gx, gy);
                if (mask) {
                    const glm::vec3 c = heat(v);
                    m_pixels[i] = pack(c.r, c.g, c.b);
                    continue;
                }
                // Normal from central differences (clamped at the border), y up
                const f32 dx = (data.at(gx + 1, gy) - data.at(gx - 1, gy)) * inv;
                const f32 dz = (data.at(gx, gy + 1) - data.at(gx, gy - 1)) * inv;
                const glm::vec3 n = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
                const f32 shade = 0.35f + 0.65f * std::max(glm::dot(n, light), 0.0f);
                const glm::vec3 c = height_tint((v - m_min) / range) * shade;
                m_pixels[i] = pack(c.r, c.g, c.b);
            }
        m_texture.upload(m_pixels);
    }


    void terrain_map::draw(const target& what, ImVec2 size) {

        user_texture::collect_released();
        collect();

        // ---- layout: the map fills [size] ----------------------------------------------------------------------------
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 extent(std::max(size.x, 32.0f), std::max(size.y, 32.0f));
        const ImVec2 p1(p0.x + extent.x, p0.y + extent.y);
        const f32 px_per_unit_view = extent.x;          // screen pixels per m_view (both axes use the same scale)

        // Aspect quantised, so a resize does not restart the evaluation on every pixel
        m_aspect = std::clamp(std::round(static_cast<f64>(extent.y / extent.x) * 64.0) / 64.0, 1.0 / 16.0, 16.0);
        if (m_frame_request) {
            const auto [min, max] = *m_frame_request;
            m_center = (min + max) * 0.5;
            m_view = std::clamp(std::max(max.x - min.x, (max.y - min.y) / m_aspect) * 1.05, 16.0, 4.0e6);
            m_frame_request.reset();
        }
        f64 view_height = m_view * m_aspect;

        ImGui::InvisibleButton("##terrain_map", extent, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        // world <-> screen of the current view
        auto to_world = [&](ImVec2 s) {
            return glm::dvec2(m_center.x + (static_cast<f64>(s.x - p0.x) / extent.x - 0.5) * m_view,
                              m_center.y + (static_cast<f64>(s.y - p0.y) / extent.y - 0.5) * view_height);
        };
        auto to_screen = [&](glm::dvec2 w) {
            return ImVec2(p0.x + static_cast<f32>(((w.x - m_center.x) / m_view + 0.5) * extent.x),
                          p0.y + static_cast<f32>(((w.y - m_center.y) / view_height + 0.5) * extent.y));
        };

        // ---- input: drag pans, wheel zooms about the cursor ------------------------------------------------------------
        ImGuiIO& io = ImGui::GetIO();
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
            m_center.x -= static_cast<f64>(io.MouseDelta.x) / px_per_unit_view * m_view;
            m_center.y -= static_cast<f64>(io.MouseDelta.y) / px_per_unit_view * m_view;
        }
        if (hovered || active)
            ImGui::SetMouseCursor(active ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
        if (hovered && io.MouseWheel != 0.0f) {
            const glm::dvec2 anchor = to_world(io.MousePos);
            const f64 next = std::clamp(m_view * std::pow(0.85, static_cast<f64>(io.MouseWheel)), 16.0, 4.0e6);
            m_center = anchor + (m_center - anchor) * (next / m_view);
            m_view = next;
            view_height = m_view * m_aspect;
            io.MouseWheel = 0.0f;
        }

        // ---- (re)evaluate when anything changed --------------------------------------------------------------------
        u64 key = 0;
        if (what.graph && what.node != INVALID_NODE_ID) {
            key = hash_combine(what.revision, what.node);
            key = hash_combine(key, hash_string(what.pin));
            key = hash_combine(key, what.seed);
            key = hash_combine(key, what.parameters_hash);
            key = hash_combine(key, what.mask ? 1u : 0u);
            key = hash_f64(hash_f64(hash_f64(hash_f64(key, m_center.x), m_center.y), m_view), m_aspect);
            key = std::max<u64>(key, 1);
        }
        // One evaluation at a time, so dragging a slider or the map never piles up work on the thread pool. An out-of-date
        // evaluation may finish (the map keeps following a dragged slider); only one running long is cancelled (it stops
        // at its next node) so the current state does not wait for it
        if (key != 0 && key != m_shown_key && key != m_pending_key && key != m_failed_key) {
            if (!m_pending.valid())
                start(what, key);
            else if (m_cancel && !active && std::chrono::steady_clock::now() - m_pending_start > std::chrono::milliseconds(150))
                m_cancel->store(true);
        }
        // ---- draw ----------------------------------------------------------------------------------------------------
        constexpr f32 ROUNDING = 6.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, p1, IM_COL32(16, 17, 20, 255), ROUNDING);
        draw->PushClipRect(p0, p1, true);
        if (key != 0 && m_texture.valid() && m_field) {
            // The shown result where it lies in the current view (panning moves it at once, the refined one follows)
            const region& r = m_field->area();
            const glm::dvec2 w0 = r.position(r.x0, r.y0) - glm::dvec2(r.spacing * 0.5);
            const glm::dvec2 w1 = r.position(r.x1(), r.y1()) - glm::dvec2(r.spacing * 0.5);
            draw->AddImage(m_texture.data()->GetTexRef(), to_screen(w0), to_screen(w1));
        }

        // World origin axes, for orientation
        const ImVec2 origin = to_screen(glm::dvec2(0.0));
        draw->AddLine(ImVec2(origin.x, p0.y), ImVec2(origin.x, p1.y), IM_COL32(255, 255, 255, 40));
        draw->AddLine(ImVec2(p0.x, origin.y), ImVec2(p1.x, origin.y), IM_COL32(255, 255, 255, 40));

        // ---- overlays ------------------------------------------------------------------------------------------------
        const f32 pad = 8.0f;
        const f32 line = ImGui::GetTextLineHeight();
        auto pill = [&](ImVec2 pos, const char* text, ImU32 color, f32 wrap = 0.0f) {      // pos: top left, returns nothing
            const ImVec2 text_size = ImGui::CalcTextSize(text, nullptr, false, wrap);
            draw->AddRectFilled(pos, ImVec2(pos.x + text_size.x + 12.0f, pos.y + text_size.y + 6.0f), IM_COL32(10, 10, 12, 190), 4.0f);
            draw->AddText(nullptr, 0.0f, ImVec2(pos.x + 6.0f, pos.y + 3.0f), color, text, nullptr, wrap);
        };

        if (key == 0) {
            const char* hint = "Select a node, or add a Surface output";
            const ImVec2 hint_size = ImGui::CalcTextSize(hint);
            draw->AddText(ImVec2(p0.x + (extent.x - hint_size.x) * 0.5f, p0.y + (extent.y - hint_size.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
        } else {
            // top left: state
            if (m_pending.valid())
                pill(ImVec2(p0.x + pad, p0.y + pad), "evaluating...", IM_COL32(255, 255, 255, 220));
            else if (!m_stats.empty() && hovered)
                pill(ImVec2(p0.x + pad, p0.y + pad), m_stats.c_str(), IM_COL32(200, 200, 205, 220));

            // bottom left: value range (+ errors / warnings above it)
            if (m_field) {
                const std::string range = std::format("{:.3g} .. {:.3g}", m_min, m_max);
                pill(ImVec2(p0.x + pad, p1.y - pad - line - 6.0f), range.c_str(), IM_COL32(220, 220, 225, 230));
            }
            if (!m_status.empty() && (m_failed ? key == m_failed_key : key == m_shown_key)) {
                const f32 wrap = std::max(60.0f, extent.x - pad * 2.0f - 12.0f);
                const ImVec2 text_size = ImGui::CalcTextSize(m_status.c_str(), nullptr, false, wrap);
                const f32 above = m_field ? line + 6.0f + 4.0f : 0.0f;         // stacked on the range pill
                pill(ImVec2(p0.x + pad, p1.y - pad - above - text_size.y - 6.0f), m_status.c_str(),
                    m_failed ? IM_COL32(255, 120, 100, 255) : IM_COL32(255, 200, 110, 255), wrap);
            }

            // bottom right: scale bar with a round length (1 / 2 / 5 x 10^n) of about a fifth of the width
            const f64 target_length = m_view * 0.2;
            const f64 magnitude = std::pow(10.0, std::floor(std::log10(target_length)));
            const f64 length = target_length >= 5.0 * magnitude ? 5.0 * magnitude : target_length >= 2.0 * magnitude ? 2.0 * magnitude : magnitude;
            const f32 bar = static_cast<f32>(length / m_view) * extent.x;
            const std::string label = length >= 1000.0 ? std::format("{:g} km", length / 1000.0) : std::format("{:g} m", length);
            const ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
            const f32 right = p1.x - pad - 6.0f;
            const f32 base = p1.y - pad - 6.0f;
            draw->AddRectFilled(ImVec2(right - std::max(bar, label_size.x) - 12.0f, base - label_size.y - 10.0f), ImVec2(p1.x - pad, p1.y - pad),
                IM_COL32(10, 10, 12, 190), 4.0f);
            draw->AddText(ImVec2(right - (bar + label_size.x) * 0.5f, base - label_size.y - 7.0f), IM_COL32(220, 220, 225, 230), label.c_str());
            draw->AddLine(ImVec2(right - bar, base), ImVec2(right, base), IM_COL32(230, 230, 235, 255), 2.0f);
            draw->AddLine(ImVec2(right - bar, base - 4.0f), ImVec2(right - bar, base), IM_COL32(230, 230, 235, 255), 2.0f);
            draw->AddLine(ImVec2(right, base - 4.0f), ImVec2(right, base), IM_COL32(230, 230, 235, 255), 2.0f);
        }

        draw->PopClipRect();
        draw->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), ROUNDING);

        // ---- readout -------------------------------------------------------------------------------------------------
        if (hovered && !active) {
            const glm::dvec2 w = to_world(io.MousePos);
            std::string text = std::format("x {:.1f}  z {:.1f}", w.x, w.y);
            if (m_field) {
                const region& r = m_field->area();
                const i64 gx = static_cast<i64>(std::floor(w.x / r.spacing + 0.5));
                const i64 gy = static_cast<i64>(std::floor(w.y / r.spacing + 0.5));
                if (r.contains(gx, gy))
                    text += std::format("\nvalue {:.3f}", m_field->at(gx, gy));
            }
            ImGui::SetTooltip("%s", text.c_str());
        }
    }

}
