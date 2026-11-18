#pragma once

#include "procedural/construct/element.h"

// Procedural construction: hooks, policies, scopes and composition.
// A hook is a named decision point with an interface type. A policy implements that interface and is bound to a scope.
// When code asks for a hook, every policy whose scope covers the asking element is collected (most specific first)
// and the hook's composition decides how they combine. See documentation/procedural_construction_interface.md §10



namespace GLT::construct {

    // TYPES ===========================================================================================================

    enum class scope_level : u8 {       // most specific first

        element,        // one element path (user override)
        region,         // a region of one element
        attribute,      // elements carrying an attribute value
        kind,           // an element kind
        construct,      // the construct asset's own choice (beats pack defaults)
        pack,           // a pack's default (later packs in the construct's list first)
        global,         // the core default (policies of the "core" pack)
    };


    struct policy_scope {

        scope_level                                     level = scope_level::pack;
        element_path                                    path{};             // element, region
        region_selector                                 region{};           // region
        attribute_id                                    attribute{};        // attribute
        attribute_value                                 value{};            // attribute: required value (monostate: present)
        kind_id                                         kind{};             // kind
    };


    enum class composition : u8 {

        first_match,    // most specific policy only
        chain,          // most specific first, each may call the next (wrapping)
        merge,          // all matching policies combined by the hook's merge function
        all,            // all matching policies are called
    };


    // @brief A hook declaration: a struct with these members
    //
    //     struct sampling_hook {
    //         using interface = i_sampling;
    //         static constexpr std::string_view name = "core.sampling";
    //         static constexpr composition default_composition = composition::chain;
    //         // only for merge: static std::shared_ptr<const i_sampling> merge(std::span<const i_sampling* const>);
    //     };
    template<typename H>
    concept hook = requires {
        typename H::interface;
        { H::name } -> std::convertible_to<std::string_view>;
        { H::default_composition } -> std::convertible_to<composition>;
    };


    // @brief A registered policy instance
    struct policy_desc {

        std::string                                     name{};             // unique; also how constructs bind it
        std::string                                     hook{};             // H::name
        policy_scope                                    scope{};
        std::shared_ptr<const void>                     instance{};         // a H::interface implementation

        template<construct::hook H, typename P>
        requires std::derived_from<P, typename H::interface>
        [[nodiscard]] static policy_desc make(std::string name, policy_scope scope, std::shared_ptr<const P> impl) {
            policy_desc d{};
            d.name = std::move(name);
            d.hook = std::string(H::name);
            d.scope = std::move(scope);
            d.instance = std::shared_ptr<const void>(impl, static_cast<const typename H::interface*>(impl.get()));
            return d;
        }
    };


    // @brief Packs change how a hook composes for constructs that use them
    struct composition_override_desc {

        std::string                                     hook{};
        composition                                     mode = composition::first_match;
    };


    // @brief A construct binding a registered policy to a scope of its own (editor overrides, per-asset choices)
    struct policy_binding {

        std::string                                     policy{};           // policy_desc::name
        std::optional<policy_scope>                     scope{};            // empty: the policy's own scope, at construct level
    };


    // @brief The composed answer to a hook query
    template<hook H>
    class policy_view {
    public:

        using interface = typename H::interface;

        policy_view() = default;
        policy_view(std::vector<const interface*> matches, std::vector<policy_scope> scopes, composition mode,
                    std::shared_ptr<const interface> merged = {})
            : m_matches(std::move(matches)), m_scopes(std::move(scopes)), m_mode(mode), m_merged(std::move(merged)) {}

        // Most specific first; the global default (if any) last
        [[nodiscard]] std::span<const interface* const> matches() const noexcept { return m_matches; }
        [[nodiscard]] std::span<const policy_scope> scopes() const noexcept { return m_scopes; }
        [[nodiscard]] composition mode() const noexcept { return m_mode; }
        [[nodiscard]] bool empty() const noexcept { return m_matches.empty() && !m_merged; }

        // first_match / chain: the most specific; merge: the merged policy. Null when nothing matched
        [[nodiscard]] const interface* get() const noexcept {
            if (m_mode == composition::merge && m_merged)
                return m_merged.get();
            return m_matches.empty() ? nullptr : m_matches.front();
        }
        [[nodiscard]] const interface* operator->() const noexcept { return get(); }
        [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }

    private:

        std::vector<const interface*>                   m_matches{};
        std::vector<policy_scope>                       m_scopes{};
        composition                                     m_mode = composition::first_match;
        std::shared_ptr<const interface>                m_merged{};
    };

    // Core hooks -------------------------------------------------------------------------------------------------------

    struct propagation_hook {
        using interface = i_propagation;
        static constexpr std::string_view name = "core.propagation";
        static constexpr composition default_composition = composition::chain;
    };


    class i_seed {
    public:
        virtual ~i_seed() = default;
        [[nodiscard]] virtual u64 seed(const element_path& path, u64 construct_seed, u64 salt) const = 0;
    };

    struct seed_hook {
        using interface = i_seed;
        static constexpr std::string_view name = "core.seed";
        static constexpr composition default_composition = composition::first_match;
    };

    // FUNCTION DECLARATION ============================================================================================

    // Does [scope] cover [e] (region: [region])? Pack / construct / global scopes always match; their order is decided by
    // the caller
    [[nodiscard]] bool scope_matches(const policy_scope& scope, const element& e, const region_selector& region);

}
