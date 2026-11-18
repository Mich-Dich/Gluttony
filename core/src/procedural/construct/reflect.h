#pragma once

#include "procedural/construct/registry.h"

// Procedural construction: reflection-driven registration (C++26 std::meta).
//
// A pack annotates its structs and classes and registers them all with one call:
//
//     namespace my_pack {
//         struct [[=construct::tag_schema{ construct::text("ship") }]] ship_tags {
//             [[=construct::attr{ .domains = construct::attribute_domain::face, .propagation = construct::text("copy") }]]
//             i32 armor = 0;
//         };
//         struct [[=construct::kind_info{ construct::text("ship.rib"), construct::shape_kind::curve }]] rib {
//             [[=construct::param{ .display = construct::text("Depth"), .min = 0.02f, .max = 1.0f }]] f32 depth = 0.25f;
//         };
//         struct [[=construct::production_info{ construct::text("ship.frames") }]] frames final : construct::production {
//             static construct::matcher match();
//             void expand(construct::expand_context& ctx, const construct::element& hull) override;
//         };
//     }
//     construct::register_pack<^^my_pack>(registry, GLT_MODULE_NAME, { .name = "ship" });
//
// IMPORTANT: strings inside annotations must be written as construct::text("..."). A plain string literal compiles, but
// GCC 16 cannot read it back ("reflect_constant failed"): a pointer to a string literal is not a valid constant for an
// annotation. text() turns it into a static string, which is.



namespace GLT::construct {

    // FUNCTION DECLARATION ============================================================================================

    // A string usable inside an annotation
    consteval const char* text(std::string_view s) { return std::define_static_string(s); }

    // TYPES ===========================================================================================================

    // Annotations ------------------------------------------------------------------------------------------------------

    struct tag_schema       { const char* name = nullptr; };

    struct attr {
        const char*                                     display = nullptr;
        const char*                                     tooltip = nullptr;
        attribute_domain                                domains = attribute_domain::element;
        const char*                                     propagation = nullptr;
        f32                                             rate = 1.0f;
        f32                                             min = 0.0f;
        f32                                             max = 0.0f;
        bool                                            user_editable = true;
        bool                                            affects_topology = false;
    };

    struct kind_info        { const char* name = nullptr; shape_kind shape = shape_kind::none; const char* display = nullptr; bool realizable = true; };
    struct param            { const char* display = nullptr; f32 min = 0.0f; f32 max = 0.0f; const char* tooltip = nullptr; };

    // Classes: production_info needs `static matcher match()`; optional `static std::vector<kind_id> emits()`
    struct production_info  { const char* name = nullptr; const char* pass = nullptr; expand_mode mode = expand_mode::per_element;
                              i32 priority = 0; bool exclusive = false; };
    // realizer_info needs `static matcher match()`
    struct realizer_info    { const char* name = nullptr; i32 priority = 0; };
    // finisher_info: optional `static std::vector<std::string> after()` / `before()`
    struct finisher_info    { const char* name = nullptr; };
    // policy_info needs `using hook = <hook struct>;` and must derive from hook::interface. Optional `static policy_scope scope()`
    struct policy_info      { const char* name = nullptr; scope_level level = scope_level::pack; };
    // stage_info: optional `static std::vector<std::string> after()` / `before()`; wrap needs stage_wrapper
    struct stage_info       { const char* name = nullptr; stage_mode mode = stage_mode::insert; const char* target = nullptr; };

    // A member of a stage's settings struct (f32, i32, bool, enum, glm::vec2, glm::vec3, profile_curve, std::string)
    struct setting {
        const char*                                     display = nullptr;
        const char*                                     tooltip = nullptr;
        const char*                                     group = nullptr;    // card in the editor ("Shape")
        const char*                                     unit = nullptr;     // "m", "deg", "%"
        f32                                             min = 0.0f;
        f32                                             max = 0.0f;
        f32                                             speed = 0.0f;
        bool                                            advanced = false;
    };

    // A stage type: a class deriving from generator_of<Settings>. Optional `static std::vector<kind_id> emits()` and
    // `static void on_add(generator_instance&, const construct_data&)` (defaults of a stage the user adds).
    // [accepts] / [guide_roles]: '|' separated names. [color]: 0xRRGGBB (0: default)
    struct generator_info {
        const char*                                     name = nullptr;
        const char*                                     display = nullptr;
        const char*                                     description = nullptr;
        u32                                             color = 0;
        bool                                            takes_input = false;
        const char*                                     accepts = nullptr;
        const char*                                     guide_roles = nullptr;
    };

