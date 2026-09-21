
#include "util/pch.h"
#include "file_watcher.h"

#include <sys/inotify.h>
#include <sys/eventfd.h>
#include <poll.h>

#include <event/event.h>
#include <event/event_bus.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::file_watcher {

    // CONSTANTS =======================================================================================================

    // IN_CLOSE_WRITE is preferred over IN_MODIFY for "file changed" semantics: it fires once when the writer closes the fd 
    // instead of once per write() call. Consumers that want every write can add IN_MODIFY themselves.
    static constexpr u32 s_watch_mask =
        IN_CREATE       |
        IN_DELETE       |
        IN_CLOSE_WRITE  |
        IN_MOVED_FROM   |
        IN_MOVED_TO     |
        IN_MOVE_SELF    |
        IN_DELETE_SELF;

    static constexpr std::size_t s_read_buffer_size = 16 * 1024;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct watch_entry {
        std::filesystem::path                           path;
        bool                                            recursive = false;
    };

    // STATIC VARIABLES ================================================================================================

    std::mutex                                          s_mutex;

    std::unordered_map<int, watch_entry>                s_watches_by_wd;

    std::unordered_map<std::string, int>                s_wd_by_path;

    int                                                 s_inotify_fd = -1;

    int                                                 s_wakeup_fd  = -1;

    std::jthread                                        s_thread;

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    void run(std::stop_token stop_token);

    void dispatch(const inotify_event* ev);

    bool add_watch(const std::filesystem::path& path, bool recursive);

    void remove_watch(int wd);

    void remove_watches_under(const std::filesystem::path& root);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool add_watch(const std::filesystem::path& path, bool recursive) {

        std::error_code error{};
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        if (error) 
            return false;
        
        if (!std::filesystem::is_directory(canonical, error) || error) 
            return false;

        const auto key = canonical.string();
        std::lock_guard lock(s_mutex);
        if (!s_wd_by_path.contains(key)) {

            const int wd = ::inotify_add_watch(s_inotify_fd, canonical.c_str(), s_watch_mask);
            if (wd < 0) 
                return false;
            s_watches_by_wd[wd] = { canonical, recursive };
            s_wd_by_path[key]   = wd;
        
        } else if (recursive) {

            s_watches_by_wd[s_wd_by_path[key]].recursive = true;
        }

        if (!recursive) 
            return true;

        // Walk subdirectories. Errors are ignored per-entry so one bad folder
        // does not abort the whole recursive watch.
        for (auto it = std::filesystem::recursive_directory_iterator(
                canonical,
                std::filesystem::directory_options::skip_permission_denied,
                error);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(error)) {

            if (error)
                break;

            if (!it->is_directory(error) || error)
                continue;

            const auto sub = it->path();
            const auto sub_key = sub.string();
            if (s_wd_by_path.contains(sub_key))
                continue;

            const int wd = ::inotify_add_watch(s_inotify_fd, sub.c_str(), s_watch_mask);
            if (wd < 0) 
                continue;

            s_watches_by_wd[wd] = { sub, true };
            s_wd_by_path[sub_key] = wd;
        }

        return true;
    }


    void remove_watch(int wd) {

        // caller must hold s_mutex
        const auto it = s_watches_by_wd.find(wd);
        if (it == s_watches_by_wd.end())
            return;

        ::inotify_rm_watch(s_inotify_fd, wd);
        s_wd_by_path.erase(it->second.path.string());
        s_watches_by_wd.erase(it);
    }


    void remove_watches_under(const std::filesystem::path& root) {

        std::lock_guard lock(s_mutex);
        std::vector<int> to_remove;
        for (const auto& [wd, entry] : s_watches_by_wd) {

            const auto rel = entry.path.lexically_relative(root);
            // rel is empty when the paths are unrelated, and starts with ".."
            // when entry.path is outside of root's subtree.
            if (rel.empty()) 
                continue;

            if (*rel.begin() == "..") 
                continue;

            to_remove.push_back(wd);
        }

        for (const int wd : to_remove)
            remove_watch(wd);
    }


    void run(std::stop_token stop_token) {

        pollfd fds[2];
        fds[0] = { s_inotify_fd, POLLIN, 0 };
        fds[1] = { s_wakeup_fd, POLLIN, 0 };

        alignas(inotify_event) char buffer[s_read_buffer_size];

        while (!stop_token.stop_requested()) {
            const int ret = ::poll(fds, 2, -1);
            if (ret < 0) {
                if (errno == EINTR) 
                    continue;
                break;
            }

            if (fds[1].revents & POLLIN) {
                std::uint64_t drain = 0;
                [[maybe_unused]] const auto _ = ::read(s_wakeup_fd, &drain, sizeof(drain));
                break;
            }

            if (!(fds[0].revents & POLLIN))
                continue;

            const ssize_t len = ::read(s_inotify_fd, buffer, sizeof(buffer));
            if (len <= 0)
                continue;

            for (const char* ptr = buffer; ptr < buffer + len; ) {

                const auto* ev = reinterpret_cast<const inotify_event*>(ptr);
                dispatch(ev);
                ptr += sizeof(inotify_event) + ev->len;
            }
        }
    }


    void dispatch(const inotify_event* ev) {

        // Copy the entry under lock, then release it before posting events or
        // touching the maps again — subscribers may call watch()/unwatch().
        watch_entry entry;
        {
            std::lock_guard lock(s_mutex);
            const auto it = s_watches_by_wd.find(ev->wd);
            if (it == s_watches_by_wd.end())
                return;
            entry = it->second;
        }

        // Kernel tells us the watch was removed (either by us or because the
        // target was deleted/moved). Always the last event on this wd.
        if (ev->mask & IN_IGNORED) {

            std::lock_guard lock(s_mutex);
            const auto it = s_watches_by_wd.find(ev->wd);
            if (it != s_watches_by_wd.end()) {

                s_wd_by_path.erase(it->second.path.string());
                s_watches_by_wd.erase(it);
            }
            return;
        }

        const std::filesystem::path full = (ev->len > 0) ? (entry.path / ev->name) : entry.path;

        if (ev->mask & IN_CREATE) {
            GLT::event_bus::post(GLT::editor::file_event(file_event_type::created, full));
            if ((ev->mask & IN_ISDIR) && entry.recursive)
                add_watch(full, /*recursive=*/true);
        }

        if (ev->mask & IN_CLOSE_WRITE) {
            GLT::event_bus::post(GLT::editor::file_event(file_event_type::modified, full));
        }

        if (ev->mask & IN_DELETE) {
            GLT::event_bus::post(GLT::editor::file_event(file_event_type::deleted, full));
            if (ev->mask & IN_ISDIR)
                remove_watches_under(full);
        }

        if (ev->mask & IN_MOVED_FROM) {
            GLT::event_bus::post(GLT::editor::file_event(file_event_type::moved_from, full));
            if (ev->mask & IN_ISDIR)
                remove_watches_under(full);
        }

        if (ev->mask & IN_MOVED_TO) {
            GLT::event_bus::post(GLT::editor::file_event(file_event_type::moved_to, full));
            if ((ev->mask & IN_ISDIR) && entry.recursive)
                add_watch(full, /*recursive=*/true);
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void init() {

        s_inotify_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (s_inotify_fd < 0)
            throw std::system_error(errno, std::generic_category(), "inotify_init1");

        s_wakeup_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (s_wakeup_fd < 0) {

            const int saved = errno;
            ::close(s_inotify_fd);
            s_inotify_fd = -1;
            throw std::system_error(saved, std::generic_category(), "eventfd");
        }

        // Start the dispatch thread last so every member is already initialized.
        s_thread = std::jthread([](std::stop_token st) { run(st); });
    }


    void shutdown() {

        s_thread.request_stop();
        if (s_wakeup_fd != -1) {
            const std::uint64_t one = 1;
            [[maybe_unused]] const auto _ = ::write(s_wakeup_fd, &one, sizeof(one));
        }

        if (s_thread.joinable())
            s_thread.join();

        if (s_inotify_fd != -1) 
            ::close(s_inotify_fd);

        if (s_wakeup_fd  != -1) 
            ::close(s_wakeup_fd);
    }


    bool watch(const std::filesystem::path& directory, bool recursive) { return add_watch(directory, recursive); }


    void unwatch(const std::filesystem::path& directory) {

        std::error_code error{};
        const auto canonical = std::filesystem::weakly_canonical(directory, error);
        if (error) 
            return;
        remove_watches_under(canonical);
    }


    void unwatch_all() {

        std::lock_guard lock(s_mutex);
        for (const auto& [wd, _] : s_watches_by_wd)
            ::inotify_rm_watch(s_inotify_fd, wd);
        s_watches_by_wd.clear();
        s_wd_by_path.clear();
    }


    bool is_watching(const std::filesystem::path& directory) {

        std::error_code error{};
        const auto canonical = std::filesystem::weakly_canonical(directory, error);
        if (error)
            return false;

        std::lock_guard lock(s_mutex);
        return s_wd_by_path.contains(canonical.string());
    }

    // CLASS PUBLIC ====================================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
