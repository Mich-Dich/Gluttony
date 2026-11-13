#pragma once

#include <asset/type.h>
#include <asset/header.h>
#include <application.h>
#include <config/project.h>

#include <fstream>
#include <util/io/serializer_yaml.h>



// Asset catalog of the default registry: UUID -> content-relative path of every asset file of the project, loaded or not
// (see i_asset_registry.h, "asset catalog"). Persisted in <project>/metadata/asset_catalog.yml through serializer::yaml:
//
//      asset_catalog:
//        version: 1
//        assets:
//        - id: 0000000000001111                    UUID, 16 hex digits
//          type: 4                                 asset type value
//          path: meshes/rock.glt_asset             content-relative, generic separators
//
// The file is a cache plus the memory of which path owned a UUID last time, which is what decides who keeps the UUID when
// a file was copied. Deleting it is harmless: the next scan rebuilds it from the asset headers
//
// Locking: m_catalog_build_mutex serializes scans (file IO, no other lock held while reading); m_catalog_mutex guards the
// map. Order when nested: m_mutex -> m_catalog_mutex. A scan never runs while m_mutex is held by the caller

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::registry_default {

    // CONSTANTS =======================================================================================================

    inline const std::filesystem::path                  CATALOG_FILE_NAME("asset_catalog.yml");
    constexpr const char*                               CATALOG_SECTION = "asset_catalog";
    constexpr u32                                       CATALOG_VERSION = 1;

    // A UUID that is not in the catalog triggers a rescan at most this often (the file may have been added outside the
    // engine); lookups of UUIDs that simply do not exist must not rescan the project every call
    constexpr std::chrono::seconds                      CATALOG_RESCAN_INTERVAL{ 2 };

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    namespace catalog_detail {

        struct scanned_file {

            UUID                                        id{ 0 };
            GLT::asset::type                            asset_type{};
            std::filesystem::path                       relative{};             // content-relative, generic
            std::filesystem::file_time_type             write_time{};
        };


        // Byte offset of header::id inside the file (header is trivially copyable, not standard layout: no offsetof)
        inline size_t header_id_offset() {

            const header probe{};
            return static_cast<size_t>(reinterpret_cast<const std::byte*>(&probe.id) - reinterpret_cast<const std::byte*>(&probe));
        }


        // Reads only the fixed header of an asset file
        inline std::optional<header> read_header(const std::filesystem::path& absolute) {

            std::ifstream file(absolute, std::ios::binary);
            if (!file)
                return std::nullopt;

            header hdr{};
            file.read(reinterpret_cast<char*>(&hdr), sizeof(header));
            if (file.gcount() != static_cast<std::streamsize>(sizeof(header)) || hdr.magic != header::MAGIC)
                return std::nullopt;
            return hdr;
        }


        // Overwrites the UUID in the header of [absolute] in place. The content hash covers the chunk payload only, so
        // nothing else in the file depends on the id
        inline bool write_header_id(const std::filesystem::path& absolute, const UUID id) {

            std::fstream file(absolute, std::ios::binary | std::ios::in | std::ios::out);
            if (!file)
                return false;

            header hdr{};
            file.read(reinterpret_cast<char*>(&hdr), sizeof(header));
            if (file.gcount() != static_cast<std::streamsize>(sizeof(header)) || hdr.magic != header::MAGIC)
                return false;

            const u64 value = id;
            file.seekp(static_cast<std::streamoff>(header_id_offset()));
            file.write(reinterpret_cast<const char*>(&value), sizeof(value));
            file.flush();
            return static_cast<bool>(file);
        }


        inline std::string uuid_text(const UUID id) {

            return std::format("{:016x}", static_cast<u64>(id));
        }


        // One row of the catalog file (flat record: what serializer::yaml::vector() handles)
        struct catalog_row {

            std::string                                 id{};                   // 16 hex digits
            u32                                         type = 0;
            std::string                                 path{};
        };


        inline void serialize_catalog(GLT::serializer::yaml& yaml, u32& version, std::vector<catalog_row>& rows) {

            yaml.entry("version", version)
                .vector("assets", rows, [&](GLT::serializer::yaml& row, const u64 index) {
                    row.entry("id", rows[index].id)
                        .entry("type", rows[index].type)
                        .entry("path", rows[index].path);
                });
        }


        // The persisted file: UUID -> path it had when the catalog was written last
        inline std::unordered_map<UUID, std::string> read_catalog_file(const std::filesystem::path& file_path) {

            std::unordered_map<UUID, std::string> result;
            std::error_code error{};
            if (!std::filesystem::is_regular_file(file_path, error))
                return result;

            u32 version = 0;
            std::vector<catalog_row> rows;
            bool success = true;
            {
                GLT::serializer::yaml yaml(file_path, CATALOG_SECTION, GLT::serializer::option::load, &success);
                serialize_catalog(yaml, version, rows);
            }
            if (!success || version != CATALOG_VERSION)
                return result;                              // a cache: the scan rebuilds it

            for (const catalog_row& row : rows) {
                u64 value = 0;
                const auto [end, parse_error] = std::from_chars(row.id.data(), row.id.data() + row.id.size(), value, 16);
                if (parse_error == std::errc{} && end == row.id.data() + row.id.size() && value != 0 && !row.path.empty())
                    result[UUID{ value }] = row.path;
            }
            return result;
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    std::filesystem::path plugin::find_path_by_id(GLT::UUID id) {

        if (id == 0)
            return {};

        ensure_catalog();

        // Unknown, or catalogued at a path that no longer exists: the file may have been added, moved or copied outside
        // the engine. One rescan (rate limited for UUIDs that really are unknown), then answer from the fresh catalog
        std::filesystem::path path = catalog_lookup(id);
        std::error_code error{};
        const bool missing = !path.empty() && !std::filesystem::exists(m_catalog_content_dir / path, error);
        const bool may_rescan = std::chrono::steady_clock::now() - m_catalog_scan_time >= CATALOG_RESCAN_INTERVAL;
        if (missing || (path.empty() && may_rescan)) {
            scan_catalog();
            path = catalog_lookup(id);
        }
        return path;
    }


    std::expected<GLT::asset::asset_ref, GLT::asset::load_error> plugin::load_by_id(GLT::UUID id) {

        if (id == 0)
            return std::unexpected{ GLT::asset::load_error::not_found };

        {   // loaded already (also covers runtime assets that were never saved)
            std::unique_lock lock(m_mutex);
            if (auto it = m_by_id.find(id); it != m_by_id.end() && slot_for(it->second)) {
                retain_unlocked(it->second);
                return GLT::asset::asset_ref{ it->second, GLT::asset::adopt_ref };
            }
        }

        const std::filesystem::path path = find_path_by_id(id);
        if (path.empty())
            return std::unexpected{ GLT::asset::load_error::not_found };
        return load(path);
    }


    u64 plugin::refresh_catalog() {

        scan_catalog();
        std::lock_guard lock(m_catalog_mutex);
        return m_catalog.size();
    }

    // CLASS PRIVATE ===================================================================================================

    // Builds the catalog for the current project if it has none yet, or the project changed. Never call with m_mutex held
    void plugin::ensure_catalog() {

        if (GLT::application::get().get_project_path().empty())
            return;                                         // no project open

        const std::filesystem::path content_dir = PROJECT_CONTENT_DIR.lexically_normal();
        {
            std::lock_guard lock(m_catalog_mutex);
            if (!m_catalog_content_dir.empty() && m_catalog_content_dir == content_dir)
                return;
        }
        scan_catalog();
    }


    // Reads the header of every asset file under the content directory and replaces the catalog with the result.
    // Repairs duplicate UUIDs (see i_asset_registry.h). Never call with m_mutex held
    void plugin::scan_catalog() {

        if (GLT::application::get().get_project_path().empty())
            return;                                         // no project open

        using namespace catalog_detail;
        std::lock_guard build_lock(m_catalog_build_mutex);

        const std::filesystem::path content_dir = PROJECT_CONTENT_DIR.lexically_normal();
        const std::filesystem::path catalog_file = PROJECT_METADATA_DIR / CATALOG_FILE_NAME;
        const auto start = std::chrono::steady_clock::now();

        // ---- what the last catalog said: decides who keeps a UUID that two files carry ----
        std::unordered_map<UUID, std::string> previous;
        {
            std::lock_guard lock(m_catalog_mutex);
            if (m_catalog_content_dir == content_dir)
                for (const auto& [id, entry] : m_catalog)
                    previous[id] = entry.path.generic_string();
        }
        if (previous.empty())
            previous = read_catalog_file(catalog_file);

        // ---- headers of every asset file ----
        std::vector<scanned_file> files;
        std::error_code error{};
        for (auto it = std::filesystem::recursive_directory_iterator(content_dir, std::filesystem::directory_options::skip_permission_denied, error);
            !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {

            if (!it->is_regular_file(error) || it->path().extension() != GLT::config::ASSET_EXTENTION)
                continue;

            const std::optional<header> hdr = read_header(it->path());
            if (!hdr) {
                LOG(warn, "asset catalog: [{}] has no valid asset header - skipped",
                    it->path().lexically_relative(content_dir).generic_string());
                continue;
            }

            scanned_file file{};
            file.id = hdr->id;
            file.asset_type = hdr->asset_type;
            file.relative = it->path().lexically_relative(content_dir).lexically_normal();
            std::error_code time_error{};
            file.write_time = std::filesystem::last_write_time(it->path(), time_error);
            files.push_back(std::move(file));
        }
        if (error) {
            LOG(warn, "asset catalog: scanning [{}] stopped early ({})", content_dir.generic_string(), error.message());
        }

        // ---- duplicate UUIDs: one file keeps it, every other one gets a fresh UUID written into its header ----
        std::unordered_map<UUID, std::vector<size_t>> by_id;
        for (size_t i = 0; i < files.size(); ++i)
            by_id[files[i].id].push_back(i);

        struct reminted { std::filesystem::path relative; UUID old_id, new_id; };
        std::vector<reminted> repairs;
        std::unordered_set<UUID> minted;

        for (auto& [id, indices] : by_id) {

            if (indices.size() < 2 && id != 0)
                continue;

            // Keeper: the file the last catalog had for this UUID; else the oldest (a copy is usually newer); else by path
            size_t keeper = SIZE_MAX;
            if (id != 0) {
                if (auto known = previous.find(id); known != previous.end())
                    for (size_t index : indices)
                        if (files[index].relative.generic_string() == known->second)
                            keeper = index;
                if (keeper == SIZE_MAX)
                    keeper = *std::min_element(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                        return files[a].write_time != files[b].write_time ? files[a].write_time < files[b].write_time
                                                                          : files[a].relative < files[b].relative;
                    });
            }

            for (size_t index : indices) {

                if (index == keeper)
                    continue;

                UUID fresh{};
                while (fresh == 0 || by_id.contains(fresh) || minted.contains(fresh))
                    fresh = UUID{};
                minted.insert(fresh);

                if (!write_header_id(content_dir / files[index].relative, fresh)) {
                    LOG(error, "asset catalog: [{}] shares UUID {} with [{}] and its header could not be rewritten - it stays out of the catalog",
                        files[index].relative.generic_string(), uuid_text(id),
                        keeper == SIZE_MAX ? std::string("-") : files[keeper].relative.generic_string());
                    files[index].id = 0;
                    continue;
                }

                if (id == 0) {
                    LOG(warn, "asset catalog: [{}] had no UUID - it now has {}", files[index].relative.generic_string(), uuid_text(fresh));
                } else {
                    LOG(warn, "asset catalog: [{}] had the same UUID {} as [{}] (a copy?) - it now has its own UUID {}",
                        files[index].relative.generic_string(), uuid_text(id), files[keeper].relative.generic_string(), uuid_text(fresh));
                }
                repairs.push_back({ files[index].relative, id, fresh });
                files[index].id = fresh;
            }
        }

        // A repaired file that is loaded right now: its slot and the id index follow the new UUID
        if (!repairs.empty()) {

            std::unique_lock lock(m_mutex);
            for (const reminted& repair : repairs) {

                auto it = m_by_path.find(repair.relative.generic_string());
                if (it == m_by_path.end())
                    continue;
                slot* s = slot_for(it->second);
                if (!s)
                    continue;

                if (auto by_id_it = m_by_id.find(repair.old_id); by_id_it != m_by_id.end() && by_id_it->second == it->second)
                    m_by_id.erase(by_id_it);
                s->asset_info.id = repair.new_id;
                m_by_id[repair.new_id] = it->second;
            }
        }

        // ---- publish ----
        std::unordered_map<UUID, catalog_entry> catalog;
        catalog.reserve(files.size());
        for (const scanned_file& file : files)
            if (file.id != 0)
                catalog[file.id] = catalog_entry{ file.relative, file.asset_type };

        {
            std::lock_guard lock(m_catalog_mutex);
            m_catalog = std::move(catalog);
            m_catalog_content_dir = content_dir;
            m_catalog_file = catalog_file;
            m_catalog_scan_time = std::chrono::steady_clock::now();
            m_catalog_dirty = true;
        }
        flush_catalog();

        LOG(info, "asset catalog: {} assets in [{}] ({} UUID{} repaired) in {:.1f} ms", files.size(), content_dir.generic_string(),
            repairs.size(), repairs.size() == 1 ? "" : "s",
            std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count());
    }


    // Path of [id] in the catalog as it is (no scan). Safe with m_mutex held
    std::filesystem::path plugin::catalog_lookup(const UUID id) const {

        std::lock_guard lock(m_catalog_mutex);
        if (auto it = m_catalog.find(id); it != m_catalog.end())
            return it->second.path;
        return {};
    }


    // Records that asset [id] lives at [relative] now (load, save, import). Writes the catalog file when that is news.
    // Safe with m_mutex held
    void plugin::catalog_note(const UUID id, const std::filesystem::path& relative, const GLT::asset::type asset_type) {

        if (id == 0 || relative.empty())
            return;

        {
            std::lock_guard lock(m_catalog_mutex);
            if (m_catalog_content_dir.empty())
                return;                                     // not built yet: the first scan finds the file anyway

            const std::filesystem::path normal = relative.lexically_normal();
            auto [it, inserted] = m_catalog.try_emplace(id, catalog_entry{ normal, asset_type });
            if (!inserted) {
                if (it->second.path == normal)
                    return;
                it->second = catalog_entry{ normal, asset_type };
            }
            m_catalog_dirty = true;
        }
        flush_catalog();
    }


    // Moves every catalog entry under [from] to [to] (registry move of a file or a folder). Safe with m_mutex held
    void plugin::catalog_move(const std::string& from, const std::string& to) {

        {
            std::lock_guard lock(m_catalog_mutex);
            const std::string from_prefix = from + "/";
            for (auto& [id, entry] : m_catalog) {

                const std::string path = entry.path.generic_string();
                if (path == from)
                    entry.path = to;
                else if (path.size() > from_prefix.size() && path.compare(0, from_prefix.size(), from_prefix) == 0)
                    entry.path = to + "/" + path.substr(from_prefix.size());
                else
                    continue;
                m_catalog_dirty = true;
            }
        }
        flush_catalog();
    }


    // Writes the catalog file if it changed. Uses the path captured at scan time: works after the application is gone
    void plugin::flush_catalog() {

        std::lock_guard lock(m_catalog_mutex);
        if (!m_catalog_dirty || m_catalog_file.empty())
            return;

        std::vector<catalog_detail::catalog_row> rows;
        rows.reserve(m_catalog.size());
        for (const auto& [id, entry] : m_catalog)
            rows.push_back({ catalog_detail::uuid_text(id), entry.asset_type.value, entry.path.generic_string() });
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.path < b.path; });     // stable diffs

        // The serializer keeps other sections of an existing file; this file holds only the catalog: start from empty
        std::error_code error{};
        std::filesystem::create_directories(m_catalog_file.parent_path(), error);
        std::filesystem::remove(m_catalog_file, error);
        std::filesystem::remove(std::filesystem::path(m_catalog_file).replace_extension(".txt"), error);   // first version's file

        u32 version = CATALOG_VERSION;
        bool success = true;
        {
            GLT::serializer::yaml yaml(m_catalog_file, CATALOG_SECTION, GLT::serializer::option::save, &success);
            catalog_detail::serialize_catalog(yaml, version, rows);
        }                                                       // written when the serializer goes out of scope
        if (!success) {
            LOG(warn, "asset catalog: cannot write [{}]", m_catalog_file.generic_string());
            return;
        }
        m_catalog_dirty = false;
    }

}
