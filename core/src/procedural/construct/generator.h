#pragma once

#include "procedural/construct/pipeline.h"

// Procedural construction: stages (generators).
//
// A stage is one step of a construct that the user sees, orders and tunes: "Trunk", "Branches", "Twigs", "Leaves",
// "Frames", "Stringers", ... Packs register stage TYPES (generator_desc); a construct holds an ordered list of stage
// INSTANCES (generator_instance: type + settings + which stages it grows on). The core runs the instances in order in
// the core.generate pipeline stage (after Interpret, before the production passes); every element a stage emits carries
// the stage's id, so editors can show what each stage created, isolate it, filter picking by it, and time it.
//
// A stage type is written as a class with a typed settings struct, registered by reflection (reflect.h):
//
//     struct branch_settings {
//         [[=construct::setting{ .display = construct::text("Count"), .group = construct::text("Distribution"), .min = 0, .max = 200 }]]
//         i32 count = 12;
//         [[=construct::setting{ .display = construct::text("Length"), .group = construct::text("Shape"), .unit = construct::text("m") }]]
//         construct::profile_curve length = construct::profile_curve::linear(1.0f, 0.3f);
//         arrangement layout = arrangement::spiral;                  // enums become choices (enumerator names)
//     };
//     struct [[=construct::generator_info{ .name = construct::text("plant.branches"), .display = construct::text("Branches"),
//                                          .takes_input = true }]] branches final : construct::generator_of<branch_settings> {
//         void generate(construct::generator_context& ctx, const branch_settings& s) override;
//     };
//
// The editor builds the stage's settings UI from the descriptors (groups become cards); the generator receives the
// settings struct filled from the instance (defaults for everything the user did not set).



namespace GLT::construct {

    // FORWARD DECLARATIONS ============================================================================================

    class evaluation;
    struct construct_data;

    // TYPES ===========================================================================================================

    // @brief A value over 0..1 (along a branch, from base to tip, ...): points (x, y) with x ascending, smoothly
    // interpolated. Packs use it for profiles: length along the parent, radius along the branch, density, ...
    struct profile_curve {

        std::vector<glm::vec2>                          points{ { 0.0f, 1.0f }, { 1.0f, 1.0f } };

        [[nodiscard]] f32 evaluate(f32 x) const noexcept;
        [[nodiscard]] f32 max_value() const noexcept;
        // Keeps points inside x 0..1, sorted, at least two, first at 0 and last at 1
        void sanitize();

        [[nodiscard]] static profile_curve constant(f32 v) { return { { { 0.0f, v }, { 1.0f, v } } }; }
        [[nodiscard]] static profile_curve linear(f32 a, f32 b) { return { { { 0.0f, a }, { 1.0f, b } } }; }
        [[nodiscard]] static profile_curve of(std::initializer_list<glm::vec2> pts) { profile_curve c{ { pts } }; c.sanitize(); return c; }

        [[nodiscard]] bool operator==(const profile_curve&) const = default;
    };


    using setting_value = std::variant<f32, i32, bool, glm::vec2, glm::vec3, profile_curve, std::string>;

    enum class setting_kind : u8 {
        number,         // f32
        integer,        // i32
        toggle,         // bool
        choice,         // i32 index into choices (enums)
        range,          // glm::vec2 (min, max)
        vector,         // glm::vec3
        profile,        // profile_curve
        text,           // std::string
    };


    // @brief One setting of a stage type (what the editor shows as one row)
    struct setting_desc {

        std::string                                     name{};             // key in generator_instance::settings
        std::string                                     display{};
        std::string                                     tooltip{};
        std::string                                     group{};            // card the row is shown in ("Shape", "Distribution")
        std::string                                     unit{};             // "m", "deg", "%" (display only)
        setting_kind                                    kind = setting_kind::number;
        setting_value                                   default_value{ 0.0f };
        f32                                             min = 0.0f, max = 0.0f;     // min == max: unbounded
        f32                                             speed = 0.0f;               // drag speed (0: derived from the range)
        std::vector<std::string>                        choices{};
        bool                                            advanced = false;   // hidden until "Show advanced"
    };


    // @brief A stage of a construct (authored data, stored in the construct asset)
    struct generator_instance {

        u32                                             id = 0;             // stable, unique in the construct (never reused)
        std::string                                     type{};             // generator_desc::name, "plant.branches"
        std::string                                     label{};            // user facing, "Branches", "Twigs"
        bool                                            enabled = true;
        std::vector<u32>                                inputs{};           // stages whose elements this one grows on
        u32                                             seed = 0;           // re-rolls this stage alone (added to the construct seed)
        std::vector<std::pair<std::string, setting_value>> settings{};      // values set by the user (others: the type's default)

