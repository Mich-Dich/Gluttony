#pragma once

#include "procedural/construct/data.h"

// Procedural construction: packs and the registry of everything packs (and the core itself) register.
// See documentation/procedural_construction_interface.md §4



namespace GLT::construct {

    // TYPES ===========================================================================================================

    struct semver {

        u32                                             major = 0, minor = 1, patch = 0;

        [[nodiscard]] auto operator<=>(const semver&) const = default;
        [[nodiscard]] std::string to_string() const { return std::format("{}.{}.{}", major, minor, patch); }
    };


    struct pack_desc {

        std::string                                     name{};             // "spaceship"
        semver                                          version{};
        std::vector<std::string>                        extends{};          // packs whose content this one may replace
        std::vector<std::string>                        depends_on{};       // packs that must be active too
        std::string                                     display{};
        std::string                                     description{};
        // Optional: bring constructs made with an older version of the pack up to date (the editor runs it on every
        // construct it opens). Return true if [data] changed
        std::function<bool(construct_data& data)>       upgrade{};
        // Optional: the pack's checks (Tools > Run self test in the editor). Append a message per failure, count passes
        std::function<void(std::vector<std::string>& failures, u32& passed)> self_test{};
        // Asset type names of older versions whose files are construct assets (".glt_construct_plant" -> "construct_plant")
        std::vector<std::string>                        legacy_asset_types{};
    };


    // Editor declarations of a pack. The Construct editor builds its UI from them (no editor code in the pack) ----------

    // @brief A guide role: what stages of the pack do with guides that have it
    struct role_desc {

        std::string                                     name{};             // the role id's text: "plant.branch"
        std::string                                     display{};          // "Branch spline"
        std::string                                     description{};
        std::vector<attribute_id>                       attributes{};       // per guide rows (whole guide attributes of a schema)
        std::vector<attribute_id>                       point_attributes{}; // per control point columns of spline guides
    };


    // @brief An "add guide" action of the editor
    struct guide_template_desc {

        std::string                                     name{};             // unique: "plant.branch_spline"
        std::string                                     display{};
        std::string                                     description{};
        // The new guide, from the current data and the selected guide (null if none). The editor makes its path unique
        std::function<guide(const construct_data& data, const guide* selected)> make{};
        // Optional: runs after the guide was added (add the stage that uses it, ...)
        std::function<void(construct_data& data)>       after_add{};
    };


    // @brief A construct to start from (File > New in the editor)
    struct preset_desc {

        std::string                                     name{};             // unique: "plant.broadleaf"
        std::string                                     display{};          // "Broadleaf tree"
        std::string                                     description{};
        std::function<construct_data()>                 make{};
    };


    struct param_desc {

        std::string                                     name{};
        std::string                                     display{};
        std::string                                     tooltip{};
        attribute_value                                 default_value{};
        f32                                             min = 0.0f, max = 0.0f;
    };


    // @brief An element kind (derived kinds of packs; the core declares the guide kinds)
    struct kind_desc {

        std::string                                     name{};             // "ship.rib"
        shape_kind                                      shape = shape_kind::none;
        std::string                                     display{};
        bool                                            realizable = true;
        u64                                             param_type = 0;     // param_block type token (0: none)
        std::vector<param_desc>                         params{};
        std::function<param_block()>                    make_default{};
    };


    // @brief A propagation mode by name ("copy" is built in; packs add "ship.armor_split", ...)
    struct propagation_mode_desc {

        std::string                                     name{};
        std::shared_ptr<const i_propagation>            impl{};
    };


    using any_desc = std::variant<tag_schema_desc, kind_desc, pass_desc, stage_desc, production_desc, realizer_desc,
                                  finisher_desc, policy_desc, composition_override_desc, propagation_mode_desc, generator_desc,
                                  role_desc, guide_template_desc, preset_desc>;

    [[nodiscard]] std::string_view desc_name(const any_desc& d) noexcept;


    // @brief One registered descriptor with where it came from
    template<typename D>
    struct registered {

        std::shared_ptr<const D>                        desc{};
        std::string                                     owner{};            // module that registered it
        std::string                                     pack{};             // pack it belongs to ("core" for the core)
        u64                                             order = 0;          // registration order (deterministic tie-break)
    };


    // @brief Immutable view of the registry. An evaluation holds one, so a pack reload never changes code mid-run
    class registry_snapshot {
    public:

        u64                                             generation = 0;
        std::vector<registered<pack_desc>>              packs{};
        std::vector<registered<tag_schema_desc>>        tag_schemas{};
        std::vector<registered<kind_desc>>              kinds{};
        std::vector<registered<pass_desc>>              passes{};
        std::vector<registered<stage_desc>>             stages{};
        std::vector<registered<production_desc>>        productions{};
        std::vector<registered<realizer_desc>>          realizers{};
        std::vector<registered<finisher_desc>>          finishers{};
        std::vector<registered<policy_desc>>            policies{};
        std::vector<registered<composition_override_desc>> compositions{};
        std::vector<registered<propagation_mode_desc>>  propagation_modes{};
        std::vector<registered<generator_desc>>         generators{};       // stage types
        std::vector<registered<role_desc>>              roles{};
        std::vector<registered<guide_template_desc>>    guide_templates{};
        std::vector<registered<preset_desc>>            presets{};

        [[nodiscard]] const pack_desc* find_pack(std::string_view name) const noexcept;
        // Does pack [a] extend [b] (directly or through other packs)?
        [[nodiscard]] bool extends(std::string_view a, std::string_view b) const;
        // Stage type by name (the most recently registered when packs register the same name)
        [[nodiscard]] const registered<generator_desc>* find_generator(std::string_view name) const noexcept;
        [[nodiscard]] const role_desc* find_role(role_id role) const noexcept;
        [[nodiscard]] const attribute_desc* find_attribute(attribute_id id) const noexcept;
    };


    // @brief Everything packs (and the core) registered. Thread-safe
    class i_registry {
    public:

        virtual ~i_registry() = default;

        virtual void add_pack(std::string_view owner, pack_desc desc) = 0;
        // Adds a descriptor to [pack]. A descriptor with the same name in the same pack is replaced
        virtual void add(std::string_view owner, std::string_view pack, any_desc desc) = 0;
        // Removes everything [owner] registered (plugin unload)
        virtual void remove_owner(std::string_view owner) = 0;

        [[nodiscard]] virtual std::shared_ptr<const registry_snapshot> snapshot() const = 0;
        // Bumped by every add / remove: cached results computed with older code are not reused
        [[nodiscard]] virtual u64 generation() const noexcept = 0;
    };


    class registry final : public i_registry {
    public:

        void add_pack(std::string_view owner, pack_desc desc) override;
        void add(std::string_view owner, std::string_view pack, any_desc desc) override;
        void remove_owner(std::string_view owner) override;
        [[nodiscard]] std::shared_ptr<const registry_snapshot> snapshot() const override;
        [[nodiscard]] u64 generation() const noexcept override;

    private:

        mutable std::mutex                              m_mutex{};
        registry_snapshot                               m_data{};
        mutable std::shared_ptr<const registry_snapshot> m_cached{};
        u64                                             m_order = 0;
    };

}
