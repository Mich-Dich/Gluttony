#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#if defined(GLT_TRACY_ENABLED)
    #include <tracy/Tracy.hpp>
#endif



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::debug::trace {

    // CONSTANTS =======================================================================================================

    inline constexpr u32                        HISTORY_FRAMES = 300;          // frames kept by the in-engine recorder

    // Tracy zone colours (0xRRGGBB). Waits are red so blocked time stands out in the Tracy timeline
    inline constexpr u32                        COLOR_WAIT = 0xC03030;
    inline constexpr u32                        COLOR_RENDER = 0x3070C0;
    inline constexpr u32                        COLOR_WORLD = 0x30A050;
    inline constexpr u32                        COLOR_IDLE = 0x606060;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // What a zone represents. [wait] = the thread is blocked (fence, vsync/present, frame limiter, ...). The frame's
    // "CPU work" is its wall time minus the main thread's outermost wait zones
    enum class zone_kind : u8 {
        work = 0,
        wait,                                   // blocked on something (GPU fence, swapchain, ...)
        idle,                                   // deliberately idle (frame limiter): neither work nor wait
    };


    // One finished zone as captured by the in-engine recorder. [name] must outlive the program (string literal or
    // __FUNCTION__): only the pointer is stored
    struct zone_record {
        const char*                             name = nullptr;
        u64                                     start_ns = 0;
        u64                                     end_ns = 0;
        u32                                     thread = 0;         // index into frame_record::threads / thread_names()
        u16                                     depth = 0;          // nesting depth on its thread
        zone_kind                               kind = zone_kind::work;
    };


    // One frame (main-thread frame_mark to frame_mark) with every zone that ENDED inside it, on any thread
    struct frame_record {
        u64                                     index = 0;
        u64                                     start_ns = 0;
        u64                                     end_ns = 0;
        u64                                     main_wait_ns = 0;   // outermost wait zones on the main thread
        std::vector<zone_record>                zones;

        [[nodiscard]] f32 duration_ms() const noexcept { return static_cast<f32>(end_ns - start_ns) * 1e-6f; }
        [[nodiscard]] f32 wait_ms() const noexcept { return static_cast<f32>(main_wait_ns) * 1e-6f; }
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] u64 now_ns() noexcept;

    // Recording of zones into the in-engine history (the Tracing window). Wait accounting runs regardless, so the stats
    // can always split CPU work from waiting. Tracy collects independently whenever its profiler is connected
    void set_capture(bool enabled) noexcept;
    [[nodiscard]] bool is_capturing() noexcept;

    // Freeze the history (new frames are dropped) so a spike can be inspected
    void set_paused(bool paused) noexcept;
    [[nodiscard]] bool is_paused() noexcept;

    // End of a frame. Call once per frame on the main thread (the game loop does). Closes the current frame_record and
    // tells Tracy (FrameMark)
    void frame_mark() noexcept;

    // Main-thread wait time of the last finished frame, in milliseconds
    [[nodiscard]] f32 last_frame_wait_ms() noexcept;

    // Wait time accumulated on the CALLING thread since its last frame_mark (main thread: the frame so far)
    [[nodiscard]] f32 current_frame_wait_ms() noexcept;

    // Copy of the recorded history, oldest first (at most HISTORY_FRAMES)
    [[nodiscard]] std::vector<frame_record> history();
    void clear_history();

    // Record exactly the next [frames] frames (clears the history, enables capture, pauses when done)
    void capture_frames(u32 frames) noexcept;
    [[nodiscard]] u32 capture_frames_remaining() noexcept;

    // Index (into thread_names()) of the thread that calls frame_mark()
    [[nodiscard]] u32 main_thread_index() noexcept;

    // Export --------------------------------------------------------------------------------------------------------
    // Human readable analysis: frame statistics, hottest zones by self time, aggregated call tree per thread (avg ms per
    // frame), and the full zone tree of the slowest frame. Meant to be pasted into a bug report / chat
    [[nodiscard]] std::string summary_text(const std::vector<frame_record>& frames);

    // Chrome trace event format (open in https://ui.perfetto.dev or chrome://tracing): every zone of every frame
    [[nodiscard]] std::string chrome_trace_json(const std::vector<frame_record>& frames);

    // Writes <dir>/trace_<timestamp>.json and <dir>/trace_<timestamp>_summary.txt. Returns the summary path (empty on error)
    [[nodiscard]] std::filesystem::path export_to_directory(const std::vector<frame_record>& frames, const std::filesystem::path& dir);

    // Stable copy of a runtime string for zone names (render graph pass names, ...). Interned once, never freed
    [[nodiscard]] const char* intern(std::string_view name);

    // Names the calling thread (in the Tracing window and in Tracy). [name] is copied
    void set_thread_name(const char* name);
    [[nodiscard]] std::vector<std::string> thread_names();

    // Tracy status --------------------------------------------------------------------------------------------------
    [[nodiscard]] bool tracy_compiled() noexcept;
    [[nodiscard]] bool tracy_connected() noexcept;
    [[nodiscard]] const char* tracy_version() noexcept;

    // CLASS DECLARATION ===============================================================================================

    // RAII zone for the in-engine recorder (the macros below pair it with a Tracy zone). Work zones cost one relaxed
    // atomic load when capture is off; wait zones always take two clock reads (wait accounting)
    class scoped_zone {
    public:
        scoped_zone(const char* name, zone_kind kind = zone_kind::work) noexcept;
        ~scoped_zone() noexcept;

        scoped_zone(const scoped_zone&) = delete;
        scoped_zone& operator=(const scoped_zone&) = delete;

    private:
        const char*                             m_name;
        u64                                     m_start_ns = 0;
        u16                                     m_depth = 0;
        zone_kind                               m_kind;
        bool                                    m_record = false;
        bool                                    m_outer_wait = false;
    };

}