        [[nodiscard]] const setting_value* find(std::string_view name) const noexcept;
        void set(std::string_view name, setting_value value);
        void reset(std::string_view name);
    };


    class generator_context;

    class generator {
    public:
        virtual ~generator() = default;
        virtual void run(generator_context& ctx) = 0;
    };


    // @brief A stage type
    struct generator_desc {

        std::string                                     name{};             // "plant.branches"
        std::string                                     display{};          // "Branches"
        std::string                                     description{};
        glm::vec3                                       color{ 0.55f, 0.75f, 1.0f };        // editor: list marker, skeleton overlay
        std::vector<setting_desc>                       settings{};
        bool                                            takes_input = false;                 // grows on other stages' elements
        std::vector<std::string>                        accepts{};          // stage types allowed as input (empty: any)
        std::vector<role_id>                            guide_roles{};      // guides it consumes (ctx.guides())
        std::vector<kind_id>                            emits{};            // empty: anything
        std::function<std::unique_ptr<generator>()>     create{};
        // Optional: the user adds a stage of this type to [data] (inputs already chosen): adjust its defaults (label,
        // settings that depend on what it grows on, ...)
        std::function<void(generator_instance& stage, const construct_data& data)> on_add{};

        [[nodiscard]] const setting_desc* find_setting(std::string_view setting) const noexcept;
    };


    // @brief What a stage sees while it runs
    class generator_context {
    public:

        generator_context(const evaluation& e, const generator_instance& instance, const generator_desc& desc,
                          std::vector<const element*> inputs, std::vector<const element*> guides);

        [[nodiscard]] const context& query() const noexcept { return m_expand.query(); }
        [[nodiscard]] const generator_instance& instance() const noexcept { return *m_instance; }
        [[nodiscard]] const generator_desc& desc() const noexcept { return *m_desc; }
        [[nodiscard]] u32 stage() const noexcept { return m_instance->id; }

        // Elements of the input stages (path order) and the guides of desc().guide_roles (path order)
        [[nodiscard]] std::span<const element* const> inputs() const noexcept { return m_inputs; }
        [[nodiscard]] std::span<const element* const> guides() const noexcept { return m_guides; }

        // Settings: the instance's value, else the type's default
        [[nodiscard]] const setting_value& setting(std::string_view name) const;
        [[nodiscard]] f32 number(std::string_view name, f32 fallback = 0.0f) const;
        [[nodiscard]] i32 integer(std::string_view name, i32 fallback = 0) const;
        [[nodiscard]] bool toggle(std::string_view name, bool fallback = false) const;

        // New elements. Segment names get the stage id ("branch@3") so two stages never collide on one parent.
        // emit(): child of [parent] (attributes inherited as in productions); emit_root(): "stage<id>/name[index]"
        element_builder emit(const element& parent, std::string_view name, i64 index, kind_id kind);
        element_builder emit_root(std::string_view name, i64 index, kind_id kind);
        void relate(relation_id kind, name_id type, element_handle a, element_handle b, attribute_set params = {});
        void diagnose(severity s, const element* on, std::string message);

        // Random numbers: from the construct seed, the stage's seed and id, and the element's path
        [[nodiscard]] rng random(const element& e, u64 salt = 0) const;
        [[nodiscard]] rng random(u64 salt = 0) const;

        // Evaluator side
        [[nodiscard]] expand_context& expansion() noexcept { return m_expand; }

    private:

        [[nodiscard]] u64 stage_salt(u64 salt) const noexcept;

        expand_context                                  m_expand;
        const generator_instance*                       m_instance;
        const generator_desc*                           m_desc;
        std::vector<const element*>                     m_inputs{};
        std::vector<const element*>                     m_guides{};
    };


    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] setting_kind kind_of(const setting_value& v) noexcept;
    [[nodiscard]] f64 setting_number(const setting_value& v) noexcept;      // numbers, ints, bools; vectors: x; else 0

    // The settings of [instance] that differ from the defaults of [desc] (others are dropped): keeps saved data small
    void prune_defaults(generator_instance& instance, const generator_desc& desc);

    // Next free stage id of a construct's stage list
    [[nodiscard]] u32 next_stage_id(std::span<const generator_instance> stages) noexcept;

}
