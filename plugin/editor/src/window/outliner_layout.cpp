
#include "util/pch.h"
#include "outliner_layout.h"

#include <charconv>



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

    namespace {

        std::string_view trim(std::string_view text) {

            const auto is_space = [](const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
            while (!text.empty() && is_space(text.front()))
                text.remove_prefix(1);
            while (!text.empty() && is_space(text.back()))
                text.remove_suffix(1);
            return text;
        }


        // Takes the next word off the front of [line]
        std::string_view next_word(std::string_view& line) {

            line = trim(line);
            const size_t end = std::min(line.find_first_of(" \t"), line.size());
            const std::string_view word = line.substr(0, end);
            line.remove_prefix(end);
            return word;
        }


        bool next_number(std::string_view& line, u64& out) {

            const std::string_view word = next_word(line);
            if (word.empty())
                return false;
            const auto result = std::from_chars(word.data(), word.data() + word.size(), out);
            return result.ec == std::errc{} && result.ptr == word.data() + word.size();
        }
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    const outliner_folder* outliner_layout::find(const folder_id id) const noexcept {

        if (id == NO_FOLDER)
            return nullptr;
        for (const auto& folder : m_folders)
            if (folder.id == id)
                return &folder;
        return nullptr;
    }


    folder_id outliner_layout::add_folder(const std::string_view name, folder_id parent) {

        if (!find(parent))
            parent = NO_FOLDER;

        outliner_folder folder{};
        folder.id = m_next_id++;
        folder.parent = parent;
        folder.name = unique_name(trim(name).empty() ? std::string_view("New Folder") : trim(name), parent, NO_FOLDER);
        m_folders.push_back(std::move(folder));
        if (outliner_folder* above = find_mutable(parent))
            above->open = true;                                 // so the new folder can be seen
        touch();
        return m_folders.back().id;
    }


    void outliner_layout::remove_folder(const folder_id id) {

        const outliner_folder* folder = find(id);
        if (!folder)
            return;
        const folder_id parent = folder->parent;

        std::vector<folder_id> moved_up;
        for (auto& other : m_folders)
            if (other.parent == id) {
                other.parent = parent;
                moved_up.push_back(other.id);
            }
        for (auto it = m_entity_folder.begin(); it != m_entity_folder.end(); ) {
            if (it->second != id) {
                ++it;
                continue;
            }
            if (parent == NO_FOLDER)
                it = m_entity_folder.erase(it);
            else {
                it->second = parent;
                ++it;
            }
        }
        std::erase_if(m_folders, [id](const outliner_folder& item) { return item.id == id; });

        // A folder that moved up may meet one with the same name there
        for (const folder_id moved : moved_up)
            if (outliner_folder* other = find_mutable(moved))
                other->name = unique_name(other->name, parent, other->id);
        touch();
    }


    bool outliner_layout::rename_folder(const folder_id id, const std::string_view name) {

        outliner_folder* folder = find_mutable(id);
        const std::string_view wanted = trim(name);
        if (!folder || wanted.empty())
            return false;

        std::string unique = unique_name(wanted, folder->parent, id);
        if (unique == folder->name)
            return true;
        folder->name = std::move(unique);
        touch();
        return true;
    }


    bool outliner_layout::move_folder(const folder_id id, folder_id new_parent) {

        if (new_parent != NO_FOLDER && !find(new_parent))
            new_parent = NO_FOLDER;
        const outliner_folder* folder = find(id);
        if (!folder || is_inside(new_parent, id))               // into itself or into one of its own sub-folders
            return false;
        if (folder->parent == new_parent)
            return true;

        // To the end of the new parent's folders
        outliner_folder moved = *folder;
        moved.parent = new_parent;
        std::erase_if(m_folders, [id](const outliner_folder& item) { return item.id == id; });
        moved.name = unique_name(moved.name, new_parent, id);
        m_folders.push_back(std::move(moved));
        if (outliner_folder* above = find_mutable(new_parent))
            above->open = true;
        touch();
        return true;
    }


    void outliner_layout::set_open(const folder_id id, const bool open) {

        if (outliner_folder* folder = find_mutable(id); folder && folder->open != open) {
            folder->open = open;
            touch();
        }
    }


    void outliner_layout::set_folder_hidden(const folder_id id, const bool hidden) {

        if (outliner_folder* folder = find_mutable(id); folder && folder->hidden != hidden) {
            folder->hidden = hidden;
            touch();
        }
    }


    bool outliner_layout::is_inside(folder_id id, const folder_id ancestor) const noexcept {

        if (ancestor == NO_FOLDER)
            return false;
        for (size_t guard = 0; id != NO_FOLDER && guard <= m_folders.size(); ++guard) {
            if (id == ancestor)
                return true;
            const outliner_folder* folder = find(id);
            id = folder ? folder->parent : NO_FOLDER;
        }
        return false;
    }


    bool outliner_layout::folder_effectively_hidden(folder_id id) const noexcept {

        for (size_t guard = 0; id != NO_FOLDER && guard <= m_folders.size(); ++guard) {
            const outliner_folder* folder = find(id);
            if (!folder)
                return false;
            if (folder->hidden)
                return true;
            id = folder->parent;
        }
        return false;
    }


    void outliner_layout::collect_subtree(const folder_id id, std::vector<folder_id>& out) const {

        if (!find(id) || std::find(out.begin(), out.end(), id) != out.end())
            return;
        out.push_back(id);
        for (const auto& folder : m_folders)
            if (folder.parent == id)
                collect_subtree(folder.id, out);
    }


    folder_id outliner_layout::folder_of(const GLT::world::entity_id entity) const noexcept {

        if (m_entity_folder.empty())
            return NO_FOLDER;
        const auto it = m_entity_folder.find(entity);
        return (it != m_entity_folder.end() && find(it->second)) ? it->second : NO_FOLDER;
    }


    void outliner_layout::assign(const GLT::world::entity_id entity, folder_id folder) {

        if (!entity.is_valid())
            return;
        if (!find(folder))
            folder = NO_FOLDER;

        const auto it = m_entity_folder.find(entity);
        if (folder == NO_FOLDER) {
            if (it == m_entity_folder.end())
                return;
            m_entity_folder.erase(it);
        } else {
            if (it != m_entity_folder.end() && it->second == folder)
                return;
            m_entity_folder[entity] = folder;
        }
        touch();
    }


    void outliner_layout::set_entity_hidden(const GLT::world::entity_id entity, const bool hidden) {

        if (!entity.is_valid())
            return;
        const bool changed = hidden ? m_hidden.insert(entity).second : m_hidden.erase(entity) != 0;
        if (changed)
            touch();
    }


    void outliner_layout::collect_hidden(std::unordered_set<GLT::world::entity_id>& out) const {

        out.insert(m_hidden.begin(), m_hidden.end());

        // Folders: decide once per folder, not once per entity
        std::unordered_map<folder_id, bool> folder_hidden;
        bool any = false;
        for (const auto& folder : m_folders)
            any |= (folder_hidden[folder.id] = folder_effectively_hidden(folder.id));
        if (!any)
            return;
        for (const auto& [entity, folder] : m_entity_folder)
            if (const auto it = folder_hidden.find(folder); it != folder_hidden.end() && it->second)
                out.insert(entity);
    }


    void outliner_layout::clear() {

        m_folders.clear();
        m_entity_folder.clear();
        m_hidden.clear();
        m_next_id = 1;
        touch();
    }


    bool outliner_layout::same_content(const outliner_layout& other) const {

        if (m_folders.size() != other.m_folders.size() || m_entity_folder != other.m_entity_folder || m_hidden != other.m_hidden)
            return false;
        for (size_t x = 0; x < m_folders.size(); ++x) {
            const auto& a = m_folders[x];
            const auto& b = other.m_folders[x];
            if (a.id != b.id || a.parent != b.parent || a.name != b.name || a.hidden != b.hidden)
                return false;
        }
        return true;
    }


    std::string outliner_layout::to_text() const {

        std::string out = "version 1\n";
        for (const auto& folder : m_folders)
            out += std::format("folder {} {} {} {} {}\n", folder.id, folder.parent, folder.open ? 1 : 0, folder.hidden ? 1 : 0, folder.name);

        // Sorted: the file does not change when nothing changed (hash map order is not stable)
        std::vector<std::pair<GLT::world::entity_id, folder_id>> entities(m_entity_folder.begin(), m_entity_folder.end());
        std::sort(entities.begin(), entities.end());
        for (const auto& [entity, folder] : entities)
            if (find(folder))
                out += std::format("entity {} {} {}\n", entity.index, entity.generation, folder);

        std::vector<GLT::world::entity_id> hidden(m_hidden.begin(), m_hidden.end());
        std::sort(hidden.begin(), hidden.end());
        for (const auto entity : hidden)
            out += std::format("hidden {} {}\n", entity.index, entity.generation);
        return out;
    }


    void outliner_layout::from_text(const std::string_view text) {

        m_folders.clear();
        m_entity_folder.clear();
        m_hidden.clear();
        m_next_id = 1;

        size_t position = 0;
        while (position < text.size()) {

            const size_t line_end = std::min(text.find('\n', position), text.size());
            std::string_view line = text.substr(position, line_end - position);
            position = line_end + 1;

            const std::string_view kind = next_word(line);
            if (kind == "folder") {

                u64 id = 0, parent = 0, open = 1, hidden = 0;
                if (!next_number(line, id) || !next_number(line, parent) || !next_number(line, open) || !next_number(line, hidden))
                    continue;
                const std::string_view name = trim(line);
                if (id == NO_FOLDER || name.empty() || find(id))
                    continue;
                m_folders.push_back({ id, parent, std::string(name), open != 0, hidden != 0 });
                m_next_id = std::max(m_next_id, id + 1);

            } else if (kind == "entity") {

                u64 index = 0, generation = 0, folder = 0;
                if (!next_number(line, index) || !next_number(line, generation) || !next_number(line, folder))
                    continue;
                if (generation == 0 || index > UINT32_MAX || generation > UINT32_MAX || folder == NO_FOLDER)
                    continue;
                m_entity_folder[{ static_cast<u32>(index), static_cast<u32>(generation) }] = folder;

            } else if (kind == "hidden") {

                u64 index = 0, generation = 0;
                if (!next_number(line, index) || !next_number(line, generation))
                    continue;
                if (generation == 0 || index > UINT32_MAX || generation > UINT32_MAX)
                    continue;
                m_hidden.insert({ static_cast<u32>(index), static_cast<u32>(generation) });
            }
        }

        // A hand-edited file: parents that do not exist, folders that contain each other
        for (auto& folder : m_folders)
            if (folder.parent == folder.id || !find(folder.parent))
                folder.parent = NO_FOLDER;
        for (auto& folder : m_folders) {
            folder_id walk = folder.parent;
            for (size_t steps = 0; walk != NO_FOLDER; ++steps) {
                if (walk == folder.id || steps > m_folders.size()) {
                    folder.parent = NO_FOLDER;                  // a loop: cut it here
                    break;
                }
                const outliner_folder* above = find(walk);
                walk = above ? above->parent : NO_FOLDER;
            }
        }
        std::erase_if(m_entity_folder, [this](const auto& item) { return !find(item.second); });
        touch();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    outliner_folder* outliner_layout::find_mutable(const folder_id id) noexcept {

        return const_cast<outliner_folder*>(find(id));
    }


    std::string outliner_layout::unique_name(const std::string_view wanted, const folder_id parent, const folder_id ignore) const {

        const auto taken = [&](const std::string_view name) {
            for (const auto& folder : m_folders)
                if (folder.parent == parent && folder.id != ignore && folder.name == name)
                    return true;
            return false;
        };

        std::string name(wanted);
        for (u32 number = 2; taken(name); ++number)
            name = std::format("{} {}", wanted, number);
        return name;
    }

}
