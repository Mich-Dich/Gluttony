
#pragma once

#include "asset/type.h"
#include "plugin_system/plugin_manager.h"
#include "asset/i_asset_factory.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {
    class i_asset_handler;
    class i_asset_registry_plugin;
}

namespace GLT::asset {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // CONSTANTS =======================================================================================================

    static constexpr u32 CUSTOM_TYPE_BEGIN = 1024;      // [0,1024) reserved for core

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace registry {

        FORCE_INLINE_R ref<GLT::asset::i_asset_registry_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref<GLT::asset::i_asset_registry_plugin>(plugin_manager::interface::asset_registry);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // One-stop shop the engine core talks to. Owns: file format, chunk table
    // dependency graph, hot-reload watching, string table, caching. Knows nothing
    // about meshes, textures, or audio - that is the handlers' job
    class i_asset_registry_plugin : public plugin_manager::i_plugin {
    public:

        // lifecycle ---------------------------------------------------------------------------------------------------

        // @param path  CAUTION - this uses a content dir relative path (PROJECT_CONTENT_DIR / path)
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::load_error> load(std::filesystem::path path) = 0;


        virtual void unload(GLT::asset::handle h) noexcept = 0;

        // persistence -------------------------------------------------------------------------------------------------

        // Persist the live asset back to its canonical path. No-op for handlers that haven't overridden serialize() (returns load_error::no_handler)
        //
        // The write is atomic: bytes go to `<path>.tmp`, then a rename swaps them in. A failed save never leaves a half-written file on disk
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> save(GLT::asset::handle h) = 0;


        // Same as save(), but moves the asset to a new canonical path. The old path entry is dropped from the path index,
        // the new one takes over. Useful for "Save As" in the editor
        // @param new_path  CAUTION - this uses a content dir relative path (PROJECT_CONTENT_DIR / path)
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> save_as(GLT::asset::handle h, const std::filesystem::path& new_path) = 0;


        [[nodiscard]] virtual bool is_loaded(GLT::asset::handle h) const = 0;


        // @param path  CAUTION - this uses a content dir relative path (PROJECT_CONTENT_DIR / path)
        [[nodiscard]] virtual GLT::asset::handle find(const std::filesystem::path& path) const = 0;

        // relocation --------------------------------------------------------------------------------------------------

        // Move a file OR directory tree within the content directory. If a loaded asset was registered at [from], its slot's canonical path,
        // virtual_path, and the m_by_path index are updated atomically. Directory moves rewrite the canonical path of every loaded asset
        // under [from], plus any matching m_source_index entries
        //
        // Callers that just want to rename a folder on disk should still route through here when a loaded asset might be inside it
        // otherwise the registry will happily hand out stale handles that point at the old path
        //
        // NOTE: this does NOT re-serialize dependent assets. In-session, dependents resolve by ID (m_by_id fast path in load_unlocked) so
        // nothing breaks. On cold start, a dependent's baked-in on-disk dep path will be stale; callers should re-save dependents
        // (query them, or just fire a bulk save) if that matters
        //
        // @param from,to  CAUTION - content dir relative paths (PROJECT_CONTENT_DIR / p)
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> move(const std::filesystem::path& from,
            const std::filesystem::path& to) = 0;

        // persistence -------------------------------------------------------------------------------------------------

        // Persist an in-memory asset that has no backing file yet. The registry takes ownership of `asset`, mints a fresh UUID
        // unless one is provided, and installs it at `path`. The asset is NOT written to disk - call save(handle) afterwards to
        // emit the first bytes
        //
        // Intended for: a plugin that builds runtime state (a fresh world, a procedural material) and wants the registry to
        // start tracking it so subsequent save() calls work. Most callers should expose a higher-level method on their own
        // interface instead of calling this directly
        //
        // Fails with `already_exists` if `path` or `id` is already claimed
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::load_error> register_runtime(
            GLT::unique_ref<GLT::asset::i_runtime_asset> asset, const std::filesystem::path& path, std::string_view name = {},
            GLT::UUID id = {}) = 0;

        // queries -----------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual const GLT::asset::info& info(GLT::asset::handle h) const = 0;


        // Non-const + const overloads; no more bare void*
        [[nodiscard]] virtual i_runtime_asset* data(GLT::asset::handle h) noexcept = 0;


        [[nodiscard]] virtual const i_runtime_asset* data(GLT::asset::handle h) const noexcept = 0;


        template<typename T> requires std::derived_from<T, i_runtime_asset>
        [[nodiscard]] T* data_as(GLT::asset::handle h) noexcept { return static_cast<T*>(data(h)); }

        // handler registration ----------------------------------------------------------------------------------------

        virtual void register_handler(i_asset_handler* handler) = 0;


        virtual void unregister_handler(i_asset_handler* handler) noexcept = 0;


        [[nodiscard]] virtual std::span<const GLT::asset::type> registered_types() const = 0;

        // factory registration ----------------------------------------------------------------------------------------

        virtual void register_factory(GLT::asset::factory::i_asset_factory_plugin* factory) = 0;


        virtual void unregister_factory(GLT::asset::factory::i_asset_factory_plugin* factory) noexcept = 0;

        // import (editor / build-time) --------------------------------------------------------------------------------

        // Routes to whichever factory binds (source_extension, target_type)
        // If out_path is empty, the registry derives one next to the source (or under a configured import root)
        // On success the imported asset is loaded and its handle returned - the editor basically always wants a preview right away
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::import_error> import(const std::filesystem::path& source, 
            GLT::asset::type target_type, const std::filesystem::path& out_path = {}, const GLT::asset::import_options& opts = {}) = 0;


        // Cheap probe: which factories could handle this file? The editor uses this to populate the "Import as…" context menu
        [[nodiscard]] virtual std::span<const GLT::asset::factory::binding> candidate_imports(const std::filesystem::path& source) const = 0;

        // type registry (for game-defined types) ----------------------------------------------------------------------

        [[nodiscard]] virtual GLT::asset::type reserve_type(std::string_view name) = 0;             // called from handler on_load


        [[nodiscard]] virtual std::optional<GLT::asset::type> type_from_name(std::string_view name) const = 0;


        [[nodiscard]] virtual std::string_view name_from_type(GLT::asset::type t) const = 0;

        // dependency graph --------------------------------------------------------------------------------------------

        virtual void add_dependency(GLT::asset::handle from, GLT::asset::handle to) = 0;


        [[nodiscard]] virtual std::span<const GLT::asset::handle> dependencies(GLT::asset::handle handle) const = 0;

        // async -------------------------------------------------------------------------------------------------------

        [[nodiscard]] virtual std::future<std::expected<GLT::asset::handle, GLT::asset::load_error>> load_async(std::filesystem::path path) = 0;

        // manage asset lifetime ---------------------------------------------------------------------------------------

        virtual void retain(GLT::asset::handle handle) = 0;


        virtual void release(GLT::asset::handle handle) = 0;

    };

}