// Profiling macros ====================================================================================================
//
//   GLT_PROFILE_SCOPE("name")          work zone, [name] must be a string literal
//   GLT_PROFILE_SCOPE_C("name", color) work zone with a Tracy colour (0xRRGGBB)
//   GLT_PROFILE_FUNCTION()             work zone named after the enclosing function
//   GLT_PROFILE_WAIT("name")           the thread is blocked (fence, present, sleep): excluded from "CPU work"
//   GLT_PROFILE_IDLE("name")           deliberate idle (frame limiter): neither work nor wait
//   GLT_PROFILE_FRAME()                end of frame (main thread, once per frame)
//   GLT_PROFILE_PLOT("name", value)    numeric plot in Tracy
//   GLT_PROFILE_THREAD("name")         names the calling thread
//   GLT_PROFILE_MESSAGE(text, size)    timeline message in Tracy
//
// Several scopes in one block are fine, but only one per source LINE (Tracy keys its static source locations by line)

#define GLT_TRACE_CONCAT_INNER(a, b)            a##b
#define GLT_TRACE_CONCAT(a, b)                  GLT_TRACE_CONCAT_INNER(a, b)

#if defined(GLT_TRACY_ENABLED)
    #define GLT_TRACY_ZONE(name)                ZoneNamedN(GLT_TRACE_CONCAT(_glt_tracy_zone_, __LINE__), name, true)
    #define GLT_TRACY_ZONE_C(name, color)       ZoneNamedNC(GLT_TRACE_CONCAT(_glt_tracy_zone_, __LINE__), name, color, true)
    #define GLT_TRACY_FUNCTION()                ZoneNamed(GLT_TRACE_CONCAT(_glt_tracy_zone_, __LINE__), true)
    #define GLT_PROFILE_PLOT(name, value)       TracyPlot(name, value)
    #define GLT_PROFILE_MESSAGE(text, size)     TracyMessage(text, size)
#else
    #define GLT_TRACY_ZONE(name)                ((void)0)
    #define GLT_TRACY_ZONE_C(name, color)       ((void)0)
    #define GLT_TRACY_FUNCTION()                ((void)0)
    #define GLT_PROFILE_PLOT(name, value)       ((void)0)
    #define GLT_PROFILE_MESSAGE(text, size)     ((void)0)
#endif

#define GLT_PROFILE_SCOPE(name)                 GLT_TRACY_ZONE(name); \
    const GLT::debug::trace::scoped_zone GLT_TRACE_CONCAT(_glt_zone_, __LINE__){ name }
#define GLT_PROFILE_SCOPE_C(name, color)        GLT_TRACY_ZONE_C(name, color); \
    const GLT::debug::trace::scoped_zone GLT_TRACE_CONCAT(_glt_zone_, __LINE__){ name }
#define GLT_PROFILE_FUNCTION()                  GLT_TRACY_FUNCTION(); \
    const GLT::debug::trace::scoped_zone GLT_TRACE_CONCAT(_glt_zone_, __LINE__){ __FUNCTION__ }
#define GLT_PROFILE_WAIT(name)                  GLT_TRACY_ZONE_C(name, GLT::debug::trace::COLOR_WAIT); \
    const GLT::debug::trace::scoped_zone GLT_TRACE_CONCAT(_glt_zone_, __LINE__){ name, GLT::debug::trace::zone_kind::wait }
#define GLT_PROFILE_IDLE(name)                  GLT_TRACY_ZONE_C(name, GLT::debug::trace::COLOR_IDLE); \
    const GLT::debug::trace::scoped_zone GLT_TRACE_CONCAT(_glt_zone_, __LINE__){ name, GLT::debug::trace::zone_kind::idle }
#define GLT_PROFILE_FRAME()                     GLT::debug::trace::frame_mark()

// Runtime, null-terminated name (e.g. std::string::c_str()): Tracy transient zone + interned copy for the recorder
#if defined(GLT_TRACY_ENABLED)
    #define GLT_TRACY_ZONE_DYNAMIC(cstr)        ZoneTransientN(GLT_TRACE_CONCAT(_glt_tracy_dzone_, __LINE__), cstr, true)
#else
    #define GLT_TRACY_ZONE_DYNAMIC(cstr)        ((void)0)
#endif
#define GLT_PROFILE_SCOPE_DYNAMIC(cstr)         GLT_TRACY_ZONE_DYNAMIC(cstr); \
    const GLT::debug::trace::scoped_zone GLT_TRACE_CONCAT(_glt_zone_, __LINE__){ \
        GLT::debug::trace::is_capturing() ? GLT::debug::trace::intern(cstr) : "" }
#define GLT_PROFILE_THREAD(name)                GLT::debug::trace::set_thread_name(name)
