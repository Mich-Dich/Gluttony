#pragma once

#include "asset/type.h"
#include "asset/header.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::registry_default {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

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
    std::vector<std::byte> compose_asset_file(const UUID& id, GLT::asset::type asset_type, GLT::asset::content_hash payload_hash, 
        const std::filesystem::path& source_path, const asset_writer_impl& writer);

    // <source_dir>/<source_stem>.<ext_for_type>
    std::filesystem::path default_output_path(const std::filesystem::path& source, GLT::asset::type target);


    FORCE_INLINE_R bool is_valid_file(const std::filesystem::path& p);
    
    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool write_blob(const std::filesystem::path& path, std::span<const std::byte> data) {

        std::error_code ec;
        GLT::vfs::create_directories(path.parent_path(), ec);
        if (ec)
            return false;

        const auto mode = GLT::vfs::file_open_mode::write | GLT::vfs::file_open_mode::truncate | GLT::vfs::file_open_mode::create;
        const auto h = GLT::vfs::open_file(path, mode, ec);
        if (ec || h == ::INVALID_HANDLE) 
            return false;

        const size_t written = GLT::vfs::write_file(h, data.data(), data.size(), 0);
        GLT::vfs::close_file(h);
        return written == data.size();
    }


    std::vector<std::byte> compose_asset_file(const UUID& id, GLT::asset::type asset_type, GLT::asset::content_hash payload_hash, 
        const std::filesystem::path& source_path, const asset_writer_impl& writer) {

        using GLT::asset::header;
        using GLT::asset::chunk_entry;
        using GLT::asset::dependency_disk;

        // ---- string table ----
        std::string strtab;
        auto push_string = [&strtab](std::string_view s) -> u64 {

            const u64 off = strtab.size();
            strtab.append(s);
            strtab.push_back('\0');
            return off;
        };

        const u64 name_off   = push_string(writer.name());
        const u64 source_off = push_string(source_path.generic_string());

        const auto& deps = writer.deps();
        std::vector<u64> dep_path_offsets(deps.size(), 0);
        for (size_t i = 0; i < deps.size(); ++i) {
            if (deps[i].by_path && !deps[i].virtual_path.empty())
                dep_path_offsets[i] = push_string(deps[i].virtual_path);
        }

        // ---- tables (as raw byte blobs, so we can memcpy them out later) ----
        std::vector<dependency_disk> dep_table(deps.size());
        for (size_t i = 0; i < deps.size(); ++i) {
            dep_table[i].id          = deps[i].id;
            dep_table[i].path_offset = dep_path_offsets[i];
            dep_table[i].target_type = deps[i].target_type.value;
        }

        const auto& chunks = writer.chunks();
        std::vector<chunk_entry> chunk_table(chunks.size());

        // ---- compute layout ----
        // Align each section start to 8 bytes.
        auto align8 = [](u64 x) { return (x + 7u) & ~u64(7u); };

        const u64 header_off   = 0;
        const u64 strtab_off   = align8(header_off + sizeof(header));
        const u64 deptab_off   = align8(strtab_off + strtab.size());
        const u64 chunktab_off = align8(deptab_off + dep_table.size() * sizeof(dependency_disk));
        const u64 chunkdata_off = align8(chunktab_off + chunk_table.size() * sizeof(chunk_entry));

        // Lay out chunk data; each chunk is 8-aligned (padding between them if needed).
        std::vector<u64> chunk_offsets(chunks.size());
        u64 cursor = chunkdata_off;
        for (size_t i = 0; i < chunks.size(); ++i) {
            cursor = align8(cursor);
            chunk_offsets[i] = cursor;
            chunk_table[i].id          = chunks[i].id;
            chunk_table[i].compression = chunks[i].compression;
            chunk_table[i].offset      = cursor;
            chunk_table[i].size_on_disk = chunks[i].bytes.size();
            chunk_table[i].size_decoded = chunks[i].bytes.size();    // no compression yet
            cursor += chunks[i].bytes.size();
        }
        const u64 total_size = align8(cursor);

        // ---- header ----
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

        // ---- emit ----
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

        return out;
    }


    std::filesystem::path default_output_path(const std::filesystem::path& source, GLT::asset::type target) {

        std::filesystem::path p = source;
        p.replace_extension(std::string(".") + std::string(GLT::asset::extension_for_type(target)));
        return p;
    }


    FORCE_INLINE_R bool is_valid_file(const std::filesystem::path& p) {

        if (p.empty())
            return false;

        std::error_code error{};
        return GLT::vfs::is_regular_file(p, error) && !error;
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
        define(map,                 "map");
        define(audio,               "audio");
        define(static_mesh,         "static_mesh");
        define(procedural_mesh,     "procedural_mesh");
        define(dynamic_mesh,        "dynamic_mesh");
        define(skeletal_mesh,       "skeletal_mesh");
        define(mesh_collection,     "mesh_collection");
        define(texture2D,           "texture2D");
        define(texture3D,           "texture3D");
        define(cube_map,            "cube_map");
        define(material,            "material");
        define(material_instance,   "material_instance");
        define(anim,                "anim");
        define(light,               "light");
        define(bvh,                 "bvh");
        define(volume,              "volume");

        LOG_LOADED
    }


    void plugin::on_unload() {

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

        std::unique_lock lock(m_mutex);
        std::unordered_set<std::string> in_flight;
        return load_unlocked(path, in_flight);
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


    bool plugin::is_loaded(GLT::asset::handle h) const {

        std::shared_lock lock(m_mutex);
        return slot_for(h) != nullptr;
    }


    GLT::asset::handle plugin::find(const std::filesystem::path& p) const {

        std::shared_lock lock(m_mutex);
        auto it = m_by_path.find(p.generic_string());
        if (it == m_by_path.end()) 
            return INVALID_HANDLE;
        return it->second;
    }

    // queries ---------------------------------------------------------------------------------------------------------

    const info& plugin::info(GLT::asset::handle h) const {

        std::shared_lock lock(m_mutex);
        if (const slot* s = slot_for(h)) 
            return s->asset_info;

        static const GLT::asset::info empty{};
        return empty;
    }


    GLT::asset::i_runtime_asset* plugin::data(GLT::asset::handle h) noexcept {

        std::shared_lock lock(m_mutex);
        slot* s = slot_for(h);
        return s ? s->data.get() : nullptr;
    }


    const GLT::asset::i_runtime_asset* plugin::data(GLT::asset::handle h) const noexcept { return const_cast<plugin*>(this)->data(h); }

    // Handler registration --------------------------------------------------------------------------------------------

    void plugin::register_handler(GLT::asset::i_asset_handler* handler) {

        if (!handler) 
            return;
            
        std::unique_lock lock(m_mutex);
        for (GLT::asset::type t : handler->types()) {

            auto [it, inserted] = m_handlers.try_emplace(t, handler);
            VALIDATE(inserted || it->second == handler, it->second = handler, "", 
                "type {} already has a handler — overriding", t.value);
        }
    }


    void plugin::unregister_handler(GLT::asset::i_asset_handler* handler) noexcept {

        if (!handler) 
            return;
            
        std::unique_lock lock(m_mutex);
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

    // Routes to whichever factory binds (source_extension, target_type).
    // If out_path is empty, the registry derives one next to the source (or under a configured import root). 
    // On success the imported asset is loaded and its handle returned — the editor basically always wants a preview right away.
    std::expected<GLT::asset::handle, GLT::asset::import_error>
    plugin::import(const std::filesystem::path& source, GLT::asset::type target_type, const std::filesystem::path& out_dir,
        const GLT::asset::import_options& opts) {


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

        asset_writer_impl writer;                                               // run the factory (NO lock held — this is the parallel part)
        auto import_res = factory->import(source, target_type, opts, writer);
        if (!import_res)
            return std::unexpected{ import_res.error() };

        GLT::asset::factory::import_result& result = *import_res;

        const std::filesystem::path final_path =
            is_valid_file(out_path)             ? out_path :                                    // prefer the given output_path
            is_valid_file(result.output_path)   ? result.output_path :                          // use result as backup
                                                  default_output_path(source, target_type);     // default as last resort

        // compose + write the file (still no lock)
        const auto file_bytes = compose_asset_file(result.id, target_type, result.payload_hash, source, writer);
        if (!write_blob(final_path, file_bytes))
            return std::unexpected{ GLT::asset::import_error::io_failure };

        {                                                                       // remember source->asset for the watcher
            std::unique_lock lock(m_mutex);
            m_source_index[source.generic_string()] = source_record{
                .id = result.id,
                .output = final_path,
                .source_hash = result.source_hash,
                .payload_hash = result.payload_hash,
            };
        }

        auto handle_res = load(final_path);                                     // load it so the editor gets a handle back
        VALIDATE(handle_res, return std::unexpected{ GLT::asset::import_error::handler_rejected }, "", 
            "import: finalize ok but load failed for '{}'", final_path.generic_string())
        
        return *handle_res;
    }


    std::span<const GLT::asset::factory::binding>
    plugin::candidate_imports(const std::filesystem::path& source) const {

        // thread_local scratch so concurrent editor queries don't stomp each other.
        thread_local std::vector<GLT::asset::factory::binding> scratch;
        scratch.clear();

        const std::string ext_full = source.extension().string();
        std::string ext = (ext_full.size() > 1) ? ext_full.substr(1) : ext_full;
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

        std::shared_lock lock(m_mutex);
        for (auto* f : m_factories) {

            const bool sniff = f->can_sniff(source);
            for (const auto& b : f->bindings()) {

                if (sniff || b.source_extension.empty() || b.source_extension == ext)
                    scratch.push_back(b);
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
        if (it == m_type_name_to_id.end()) return std::nullopt;
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

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    slot* plugin::slot_for(GLT::asset::handle h) noexcept {

        if (h == INVALID_HANDLE) 
            return nullptr;
    
        const u32 idx = handle_index(h);
        const u32 gen = handle_generation(h);
        if (idx >= m_slots.size()) 
            return nullptr;
        
        slot& s = m_slots[idx];
        if (!s.alive || s.generation != gen) 
            return nullptr;

        return &s;
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

        GLT::asset::registry_default::slot& s = m_slots[idx];
        s = GLT::asset::registry_default::slot{};                   // reset (bumps generation the first time)
        s.alive = true;
        return make_handle(idx, s.generation);
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

        if (auto it = m_by_path.find(key); it != m_by_path.end())                   // Already resident?
            return it->second;

        if (!in_flight.insert(key).second)                                          // Cycle guard.
            return std::unexpected{ GLT::asset::load_error::cyclic_dependency };

        struct flight_guard {
            std::unordered_set<std::string>& set;
            std::string key;
            ~flight_guard() { set.erase(key); }
        } guard{ in_flight, key };

        // --- read the raw file ---
        auto bytes_res = read_file(path);
        if (!bytes_res)
            return std::unexpected{ bytes_res.error() };

        std::vector<std::byte>& bytes = *bytes_res;
        if (bytes.size() < sizeof(header))
            return std::unexpected{ GLT::asset::load_error::corrupt_header };

        // --- header ---
        header hdr{};
        std::memcpy(&hdr, bytes.data(), sizeof(header));

        if (hdr.magic != header::MAGIC)
            return std::unexpected{ GLT::asset::load_error::corrupt_header };

        if (hdr.format_version > header::CURRENT_VERSION)
            return std::unexpected{ GLT::asset::load_error::unsupported_version };

        // --- string table (name + source path, NUL-terminated blobs) ---
        auto read_cstr = [&](u64 off) -> std::string {
            if (off >= bytes.size())
                return {};

            const char* p = reinterpret_cast<const char*>(bytes.data() + off);
            const char* e = reinterpret_cast<const char*>(bytes.data() + bytes.size());
            const char* z = std::find(p, e, '\0');
            return std::string(p, z);
        };
        const std::string asset_name   = read_cstr(hdr.name_offset);
        const std::string source_path  = read_cstr(hdr.source_path_offset);

        // --- dependency table (dependency_disk[]) ---
        //
        // Each row carries the target asset's UUID AND (optionally) a
        // NUL-terminated virtual path in the string table. Resolution order:
        //   1. id already resident  → reuse the existing handle
        //   2. path_offset != 0     → load_unlocked(path.parent_path() / path)
        //   3. otherwise            → INVALID_HANDLE (unresolved slot)
        //
        // We keep a slot for every row even when the dep can't be resolved,
        // because the handler indexes dependencies[submesh.material_slot] and
        // relies on positional alignment.
        std::vector<GLT::asset::handle> resolved_deps;
        resolved_deps.reserve(hdr.dependency_count);

        if (hdr.dependency_count > 0) {

            const u64 need = sizeof(GLT::asset::dependency_disk) * hdr.dependency_count;
            if (hdr.dependency_table_offset + need > bytes.size())
                return std::unexpected{ GLT::asset::load_error::corrupt_header };

            std::vector<GLT::asset::dependency_disk> dep_disk(hdr.dependency_count);
            std::memcpy(dep_disk.data(), bytes.data() + hdr.dependency_table_offset, need);

            for (const GLT::asset::dependency_disk& d : dep_disk) {

                // fast path — already resident by id
                if (auto it = m_by_id.find(d.id); it != m_by_id.end()) {
                    resolved_deps.push_back(it->second);
                    continue;
                }

                // slow path — resolve by path relative to the importing asset
                if (d.path_offset == 0) {
                    resolved_deps.push_back(INVALID_HANDLE);
                    continue;
                }

                const std::string dep_path = read_cstr(d.path_offset);
                if (dep_path.empty()) {
                    resolved_deps.push_back(INVALID_HANDLE);
                    continue;
                }

                const std::filesystem::path abs_dep = path.parent_path() / dep_path;

                auto child = load_unlocked(abs_dep, in_flight);
                if (!child) {
                    // A missing/broken dependency is not fatal — hand back an
                    // unresolved slot so the handler can substitute a fallback.
                    resolved_deps.push_back(INVALID_HANDLE);
                    continue;
                }
                resolved_deps.push_back(*child);
            }
        }

        // chunk table
        std::vector<chunk_entry> chunks(hdr.chunk_count);
        if (hdr.chunk_count > 0) {

            const u64 need = sizeof(chunk_entry) * hdr.chunk_count;
            if (hdr.chunk_table_offset + need > bytes.size())
                return std::unexpected{ GLT::asset::load_error::corrupt_header };

            std::memcpy(chunks.data(), bytes.data() + hdr.chunk_table_offset, need);
        }

        // handler lookup
        GLT::asset::i_asset_handler* handler = nullptr;
        if (auto it = m_handlers.find(GLT::asset::type{ hdr.asset_type }); it != m_handlers.end())
            handler = it->second;

        if (!handler)
            return std::unexpected{ GLT::asset::load_error::no_handler };

        // claim a slot and populate info
        const GLT::asset::handle handle = acquire_slot();
        slot* s = slot_for(handle);
        if (!s)
            return std::unexpected{ GLT::asset::load_error::out_of_memory };

        s->handler = handler;
        s->canonical_path = path;
        s->chunks_storage = std::move(chunks);
        s->deps_storage = std::move(resolved_deps);

        GLT::asset::info& ai = s->asset_info;
        ai.id = hdr.id;
        ai.asset_type = hdr.asset_type;
        ai.hash = hdr.hash;
        ai.flag_bits = static_cast<GLT::asset::flags>(hdr.flags);
        ai.format_version = hdr.format_version;
        ai.engine_version = hdr.min_engine_version;
        ai.virtual_path = path;
        ai.source_path = source_path;
        ai.name = asset_name;
        ai.chunks = s->chunks_storage;
        ai.dependencies = s->deps_storage;
        ai.dependents = s->dependents_storage;
        ai.last_loaded = std::chrono::system_clock::now();

        // handler decodes
        chunk_reader_impl reader{ std::span<const std::byte>(bytes), s->chunks_storage };
        auto decoded = handler->deserialize(ai, reader);
        if (!decoded) {

            release_slot(handle);
            return std::unexpected{ decoded.error() };
        }
        s->data = std::move(*decoded);
        ai.bytes_resident = s->data ? s->data->memory_usage() : 0;

        // commit
        m_by_path.emplace(key, handle);
        m_by_id.emplace(hdr.id, handle);

        // Wire dependency edges both ways. INVALID_HANDLE entries in
        // s->deps_storage simply don't match any live slot, so they're skipped.
        for (GLT::asset::handle dep : s->deps_storage) {
            if (slot* d = slot_for(dep)) {
                d->dependents_storage.push_back(handle);
                d->asset_info.dependents = d->dependents_storage;
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

}
