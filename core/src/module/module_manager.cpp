#include "util/pch.h"
#include "module/module_manager.h"

#if defined(PLATFORM_LINUX)
    #include <dlfcn.h>
#endif

#include "module/abi.h"
#include "plugin_system/plugin_manager.h"
#include "editor/i_editor.h"
#include "scripting/i_script_plugin.h"
#include "procedural/construct/engine/service.h"
#include "util/data_structures/thread_pool.h"
#include "application.h"



namespace GLT::modules {

    // CONSTANTS =======================================================================================================

    static constexpr std::string_view                   OWNER = "module_manager";
    static constexpr std::string_view                   MANIFEST_EXTENSION = ".glt_module";
    static constexpr abi_stamp                          ENGINE_ABI = current_abi();

    #if defined(PLATFORM_LINUX)
        static constexpr std::string_view               LIBRARY_EXTENSION = ".so";
    #elif defined(PLATFORM_WINDOWS)
        static constexpr std::string_view               LIBRARY_EXTENSION = ".dll";
    #endif

    // TYPES ===========================================================================================================

    struct loaded_module {

        module_info                                     info{};
        context                                         ctx{};
        const descriptor*                               desc = nullptr;
        void*                                           handle = nullptr;
    };

    // STATIC VARIABLES ================================================================================================

    static std::vector<participant>                     s_participants{};
    static std::vector<std::unique_ptr<loaded_module>>  s_modules{};           // unique_ptr: ctx.id points into info.id
    static u32                                          s_load_counter = 0;

    static std::mutex                                   s_build_mutex{};
    static build_status                                 s_build{};
    static std::thread                                  s_build_thread{};

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    #if defined(PLATFORM_LINUX)

        static void* open_library(const std::filesystem::path& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }

        static void* find_symbol(void* handle, const char* name) { return dlsym(handle, name); }

        static void close_library(void* handle) { if (handle) dlclose(handle); }

        static std::string library_error() {
            const char* msg = dlerror();
            return msg ? std::string(msg) : std::string("unknown error");
        }

    #else

        static void* open_library(const std::filesystem::path&) { return nullptr; }

        static void* find_symbol(void*, const char*) { return nullptr; }

        static void close_library(void*) {}

        static std::string library_error() { return "game modules are only implemented on Linux"; }

    #endif


    static script::i_script_plugin* scripting_plugin() {

        auto* plugin = plugin_manager::get_plugin_raw<plugin_manager::i_plugin>(plugin_manager::interface::scripting);
        return plugin ? plugin->as<script::i_script_plugin>() : nullptr;
    }


    static std::filesystem::path loaded_dir(const std::filesystem::path& library) {

        return library.parent_path().parent_path() / "loaded";                     // <project>/build/<config>/loaded
    }


    static std::string quote(const std::filesystem::path& p) {

        std::string out = "'";
        for (const char c : p.string()) {
            if (c == '\'')
                out += "'\\''";
            else
                out += c;
        }
        return out + "'";
    }


    static std::filesystem::path application_project_dir() { return application::get().get_project_path(); }


    static void release(loaded_module& m) {

        for (auto it = s_participants.rbegin(); it != s_participants.rend(); ++it) {
            if (!it->release)
                continue;
            try {
                it->release(m.info.id);
            } catch (const std::exception& e) {
                LOG(error, "Module [{}]: release by [{}] threw: {}", m.info.id, it->name, e.what());
            }
        }
    }


    static void close(loaded_module& m) {

        close_library(m.handle);
        m.handle = nullptr;
        std::error_code ec{};
        std::filesystem::remove(m.info.loaded_path, ec);
    }


