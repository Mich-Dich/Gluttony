#pragma once

#include <world/entity.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using folder_id = u64;                                      // 0: no folder (top level of the outliner)

    inline constexpr folder_id                  NO_FOLDER = 0;


    struct outliner_folder {

        folder_id                               id = NO_FOLDER;
        folder_id                               parent = NO_FOLDER;
        std::string                             name{};
        bool                                    open = true;
        bool                                    hidden = false;         // hides every entity in it (and in its sub-folders)
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // How the outliner of ONE world is organised: folders (nested), which top-level entity sits in which folder, and
    // what is hidden. A tool for the person working in the editor: nothing in here is part of the world, the world
    // file does not know about it and the game never sees it. Stored next to the project's other editor data
    // (<project>/metadata/outliner/<world id>.txt), see to_text() / from_text().
    //
    // Entities are remembered by id. Ids of entities that are gone (deleted, or in a region that is streamed out) are
    // kept: an undo or the region coming back makes them valid again.
    //
    // Plain value: copy it to keep a state (undo steps do that).
    class outliner_layout {
    public:

        // ---- folders ----

        // In display order: siblings appear in the order they have in here
        [[nodiscard]] std::span<const outliner_folder> folders() const noexcept             { return m_folders; }
        [[nodiscard]] const outliner_folder* find(folder_id id) const noexcept;
        [[nodiscard]] bool has_folders() const noexcept                                     { return !m_folders.empty(); }

        // Adds a folder at the end of [parent] (NO_FOLDER: top level). The name is made unique among its siblings
        folder_id add_folder(std::string_view name, folder_id parent = NO_FOLDER);

        // Removes the folder. Its sub-folders and entities move to the folder it was in
        void remove_folder(folder_id id);

        // Empty names are refused. The name is made unique among the siblings
        bool rename_folder(folder_id id, std::string_view name);

        // Puts the folder (with everything in it) into [new_parent]. False if that is the folder itself or inside it
        bool move_folder(folder_id id, folder_id new_parent);

        void set_open(folder_id id, bool open);
        void set_folder_hidden(folder_id id, bool hidden);

        // True if [id] is [ancestor] or lies inside it
        [[nodiscard]] bool is_inside(folder_id id, folder_id ancestor) const noexcept;

        // The folder itself or one it lies in is hidden
        [[nodiscard]] bool folder_effectively_hidden(folder_id id) const noexcept;

        // The folder and every folder inside it, in display order
        void collect_subtree(folder_id id, std::vector<folder_id>& out) const;

        // ---- entities ----

        // NO_FOLDER if the entity is in none (or in one that no longer exists)
        [[nodiscard]] folder_id folder_of(GLT::world::entity_id entity) const noexcept;
        void assign(GLT::world::entity_id entity, folder_id folder);                    // NO_FOLDER: back to the top level
        [[nodiscard]] const std::unordered_map<GLT::world::entity_id, folder_id>& assignments() const noexcept { return m_entity_folder; }

        [[nodiscard]] bool entity_hidden(GLT::world::entity_id entity) const noexcept   { return m_hidden.contains(entity); }
        void set_entity_hidden(GLT::world::entity_id entity, bool hidden);
        [[nodiscard]] const std::unordered_set<GLT::world::entity_id>& hidden_entities() const noexcept { return m_hidden; }

        // Every entity that has to be hidden in the world: the ones hidden themselves plus the ones in a hidden folder.
        // (What is below such an entity in the hierarchy follows by itself, see i_world::set_hidden)
        void collect_hidden(std::unordered_set<GLT::world::entity_id>& out) const;

        // ---- state ----

        // Goes up with every change (also with ones that are not worth an undo step, like opening a folder)
        [[nodiscard]] u64 revision() const noexcept                                     { return m_revision; }
        [[nodiscard]] bool empty() const noexcept                                       { return m_folders.empty() && m_entity_folder.empty() && m_hidden.empty(); }
        void clear();

        // What an undo step compares: everything except which folders are open
        [[nodiscard]] bool same_content(const outliner_layout& other) const;

        // Text form, one item per line (names last, so they can contain spaces):
        //      version 1
        //      folder <id> <parent id> <open 0|1> <hidden 0|1> <name>
        //      entity <index> <generation> <folder id>
        //      hidden <index> <generation>
        [[nodiscard]] std::string to_text() const;

        // Replaces the content. Lines that make no sense are skipped, folders without a parent end up at the top level
        void from_text(std::string_view text);

    private:

        [[nodiscard]] outliner_folder* find_mutable(folder_id id) noexcept;
        [[nodiscard]] std::string unique_name(std::string_view wanted, folder_id parent, folder_id ignore) const;
        void touch() noexcept                                                           { ++m_revision; }

        std::vector<outliner_folder>                                    m_folders{};
        std::unordered_map<GLT::world::entity_id, folder_id>            m_entity_folder{};
        std::unordered_set<GLT::world::entity_id>                       m_hidden{};
        folder_id                                                       m_next_id = 1;
        u64                                                             m_revision = 0;
    };

}
