
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

    // First asset-type id available to plugins. [0, CUSTOM_TYPE_BEGIN) is reserved for the engine's core types
    // (see [core_types] in asset/type.h)
    static constexpr u32 CUSTOM_TYPE_BEGIN = 1024;      // [0,1024) reserved for core

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    namespace registry {

        // @brief Fetches the process-wide asset registry plugin
        // @return Strong reference to the registry, or an empty ref if it isn't loaded
        FORCE_INLINE_R ref<GLT::asset::i_asset_registry_plugin> get_ref() {

            return GLT::plugin_manager::get_plugin_ref<GLT::asset::i_asset_registry_plugin>(plugin_manager::interface::asset_registry);
        }

    }

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief The engine's one-stop shop for asset loading, saving, and lifetime
    //
    // Owns the on-disk format, chunk table, dependency graph, hot-reload watching, string table, and asset cache
    // Knows nothing about meshes, textures, or audio - that's the handlers' job. Callers reach it via [registry::get_ref()]
    class i_asset_registry_plugin : public plugin_manager::i_plugin {
    public:

        // lifecycle ---------------------------------------------------------------------------------------------------

        // @brief Loads an asset from disk, or returns the existing handle
        //
        // @param path  CAUTION - content-dir-relative path (PROJECT_CONTENT_DIR / path)
        // @return Handle to the loaded asset, or a load_error
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::load_error> load(std::filesystem::path path) = 0;


        // @brief Drops one reference to an asset
        //
        // The asset is unloaded and its slot freed once the last reference is released. Prefer [asset_ref] (RAII
        // over calling this directly
        //
        // @param h Handle to release
        virtual void unload(GLT::asset::handle h) noexcept = 0;

        // persistence -------------------------------------------------------------------------------------------------

        // @brief Persists a live asset back to its canonical path
        //
        // No-op for handlers that haven't overridden [serialize()] (returns [load_error::no_handler])
        //
        // The write is atomic: bytes go to [<path>.tmp], then a rename swaps them in. A failed save never leaves
        // a half-written file on disk
        //
        // @param h Handle to save
        // @return Success, or a load_error
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> save(GLT::asset::handle h) = 0;


        // @brief Saves an asset under a new canonical path
        //
        // The old path entry is dropped from the path index, the new one takes over. Useful for "Save As" in the editor
        //
        // @param h         Handle to save
        // @param new_path  CAUTION - content-dir-relative path (PROJECT_CONTENT_DIR / path)
        // @return Success, or a load_error
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> save_as(GLT::asset::handle h, const std::filesystem::path& new_path) = 0;


        // @brief Reports whether a handle refers to a currently-loaded asset
        // @param h Handle to test
        // @return true if the handle is live
        [[nodiscard]] virtual bool is_loaded(GLT::asset::handle h) const = 0;


        // @brief Looks up the handle of an already-loaded asset by path
        //
        // @param path  CAUTION - content-dir-relative path (PROJECT_CONTENT_DIR / path)
        // @return The asset's handle, or INVALID_HANDLE if not loaded
        [[nodiscard]] virtual GLT::asset::handle find(const std::filesystem::path& path) const = 0;

        // relocation --------------------------------------------------------------------------------------------------

        // @brief Moves a file OR directory tree inside the content directory
        //
        // If a loaded asset was registered at [from], its slot's [canonical_path], [virtual_path], and the [m_by_path]
        // index are updated atomically. Directory moves rewrite the canonical path of every loaded asset under [from],
        // plus any matching [m_source_index] entries
        //
        // Callers that just want to rename a folder on disk should still route through here when a loaded asset might
        // be inside it - otherwise the registry will happily hand out stale handles that point at the old path
        //
        // NOTE: this does NOT re-serialize dependent assets. In-session, dependents resolve by ID (m_by_id fast path
        // in [load_unlocked]) so nothing breaks. On cold start, a dependent's baked-in on-disk dep path will be stale;
        // callers should re-save dependents if that matters
        //
        // @param from  CAUTION - content-dir-relative path (PROJECT_CONTENT_DIR / p)
        // @param to    CAUTION - content-dir-relative path (PROJECT_CONTENT_DIR / p)
        // @return Success, or a load_error
        [[nodiscard]] virtual std::expected<void, GLT::asset::load_error> move(const std::filesystem::path& from,
            const std::filesystem::path& to) = 0;

        // persistence -------------------------------------------------------------------------------------------------

        // @brief Installs an in-memory asset that has no backing file yet
        //
        // The registry takes ownership of asset, mints a fresh UUID unless one is provided, and installs it at [path]
        // The asset is NOT written to disk - call [save(handle)] afterwards to emit the first bytes
        //
        // Intended for: a plugin that builds runtime state (a fresh world, a procedural material) and wants the registry
        // to start tracking it so subsequent [save()] calls work. Most callers should expose a higher-level method on their
        // own interface instead of calling this directly
        //
        // Fails with [already_exists] if [path] or [id] is already claimed
        //
        // @param asset  Runtime asset to adopt
        // @param path   Content-dir-relative path to install it at
        // @param name   Optional display name; defaults to the path's stem
        // @param id     Optional preassigned UUID; a fresh one is minted when zero
        // @return Handle to the newly-registered asset, or a load_error
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::load_error> register_runtime(
            GLT::unique_ref<GLT::asset::i_runtime_asset> asset, const std::filesystem::path& path, std::string_view name = {},
            GLT::UUID id = {}) = 0;

        // queries -----------------------------------------------------------------------------------------------------

        // @brief Returns the metadata of a loaded asset
        // @param h Handle to query
        // @return Reference to the asset's [info]; an empty info if the handle is stale
        [[nodiscard]] virtual const GLT::asset::info& info(GLT::asset::handle h) const = 0;


        // @brief Returns a mutable pointer to the asset's runtime data
        // @param h Handle to query
        // @return Pointer to the asset, or nullptr if the handle is stale
        [[nodiscard]] virtual i_runtime_asset* data(GLT::asset::handle h) noexcept = 0;


        // @brief Returns a const pointer to the asset's runtime data
        // @param h Handle to query
        // @return Pointer to the asset, or nullptr if the handle is stale
        [[nodiscard]] virtual const i_runtime_asset* data(GLT::asset::handle h) const noexcept = 0;


        // @brief Typed convenience wrapper around [data()]
        // @tparam T A concrete i_runtime_asset subclass
        // @param h Handle to query
        // @return Pointer to the asset cast to [T*], or nullptr if the handle is stale
        template<typename T> requires std::derived_from<T, i_runtime_asset>
        [[nodiscard]] T* data_as(GLT::asset::handle h) noexcept { return static_cast<T*>(data(h)); }

        // handler registration ----------------------------------------------------------------------------------------

        // @brief Registers a handler for its declared types
        //
        // Called from [i_asset_handler::on_load()]. Overrides any handler previously registered for the same type
        //
        // @param handler Handler to register
        virtual void register_handler(i_asset_handler* handler) = 0;


        // @brief Removes a handler and drops every asset it owned
        //
        // Called from [i_asset_handler::on_unload()]
        //
        // @param handler Handler to remove
        virtual void unregister_handler(i_asset_handler* handler) noexcept = 0;


        // @brief Returns the asset types that currently have a registered handler
        // @return Span over the live type list. Valid until the next call
        [[nodiscard]] virtual std::span<const GLT::asset::type> registered_types() const = 0;

        // factory registration ----------------------------------------------------------------------------------------

        // @brief Registers an import factory
        // @param factory Factory to register
        virtual void register_factory(GLT::asset::factory::i_asset_factory_plugin* factory) = 0;


        // @brief Removes an import factory
        // @param factory Factory to remove
        virtual void unregister_factory(GLT::asset::factory::i_asset_factory_plugin* factory) noexcept = 0;

        // import (editor / build-time) --------------------------------------------------------------------------------

        // @brief Imports a source file into an engine asset
        //
        // Routes to whichever factory binds ([source_extension], [target_type]). If [out_path] is empty, the registry
        // derives one next to the source (or under a configured import root). On success, the imported asset is loaded
        // and its handle returned - the editor basically always wants a preview right away
        //
        // @param source       Path to the source file
        // @param target_type  Type of asset to produce
        // @param out_path     Optional explicit output path
        // @param opts         Import-time options forwarded to the factory
        // @return Handle to the imported asset, or an import_error
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::import_error> import(const std::filesystem::path& source, 
            GLT::asset::type target_type, const std::filesystem::path& out_path = {}, const GLT::asset::import_options& opts = {}) = 0;


        // @brief Probes which factories could handle a given source file
        //
        // Cheap - the editor uses it to populate the "Import as..." context menu without committing to a target type
        //
        // @param source Path to the candidate source file
        // @return Span over the matching bindings. Valid until the next call
        [[nodiscard]] virtual std::span<const GLT::asset::factory::binding> candidate_imports(const std::filesystem::path& source) const = 0;

        // type registry (for game-defined types) ----------------------------------------------------------------------

        // @brief Hands out a fresh asset-type id for a plugin-defined type
        //
        // Idempotent - calling twice with the same name returns the same id
        // Called from [i_asset_handler::on_load()]
        //
        // @param name Plugin-defined type name (e.g. "mygame.weapon")
        // @return The reserved type, or [core_types::invalid] if the id space is exhausted
        [[nodiscard]] virtual GLT::asset::type reserve_type(std::string_view name) = 0;


        // @brief Looks up a type id by name
        // @param name Type name to resolve
        // @return The matching type, or std::nullopt
        [[nodiscard]] virtual std::optional<GLT::asset::type> type_from_name(std::string_view name) const = 0;


        // @brief Looks up a type name by id
        // @param t Type id to resolve
        // @return The matching name, or an empty string_view
        [[nodiscard]] virtual std::string_view name_from_type(GLT::asset::type t) const = 0;

        // dependency graph --------------------------------------------------------------------------------------------

        // @brief Adds a directed edge to the dependency graph
        //
        // Idempotent per (from, to) pair. Both [info.dependencies] and [info.dependents] are updated on the respective sides
        //
        // @param from  Dependent asset
        // @param to    Dependency asset
        virtual void add_dependency(GLT::asset::handle from, GLT::asset::handle to) = 0;


        // @brief Returns the direct dependencies of an asset
        // @param handle  Asset to query
        // @return Span over the dependency handles. Empty for stale handles
        [[nodiscard]] virtual std::span<const GLT::asset::handle> dependencies(GLT::asset::handle handle) const = 0;

        // async -------------------------------------------------------------------------------------------------------

        // @brief Asynchronously loads an asset
        //
        // Runs [load()] on the thread pool. The returned future becomes ready when the load completes
        //
        // @param path Content-dir-relative path
        // @return Future producing the load result
        [[nodiscard]] virtual std::future<std::expected<GLT::asset::handle, GLT::asset::load_error>> load_async(std::filesystem::path path) = 0;

        // manage asset lifetime ---------------------------------------------------------------------------------------

        // @brief Adds one reference to an asset
        // @param handle Asset to retain
        virtual void retain(GLT::asset::handle handle) = 0;


        // @brief Drops one reference to an asset
        //
        // When the last reference goes away, the asset's data is destroyed, its slot freed, and its own dependencies
        // released in turn. Prefer [asset_ref] (RAII) over calling this directly
        //
        // @param handle Asset to release
        virtual void release(GLT::asset::handle handle) = 0;

        // PEEK API (for debugging, editor, ...) -----------------------------------------------------------------------

        // @brief Reads a single named chunk from an asset file WITHOUT loading the asset or invoking its handler
        //
        // The registry parses only the header + chunk table and returns the (decompressed) payload bytes. Intended for
        // editor tooling: thumbnails, metadata inspectors, source diffs. Runtime code should go through [load()] / [data()]
        //
        // @param path  CAUTION - content-dir-relative path (PROJECT_CONTENT_DIR / path)
        // @param id    Chunk id to fetch
        // @return The chunk's bytes, or a load_error ([not_found] when the chunk is absent)
        [[nodiscard]] virtual std::expected<std::vector<std::byte>, GLT::asset::load_error> read_chunk(
            const std::filesystem::path& path, chunk_id id) const = 0;
            
    };

}
