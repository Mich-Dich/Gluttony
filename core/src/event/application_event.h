
#pragma once

#include "event.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Per-frame tick event, broadcast by the application's main loop
    class update_event : public event {
    public:

        // @param delta_time  Seconds elapsed since the previous frame
        update_event(const f32 delta_time) : m_delta_time(delta_time) {}

        DEFAULT_GETTER_C(u32, delta_time);

        FORCE_INLINE_R std::string to_string() const override { return std::format("update event [{}]", m_delta_time); }

    private:
        f32 m_delta_time;
    };


    // @brief Asks every listening system to persist its state
    //
    // The registry subscribes to this to kick off its background save jobs. Subscribers that only care about a single-asset
    // "save as" should check [is_forced_save_as()] and bail out
    class save_event : public event {
    public:

        save_event() = default;

        // @param force_as  Set when the save was triggered by a "Save As" action
        explicit save_event(bool force_as) noexcept : m_force_as(force_as) {}

        // @brief Reports whether this is a "Save As" request
        // @return true if the save was forced into the "as" path
        [[nodiscard]] bool is_forced_save_as() const noexcept { return m_force_as; }

        FORCE_INLINE_R std::string to_string() const override { return std::format("save event, force [{}]", m_force_as); }

    private:

        bool                                                        m_force_as{ false };
    };


    // @brief Reusable "where should this go?" request
    //
    // The editor layer owns the popup UI; the requestor owns the callback. Multiple requests queue up and resolve one at a time
    //
    // Lifetime rules:
    // @brief  - [on_resolved] is called exactly once, with an empty path if the user cancelled
    // @brief  - It runs on the main thread, during the editor's update pass
    // @brief  - Captures inside [on_resolved] must remain valid until the popup resolves. If the requestor might die first,
    //     use a weak handle or a correlation id instead of a raw pointer capture
    class save_as_request_event : public GLT::event {
    public:

        // @brief One queued "Save As" prompt
        struct request {

            std::string                                             title;              // "Save World As", "Export Mesh", ...
            std::string                                             default_name;       // "level01" (no extension)
            std::filesystem::path                                   default_dir;        // PROJECT_CONTENT_DIR / "world"
            std::string                                             extension;          // "glt_world" (no dot)
            std::function<void(const std::filesystem::path&)>       on_resolved;        // empty = cancelled
        };

        // @param req  The prompt to queue
        save_as_request_event(request req) noexcept : m_request(std::move(req)) {}

        // @brief Returns the queued request
        // @return Const reference to the stored request; valid for the lifetime of the event
        [[nodiscard]] const request& get() const noexcept { return m_request; }

        FORCE_INLINE_R std::string to_string() const override { return std::format("save as request event"); }

    private:

        request                                                     m_request;
    };


    // @brief Asks the editor to surface a notification to the user
    class notification_event : public event {
    public:

        // @param title     Short headline
        // @param severity  Log severity, used to pick the notification's colour / icon
        notification_event(std::string title, GLT::logger::severity severity)
            : m_title(std::move(title)), m_severity(severity) {}


        // @param title        Short headline
        // @param description  Longer body text shown under the title
        // @param severity     Log severity, used to pick the notification's colour / icon
        notification_event(std::string title, std::string description, GLT::logger::severity severity)
            : m_title(std::move(title)), m_description(std::move(description)), m_severity(severity) {}


        // @brief Returns the notification headline
        [[nodiscard]] const std::string& get_title() const noexcept { return m_title; }


        // @brief Returns the notification body text
        [[nodiscard]] const std::string& get_description() const noexcept { return m_description; }


        // @brief Returns the notification's severity
        [[nodiscard]] GLT::logger::severity get_severity() const noexcept { return m_severity; }


        FORCE_INLINE_R std::string to_string() const override { return std::format("notification [{}] - {}", m_title, m_description); }

    private:

        std::string                         m_title;
        std::string                         m_description;
        GLT::logger::severity               m_severity;
    };


    // @brief The windowing system reports a new client-area size
    class window_resize_event : public event {
    public:
        // @param width   New client-area width in pixels
        // @param height  New client-area height in pixels
        window_resize_event(u32 width, u32 height) : m_width(width), m_height(height) {}

        DEFAULT_GETTER_C(u32, width);
        DEFAULT_GETTER_C(u32, height);

        FORCE_INLINE_R std::string to_string() const override { return std::format("window resize event [{}, {}]", m_width, m_height); }

    private:
        u32                                                         m_width, m_height;
    };


    // @brief The framebuffer (not the window) changed size
    //
    // Fires separately from [window_resize_event] because the framebuffer can change size while the window stays fixed
    // (e.g. DPI changes on macOS)
    class window_framebuffer_resize_event : public event {
    public:
        // @param width   New framebuffer width in pixels
        // @param height  New framebuffer height in pixels
        window_framebuffer_resize_event(u32 width, u32 height) : m_width(width), m_height(height) {}

        DEFAULT_GETTER_C(u32, width);
        DEFAULT_GETTER_C(u32, height);

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window framebuffer resize event [{}, {}]", m_width, m_height);
        }

    private:
        u32                                                         m_width, m_height;
    };


    // @brief The window gained or lost input focus
    class window_focus_event : public event {
    public:
        // @param focused  true if the window is now focused
        window_focus_event(bool focused) : m_focused(focused) {}

        DEFAULT_GETTER_C(bool, focused);

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window focus event [{}]", m_focused ? "focused" : "unfocused");
        }

    private:
        bool                                                        m_focused;
    };


    // @brief The window was moved to a new screen position
    class window_move_event : public event {
    public:
        // @param x  New left edge in screen pixels
        // @param y  New top edge in screen pixels
        window_move_event(i32 x, i32 y) : m_x(x), m_y(y) {}

        DEFAULT_GETTER_C(i32, x)
        DEFAULT_GETTER_C(i32, y)

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window moved to [{}, {}]", m_x, m_y);
        }

    private:
        i32                                                         m_x, m_y;
    };

    
    // @brief The windowing system asks for a repaint of the client area
    class window_refresh_event : public event {
    public:
        window_refresh_event() {}

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window refreshing");
        }

    private:
    };

    
    // @brief The window is about to close
    class window_close_event : public event {
    public:
        window_close_event() {}

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window closing");
        }

    private:
    };


    // @brief The window was minimized or restored from a minimized state
    class window_iconify_event : public event {
    public:
        // @param iconified  true if the window is now minimized
        window_iconify_event(bool iconified) : m_iconified(iconified) {}

        DEFAULT_GETTER_C(bool, iconified);

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window iconify event [{}]", m_iconified ? "iconified" : "restored");
        }

    private:
        bool                                                        m_iconified;
    };


    // @brief The window was maximized or restored from a maximized state
    class window_maximize_event : public event {
    public:
        // @param maximized  true if the window is now maximized
        window_maximize_event(bool maximized) : m_maximized(maximized) {}

        DEFAULT_GETTER_C(bool, maximized);

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window maximize event [{}]", m_maximized ? "maximized" : "restored");
        }

    private:
        bool                                                        m_maximized;
    };


    // @brief Content scale (DPI) of the window changed
    //
    // Fires when the window moves to a monitor with a different DPI, or when the user changes scaling at runtime
    // UI code should rescale its hard-coded pixel sizes accordingly
    class window_content_scale_event : public event {
    public:
        // @param x_scale  Horizontal content scale factor (1.0 = 100%)
        // @param y_scale  Vertical content scale factor (1.0 = 100%)
        window_content_scale_event(f32 x_scale, f32 y_scale) : m_x_scale(x_scale), m_y_scale(y_scale) {}

        DEFAULT_GETTER_C(f32, x_scale);
        DEFAULT_GETTER_C(f32, y_scale);

        FORCE_INLINE_R std::string to_string() const override {
            return std::format("window content scale event [{:.2f}, {:.2f}]", m_x_scale, m_y_scale);
        }

    private:
        f32                                                         m_x_scale, m_y_scale;
    };


    // @brief Files or folders were dropped onto the window
    class window_drop_event : public event {
    public:
        // @param paths  Absolute paths of the dropped items
        window_drop_event(const std::vector<std::string>& paths) : m_paths(paths) {}

        DEFAULT_GETTER_C(std::vector<std::string>, paths);

        FORCE_INLINE_R std::string to_string() const override {
            std::string result = "window drop event [";
            for (size_t i = 0; i < m_paths.size(); ++i) {
                result += m_paths[i];
                if (i < m_paths.size() - 1) result += ", ";
            }
            result += "]";
            return result;
        }

    private:
        std::vector<std::string>                                    m_paths;
    };

}
