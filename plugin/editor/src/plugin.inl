#pragma once

#include <util/pch.h>
#include <layer/layer.h>
#include <layer/layer_stack.h>
#include <application.h>

#include <event/event_bus.h>

#include "editor_layer.h"

#include "resource_manager/icon_manager.h"
#include "config/implot_config.h"
#include "util/extension_host.h"
#include "util/event/asset_event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    plugin::plugin() {}
    
    
    plugin::~plugin() {}

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void plugin::on_load() {

        implot_config::init();
        icon_manager::init();
        mp_editor_layer = GLT::application::get().get_layer_stack_ref().push_layer<editor_layer>();
        GLT::editor::run_editor_hooks(*this);       // plugins loaded before the editor register now
    }


    void plugin::on_unload() {

        GLT::application::get().get_layer_stack_ref().pop_layer(mp_editor_layer);      // ours, not whatever is on top
        mp_editor_layer = {};
        extension_host::clear();                    // after the layer (and its windows) are gone
        icon_manager::shutdown();
        implot_config::shutdown();
    }

    // i_editor --------------------------------------------------------------------------------------------------------

    void plugin::register_asset_editor(std::string_view owner, GLT::asset::type type, GLT::editor::window_factory factory) {
        extension_host::add_asset_editor(owner, type, std::move(factory));
    }


    void plugin::register_asset_creator(std::string_view owner, GLT::editor::asset_creator creator) {
        extension_host::add_asset_creator(owner, std::move(creator));
    }


    void plugin::register_menu_item(std::string_view owner, std::string_view menu_path, std::function<void()> action) {
        extension_host::add_menu_item(owner, menu_path, std::move(action));
    }


    void plugin::open_window(std::string_view owner, GLT::unique_ref<GLT::editor::i_editor_window> window) {
        extension_host::queue_window(owner, std::move(window));      // editor_layer::update() moves it into the dock space
    }


    void plugin::open_asset(GLT::asset::type type, const std::filesystem::path& asset_path) {
        GLT::event_bus::post(asset_open_event{ type, asset_path });
    }


    GLT::world::i_world* plugin::edited_world() { return GLT::application::get().current_world(); }


    void plugin::unregister_all(std::string_view owner) { extension_host::remove_owner(owner); }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
