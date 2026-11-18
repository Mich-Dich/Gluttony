#include "util/pch.h"

#include "procedural/construct/editor/integration.h"

#include "asset/i_asset_registry.h"
#include "editor/i_editor.h"

#include "procedural/construct/engine/service.h"
#include "procedural/construct/editor/construct_editor_window.h"



namespace GLT::construct {

    // CONSTANTS =======================================================================================================

    namespace {

        constexpr std::string_view                      OWNER = "procedural_construct";
        constexpr std::string_view                      TYPE_NAME = "construct";

        struct state {
            construct_asset_handler                     handler{};
            ref<GLT::asset::i_asset_registry_plugin>    registry{};
            GLT::asset::type                            type{ GLT::asset::core_types::invalid };
            std::vector<GLT::asset::type>               all_types{};
            bool                                        installed = false;
        };

        state& get_state() {
            static state s{};
            return s;
        }

        void register_editor(GLT::editor::i_editor& editor) {

            state& s = get_state();
            editor.register_menu_item(OWNER, "Construct/Construct editor", [] { open_construct_editor(); });
            if (!s.registry || s.type == GLT::asset::core_types::invalid)
                return;

            for (const GLT::asset::type t : s.all_types)
                editor.register_asset_editor(OWNER, t, [](const std::filesystem::path& path) -> GLT::unique_ref<GLT::editor::i_editor_window> {
                    state& st = get_state();
                    if (!st.registry)
                        return nullptr;
                    auto loaded = st.registry->load(path);
                    if (!loaded) {
                        LOG(error, "construct: could not load [{}]", path.generic_string())
                        return nullptr;
                    }
                    return GLT::create_unique_ref<editor::construct_editor_window>(path, std::move(*loaded));
                });

            GLT::editor::asset_creator creator{};
            creator.type = s.type;
            creator.label = "Construct";
            creator.tooltip = "A procedural construct: stages and guides, generated wherever it is used (opens the Construct editor)";
            creator.default_name = "New Construct";
            creator.create = [](const std::filesystem::path& content_path) -> std::expected<void, std::string> {
                state& st = get_state();
                if (!st.registry)
                    return std::unexpected(std::string("no asset registry"));
                auto asset = GLT::create_unique_ref<construct_asset>(st.type);
                auto registered = st.registry->register_runtime(std::move(asset), content_path, content_path.stem().string());
                if (!registered)
                    return std::unexpected(std::format("could not register the asset (error {})", static_cast<int>(registered.error())));
                if (auto saved = st.registry->save(registered->get()); !saved)
                    return std::unexpected(std::format("could not write the file (error {})", static_cast<int>(saved.error())));
                return {};
            };
            editor.register_asset_creator(OWNER, std::move(creator));
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void install_engine_integration() {

        state& s = get_state();
        if (s.installed)
            return;
        s.installed = true;
        s.registry = GLT::asset::registry::get_ref();
        if (s.registry) {
            s.type = s.registry->reserve_type(TYPE_NAME);
            s.all_types = { s.type };
            const auto snap = packs().snapshot();
            for (const auto& p : snap->packs)
                for (const std::string& legacy : p.desc->legacy_asset_types)
                    if (const GLT::asset::type t = s.registry->reserve_type(legacy); std::ranges::find(s.all_types, t) == s.all_types.end())
                        s.all_types.push_back(t);
            s.handler.set_types(s.all_types);
            s.registry->register_handler(&s.handler);
        } else {
            LOG(warn, "construct: no asset registry, construct assets are unavailable")
        }
        GLT::editor::add_editor_hook(OWNER, [](GLT::editor::i_editor& editor) { register_editor(editor); });
    }


    void remove_engine_integration() {

        state& s = get_state();
        if (!s.installed)
            return;
        s.installed = false;
        GLT::editor::remove_editor_hook(OWNER);
        if (GLT::editor::i_editor* editor = GLT::editor::get())
            editor->unregister_all(OWNER);                  // closes the Construct editors: they hold results of pack code
        GLT::thread_pool::wait_for_all();                   // evaluations in flight run pack code
        if (s.registry)
            s.registry->unregister_handler(&s.handler);
        s.registry.reset();
        s.type = GLT::asset::core_types::invalid;
        s.all_types.clear();
    }


    GLT::asset::type construct_asset_type() noexcept { return get_state().type; }


    bool open_construct_editor() {

        GLT::editor::i_editor* editor = GLT::editor::get();
        if (!editor)
            return false;
        editor->open_window(OWNER, GLT::create_unique_ref<editor::construct_editor_window>());
        return true;
    }

}