    // A pack: put it on a namespace. Everything annotated in that namespace (not in nested ones) belongs to the pack.
    // Used by register_packs_in<^^root>() (game modules call it through module::register_module). Optional functions
    // in the same namespace, found by name:
    //     void customize_pack(construct::pack_desc& desc);                    // upgrade, self_test, legacy types, ...
    //     void register_extras(construct::i_registry& registry, std::string_view owner, const std::string& pack);
    //                                                                          // roles, guide templates, presets
    // [extends] / [depends_on]: '|' separated pack names
    struct pack {
        const char*                                     name = nullptr;
        const char*                                     display = nullptr;
        const char*                                     description = nullptr;
        u32                                             major = 0;
        u32                                             minor = 1;
        u32                                             patch = 0;
        const char*                                     extends = nullptr;
        const char*                                     depends_on = nullptr;
    };

    namespace detail {

        template<typename A>
        consteval std::optional<A> find_annotation(std::meta::info owner) {
            for (const std::meta::info a : std::meta::annotations_of(owner))
                if (std::meta::remove_cvref(std::meta::type_of(a)) == ^^A)
                    return std::meta::extract<A>(a);
            return std::nullopt;
        }

        template<typename A>
        consteval bool has_annotation(std::meta::info owner) { return find_annotation<A>(owner).has_value(); }

        template<typename T>
        attribute_value to_value(const T& v) {
            if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, i32> || std::is_same_v<T, i64> || std::is_same_v<T, f32> ||
                          std::is_same_v<T, f64> || std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> ||
                          std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat> || std::is_same_v<T, name_id> ||
                          std::is_same_v<T, std::string>)
                return attribute_value(v);
            else if constexpr (std::is_same_v<T, u32>)
                return attribute_value(static_cast<i64>(v));
            else
                return attribute_value{};
        }

        consteval const char* or_default(const char* s, const char* fallback) { return s ? s : fallback; }
        consteval bool set(const char* s) { return s != nullptr; }

        // "two_sided" -> "Two sided"
        inline std::string pretty(std::string_view id) {
            std::string out(id);
            for (char& c : out)
                if (c == '_')
                    c = ' ';
            if (!out.empty())
                out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
            return out;
        }

        inline std::vector<std::string> split(std::string_view list) {
            std::vector<std::string> out;
            while (!list.empty()) {
                const size_t bar = list.find('|');
                std::string_view item = list.substr(0, bar);
                if (!item.empty())
                    out.emplace_back(item);
                if (bar == std::string_view::npos)
                    break;
                list.remove_prefix(bar + 1);
            }
            return out;
        }

        template<typename M>
        constexpr bool is_setting_type = std::is_same_v<M, f32> || std::is_same_v<M, i32> || std::is_same_v<M, bool> || std::is_enum_v<M>
                                      || std::is_same_v<M, glm::vec2> || std::is_same_v<M, glm::vec3> || std::is_same_v<M, profile_curve>
                                      || std::is_same_v<M, std::string>;

    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // @brief Attribute id of a reflected schema member: attribute_of<^^ship_tags::armor>() -> "ship.armor"
    template<std::meta::info Member>
    [[nodiscard]] attribute_id attribute_of() {
        static constexpr std::optional<tag_schema> schema = detail::find_annotation<tag_schema>(std::meta::parent_of(Member));
        static_assert(schema.has_value(), "attribute_of: the member's class needs a [[=construct::tag_schema{...}]] annotation");
        static constexpr const char* member = std::define_static_string(std::meta::identifier_of(Member));
        static const attribute_id id = intern(std::format("{}.{}", schema->name, member));
        return id;
    }


