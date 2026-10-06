#pragma once

#include <asset/type.h>
#include <asset/header.h>
#include <application.h>
#include <config/project.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::registry_default {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct composed_file {

        std::vector<std::byte>              bytes;
        std::vector<chunk_entry>            chunks;     // final offsets, post-alignment
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================
    
    // Write a blob to the VFS, creating parent directories as needed.
    bool write_blob(const std::filesystem::path& path, std::span<const std::byte> data);

    // All offsets inside the file are relative to byte 0.
    // Layout (see header.h):
    //   [header]
    //   [string table]        name + source_path + each dep's path
    //   [dependency_disk[]]   ids + offsets + target types
    //   [chunk_entry[]]       offsets into the chunk region
    //   [chunk data]          each chunk, 8-byte aligned
    //
    // Returns the composed file as a single byte vector.
    composed_file compose_asset_file(const UUID& id, GLT::asset::type asset_type, GLT::asset::content_hash payload_hash, 
        const std::filesystem::path& source_path, const asset_writer_impl& writer);

    // <source_dir>/<source_stem>.<ext_for_type>
    std::filesystem::path default_output_path(const std::filesystem::path& source, GLT::asset::type target);

    FORCE_INLINE_R bool is_valid_file(const std::filesystem::path& p);
    
    GLT::asset::content_hash hash_writer(const asset_writer_impl& w);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool write_blob(const std::filesystem::path& path, std::span<const std::byte> data) {

        std::error_code error{};
        GLT::vfs::create_directories(path.parent_path(), error);
        if (error)
            return false;

        const auto mode = GLT::vfs::file_open_mode::write | GLT::vfs::file_open_mode::truncate | GLT::vfs::file_open_mode::create;
        const auto h = GLT::vfs::open_file(path, mode, error);
        if (error || h == ::INVALID_HANDLE) 
            return false;

        const size_t written = GLT::vfs::write_file(h, data.data(), data.size(), 0);
        GLT::vfs::close_file(h);
        return written == data.size();
    }


    composed_file compose_asset_file(const UUID& id, GLT::asset::type asset_type, GLT::asset::content_hash payload_hash, 
        const std::filesystem::path& source_path, const asset_writer_impl& writer) {

        using GLT::asset::header;
        using GLT::asset::chunk_entry;
        using GLT::asset::dependency_disk;

        // Align each section start to 8 bytes.
        auto align8 = [](u64 x) { return (x + 7u) & ~u64(7u); };

        // string table ------------------------------------------------------------------------------------------------
        const u64 header_off = 0;
        const u64 strtab_off = align8(header_off + sizeof(header));

        std::string strtab;
        auto push_string = [&strtab, strtab_off](std::string_view s) -> u64 {
            const u64 off = strtab_off + strtab.size();   // file-absolute
            strtab.append(s);
            strtab.push_back('\0');
            return off;
        };

        const u64 name_off = push_string(writer.name());
        const u64 source_off = push_string(source_path.generic_string());

        const auto& deps = writer.deps();
        std::vector<u64> dep_path_offsets(deps.size(), 0);
        for (size_t i = 0; i < deps.size(); ++i) {
            if (deps[i].by_path && !deps[i].virtual_path.empty())
                dep_path_offsets[i] = push_string(deps[i].virtual_path);
        }

        // tables (as raw byte blobs, so we can memcpy them out later) -------------------------------------------------
        std::vector<dependency_disk> dep_table(deps.size());
        for (size_t i = 0; i < deps.size(); ++i) {
            dep_table[i].id = deps[i].id;
            dep_table[i].path_offset = dep_path_offsets[i];
            dep_table[i].target_type = deps[i].target_type.value;
        }

        const auto& chunks = writer.chunks();
        std::vector<chunk_entry> chunk_table(chunks.size());

        // compute layout ----------------------------------------------------------------------------------------------

        const u64 deptab_off = align8(strtab_off + strtab.size());
        const u64 chunktab_off = align8(deptab_off + dep_table.size() * sizeof(dependency_disk));
        const u64 chunkdata_off = align8(chunktab_off + chunk_table.size() * sizeof(chunk_entry));

        // Lay out chunk data; each chunk is 8-aligned (padding between them if needed).
        std::vector<u64> chunk_offsets(chunks.size());
        u64 cursor = chunkdata_off;
        for (size_t i = 0; i < chunks.size(); ++i) {
            cursor = align8(cursor);
            chunk_offsets[i] = cursor;
            chunk_table[i].id = chunks[i].id;
            chunk_table[i].compression = chunks[i].compression;
            chunk_table[i].offset = cursor;
            chunk_table[i].size_on_disk = chunks[i].bytes.size();
            chunk_table[i].size_decoded = chunks[i].bytes.size();    // no compression yet
            cursor += chunks[i].bytes.size();
        }
        const u64 total_size = align8(cursor);

        // header ------------------------------------------------------------------------------------------------------
        header hdr{};
        hdr.magic = header::MAGIC;
        hdr.format_version = header::CURRENT_VERSION;
        hdr.min_engine_version = header::CURRENT_VERSION;
        hdr.flags = 0;                    // set compression flag per-chunk later
        hdr.id = id;
        hdr.asset_type = asset_type;
        hdr.hash = payload_hash;
        hdr.name_offset = name_off;
        hdr.source_path_offset = source_off;
        hdr.chunk_table_offset = chunktab_off;
        hdr.dependency_table_offset = deptab_off;
        hdr.string_table_offset = strtab_off;
        hdr.chunk_count = static_cast<u32>(chunk_table.size());
        hdr.dependency_count = static_cast<u32>(dep_table.size());
        hdr.total_size = total_size;

        // emit --------------------------------------------------------------------------------------------------------
        std::vector<std::byte> out(total_size, std::byte{0});
        auto store = [&out](u64 off, const void* src, size_t n) { std::memcpy(out.data() + off, src, n); };

        store(header_off,  &hdr, sizeof(hdr));
        if (!strtab.empty())
            store(strtab_off, strtab.data(), strtab.size());

        if (!dep_table.empty())
            store(deptab_off, dep_table.data(), dep_table.size() * sizeof(dependency_disk));

        if (!chunk_table.empty())
            store(chunktab_off, chunk_table.data(), chunk_table.size() * sizeof(chunk_entry));

        for (size_t i = 0; i < chunks.size(); ++i) {
            if (!chunks[i].bytes.empty())
                store(chunk_offsets[i], chunks[i].bytes.data(), chunks[i].bytes.size());
        }

        return composed_file{ std::move(out), std::move(chunk_table) };
    }


    std::filesystem::path default_output_path(const std::filesystem::path& source, GLT::asset::type target) {

        std::filesystem::path p = source;
        p.replace_extension(std::string(".") + std::string(GLT::asset::type_to_extension(target)));
        return p;
    }


    FORCE_INLINE_R bool is_valid_file(const std::filesystem::path& p) {

        return !p.empty() && p.has_extension();         // check if file
    }


    // Cheap xxh3-ish placeholder - swap for whatever you actually use.
    GLT::asset::content_hash hash_writer(const asset_writer_impl& w) {

        u64 h = 0xcbf29ce484222325ULL;
        auto mix = [&h](const void* p, size_t n) {
            const auto* b = static_cast<const u8*>(p);
            for (size_t i = 0; i < n; ++i) {
                h ^= b[i];
                h *= 0x100000001b3ULL;
            }
        };
        for (const auto& c : w.chunks()) {
            mix(&c.id, sizeof(c.id));
            if (!c.bytes.empty())
                mix(c.bytes.data(), c.bytes.size());
        }
        return h;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    void plugin::on_load() {

        m_type_names.resize(CUSTOM_TYPE_BEGIN);     // Pre-size the type name table so [0, CUSTOM_TYPE_BEGIN) is contiguous.

        using namespace GLT::asset::core_types;
        auto define = [this](GLT::asset::type t, std::string_view n) {

            m_type_names[t.value] = std::string(n);
            m_type_name_to_id[std::string(n)] = t;
        };

        define(invalid,             "invalid");
        define(world,               "world");
        define(region,              "region");
        define(audio,               "audio");

		// ------ mesh types ------
        define(static_mesh,         "static_mesh");
        define(procedural_mesh,     "procedural_mesh");
        define(dynamic_mesh,        "dynamic_mesh");
        define(skeletal_mesh,       "skeletal_mesh");
        define(mesh_collection,     "mesh_collection");

		// ------ texture types ------
        define(texture2D,           "texture2D");
        define(texture3D,           "texture3D");
        define(cube_map,            "cube_map");

		// ------ material types ------
        define(material,            "material");
        define(material_instance,   "material_instance");

        define(anim,                "anim");
        define(light,               "light");
        define(bvh,                 "bvh");
        define(volume,              "volume");

        // Bulk save is the registry's job; "save as" is UI-driven and lives in the editor layer
        m_save_subscription = GLT::event_bus::subscribe<GLT::save_event>([this](GLT::save_event& e) { on_save_event(e); });

        LOG_LOADED
    }


    void plugin::on_unload() {

        GLT::event_bus::unsubscribe(m_save_subscription);

        std::unique_lock lock(m_mutex);

        // Tear down every live slot. Handlers get the chance to flush / release.
        for (auto& s : m_slots) {

            if (!s.alive)
                continue;

            s.data.reset();
            s.alive   = false;
            s.handler = nullptr;
        }
        m_slots.clear();
        m_free_indices.clear();
        m_by_path.clear();
        m_handlers.clear();
        m_type_name_to_id.clear();
        m_type_names.clear();

        LOG_UNLOADED
    }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // lifecycle -------------------------------------------------------------------------------------------------------

    std::expected<GLT::asset::handle, GLT::asset::load_error> plugin::load(std::filesystem::path path) {

        const auto relative = GLT::project::to_content_relative(path);
        if (relative.empty())
            return std::unexpected{ GLT::asset::load_error::not_found };

        std::unique_lock lock(m_mutex);
        std::unordered_set<std::string> in_flight;
        return load_unlocked(relative, in_flight);
    }


    void plugin::unload(GLT::asset::handle h) noexcept {

        std::unique_lock lock(m_mutex);
        slot* s = slot_for(h);
        if (!s)
            return;

        // Detach from every dependent so their info.dependents shrinks correctly.
        for (GLT::asset::handle dep : s->dependents_storage) {
            if (slot* d = slot_for(dep)) {
                auto& v = d->dependents_storage;
                v.erase(std::remove(v.begin(), v.end(), h), v.end());
                d->asset_info.dependents = v;
            }
        }

        // Detach from every dep so their info.dependents shrinks correctly.
        for (GLT::asset::handle dep : s->deps_storage) {
            if (slot* d = slot_for(dep)) {
                auto& v = d->dependents_storage;
                v.erase(std::remove(v.begin(), v.end(), h), v.end());
                d->asset_info.dependents = v;
            }
        }

        m_by_path.erase(s->canonical_path.generic_string());
        release_slot(h);
    }


    std::expected<void, GLT::asset::load_error> plugin::save(GLT::asset::handle h) { return save_as(h, {}); }


    std::expected<void, GLT::asset::load_error> plugin::save_as(GLT::asset::handle h, const std::filesystem::path& new_path) {

        // We deliberately do NOT hold m_mutex across handler->serialize(). Handlers are allowed to call back into the registry

        GLT::asset::info info_snap{};
        asset_writer_impl writer;
        std::filesystem::path target;
        GLT::asset::i_runtime_asset* asset_ptr = nullptr;
        GLT::asset::i_asset_handler* handler_ptr = nullptr;

        {
            std::unique_lock lock(m_mutex);

            GLT::asset::registry_default::slot* slot = slot_for(h);
            if (!slot)
                return std::unexpected{ GLT::asset::load_error::not_found };

            if (!slot->handler || !slot->data)
                return std::unexpected{ GLT::asset::load_error::not_found };

            // resolve target (project-relative) -----------------------------------------------------------------------
            if (new_path.empty()) {
                target = slot->canonical_path;
            } else {
                auto relative = GLT::project::to_content_relative(new_path);
                if (relative.empty())
                    return std::unexpected{ GLT::asset::load_error::not_found };
                target = relative;
            }

            // seed the writer -----------------------------------------------------------------------------------------
            writer.set_name(slot->asset_info.name);

            // Re-declare every dependency so the file stays self-describing
            for (GLT::asset::handle dep : slot->deps_storage) {

                const GLT::asset::registry_default::slot* dep_slot = slot_for(dep);
                if (!dep_slot)
                    continue;

                std::error_code error{};
                const auto rel = std::filesystem::relative(dep_slot->canonical_path, slot->canonical_path.parent_path(), error);
                const std::string path_str = error ? dep_slot->canonical_path.generic_string() : rel.generic_string();

                writer.declare_dependency(dep_slot->asset_info.id, path_str, dep_slot->asset_info.asset_type);
            }

            info_snap = slot->asset_info;
            asset_ptr = slot->data.get();
            handler_ptr = slot->handler;
        }

        // handler serialize + compose + write (no lock held) ----------------------------------------------------------

        if (auto result = handler_ptr->serialize(info_snap, *asset_ptr, writer); !result)
            return std::unexpected{ result.error() };

        const GLT::asset::content_hash new_hash = hash_writer(writer);
        auto composed = compose_asset_file(info_snap.id, info_snap.asset_type, new_hash, info_snap.source_path, writer);

        const std::filesystem::path abs_target = PROJECT_CONTENT_DIR / target;
        const std::filesystem::path abs_tmp = PROJECT_CONTENT_DIR / (target.string() + ".tmp");

        if (!write_blob(abs_tmp, composed.bytes))
            return std::unexpected{ GLT::asset::load_error::out_of_memory };

        std::error_code error{};
        std::filesystem::rename(abs_tmp, abs_target, error);
        if (error) {
            std::filesystem::remove(abs_tmp, error);
            return std::unexpected{ GLT::asset::load_error::out_of_memory };
        }

        // Re-acquire the lock and repair the slot to match what we wrote ----------------------------------------------

        std::unique_lock lock(m_mutex);

        GLT::asset::registry_default::slot* slot = slot_for(h);
        if (!slot)
            return std::unexpected{ GLT::asset::load_error::not_found };    // slot disappeared under us

        const std::string old_key = slot->canonical_path.generic_string();
        const std::string new_key = target.generic_string();

        slot->canonical_path = target;
        slot->chunks_storage = std::move(composed.chunks);
        slot->asset_info.chunks = slot->chunks_storage;                   // re-anchor the span
        slot->asset_info.hash = new_hash;
        slot->asset_info.last_modified = std::chrono::system_clock::now();

        if (old_key != new_key) {
            m_by_path.erase(old_key);
            m_by_path.emplace(new_key, h);
        }

        return {};
    }


    bool plugin::is_loaded(GLT::asset::handle h) const {

        std::shared_lock lock(m_mutex);
        return slot_for(h) != nullptr;
    }


    GLT::asset::handle plugin::find(const std::filesystem::path& path) const {

        auto relative = GLT::project::to_content_relative(path);
        if (relative.empty())
            return {};

        std::shared_lock lock(m_mutex);
        auto it = m_by_path.find(relative.generic_string());
        if (it == m_by_path.end()) 
            return INVALID_HANDLE;

        return it->second;
    }

    // relocation ------------------------------------------------------------------------------------------------------

    std::expected<void, GLT::asset::load_error> plugin::move(const std::filesystem::path& from, const std::filesystem::path& to) {

        // Normalize on both sides so the prefix match below is reliable.
        auto from_rel = GLT::project::to_content_relative(from).lexically_normal();
        auto to_rel   = GLT::project::to_content_relative(to).lexically_normal();
        if (from_rel.empty() || to_rel.empty())
            return std::unexpected{ GLT::asset::load_error::not_found };

        const auto abs_from = (PROJECT_CONTENT_DIR / from_rel).lexically_normal();
        const auto abs_to = (PROJECT_CONTENT_DIR / to_rel).lexically_normal();

        std::error_code error{};
        if (!GLT::vfs::exists(abs_from, error) || error)
            return std::unexpected{ GLT::asset::load_error::not_found };

        // Case-only rename on Windows: exists() is case-insensitive, so it reports the target as already present. equivalent() tells us it's the same file
        bool target_claimed = GLT::vfs::exists(abs_to, error) && !error;
        if (target_claimed) {
            std::error_code eq_err{};
            if (GLT::vfs::equivalent(abs_from, abs_to, eq_err) && !eq_err)
                target_claimed = false;
        }
        if (target_claimed)
            return std::unexpected{ GLT::asset::load_error::already_exists };

        [[maybe_unused]] const bool is_dir = GLT::vfs::is_directory(abs_from, error);
        if (error)
            return std::unexpected{ GLT::asset::load_error::not_found };

        GLT::vfs::create_directories(abs_to.parent_path(), error);
        if (error)
            return std::unexpected{ GLT::asset::load_error::out_of_memory };

        GLT::vfs::rename(abs_from, abs_to, error);
        if (error)
            return std::unexpected{ GLT::asset::load_error::not_found };

        // ---- in-memory bookkeeping, under the lock ---------------------------------------------------------------

        std::unique_lock lock(m_mutex);

        const std::string from_str = from_rel.generic_string();
        const std::string to_str = to_rel.generic_string();
        const std::string from_prefix = from_str + "/";
        const std::string to_prefix = to_str + "/";

        // Maps [from_str] -> [to_str], and any [<from_str>/...] -> [<to_str>/...]. Returns empty for unaffected paths
        auto rewrite = [&](std::string_view old) -> std::string {
            if (old == from_str)
                return to_str;
            if (old.size() > from_prefix.size() &&
                old.compare(0, from_prefix.size(), from_prefix) == 0)
                return to_prefix + std::string(old.substr(from_prefix.size()));
            return {};
        };

        // m_by_path + slot paths. Can't mutate map keys in place, so collect first
        {
            std::vector<std::pair<std::string, GLT::asset::handle>> hits;
            for (const auto& [key, h] : m_by_path)
                if (!rewrite(key).empty())
                    hits.emplace_back(key, h);

            for (const auto& [old_key, h] : hits) {
                slot* s = slot_for(h);
                if (!s) { m_by_path.erase(old_key); continue; }

                const std::string new_key = rewrite(old_key);
                m_by_path.erase(old_key);

                const std::filesystem::path new_path{ new_key };
                s->canonical_path          = new_path;
                s->asset_info.virtual_path = new_path;
                m_by_path.emplace(new_key, h);
            }
        }

        // m_source_index. BOTH the key (source path) and the value's output can be affected: a dropped .fbx inside the renamed dir
        // moves its key, while the .glt_* it produced moves its output
        {
            struct pending { std::string old_key, new_key; source_record rec; };
            std::vector<pending> todo;
            todo.reserve(4);

            for (const auto& [src_key, rec] : m_source_index) {
                std::string new_key = rewrite(src_key);
                std::string new_out = rewrite(rec.output.generic_string());

                const bool key_moved = !new_key.empty();
                const bool out_moved = !new_out.empty();
                if (!key_moved && !out_moved)
                    continue;

                if (!key_moved)
                    new_key = src_key;

                if (!out_moved)
                    new_out = rec.output.generic_string();

                source_record updated = rec;
                updated.output = std::filesystem::path{ std::move(new_out) };
                todo.push_back(pending{ src_key, std::move(new_key), std::move(updated) });
            }

            for (auto& p : todo) {
                m_source_index.erase(p.old_key);
                m_source_index.emplace(std::move(p.new_key), std::move(p.rec));
            }
        }

        return {};
    }

    // persistence -----------------------------------------------------------------------------------------------------

    std::expected<GLT::asset::handle, GLT::asset::load_error>plugin::register_runtime(GLT::unique_ref<GLT::asset::i_runtime_asset> asset,
        const std::filesystem::path& path, std::string_view name, GLT::UUID id) {

        if (!asset)
            return std::unexpected{ GLT::asset::load_error::out_of_memory };

        auto relative = GLT::project::to_content_relative(path);
        if (relative.empty())
            return std::unexpected{ GLT::asset::load_error::not_found };

        std::unique_lock lock(m_mutex);

        const std::string key = relative.generic_string();
        if (m_by_path.contains(key))
            return std::unexpected{ GLT::asset::load_error::already_exists };

        // The asset must have a handler - that's how save() will serialize it,
        // and how a future load() would reconstruct it.
        const GLT::asset::type asset_type = asset->type();
        auto hit = m_handlers.find(asset_type);
        if (hit == m_handlers.end())
            return std::unexpected{ GLT::asset::load_error::no_handler };

        if (id == 0)
            id = GLT::UUID{};

        if (m_by_id.contains(id))
            return std::unexpected{ GLT::asset::load_error::already_exists };

        const GLT::asset::handle handle = acquire_slot();
        GLT::asset::registry_default::slot* slot = slot_for(handle);
        if (!slot)
            return std::unexpected{ GLT::asset::load_error::out_of_memory };

        slot->handler = hit->second;
        slot->canonical_path = path;
        // slot->chunks_storage stays empty - no disk chunks yet.

        GLT::asset::info& ai = slot->asset_info;
        ai.id = id;
        ai.asset_type = asset_type;
        ai.hash = 0;                                                // save() recomputes
        ai.flag_bits = GLT::asset::flags::none;
        ai.format_version = GLT::asset::header::CURRENT_VERSION;
        ai.engine_version = GLT::asset::header::CURRENT_VERSION;
        ai.virtual_path = path;
        ai.source_path = std::filesystem::path{};                                        // caller can set later
        ai.name = name.empty() ? path.stem().string() : std::string(name);
        ai.chunks = slot->chunks_storage;
        ai.dependencies = slot->deps_storage;
        ai.dependents = slot->dependents_storage;
        ai.last_loaded = std::chrono::system_clock::now();
        ai.bytes_resident = asset->memory_usage();

        slot->data = std::move(asset);

        m_by_path.emplace(key, handle);
        m_by_id.emplace(id, handle);

        return handle;
    }

    // queries ---------------------------------------------------------------------------------------------------------

    const info& plugin::info(GLT::asset::handle handle) const {

        std::shared_lock lock(m_mutex);
        if (const slot* s = slot_for(handle)) 
            return s->asset_info;

        static const GLT::asset::info empty{};
        return empty;
    }


    GLT::asset::i_runtime_asset* plugin::data(GLT::asset::handle handle) noexcept {

        std::shared_lock lock(m_mutex);
        slot* s = slot_for(handle);
        return s ? s->data.get() : nullptr;
    }


    const GLT::asset::i_runtime_asset* plugin::data(GLT::asset::handle handle) const noexcept { return const_cast<plugin*>(this)->data(handle); }

    // Handler registration --------------------------------------------------------------------------------------------

    void plugin::register_handler(GLT::asset::i_asset_handler* handler) {

        if (!handler)
            return;

        std::unique_lock lock(m_mutex);
        for (GLT::asset::type t : handler->types()) {

            LOG(info, "register_handler: [{}] -> handler {:#x}", GLT::asset::type_to_string(t), reinterpret_cast<uintptr_t>(handler));
            auto [it, inserted] = m_handlers.try_emplace(t, handler);
            VALIDATE(inserted || it->second == handler, it->second = handler, "", "type {} already has a handler - overriding", t.value);
        }
    }


    void plugin::unregister_handler(GLT::asset::i_asset_handler* handler) noexcept {

        if (!handler)
            return;
        std::unique_lock lock(m_mutex);
        LOG(info, "unregister_handler: handler {:#x}", reinterpret_cast<uintptr_t>(handler));

        // Drop any loaded assets owned by this handler before it goes away.
        for (auto& s : m_slots) {
            if (s.alive && s.handler == handler) {
                s.data.reset();
                s.alive   = false;
                s.handler = nullptr;
            }
        }

        for (auto it = m_handlers.begin(); it != m_handlers.end(); ) {
            if (it->second == handler)
                it = m_handlers.erase(it);
            else
                ++it;
        }
    }


    std::span<const GLT::asset::type> plugin::registered_types() const {

        std::shared_lock lock(m_mutex);
        m_registered_types_cache.clear();
        m_registered_types_cache.reserve(m_handlers.size());
        for (auto& [t, _] : m_handlers) 
            m_registered_types_cache.push_back(t);

        return m_registered_types_cache;
    }

    // factory registration ----------------------------------------------------------------------------------------

    void plugin::register_factory(GLT::asset::factory::i_asset_factory_plugin* factory) {

        if (!factory)
            return;

        std::unique_lock lock(m_mutex);
        if (!std::contains(m_factories, factory))
            m_factories.push_back(factory);
    }


    void plugin::unregister_factory(GLT::asset::factory::i_asset_factory_plugin* factory) noexcept {

        if (!factory)
            return;

        std::unique_lock lock(m_mutex);
        std::erase(m_factories, factory);
    }

    // import (editor / build-time) --------------------------------------------------------------------------------

    std::expected<GLT::asset::handle, GLT::asset::import_error> plugin::import(const std::filesystem::path& source,
        GLT::asset::type target_type, const std::filesystem::path& out_path, const GLT::asset::import_options& opts) {

        // Source is typically absolute (drag-drop from OS). We never store it absolute, but factories expect the real path for reading
        const std::filesystem::path abs_source = source.is_absolute() ? source : PROJECT_CONTENT_DIR / source;

        // factory selection -------------------------------------------------------------------------------------------

        GLT::asset::factory::i_asset_factory_plugin* factory = nullptr;         // pick a factory (under shared lock)
        {
            std::shared_lock lock(m_mutex);
            const std::string ext_full = source.extension().string();
            std::string ext = (ext_full.size() > 1) ? ext_full.substr(1) : ext_full;
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
            for (auto* f : m_factories) {
                if (f->can_sniff(source)) { 

                    factory = f; 
                    break; 
                }
                for (const auto& b : f->bindings()) {

                    if (b.target_type != target_type)
                        continue;

                    if (b.source_extension.empty() || b.source_extension == ext) {
                        factory = f;
                        break;
                    }
                }
                if (factory) 
                    break;
            }
        }
        if (!factory)
            return std::unexpected{ GLT::asset::import_error::not_supported };

        asset_writer_impl writer;                       // run the factory (NO lock held - this is the parallel part)
        auto import_res = factory->import(abs_source, target_type, opts, writer);
        if (!import_res)
            return std::unexpected{ import_res.error() };

        GLT::asset::factory::import_result& result = *import_res;

        // resolve output (project-relative) ---------------------------------------------------------------------------

        std::filesystem::path out_rel;
        if (is_valid_file(out_path)) {

            out_rel = GLT::project::to_content_relative(out_path);
            if (out_rel.empty())
                return std::unexpected{ GLT::asset::import_error::io_failure };

        } else if (is_valid_file(result.output_path)) {

            out_rel = GLT::project::to_content_relative(result.output_path);
            if (out_rel.empty())
                return std::unexpected{ GLT::asset::import_error::io_failure };

        } else {

            // default_output_path now yields a project-relative path because abs_source is under PROJECT_CONTENT_DIR.
            out_rel = GLT::project::to_content_relative(default_output_path(abs_source, target_type));
            if (out_rel.empty())
                return std::unexpected{ GLT::asset::import_error::io_failure };
        }

        // compose + write (prefix the VFS call) -----------------------------------------------------------------------

        const auto composed = compose_asset_file(result.id, target_type, result.payload_hash, out_rel, writer);
        if (!write_blob(PROJECT_CONTENT_DIR / out_rel, composed.bytes))
            return std::unexpected{ GLT::asset::import_error::io_failure };

        // source index keyed by project-relative source ---------------------------------------------------------------

        std::filesystem::path source_rel = GLT::project::to_content_relative(abs_source);

        // If source lives outside the content dir, keep it absolute - it's an external file the watcher needs to monitor,
        // and it can never be loaded back as an asset anyway.

        {
            std::unique_lock lock(m_mutex);
            m_source_index[source_rel.generic_string()] = source_record{
                .id = result.id,
                .output = out_rel,
                .source_hash = result.source_hash,
                .payload_hash = result.payload_hash,
            };
        }

        auto handle_res = load(out_rel);            // load() re-normalizes; harmless
        VALIDATE(handle_res,
            return std::unexpected{ GLT::asset::import_error::handler_rejected }, "",
            "import: finalize ok but load failed for [{}]", out_rel.generic_string());

        return *handle_res;
    }


    std::span<const GLT::asset::factory::binding>
    plugin::candidate_imports(const std::filesystem::path& source) const {

        thread_local std::vector<GLT::asset::factory::binding> scratch;
        scratch.clear();

        const std::string ext_full = source.extension().string();
        std::string ext = (ext_full.size() > 1) ? ext_full.substr(1) : ext_full;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

        std::shared_lock lock(m_mutex);
        for (auto* f : m_factories) {

            const bool sniff = f->can_sniff(source);
            for (auto& b : f->bindings()) {

                if (sniff || b.source_extension.empty() || b.source_extension == ext) {

                    // Make a local copy so we can stamp the backpointer without
                    // mutating the factory's own bindings array.
                    GLT::asset::factory::binding copy = b;
                    copy.factory = f;
                    scratch.push_back(copy);
                }
            }
        }
        return scratch;
    }

    // type registry ---------------------------------------------------------------------------------------------------

    GLT::asset::type plugin::reserve_type(std::string_view name) {

        std::unique_lock lock(m_mutex);

        // Already registered? Return existing.
        if (auto it = m_type_name_to_id.find(std::string(name)); it != m_type_name_to_id.end())
            return it->second;

        VALIDATE(m_next_custom_type < 0xFFFF'FFFFu, return core_types::invalid, "", " custom type id space exhausted");

        const GLT::asset::type t{ m_next_custom_type++ };
        m_type_names.push_back(std::string(name));
        m_type_name_to_id.emplace(std::string(name), t);
        return t;
    }


    std::optional<GLT::asset::type> plugin::type_from_name(std::string_view name) const {

        std::shared_lock lock(m_mutex);
        auto it = m_type_name_to_id.find(std::string(name));
        if (it == m_type_name_to_id.end())
            return std::nullopt;
        return it->second;
    }


    std::string_view plugin::name_from_type(GLT::asset::type t) const {

        std::shared_lock lock(m_mutex);
        if (t.value >= m_type_names.size()) return {};
        return m_type_names[t.value];
    }

    // dependency graph ------------------------------------------------------------------------------------------------

    void plugin::add_dependency(GLT::asset::handle from, GLT::asset::handle to) {

        std::unique_lock lock(m_mutex);
        slot* a = slot_for(from);
        slot* b = slot_for(to);
        if (!a || !b) return;

        if (std::find(a->deps_storage.begin(), a->deps_storage.end(), to) == a->deps_storage.end())
            a->deps_storage.push_back(to);
        if (std::find(b->dependents_storage.begin(), b->dependents_storage.end(), from) == b->dependents_storage.end())
            b->dependents_storage.push_back(from);

        a->asset_info.dependencies = a->deps_storage;
        b->asset_info.dependents   = b->dependents_storage;
    }


    std::span<const GLT::asset::handle> plugin::dependencies(GLT::asset::handle h) const {

        std::shared_lock lock(m_mutex);
        if (const slot* s = slot_for(h)) return s->deps_storage;
        return {};
    }

    // async -----------------------------------------------------------------------------------------------------------

    std::future<std::expected<GLT::asset::handle, GLT::asset::load_error>> plugin::load_async(std::filesystem::path path) {

        // Straightforward approach: run load() on the thread pool. If you have a
        // per-frame streaming budget you'd want a task queue instead, but this is
        // the correct starting point.
        return std::async(std::launch::async,
            [this, path = std::move(path)]() {
                return load(path);
            });
    }

    // manage asset lifetime -------------------------------------------------------------------------------------------

    void plugin::retain(GLT::asset::handle h) {

        std::unique_lock lock(m_mutex);
        if (slot* s = slot_for(h))
            ++s->ref_count;
    }


    void plugin::release(GLT::asset::handle h) {

        if (h == INVALID_HANDLE)
            return;

        std::vector<GLT::asset::handle> deps_to_release;
        GLT::unique_ref<GLT::asset::i_runtime_asset> data_to_destroy;

        {
            std::unique_lock lock(m_mutex);

            slot* s = slot_for(h);
            if (!s)
                return;

            if (s->ref_count == 0) {
                LOG(warn, "release() on asset with ref_count == 0");
                return;
            }

            --s->ref_count;
            if (s->ref_count > 0)
                return;

            // Last reference is gone. Pull data out so it is destroyed after unlock
            deps_to_release = s->deps_storage;
            data_to_destroy = std::move(s->data);

            // Remove from indexes and free slot
            m_by_path.erase(s->canonical_path.generic_string());
            m_by_id.erase(s->asset_info.id);
            release_slot(h);
        }

        // Destroying data may trigger asset_ref destructors
        data_to_destroy.reset();

        // Releasing dependencies may cascade unloads
        for (GLT::asset::handle dep : deps_to_release)
            release(dep);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    slot* plugin::slot_for(GLT::asset::handle h) noexcept {

        if (h == INVALID_HANDLE) 
            return nullptr;
    
        const u32 idx = handle_index(h);
        const u32 gen = handle_generation(h);
        if (idx >= m_slots.size()) 
            return nullptr;
        
        GLT::asset::registry_default::slot& slot = m_slots[idx];
        if (!slot.alive || slot.generation != gen) 
            return nullptr;

        return &slot;
    }


    const slot* plugin::slot_for(GLT::asset::handle h) const noexcept { return const_cast<plugin*>(this)->slot_for(h); }


    GLT::asset::handle plugin::acquire_slot() {

        u32 idx;
        if (!m_free_indices.empty()) {

            idx = m_free_indices.back();
            m_free_indices.pop_back();
        
        } else {

            idx = static_cast<u32>(m_slots.size());
            m_slots.emplace_back();
        }

        GLT::asset::registry_default::slot& slot = m_slots[idx];
        slot = GLT::asset::registry_default::slot{};                   // reset (bumps generation the first time)
        slot.alive = true;
        return make_handle(idx, slot.generation);
    }


    void plugin::on_save_event(GLT::save_event& event) {

        // "Save as" is a single-asset, dialog-driven request handled by the editor. The registry only cares about the bulk engine-save path
        if (event.is_forced_save_as())
            return;

        // Snapshot handles under the shared lock. save_as() re-acquires m_mutex on its own, so we must release before dispatching
        // otherwise the first worker would deadlock against us the moment it calls into the registry
        std::vector<GLT::asset::handle> handles;
        {
            std::shared_lock lock(m_mutex);
            LOG(info, "on_save_event: m_handlers.size()={}, static_mesh present={}",
                m_handlers.size(), m_handlers.contains(GLT::asset::core_types::static_mesh));

            for (u32 index = 0; index < (u32)m_slots.size(); ++index) {
                const slot& s = m_slots[index];

                const auto asset_type = info(make_handle(index, s.generation)).asset_type;

                LOG(info, "slot[{}] alive[{}] type[{}] handler={:#x} data={}", 
                    index, s.alive, GLT::asset::type_to_string(asset_type),
                    reinterpret_cast<uintptr_t>(s.handler), (const void*)s.data.get());
            }

            handles.reserve(m_slots.size());
            for (u32 index = 0; index < static_cast<u32>(m_slots.size()); ++index) {
                const slot& slot = m_slots[index];
                if (slot.alive)
                    handles.push_back(make_handle(index, slot.generation));
            }
        }

        VALIDATE(!handles.empty(), return, "queueing {} asset(s) for background save", "nothing to save", handles.size())

        // save_as() already keeps the heavy work (serialize + compose + write) outside the lock, so per-asset tasks actually parallelize
        // The write-back section serializes briefly, which is fine
        for (GLT::asset::handle handle : handles) {
            GLT::thread_pool::push([this, handle]() {
                if (auto res = save(handle); !res) {
                    LOG(warn, "asset [{}] failed to persist [{}]", info(handle).name, GLT::util::enum_to_string(res.error()));
                }
            });
        }
    }


    void plugin::release_slot(GLT::asset::handle h) noexcept {

        slot* s = slot_for(h);
        if (!s)
            return;

        s->data.reset();
        s->alive = false;
        s->handler = nullptr;
        s->generation++;            // invalidate any outstanding handles
        m_free_indices.push_back(handle_index(h));
    }


    std::expected<GLT::asset::handle, GLT::asset::load_error> plugin::load_unlocked(const std::filesystem::path& path,
        std::unordered_set<std::string>& in_flight) {

        const std::string key = path.generic_string();

        if (auto it = m_by_path.find(key); it != m_by_path.end())   return it->second;

        if (!in_flight.insert(key).second)                          return std::unexpected{ GLT::asset::load_error::cyclic_dependency };

        struct flight_guard {
            std::unordered_set<std::string>& set;
            std::string key;
            ~flight_guard() { set.erase(key); }
        } guard{ in_flight, key };

        auto bytes_res = read_file(PROJECT_CONTENT_DIR / path);
        if (!bytes_res)                                             return std::unexpected{ bytes_res.error() };

        std::vector<std::byte>& bytes = *bytes_res;
        if (bytes.size() < sizeof(header))                          return std::unexpected{ GLT::asset::load_error::corrupt_header };

        header hdr{};
        std::memcpy(&hdr, bytes.data(), sizeof(header));
        if (hdr.magic != header::MAGIC)                             return std::unexpected{ GLT::asset::load_error::corrupt_header };
        if (hdr.format_version > header::CURRENT_VERSION)           return std::unexpected{ GLT::asset::load_error::unsupported_version };

        auto read_cstr = [&](u64 off) -> std::string {
            if (off >= bytes.size())
                return {};
            const char* p = reinterpret_cast<const char*>(bytes.data() + off);
            const char* e = reinterpret_cast<const char*>(bytes.data() + bytes.size());
            const char* z = std::find(p, e, '\0');
            return std::string(p, z);
        };

        const std::string asset_name = path.filename().replace_extension("");
        const std::string source_path = read_cstr(hdr.source_path_offset);

        // --- dependency table (dependency_disk[]) ---
        //
        // Each row carries the target asset's UUID AND (optionally) a NUL-terminated virtual path in the string table. 
        // Resolution order:
        //   1. id already resident  → reuse the existing handle
        //   2. path_offset != 0     → load_unlocked(path.parent_path() / path)
        //   3. otherwise            → INVALID_HANDLE (unresolved slot)
        //
        // We keep a slot for every row even when the dep can't be resolved, because the handler indexes 
        // dependencies[submesh.material_slot] and relies on positional alignment.
        std::vector<GLT::asset::handle> resolved_deps;
        std::vector<UUID> resolved_dep_ids;
        resolved_deps.reserve(hdr.dependency_count);
        resolved_dep_ids.reserve(hdr.dependency_count);

        if (hdr.dependency_count > 0) {

            const u64 need = sizeof(GLT::asset::dependency_disk) * hdr.dependency_count;
            if (hdr.dependency_table_offset + need > bytes.size())  return std::unexpected{ GLT::asset::load_error::corrupt_header };

            std::vector<GLT::asset::dependency_disk> dep_disk(hdr.dependency_count);
            std::memcpy(dep_disk.data(), bytes.data() + hdr.dependency_table_offset, need);

            for (const GLT::asset::dependency_disk& dep : dep_disk) {

                resolved_dep_ids.push_back(dep.id);        // ← always, even if resolution fails

                // fast path - already resident by id
                if (auto it = m_by_id.find(dep.id); it != m_by_id.end()) {
                    resolved_deps.push_back(it->second);
                    continue;
                }

                // slow path - resolve by path relative to the importing asset
                if (dep.path_offset == 0) {
                    LOG(warn, "[{}] dependency id={:#x} has no path and is not resident", key, static_cast<unsigned long long>(dep.id));
                    resolved_deps.push_back(INVALID_HANDLE);
                    continue;
                }

                const std::string dep_path = read_cstr(dep.path_offset);
                if (dep_path.empty()) {
                    LOG(warn, "[{}] dependency id={:#x} has an empty path", key, static_cast<unsigned long long>(dep.id));
                    resolved_deps.push_back(INVALID_HANDLE);
                    continue;
                }

                // recurse
                const std::filesystem::path abs_dep = path.parent_path() / dep_path;

                auto child = load_unlocked(abs_dep, in_flight);
                if (!child) {
                    LOG(warn, "[{}] dependency [{}] failed to load (error={})", key, dep_path, static_cast<int>(child.error()));
                    resolved_deps.push_back(INVALID_HANDLE);
                    continue;
                }
                resolved_deps.push_back(*child);
            }
        }

        // chunk table ------------------------------------------------------------------------------
        std::vector<chunk_entry> chunks(hdr.chunk_count);
        if (hdr.chunk_count > 0) {

            const u64 need = sizeof(chunk_entry) * hdr.chunk_count;
            if (hdr.chunk_table_offset + need > bytes.size())       return std::unexpected{ GLT::asset::load_error::corrupt_header };

            std::memcpy(chunks.data(), bytes.data() + hdr.chunk_table_offset, need);
        }

        // handler lookup ---------------------------------------------------------------------------
        GLT::asset::i_asset_handler* handler = nullptr;
        if (auto it = m_handlers.find(GLT::asset::type{ hdr.asset_type }); it != m_handlers.end())
            handler = it->second;

        if (!handler)                                               return std::unexpected{ GLT::asset::load_error::no_handler };

        // claim slot + populate info ---------------------------------------------------------------
        const GLT::asset::handle handle = acquire_slot();
        GLT::asset::registry_default::slot* slot = slot_for(handle);
        if (!slot)                                                  return std::unexpected{ GLT::asset::load_error::out_of_memory };

        slot->handler = handler;
        slot->canonical_path = path;
        slot->chunks_storage = std::move(chunks);
        slot->deps_storage = std::move(resolved_deps);
        slot->dep_ids_storage  = std::move(resolved_dep_ids); 

        GLT::asset::info& asset_info = slot->asset_info;
        asset_info.id = hdr.id;
        asset_info.asset_type = hdr.asset_type;
        asset_info.hash = hdr.hash;
        asset_info.flag_bits = static_cast<GLT::asset::flags>(hdr.flags);
        asset_info.format_version = hdr.format_version;
        asset_info.engine_version = hdr.min_engine_version;
        asset_info.virtual_path = path;
        asset_info.source_path = source_path;
        asset_info.name = asset_name;
        asset_info.chunks = slot->chunks_storage;
        asset_info.dependencies = slot->deps_storage;
        asset_info.dependency_ids = slot->dep_ids_storage;
        asset_info.dependents = slot->dependents_storage;
        asset_info.last_loaded = std::chrono::system_clock::now();

        // handler decodes --------------------------------------------------------------------------
        chunk_reader_impl reader{ std::span<const std::byte>(bytes), slot->chunks_storage };
        auto decoded = handler->deserialize(asset_info, reader);
        if (!decoded) {

            release_slot(handle);
            return std::unexpected{ decoded.error() };
        }
        slot->data = std::move(*decoded);
        asset_info.bytes_resident = slot->data ? slot->data->memory_usage() : 0;

        // commit -----------------------------------------------------------------------------------

        for (GLT::asset::handle dep : resolved_deps)                // retain all dependencies
            if (dep != INVALID_HANDLE)
                retain_unlocked(dep);

        m_by_path.emplace(key, handle);
        m_by_id.emplace(hdr.id, handle);

        for (GLT::asset::handle dep : slot->deps_storage) {
            if (GLT::asset::registry_default::slot* dep_slot = slot_for(dep)) {
                dep_slot->dependents_storage.push_back(handle);
                dep_slot->asset_info.dependents = dep_slot->dependents_storage;
            }
        }

        return handle;
    }


    std::expected<std::vector<std::byte>, GLT::asset::load_error> plugin::read_file(const std::filesystem::path& path) const {

        std::error_code error{};
        std::vector<std::byte> file_content;
        
        handle opend_file = GLT::vfs::open_file(path, GLT::vfs::file_open_mode::read, error);
        VALIDATE(!error && opend_file != 0, return std::unexpected{ GLT::asset::load_error::not_found }, "", 
            "Failed to open file [{}]", path.generic_string())

        const u64 file_size = GLT::vfs::file_size(path, error);
        VALIDATE(!error, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "", 
            "Failed to get file size [{}]", path.generic_string())
        
        file_content.resize(file_size);
        const u64 read_size = GLT::vfs::read_file(opend_file, file_content.data(), file_size, 0);
        VALIDATE(read_size > 0, return std::unexpected{ GLT::asset::load_error::corrupt_header }, "", 
            "Failed to read file content")

        return file_content;
    }


    void plugin::retain_unlocked(GLT::asset::handle h) {

        if (GLT::asset::registry_default::slot* slot = slot_for(h))
            ++slot->ref_count;
    }

}
