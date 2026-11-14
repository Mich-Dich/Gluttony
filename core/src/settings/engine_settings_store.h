#pragma once

#include "reflection/registry.h"

#include <charconv>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::engine_settings {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Where a value comes from. A value is looked up top-down: user -> project -> the default in the code
    enum class scope : u8 {

        project = 0,                            // the project's values: travel with the project, ship with a packaged game
        user,                                   // one person's changes on top of that (this computer / the player)
        count,
    };


    // One group of settings: a reflected struct that lives in the core or in a plugin. The owner keeps reading its
    // struct as before; the store writes the stored values into it and calls [on_changed] when it did
    struct block_desc {

        std::string                             name;           // stable id, used in the files ("renderer.vk_rasterizer.visual")
        std::string                             group;          // heading in the settings window ("Rendering")
        const GLT::reflect::type_descriptor*    type = nullptr; // members: bool, integers and floats are stored and shown
        void*                                   live = nullptr; // the instance the owner reads. Must outlive the registration
        std::function<void()>                   on_changed{};   // optional: called after the store changed [live]
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // True for the members the store keeps (and the settings window shows)
    [[nodiscard]] inline bool is_setting(const GLT::reflect::member_descriptor& member);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // The engine settings in memory: the registered blocks plus what the two files hold. No file access in here
    // (engine_settings.cpp does that), so it can be tested on its own.
    //
    // A file only holds the values that were changed at its layer. Everything else falls through to the layer below, so
    // a new default in the code reaches every project that did not touch the value. Blocks and keys nobody registered
    // (a plugin that is not loaded right now) are kept as they are.
    class store {
    public:

        using value_map = std::map<std::string, std::string, std::less<>>;                  // member name -> value as text
        using block_map = std::map<std::string, value_map, std::less<>>;                    // block name -> its values

        struct block {

            block_desc                          desc{};
            std::vector<std::byte>              defaults{};     // copy of [live] as it was when the block was registered
        };

        // Registers a block and writes the stored values into desc.live (no on_changed call: the owner is just setting
        // itself up). False if the description is unusable or the name is taken
        bool add(block_desc desc);

        void remove(std::string_view name);

        [[nodiscard]] std::span<const block> blocks() const                                 { return m_blocks; }
        [[nodiscard]] const block* find(std::string_view name) const;

        // ---- values ----
        // [value] points at a variable of the member's type (bool / u32 / f32 / ...)

        // The value of a member as [view] sees it. scope::project: default + project file. scope::user: what is in use
        void read(const block& owner, const GLT::reflect::member_descriptor& member, scope view, void* value) const;

        // The value [target] falls back to when it does not set the member (project: the default, user: the project value)
        void read_inherited(const block& owner, const GLT::reflect::member_descriptor& member, scope target, void* value) const;

        // Sets the member at [target]. Setting it to the inherited value removes the entry. Updates the live instance
        void write(const block& owner, const GLT::reflect::member_descriptor& member, scope target, const void* value);

        // Removes the member's entry at [target]
        void clear(const block& owner, const GLT::reflect::member_descriptor& member, scope target);

        [[nodiscard]] bool is_set(const block& owner, const GLT::reflect::member_descriptor& member, scope target) const;

        // Takes over what somebody wrote into the live instance directly (e.g. the viewport's quick settings): every
        // member that differs from the stored value becomes an entry at [target]. True if there was a difference
        bool adopt_live(const block& owner, scope target);

        // ---- files ----

        // Replaces the content of a layer with [text] (the content of its file) and updates every live instance
        void parse(scope target, std::string_view text);

        [[nodiscard]] std::string emit(scope target) const;

        [[nodiscard]] bool dirty(scope target) const                                        { return m_dirty[static_cast<size_t>(target)]; }
        void clear_dirty(scope target)                                                      { m_dirty[static_cast<size_t>(target)] = false; }

        // Forgets every stored value (another project was opened) and puts the live instances back to their defaults
        void reset();

    private:

        [[nodiscard]] const std::string* stored(const block& owner, const GLT::reflect::member_descriptor& member, scope target) const;
        void resolve(const block& owner, const GLT::reflect::member_descriptor& member, scope view, void* value) const;
        bool apply(const block& owner, bool notify);        // stored values -> live. True if live changed

        std::vector<block>                      m_blocks{};
        std::array<block_map, static_cast<size_t>(scope::count)> m_layers{};
        std::array<bool, static_cast<size_t>(scope::count)>      m_dirty{};
    };

}

#include "engine_settings_store.inl"