    template<typename T>
    [[nodiscard]] tag_schema_desc reflect_tag_schema() {

        static constexpr std::optional<tag_schema> schema = detail::find_annotation<tag_schema>(^^T);
        static_assert(schema.has_value() && schema->name, "reflect_tag_schema: needs [[=construct::tag_schema{ construct::text(\"name\") }]]");

        tag_schema_desc desc{};
        desc.name = schema->name;
        [[maybe_unused]] const T prototype{};
        template for (constexpr std::meta::info m : std::define_static_array(
                          std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()))) {
            static constexpr const char* member = std::define_static_string(std::meta::identifier_of(m));
            static constexpr std::optional<attr> a = detail::find_annotation<attr>(m);
            attribute_desc d{};
            d.id = intern(std::format("{}.{}", desc.name, member));
            d.display = member;
            d.default_value = detail::to_value(prototype.[:m:]);
            if constexpr (a.has_value()) {
                if constexpr (detail::set(a->display)) d.display = a->display;
                if constexpr (detail::set(a->tooltip)) d.tooltip = a->tooltip;
                d.domains = a->domains;
                if constexpr (detail::set(a->propagation)) d.propagation = intern(a->propagation);
                d.rate = a->rate;
                d.min = a->min;
                d.max = a->max;
                d.user_editable = a->user_editable;
                d.affects_topology = a->affects_topology;
            }
            desc.attributes.push_back(std::move(d));
        }
        return desc;
    }


    template<typename P>
    [[nodiscard]] kind_desc reflect_kind() {

        static constexpr std::optional<kind_info> info = detail::find_annotation<kind_info>(^^P);
        static_assert(info.has_value() && info->name, "reflect_kind: needs [[=construct::kind_info{ construct::text(\"name\"), ... }]]");

        kind_desc desc{};
        desc.name = info->name;
        desc.shape = info->shape;
        desc.display = detail::or_default(info->display, info->name);
        desc.realizable = info->realizable;
        desc.param_type = param_block::type_token<P>();
        desc.make_default = [] { return param_block::make(P{}); };
        [[maybe_unused]] const P prototype{};
        template for (constexpr std::meta::info m : std::define_static_array(
                          std::meta::nonstatic_data_members_of(^^P, std::meta::access_context::unchecked()))) {
            static constexpr const char* member = std::define_static_string(std::meta::identifier_of(m));
            static constexpr std::optional<param> p = detail::find_annotation<param>(m);
            param_desc d{};
            d.name = member;
            d.display = member;
            d.default_value = detail::to_value(prototype.[:m:]);
            if constexpr (p.has_value()) {
                if constexpr (detail::set(p->display)) d.display = p->display;
                if constexpr (detail::set(p->tooltip)) d.tooltip = p->tooltip;
                d.min = p->min;
                d.max = p->max;
            }
            desc.params.push_back(std::move(d));
        }
        return desc;
    }


    template<typename T>
    [[nodiscard]] production_desc reflect_production() {

        static constexpr std::optional<production_info> info = detail::find_annotation<production_info>(^^T);
        static_assert(info.has_value() && info->name);
        production_desc d{};
        d.name = info->name;
        d.pass = detail::or_default(info->pass, "primary");
        d.mode = info->mode;
        d.priority = info->priority;
        d.exclusive = info->exclusive;
        d.match = T::match();
        if constexpr (requires { T::emits(); })
            d.emits = T::emits();
        d.create = [] { return std::make_unique<T>(); };
        return d;
    }


    template<typename T>
    [[nodiscard]] realizer_desc reflect_realizer() {

        static constexpr std::optional<realizer_info> info = detail::find_annotation<realizer_info>(^^T);
        static_assert(info.has_value() && info->name);
        realizer_desc d{};
        d.name = info->name;
        d.priority = info->priority;
        d.match = T::match();
        d.create = [] { return std::make_unique<T>(); };
        return d;
    }


    template<typename T>
    [[nodiscard]] finisher_desc reflect_finisher() {

        static constexpr std::optional<finisher_info> info = detail::find_annotation<finisher_info>(^^T);
        static_assert(info.has_value() && info->name);
        finisher_desc d{};
        d.name = info->name;
        if constexpr (requires { T::after(); })  d.after = T::after();
        if constexpr (requires { T::before(); }) d.before = T::before();
        d.create = [] { return std::make_unique<T>(); };
        return d;
    }


    template<typename T>
    [[nodiscard]] stage_desc reflect_stage() {

        static constexpr std::optional<stage_info> info = detail::find_annotation<stage_info>(^^T);
        static_assert(info.has_value() && info->name);
        stage_desc d{};
        d.name = info->name;
        d.mode = info->mode;
        if constexpr (detail::set(info->target)) d.target = info->target;
        if constexpr (requires { T::after(); })  d.after = T::after();
        if constexpr (requires { T::before(); }) d.before = T::before();
        d.create = [] { return std::make_unique<T>(); };
        return d;
    }


    template<typename T>
    [[nodiscard]] policy_desc reflect_policy() {

        static constexpr std::optional<policy_info> info = detail::find_annotation<policy_info>(^^T);
        static_assert(info.has_value() && info->name);
        static_assert(requires { typename T::hook; }, "a policy class needs `using hook = <hook struct>;`");
        using H = typename T::hook;
        policy_scope scope{};
        scope.level = info->level;
        if constexpr (requires { T::scope(); })
            scope = T::scope();
        return policy_desc::make<H, T>(info->name, std::move(scope), std::make_shared<const T>());
    }


    // @brief The settings rows of a stage's settings struct
    template<typename S>
    [[nodiscard]] std::vector<setting_desc> reflect_settings() {

        std::vector<setting_desc> out;
        [[maybe_unused]] const S prototype{};
        template for (constexpr std::meta::info m : std::define_static_array(
                          std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unchecked()))) {
            using M = [:std::meta::type_of(m):];
            static_assert(detail::is_setting_type<M>, "stage settings: members must be f32, i32, bool, an enum, glm::vec2, glm::vec3, "
                                                      "profile_curve or std::string");
            static constexpr const char* member = std::define_static_string(std::meta::identifier_of(m));
            static constexpr std::optional<setting> a = detail::find_annotation<setting>(m);
            setting_desc d{};
            d.name = member;
            d.display = detail::pretty(member);
            if constexpr (std::is_same_v<M, f32>)                { d.kind = setting_kind::number;  d.default_value = prototype.[:m:]; }
            else if constexpr (std::is_same_v<M, i32>)           { d.kind = setting_kind::integer; d.default_value = prototype.[:m:]; }
            else if constexpr (std::is_same_v<M, bool>)          { d.kind = setting_kind::toggle;  d.default_value = prototype.[:m:]; }
            else if constexpr (std::is_enum_v<M>) {
                d.kind = setting_kind::choice;
                d.default_value = static_cast<i32>(prototype.[:m:]);
                template for (constexpr std::meta::info e : std::define_static_array(std::meta::enumerators_of(^^M)))
                    d.choices.push_back(detail::pretty(std::meta::identifier_of(e)));
            }
            else if constexpr (std::is_same_v<M, glm::vec2>)     { d.kind = setting_kind::range;   d.default_value = prototype.[:m:]; }
            else if constexpr (std::is_same_v<M, glm::vec3>)     { d.kind = setting_kind::vector;  d.default_value = prototype.[:m:]; }
            else if constexpr (std::is_same_v<M, profile_curve>) { d.kind = setting_kind::profile; d.default_value = prototype.[:m:]; }
            else                                                 { d.kind = setting_kind::text;    d.default_value = prototype.[:m:]; }
            if constexpr (a.has_value()) {
                if constexpr (detail::set(a->display)) d.display = a->display;
                if constexpr (detail::set(a->tooltip)) d.tooltip = a->tooltip;
                if constexpr (detail::set(a->group))   d.group = a->group;
                if constexpr (detail::set(a->unit))    d.unit = a->unit;
                d.min = a->min;
                d.max = a->max;
                d.speed = a->speed;
                d.advanced = a->advanced;
            }
            out.push_back(std::move(d));
        }
        return out;
    }


    // @brief A stage's settings struct filled from the instance (the type's defaults for what the user did not set)
    template<typename S>
    [[nodiscard]] S read_settings(const generator_context& ctx) {

        S out{};
        template for (constexpr std::meta::info m : std::define_static_array(
                          std::meta::nonstatic_data_members_of(^^S, std::meta::access_context::unchecked()))) {
            using M = [:std::meta::type_of(m):];
            static constexpr const char* member = std::define_static_string(std::meta::identifier_of(m));
            const setting_value& v = ctx.setting(member);
            if constexpr (std::is_same_v<M, f32>)          out.[:m:] = ctx.number(member, out.[:m:]);
            else if constexpr (std::is_same_v<M, i32>)     out.[:m:] = ctx.integer(member, out.[:m:]);
            else if constexpr (std::is_same_v<M, bool>)    out.[:m:] = ctx.toggle(member, out.[:m:]);
            else if constexpr (std::is_enum_v<M>)          out.[:m:] = static_cast<M>(ctx.integer(member, static_cast<i32>(out.[:m:])));
            else {
                if (const M* typed = std::get_if<M>(&v))
                    out.[:m:] = *typed;
            }
        }
        return out;
    }


    // @brief Base of stage types with a typed settings struct (see generator.h)
    template<typename S>
    class generator_of : public generator {
    public:
        using settings_type = S;
        virtual void generate(generator_context& ctx, const S& settings) = 0;
        void run(generator_context& ctx) final { generate(ctx, read_settings<S>(ctx)); }
    };


    template<typename T>
    [[nodiscard]] generator_desc reflect_generator() {

        static constexpr std::optional<generator_info> info = detail::find_annotation<generator_info>(^^T);
        static_assert(info.has_value() && info->name, "reflect_generator: needs [[=construct::generator_info{ .name = construct::text(\"...\") }]]");
        static_assert(requires { typename T::settings_type; }, "a stage type derives from construct::generator_of<Settings>");
        generator_desc d{};
        d.name = info->name;
        d.display = detail::or_default(info->display, info->name);
        if constexpr (detail::set(info->description)) d.description = info->description;
        if constexpr (info->color != 0)
            d.color = glm::vec3(static_cast<f32>((info->color >> 16) & 0xff), static_cast<f32>((info->color >> 8) & 0xff),
                                static_cast<f32>(info->color & 0xff)) / 255.0f;
        d.takes_input = info->takes_input;
        if constexpr (detail::set(info->accepts))     d.accepts = detail::split(info->accepts);
        if constexpr (detail::set(info->guide_roles))
            for (const std::string& r : detail::split(info->guide_roles))
                d.guide_roles.push_back(intern(r));
        if constexpr (requires { T::emits(); })
            d.emits = T::emits();
        if constexpr (requires (generator_instance& g, const construct_data& c) { T::on_add(g, c); })
            d.on_add = [](generator_instance& g, const construct_data& c) { T::on_add(g, c); };
        d.settings = reflect_settings<typename T::settings_type>();
        d.create = [] { return std::make_unique<T>(); };
        return d;
    }


    // @brief Registers everything annotated in namespace [Namespace] as pack [desc.name]
    template<std::meta::info Namespace>
    void register_pack(i_registry& registry, std::string_view owner, pack_desc desc) {

        const std::string pack = desc.name;
        registry.add_pack(owner, std::move(desc));
        template for (constexpr std::meta::info m : std::define_static_array(
                          std::meta::members_of(Namespace, std::meta::access_context::unchecked()))) {
            if constexpr (std::meta::is_type(m) && std::meta::is_class_type(m) && std::meta::is_complete_type(m)) {
                using T = [:m:];
                if constexpr (detail::has_annotation<tag_schema>(m))        registry.add(owner, pack, reflect_tag_schema<T>());
                if constexpr (detail::has_annotation<kind_info>(m))         registry.add(owner, pack, reflect_kind<T>());
                if constexpr (detail::has_annotation<production_info>(m))   registry.add(owner, pack, reflect_production<T>());
                if constexpr (detail::has_annotation<realizer_info>(m))     registry.add(owner, pack, reflect_realizer<T>());
                if constexpr (detail::has_annotation<finisher_info>(m))     registry.add(owner, pack, reflect_finisher<T>());
                if constexpr (detail::has_annotation<stage_info>(m))        registry.add(owner, pack, reflect_stage<T>());
                if constexpr (detail::has_annotation<policy_info>(m))       registry.add(owner, pack, reflect_policy<T>());
                if constexpr (detail::has_annotation<generator_info>(m))    registry.add(owner, pack, reflect_generator<T>());
            }
        }
    }



    namespace detail {

        // A function named [name] declared directly in namespace [ns], or a null reflection
        consteval std::meta::info find_function(std::meta::info ns, std::string_view name) {

            for (const std::meta::info m : std::meta::members_of(ns, std::meta::access_context::unchecked()))
                if (std::meta::is_function(m) && std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
                    return m;
            return std::meta::info{};
        }

    }


    // @brief Registers every namespace annotated with [[=construct::pack{...}]] in [Namespace] (itself included, nested
    //        namespaces too) as a pack owned by [owner]
    template<std::meta::info Namespace>
    void register_packs_in(i_registry& registry, std::string_view owner) {

        if constexpr (detail::has_annotation<pack>(Namespace)) {

            constexpr std::optional<pack> info = detail::find_annotation<pack>(Namespace);
            static_assert(detail::set(info->name), "construct::pack needs a name: construct::pack{ construct::text(\"my_pack\") }");

            pack_desc desc{};
            desc.name = info->name;
            desc.version = semver{ info->major, info->minor, info->patch };
            desc.display = detail::or_default(info->display, info->name);
            if constexpr (detail::set(info->description))   desc.description = info->description;
            if constexpr (detail::set(info->extends))       desc.extends = detail::split(info->extends);
            if constexpr (detail::set(info->depends_on))    desc.depends_on = detail::split(info->depends_on);

            constexpr std::meta::info customize = detail::find_function(Namespace, "customize_pack");
            if constexpr (customize != std::meta::info{})
                [:customize:](desc);

            const std::string name = desc.name;
            register_pack<Namespace>(registry, owner, std::move(desc));

            constexpr std::meta::info extras = detail::find_function(Namespace, "register_extras");
            if constexpr (extras != std::meta::info{})
                [:extras:](registry, owner, name);
        }

        template for (constexpr std::meta::info m : std::define_static_array(
                          std::meta::members_of(Namespace, std::meta::access_context::unchecked()))) {
            if constexpr (std::meta::is_namespace(m) && !std::meta::is_namespace_alias(m))
                register_packs_in<m>(registry, owner);
        }
    }

}