    // Every built-in participant. Order matters: release runs in reverse (editor windows close first, then the construct
    // packs go, then the script classes)
    static void add_builtin_participants() {

        add_participant({
            .name = "scripting",
            .after_load = {},
            .release = [](std::string_view id) {
                if (auto* scripting = scripting_plugin()) {
                    scripting->unregister_by_registrant(id);
                    scripting->clear_default_globals(id);
                }
            },
        });
        add_participant({
            .name = "construct",
            .after_load = {},
            .release = [](std::string_view id) {
                thread_pool::wait_for_all();                // evaluations may be running stage code of the module
                construct::packs().remove_owner(id);
            },
        });
        add_participant({
            .name = "editor",
            .after_load = {},
            .release = [](std::string_view id) {
                editor::remove_editor_hook(id);
                if (auto* ed = editor::get())
                    ed->unregister_all(id);
            },
        });
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void add_participant(participant p) {

        for (participant& existing : s_participants)
            if (existing.name == p.name) {
                existing = std::move(p);
                return;
            }
        s_participants.push_back(std::move(p));
    }


    void remove_participant(std::string_view name) {

        std::erase_if(s_participants, [&](const participant& p) { return p.name == name; });
    }


    std::filesystem::path output_dir(const std::filesystem::path& project_dir, const build_config config) {

        return project_dir / "build" / build_dir_name(config) / "bin";
    }


    std::vector<std::filesystem::path> find_modules(const std::filesystem::path& project_dir) {

        // Each module has a manifest next to it (<name>.glt_module, written by the project CMake): no library is
        // opened just to find out whether it is a module (the folder also holds the libraries the modules link)
        std::vector<std::filesystem::path> out{};
        const std::filesystem::path dir = output_dir(project_dir);
        std::error_code ec{};
        if (!std::filesystem::is_directory(dir, ec))
            return out;

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != MANIFEST_EXTENSION)
                continue;

            std::ifstream file(entry.path());
            std::string line{}, library{};
            while (std::getline(file, line)) {
                constexpr std::string_view KEY = "library:";
                if (line.starts_with(KEY)) {
                    library = line.substr(KEY.size());
                    std::erase_if(library, [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '"'; });
                }
            }
            if (library.empty())
                library = entry.path().stem().string() + std::string(LIBRARY_EXTENSION);

            const std::filesystem::path path = dir / library;
            if (std::filesystem::exists(path, ec))
                out.push_back(path);
            else
                LOG(warn, "Module manifest [{}] names [{}], which does not exist (not built yet?)", entry.path().generic_string(),
                    path.generic_string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }


    u32 load_project_modules(const std::filesystem::path& project_dir) {

        u32 loaded = 0;
        for (const std::filesystem::path& library : find_modules(project_dir)) {
            if (const auto result = load_module(library, project_dir); result)
                ++loaded;
            else
                LOG(error, "Game module [{}] not loaded:\n{}", library.generic_string(), result.error());
        }
        if (loaded == 0 && std::filesystem::exists(project_dir / "CMakeLists.txt"))
            LOG(info, "No game module built for this configuration yet: [{}] (Tools > Game module > Build and load)",
                output_dir(project_dir).generic_string());
        return loaded;
    }


    std::expected<void, std::string> load_module(const std::filesystem::path& library, const std::filesystem::path& project_dir) {

        std::error_code ec{};
        if (!std::filesystem::exists(library, ec))
            return std::unexpected(std::format("[{}] does not exist", library.generic_string()));

        // Load a copy: the build may overwrite the original while it is loaded, and a new file name per load keeps the
        // dynamic loader from handing back an old, still-open image of the same path
        auto entry = std::make_unique<loaded_module>();
        entry->info.library_path = library;
        entry->info.library_time = std::filesystem::last_write_time(library, ec);
        const std::filesystem::path copy_dir = loaded_dir(library);
        std::filesystem::create_directories(copy_dir, ec);
        entry->info.loaded_path = copy_dir / std::format("{}.{}{}", library.stem().string(), ++s_load_counter, library.extension().string());
        std::filesystem::copy_file(library, entry->info.loaded_path, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return std::unexpected(std::format("could not copy it to [{}]: {}", entry->info.loaded_path.generic_string(), ec.message()));

        entry->handle = open_library(entry->info.loaded_path);
        if (!entry->handle) {
            const std::string error = library_error();
            close(*entry);
            return std::unexpected(std::format("could not open it: {}\n(an undefined symbol usually means the engine changed: "
                "rebuild the module)", error));
        }

        using descriptor_fn = const descriptor* (*)();
        auto fn = reinterpret_cast<descriptor_fn>(find_symbol(entry->handle, GLT_MODULE_DESCRIPTOR_SYMBOL));
        const descriptor* desc = fn ? fn() : nullptr;
        if (!desc) {
            close(*entry);
            return std::unexpected(std::format("it exports no [{}] (GLT_GAME_MODULE missing?)", GLT_MODULE_DESCRIPTOR_SYMBOL));
        }

        // Only the stamp is read before it matched: everything after it may have another layout
        if (const std::string difference = abi_difference(ENGINE_ABI, desc->abi); !difference.empty()) {
            close(*entry);
            return std::unexpected("it was built for another engine:\n" + difference);
        }

        entry->desc = desc;
        entry->info.id = desc->name ? desc->name : library.stem().string();
        entry->info.root_namespace = desc->root_namespace ? desc->root_namespace : "";
        for (const auto& m : s_modules)
            if (m->info.id == entry->info.id) {
                close(*entry);
                return std::unexpected(std::format("a module named [{}] is already loaded (from [{}])", entry->info.id,
                    m->info.library_path.generic_string()));
            }

        entry->ctx = context{
            .id = entry->info.id,
            .project_dir = project_dir,
            .library_path = library,
            .scripting = scripting_plugin(),
            .construct = &construct::packs(),
        };

        loaded_module& m = *entry;
        s_modules.push_back(std::move(entry));

        try {
            if (m.desc->load)
                m.desc->load(m.ctx);
        } catch (const std::exception& e) {
            const std::string error = std::format("its load step threw: {}", e.what());
            release(m);
            close(m);
            s_modules.pop_back();
            return std::unexpected(error);
        }

        for (const participant& p : s_participants)
            if (p.after_load)
                p.after_load(m.info.id);

        LOG(info, "Game module [{}] loaded from [{}]", m.info.id, library.generic_string());
        return {};
    }


    void unload_module(std::string_view id) {

        const auto it = std::find_if(s_modules.begin(), s_modules.end(), [&](const auto& m) { return m->info.id == id; });
        if (it == s_modules.end())
            return;

        loaded_module& m = **it;
        try {
            if (m.desc->unload)
                m.desc->unload(m.ctx);
        } catch (const std::exception& e) {
            LOG(error, "Game module [{}]: unload step threw: {}", m.info.id, e.what());
        }
        release(m);
        const std::string name = m.info.id;
        close(m);
        s_modules.erase(it);
        LOG(info, "Game module [{}] unloaded", name);
    }


    void unload_all() {

        while (!s_modules.empty()) {
            const std::string id = s_modules.back()->info.id;
            unload_module(id);
        }
    }


    std::vector<module_info> loaded_modules() {

        std::vector<module_info> out{};
        out.reserve(s_modules.size());
        for (const auto& m : s_modules)
            out.push_back(m->info);
        return out;
    }


    bool has_newer_build(std::string_view id) {

        for (const auto& m : s_modules)
            if (m->info.id == id) {
                std::error_code ec{};
                const auto time = std::filesystem::last_write_time(m->info.library_path, ec);
                return !ec && time > m->info.library_time;
            }
        return false;
    }

    // Build -----------------------------------------------------------------------------------------------------------

    std::vector<std::string> build_commands(const std::filesystem::path& project_dir, const build_config config) {

        const std::filesystem::path build_dir = project_dir / "build" / build_dir_name(config);
        std::vector<std::string> out{};

        std::error_code ec{};
        if (!std::filesystem::exists(build_dir / "CMakeCache.txt", ec)) {
            std::string configure = std::format("cmake -S {} -B {} -DCMAKE_BUILD_TYPE={}", quote(project_dir), quote(build_dir),
                cmake_build_type(config));
            if (std::system("command -v ninja > /dev/null 2>&1") == 0)
                configure += " -G Ninja";
            #if defined(GLT_ENGINE_SOURCE_DIR)
                configure += " -DGLUTTONY_DIR=" + quote(GLT_ENGINE_SOURCE_DIR);
            #endif
            out.push_back(std::move(configure));
        }
        out.push_back(std::format("cmake --build {}", quote(build_dir)));
        return out;
    }


    bool build(const std::filesystem::path& project_dir, std::function<void(bool ok)> on_done, const build_config config) {

        std::error_code ec{};
        if (!std::filesystem::exists(project_dir / "CMakeLists.txt", ec)) {
            LOG(error, "Can not build the game module: [{}] has no CMakeLists.txt (template: documentation/game_module.md)",
                project_dir.generic_string());
            return false;
        }

        std::lock_guard lock(s_build_mutex);
        if (s_build.running)
            return false;
        if (s_build_thread.joinable())
            s_build_thread.join();                          // the previous build is done (running == false)

        s_build.running = true;
        s_build.last_output.clear();
        const std::vector<std::string> commands = build_commands(project_dir, config);

        s_build_thread = std::thread([commands, on_done = std::move(on_done)]() mutable {

            bool ok = true;
            std::string output{};
            for (const std::string& command : commands) {

                LOG(info, "Game module build: {}", command);
                output += "$ " + command + "\n";
                FILE* pipe = popen((command + " 2>&1").c_str(), "r");
                if (!pipe) {
                    output += "could not start the command\n";
                    ok = false;
                    break;
                }
                std::array<char, 1024> buffer{};
                std::string line{};
                while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
                    line += buffer.data();
                    if (!line.empty() && line.back() == '\n') {
                        output += line;
                        line.pop_back();
                        LOG(trace, "[build] {}", line);
                        line.clear();
                    }
                }
                output += line;
                const int status = pclose(pipe);
                if (status != 0) {
                    ok = false;
                    break;
                }
            }

            if (ok) {
                LOG(info, "Game module build succeeded");
            } else {
                LOG(error, "Game module build failed:\n{}", output);
            }

            {
                std::lock_guard lock(s_build_mutex);
                s_build.running = false;
                s_build.last_ok = ok;
                s_build.last_output = std::move(output);
            }
            if (on_done)
                thread_pool::push_main([on_done = std::move(on_done), ok]() { on_done(ok); });
        });
        return true;
    }


    build_status get_build_status() {

        std::lock_guard lock(s_build_mutex);
        return s_build;
    }

    // Install ---------------------------------------------------------------------------------------------------------

    void install() {

        add_builtin_participants();

        // Copies a crashed run left behind. The folder only holds copies made by load_module(); deleting a file another
        // process has mapped is fine on Linux (the mapping stays valid)
        std::error_code ec{};
        const std::filesystem::path stale = output_dir(application_project_dir()).parent_path() / "loaded";
        if (std::filesystem::is_directory(stale, ec))
            for (const auto& entry : std::filesystem::directory_iterator(stale, ec))
                std::filesystem::remove(entry.path(), ec);

        editor::add_editor_hook(OWNER, [](editor::i_editor& ed) {

            ed.register_menu_item(OWNER, "Game module/Build and load", [] {
                const std::filesystem::path project = application_project_dir();
                (void)build(project, [project](bool ok) {
                    if (!ok)
                        return;
                    unload_all();                           // main thread, between frames (pump_main_thread)
                    load_project_modules(project);
                });
            });
            ed.register_menu_item(OWNER, "Game module/Load", [] {
                thread_pool::push_main([] {
                    unload_all();
                    load_project_modules(application_project_dir());
                });
            });
            ed.register_menu_item(OWNER, "Game module/Unload", [] {
                thread_pool::push_main([] { unload_all(); });
            });
        });
    }


    void remove() {

        {
            std::unique_lock lock(s_build_mutex);
            if (s_build.running)
                LOG(info, "Waiting for the game module build to finish");
        }
        if (s_build_thread.joinable())
            s_build_thread.join();

        editor::remove_editor_hook(OWNER);
        if (auto* ed = editor::get())
            ed->unregister_all(OWNER);

        unload_all();
        s_participants.clear();
    }

}
