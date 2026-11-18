# Procedural Construction: Plugin Interface

> **Status: interface design (proposal).** Companion to [procedural_construction.md](procedural_construction.md), which
> explains the concepts (guides, elements, productions, relations, realizers, policies). This document fixes the C++
> interface the `construct` plugin offers to **construct packs**. Conventions follow the terrain SDK
> (`plugin/terrain/include/terrain/`): `GLT::` namespaces, `i_` interfaces, registries keyed by an owner string for
> unloading, shared `const` descriptors so evaluations survive a pack reload, a registry generation counter for cache
> invalidation.
>
> Signatures are sketches: names, ownership and call contracts are the decision; exact parameter lists may still move
> during implementation.

## Decisions

Every open decision is settled in favour of **giving packs the choice**. Where a choice exists, the core provides a
default and the pack (or a tag, or a single element) can pick something else, including several things at once.

| Question | Decision | Where |
|---|---|---|
| Language for pack code | **C++26 only.** No node graph. Reflection (`std::meta`) declares tag schemas, element kinds and parameters; a builder API produces the same descriptors for packs that build them at runtime | §2, §4 |
| Boolean / meshing backend | **Both**: an exact mesh backend (Manifold) and an in-house SDF backend, plus backends registered by packs. Chosen **per operation**; one result can mix both (an SDF blend zone stitched into an exact mesh) | §13 |
| Joint ownership | **Per joint**: side A, side B, both sides, a separate joint realizer, or any combination. The resolver decides, `joint_ownership_hook` can override | §7 |
| Policy composition | **Per hook and overridable**: first match, chain (wrapping), merge across scopes, or call all. A hook declares its default; packs can change it for their scope | §10 |
| Packs adding pipeline stages | **Yes**: packs insert stages, insert passes, wrap stages and replace stages (core ones included), ordered by `after` / `before` constraints | §5 |
| Final mesh topology | **Chosen by packs, several at once**: every construct produces a list of outputs, each with its own topology (as realized, shells per element, merged shells, watertight, open surface) and purpose (render, collision, navigation, export) | §9 |
| Node-graph authoring | **No.** The editor edits guides, tags, parameters and overrides, never logic | — |

Further defaults chosen the same way (maximum control for packs):

- Every built-in feature of the core (stages, productions, policies, backends, solvers, constraint types) is registered
  through the **same public registry** packs use. Nothing is private to the core, so a pack can replace any of it.
- Packs can register their own **geometry backends**, **constraint solvers**, **iterative solvers**, **field providers**,
  **space services** (routing, partitioning) and **editor tools**.
- **Kits** (reusable parts with sockets) exist both as assets and as code.

## Interface rules

1. **Pack callbacks are pure.** A production, resolver, realizer, finisher, solver or policy may depend only on its
   inputs, the `context`, its parameters and its seed. No globals, no static mutable state, no wall clock. They run on
   worker threads.
2. **Paths are given, not counted.** Everything a callback emits is named by the callback (`emit("rib", station_index)`),
   so the stable path never depends on evaluation order.
3. **Descriptors are immutable and shared.** The registry hands out `std::shared_ptr<const ..._desc>`. An evaluation in
   flight keeps the code it started with while a pack is reloaded.
4. **Everything has an owner.** `add(owner, desc)` / `remove_owner(owner)`. `owner` is the registering plugin's
   `GLT_MODULE_NAME`.
5. **Fail locally.** A callback reports problems with `ctx.diagnose(...)` on the element it works on. An exception
   marks that element failed; the rest of the construct still evaluates.

## Header layout

```
plugin/construct/include/construct/
├─ types.h        ids, paths, frames, attribute values, region selectors
├─ tags.h         tag schemas: reflection annotations, attribute access
├─ element.h      element, guide, shapes, relations
├─ pack.h         pack_desc, register_pack, i_registry
├─ pipeline.h     stages, passes, stage contexts
├─ production.h   production base, matcher, expand_context
├─ resolve.h      resolver base, candidates, joints, ownership, iteration
├─ realize.h      realizer / joint realizer bases, products
├─ output.h       finishers, output descriptions, topology
├─ policy.h       hooks, scopes, composition, built-in hook list
├─ propagation.h  lineage events, propagation modes
├─ context.h      the read-only query API
├─ geometry.h     mesh, sdf, curve, patch types; kernel operations; backends
├─ surface.h      skins, panels, surface parameterization, detail operators
├─ space.h        voids, rooms, routing
├─ field.h        field providers
├─ partition.h    cells, large constructs
├─ constraint.h   constraint types and solvers
├─ solver.h       iterative operators (catenary, inflate, relax, drape)
├─ kit.h          kits, parts, sockets
├─ editor.h       pack editor extensions (optional, editor builds)
└─ construct.h    i_construct: the plugin as seen by packs; evaluation API
```

## 1. Basics (`types.h`)

```cpp
namespace GLT::construct {

    // Interned names. Stable across sessions (the string is stored), cheap to compare (the id)
    struct name_id { u32 value = 0; [[nodiscard]] std::string_view str() const; };
    [[nodiscard]] name_id intern(std::string_view name);              // thread-safe

    using kind_id      = name_id;      // "ship.rib", "plant.branch", "guide.volume"
    using role_id      = name_id;      // "envelope", "skeleton", "leaf", "cutter", ...
    using attribute_id = name_id;      // "ship.armor", "plant.vigor"
    using relation_id  = name_id;      // "core.child_of", "core.joint", "ship.weld"
    using pass_id      = name_id;      // "primary", "secondary", "detail", pack-defined

    // @brief Stable identity of an element: the path of what created it. "hull/frames/station[12]/rib"
    //
    // Seeds, caches, overrides and LOD selection key on it. Segments are (name, index); the index is chosen by the
    // emitting code, never by evaluation order
    struct path_segment { name_id name; i64 index = -1; };     // -1: no index

    class element_path {
    public:
        [[nodiscard]] element_path child(std::string_view name, i64 index = -1) const;
        [[nodiscard]] std::span<const path_segment> segments() const noexcept;
        [[nodiscard]] u64 hash() const noexcept;                         // also the base of the element's seed
        [[nodiscard]] std::string to_string() const;                     // "hull/frames/station[12]/rib"
        [[nodiscard]] static std::expected<element_path, parse_error> parse(std::string_view text);
        auto operator<=>(const element_path&) const = default;           // total order: deterministic iteration
    };

    // Per-evaluation handle (index into the evaluation's element table). NOT stable: store paths, not handles
    struct element_handle { u32 index = ~0u; [[nodiscard]] bool valid() const noexcept { return index != ~0u; } };

    // @brief A placement in the world: f64 origin, f32 rotation and scale. Geometry inside an element is f32, relative to it
    struct frame {
        glm::dvec3      origin{ 0.0 };
        glm::quat       rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3       scale{ 1.0f };
    };

    // @brief Attribute values. Tags are attributes too (bool or enum typed)
    using attribute_value = std::variant<bool, i32, i64, f32, f64, glm::vec2, glm::vec3, glm::vec4, glm::quat,
                                         name_id /*enum value*/, std::string, asset::asset_ref>;

    // @brief Which part of an element something applies to
    struct whole_element {};
    struct face_set      { std::vector<u32> faces; };                    // volume / surface guides: face indices of the realized cage
    struct face_group    { name_id group; };                            // named face group (survives edits through lineage)
    struct curve_span    { f64 t0 = 0.0, t1 = 1.0; };                   // curve parameter span
    struct patch_region  { glm::dvec2 uv0{ 0.0 }, uv1{ 1.0 }; };        // surface parameter rectangle
    struct point_index   { u32 index = 0; };                            // a control point / a point of a point set
    using region_selector = std::variant<whole_element, face_set, face_group, curve_span, patch_region, point_index>;

    // @brief Problems reported by pack code (shown on the element in the editor, logged in headless builds)
    enum class severity : u8 { info, warning, error };
}
```

## 2. Tag schemas (`tags.h`)

A pack declares its attributes as annotated structs. Reflection walks them at registration: every non-static data
member becomes an attribute `<schema>.<member>` with its type, its default (the member initializer), the element regions
it may live on, and its propagation mode.

```cpp
namespace GLT::construct {

    // Where an attribute may be stored
    enum class attribute_domain : u16 {
        element  = 1 << 0,     // the element as a whole
        face     = 1 << 1,     // per face / face group
        edge     = 1 << 2,
        vertex   = 1 << 3,
        corner   = 1 << 4,     // per face corner (UVs, split normals)
        curve    = 1 << 5,     // along a curve, interpolated by parameter (radius, twist)
        point    = 1 << 6,     // per point of a point set / control point
    };

    // Annotations (C++26: [[=value]] on a declaration, read with std::meta)
    struct tag_schema   { std::string_view name; };                       // on the struct: attribute prefix
    struct attr {
        std::string_view    display = {};
        std::string_view    tooltip = {};
        attribute_domain    domains = attribute_domain::element;
        name_id             propagation = {};      // mode name: "copy", "interpolate", "decay", ... ({} = pack default)
        f32                 rate = 1.0f;           // parameter of rate-based modes (decay)
        f32                 min = 0.0f, max = 0.0f;   // editor range, min == max: unbounded
        bool                user_editable = true;
        bool                affects_topology = false;   // editor: changing it re-runs Expand, not only Realize
    };
    struct enumeration  { std::span<const std::string_view> values; };    // on a name_id member: closed set of values

    // Typed reference to an attribute, from a pointer to member of a schema struct.
    // Resolved once at registration (name -> attribute_id), then O(1)
    template<auto Member> struct attribute_ref { [[nodiscard]] static attribute_id id(); };

    // Runtime descriptor (what reflection produces, what builders produce by hand)
    struct attribute_desc {
        attribute_id        id;
        std::string         display, tooltip;
        attribute_value     default_value;
        attribute_domain    domains;
        name_id             propagation;
        f32                 rate, min, max;
        bool                user_editable, affects_topology;
        std::vector<name_id> enum_values;
    };
    struct tag_schema_desc { name_id name; std::vector<attribute_desc> attributes; };

    // Builder for schemas only known at runtime (data-driven packs)
    class tag_schema_builder {
    public:
        explicit tag_schema_builder(std::string_view name);
        tag_schema_builder& add(attribute_desc desc);
        [[nodiscard]] tag_schema_desc build();
    };
}
```

Example:

```cpp
namespace my_ship_pack {

    struct [[=construct::tag_schema{ "ship" }]] ship_tags {

        [[=construct::attr{ .display = "Armor", .domains = construct::attribute_domain::element | construct::attribute_domain::face,
                            .propagation = construct::intern("copy"), .min = 0, .max = 5 }]]
        i32 armor = 0;

        [[=construct::attr{ .domains = construct::attribute_domain::edge, .propagation = construct::intern("boundary"),
                            .user_editable = false }]]
        bool weld_seam = false;

        static constexpr std::string_view load_values[] = { "low", "normal", "high" };

        [[=construct::attr{ .display = "Load", .affects_topology = true }]]
        [[=construct::enumeration{ load_values }]]
        construct::name_id load = construct::intern("normal");      // "low" | "normal" | "high"
    };
}

// reading it anywhere:
const i32 armor = ctx.attribute<&my_ship_pack::ship_tags::armor>(rib, construct::face_group{ "outer" });
```

Attributes are stored in **layers** per domain (`attribute_layers`): sparse per element, dense per mesh domain once
realized. A layer only exists where a value differs from the default.

## 3. Elements, guides and relations (`element.h`)

```cpp
namespace GLT::construct {

    enum class shape_kind : u8 { point, frame, curve, surface, volume, graph };
    enum class element_origin : u8 { authored, derived };
    enum class orientation : u8 { solid, cavity };      // volumes: which side is material (G14)

    // Abstract shapes (not meshes). All in the element's frame
    struct point_shape   { std::vector<glm::vec3> points; };
    struct frame_shape   { frame local; };
    struct curve_shape   { geometry::curve curve; };                  // spline / polyline with RMF frames (§13)
    struct surface_shape { geometry::patch patch; };                  // plane, quad patch, trimmed patch, curve-bounded
    struct volume_shape  { geometry::cage cage; orientation side = orientation::solid; };   // primitive + edit stack result
    struct graph_shape   { std::vector<glm::vec3> nodes; std::vector<std::pair<u32, u32>> edges; };
    using shape_data = std::variant<point_shape, frame_shape, curve_shape, surface_shape, volume_shape, graph_shape>;

    // @brief Typed parameter block of an element kind (a reflected struct, see element kinds below)
    class param_block {
    public:
        template<typename P> [[nodiscard]] const P& as() const;      // checked against the kind's param type
        template<typename P> [[nodiscard]] P& as_mut();               // only on elements the caller is building
    };

    // @brief The unit of everything. Immutable once emitted
    struct element {
        element_path        path;
        kind_id             kind;
        shape_kind          shape;
        element_origin      origin;
        role_id             role;            // guides: authored role; derived: role the production assigns (may be empty)
        frame               placement;
        shape_data          data;
        attribute_layers    attributes;
        param_block         params;
        pass_id             pass;            // pass that produced it (guides: "authored")
        u32                 depth = 0;       // production depth (recursion)
        element_handle      parent;          // child_of shortcut
    };

    // @brief Element kinds: declared by reflection on the params struct
    struct kind_info {
        std::string_view    name;            // "ship.rib"
        shape_kind          shape;
        std::string_view    display = {};
        bool                realizable = true;   // false: structural only, never produces products itself
    };
    struct param { std::string_view display = {}; f32 min = 0.0f, max = 0.0f; std::string_view tooltip = {}; };

    // Guides: authored elements with an edit stack and modifiers
    struct edit_step;                         // transform, loop_cut, extrude, inset, assign_region, ... (geometry.h)
    struct guide_modifiers {
        std::optional<glm::vec4> mirror_plane;                       // plane in the guide's frame
        u32                      radial_count = 1;  glm::vec3 radial_axis{ 0, 1, 0 };
        u32                      linear_count = 1;  glm::vec3 linear_offset{ 0 };
    };
    struct guide {
        element             base;             // origin == authored
        std::vector<edit_step> edits;         // replayed in Interpret, every step emits lineage events
        guide_modifiers     modifiers;        // expanded into derived copies with a core.mirror_of / core.array_of relation
    };

    // @brief A typed link between two elements (or element regions)
    enum class joint_side : u8 { none = 0, a = 1, b = 2, both = 3 };

    struct contact_point { glm::dvec3 position; glm::vec3 normal; };
    struct contact_curve { geometry::curve curve; };                  // in world frame
    struct contact_patch { geometry::mesh patch; };
    using contact_geometry = std::variant<std::monostate, contact_point, contact_curve, contact_patch>;

    struct relation {
        relation_id         kind;             // core.child_of | core.constraint | core.joint | core.attach | core.cut | pack kinds
        name_id             type;             // joints: "ship.weld", "plant.fork", ...; constraints: "core.equal_length", ...
        element_handle      a, b;
        region_selector     region_a, region_b;
        contact_geometry    contact;
        param_block         params;
        joint_ownership     ownership;        // joints only (§7)
        element_path        path;             // stable: "<a path>~<type>~<b path>[i]"
    };
}
```

Example kind:

```cpp
namespace my_ship_pack {
    struct [[=construct::kind_info{ "ship.rib", construct::shape_kind::curve, "Rib" }]] rib {
        [[=construct::param{ "Web depth",    0.02f, 1.0f }]] f32 depth  = 0.25f;
        [[=construct::param{ "Flange width", 0.0f,  0.5f }]] f32 flange = 0.08f;
        [[=construct::param{ "Thickness",    0.002f, 0.1f }]] f32 thickness = 0.01f;
        [[=construct::param{ "Lightening holes" }]]          bool holes = true;
    };
}
```

## 4. Packs and registration (`pack.h`)

A pack is a custom plugin. On load it registers itself and everything it contains. Reflection does the walking; every
descriptor can also be built and added by hand.

```cpp
namespace GLT::construct {

    struct pack_desc {
        std::string                 name;                 // "spaceship"
        semver                      version;              // recorded in every construct that uses the pack
        std::vector<std::string>    extends;              // packs whose content this one may override and replace
        std::vector<std::string>    depends_on;           // packs that must be loaded too (used, not overridden)
        std::string                 display, description;
    };

    // One call per pack: walks [Namespace] for annotated tag schemas, kinds, productions, resolvers, realizers,
    // finishers, stages, passes, policies, solvers, constraint types, interface types, kits and editor extensions
    template<std::meta::info Namespace>
    void register_pack(pack_desc desc);                   // owner = GLT_MODULE_NAME of the caller

    // Kinds of descriptor the registry holds (each is a struct in its own header)
    using any_desc = std::variant<tag_schema_desc, kind_desc, role_desc, relation_type_desc, interface_type_desc,
                                  stage_desc, pass_desc, production_desc, resolver_desc, realizer_desc,
                                  joint_realizer_desc, finisher_desc, policy_desc, composition_override_desc,
                                  backend_desc, solver_desc, constraint_type_desc, constraint_solver_desc,
                                  field_provider_desc, space_service_desc, kit_desc, editor_extension_desc>;

    // @brief Everything packs (and the core itself) have registered. Thread-safe
    class i_registry {
    public:
        virtual ~i_registry() = default;

        virtual void add_pack(std::string_view owner, pack_desc desc) = 0;
        virtual void add(std::string_view owner, std::string_view pack, any_desc desc) = 0;     // adds or replaces
        virtual void remove_owner(std::string_view owner) = 0;

        template<typename D> [[nodiscard]] std::shared_ptr<const D> find(std::string_view name) const;
        template<typename D> [[nodiscard]] std::vector<std::shared_ptr<const D>> all() const;

        // Bumped by every add / remove. Cached results computed with older code are not reused
        [[nodiscard]] virtual u64 generation() const noexcept = 0;
    };
}
```

**Annotations** that `register_pack` looks for. Values that are not constants (matchers, ordering lists, emitted kinds)
come from static member functions found by reflection on the same class: `match()`, `match_a()` / `match_b()`,
`after()`, `before()`, `emits()`, `scope()`.

```cpp
namespace GLT::construct {
    struct production_info     { std::string_view name; std::string_view pass = "primary"; expand_mode mode = expand_mode::per_element;
                                 i32 priority = 0; bool exclusive = false; };
    struct resolver_info       { std::string_view name; std::string_view pass = "primary"; f64 reach = 0.0; bool global = false; };
    struct realizer_info       { std::string_view name; i32 priority = 0; };
    struct joint_realizer_info { std::string_view name; };                    // joint types from static joint_types()
    struct finisher_info       { std::string_view name; };
    struct stage_info          { std::string_view name; stage_mode mode = stage_mode::insert; std::string_view target = {}; };
    struct pass_info           { std::string_view name; std::string_view stage = "core.expand"; u32 max_depth = 64;
                                 bool lod_droppable = false; bool solve_constraints = false; };
    struct policy_info         { std::string_view hook; scope_level level = scope_level::pack; };
    struct interface_info      { std::string_view name; };
    struct solver_info         { std::string_view name; };
    struct constraint_type_info{ std::string_view name; };
    struct field_info          { std::string_view name; field_scope scope = field_scope::construct; };
    struct propagation_mode_info { std::string_view name; };
}
```

**Override rules.** A descriptor name is unique per registry. A pack may replace a descriptor only if it is the core's or
belongs to a pack it `extends`. Two packs replacing the same descriptor without one extending the other is a load error
for a construct that uses both, naming both packs. The construct selects its packs explicitly (`CHUNK_PACKS`), so
unrelated packs loaded in the editor never interfere.

**Reload.** Unload a pack = `remove_owner`. Evaluations in flight hold `shared_ptr`s and finish with the old code; the
generation bump discards their cached results. Constructs referencing a missing kind keep guides, overrides and
parameters, and show the elements as placeholders.

## 5. Pipeline: stages and passes (`pipeline.h`)

The pipeline is an ordered list of **stages**, built per construct from the stages of the core and of the construct's
packs. The six phases of the concept document are ordinary registered stages (`core.interpret`, `core.expand`,
`core.resolve`, `core.realize`, `core.finish`, `core.publish`). Packs add, wrap or replace stages and add passes.

```cpp
namespace GLT::construct {

    // What a stage may read and write. Declared so the evaluator can check order, parallelise and invalidate
    enum class channel : u16 {
        guides = 1 << 0, elements = 1 << 1, relations = 1 << 2, fields = 1 << 3,
        products = 1 << 4, outputs = 1 << 5, space = 1 << 6,
    };

    enum class stage_mode : u8 {
        insert,         // a new stage
        wrap,           // decorates an existing stage: receives it as next (run before / after / instead)
        replace,        // takes the place of an existing stage (core or a pack it extends)
    };

    struct stage_desc {
        std::string                 name;                 // "ship.mass_balance"
        stage_mode                  mode = stage_mode::insert;
        std::string                 target;               // wrap / replace: the stage acted on
        std::vector<std::string>    after, before;        // ordering constraints (insert)
        channel                     reads{}, writes{};
        std::function<std::unique_ptr<class stage>()> create;
    };

    // @brief One step of the pipeline. Runs once per evaluation (not per element); fans out work itself
    class stage {
    public:
        virtual ~stage() = default;
        virtual void run(stage_context& ctx) = 0;
    };

    // A wrapping stage gets the wrapped one
    class stage_wrapper : public stage {
    public:
        void set_next(stage& next) { m_next = &next; }
    protected:
        void run_next(stage_context& ctx) { m_next->run(ctx); }
    private:
        stage* m_next = nullptr;
    };

    // @brief What a stage sees: everything earlier stages wrote, plus write access to its declared channels
    class stage_context {
    public:
        [[nodiscard]] const context& query() const;                   // §12
        [[nodiscard]] element_table& elements();                      // writes: only if declared
        [[nodiscard]] relation_table& relations();
        [[nodiscard]] product_table& products();
        [[nodiscard]] output_table& outputs();

        // Parallel helpers on GLT::thread_pool. Results are committed in path order (deterministic)
        template<typename F> void for_each_element(const matcher& m, F&& f);
        template<typename F> void for_each_island(F&& f);            // connected groups of related elements

        // Run built-in machinery from a custom stage (a replacing stage can reuse the parts it keeps)
        void run_productions(pass_id pass);
        void run_resolvers(const iteration_settings& settings);
        void run_realizers(lod_request lod);

        [[nodiscard]] bool cancelled() const noexcept;
        void diagnose(severity s, element_handle on, std::string message);
    };

    // @brief Passes order productions inside core.expand (and resolvers inside core.resolve)
    struct pass_desc {
        std::string                 name;                 // "primary", "secondary", "detail", "ship.plating"
        std::string                 stage = "core.expand";
        std::vector<std::string>    after, before;
        u32                         max_depth = 64;       // fixpoint limit inside this pass
        bool                        lod_droppable = false;   // LOD policies may skip this pass entirely
        bool                        solve_constraints = false;   // run constraint islands of this pass's elements after it (§18)
    };
}
```

Ordering is a topological sort of all `after` / `before` constraints. A cycle, or a stage reading a channel that no
earlier stage writes, is a load error naming the stages involved.

## 6. Productions (`production.h`)

```cpp
namespace GLT::construct {

    // @brief Which elements a production (or resolver, realizer) applies to
    struct matcher {
        std::vector<kind_id>        kinds;                // empty: any
        std::vector<shape_kind>     shapes;
        std::vector<role_id>        roles;
        std::vector<std::pair<attribute_id, attribute_value>> require;   // attribute equals value
        std::vector<attribute_id>   require_present;
        std::optional<u32>          max_depth;
        std::function<bool(const element&, const context&)> predicate;   // optional, pure
    };

    enum class expand_mode : u8 {
        per_element,     // expand() once per match, in parallel
        batch,           // expand_all() once with every match of the pass (space colonization, global layouts)
    };

    struct production_desc {
        std::string                 name;
        matcher                     match;
        std::string                 pass = "primary";
        expand_mode                 mode = expand_mode::per_element;
        std::vector<kind_id>        emits;                // for validation and the editor; emitting others is an error
        i32                         priority = 0;         // several productions matching one element: higher first
        bool                        exclusive = false;    // true: lower-priority productions skip the element
        std::function<std::unique_ptr<class production>()> create;
    };

    class production {
    public:
        virtual ~production() = default;
        virtual void expand(expand_context& ctx, const element& match) {}
        virtual void expand_all(expand_context& ctx, std::span<const element* const> matches) {
            for (const element* e : matches) expand(ctx, *e);
        }
    };

    // @brief Emitting side of a production
    class expand_context {
    public:
        [[nodiscard]] const context& query() const;
        [[nodiscard]] rng random(u64 salt = 0) const;                 // seeded from the match's path (seed_hook)

        // New element as child of [parent]: path = parent.path / name[index]
        element_builder emit(const element& parent, std::string_view name, i64 index, kind_id kind);
        // New relation (constraints, attach, pack relations); joints are created by resolvers
        relation_builder relate(relation_id kind, name_id type, element_handle a, element_handle b);

        void diagnose(severity s, const element& on, std::string message);
    };

    class element_builder {      // fluent; committed when it goes out of scope
    public:
        element_builder& placement(const frame& f);
        element_builder& shape(shape_data data);
        element_builder& role(role_id r);
        template<auto Member> element_builder& set(attribute_value v, region_selector where = whole_element{});
        template<typename P> element_builder& params(P value);
        [[nodiscard]] element_handle handle() const;                  // usable for relate() in the same expand call
    };
}
```

Example: frame stations every N metres along the hull, density from the `load` tag.

```cpp
namespace my_ship_pack {

    struct [[=construct::production_info{ .name = "ship.frame_stations", .pass = "primary" }]]
    frame_stations final : construct::production {

        static construct::matcher match() { return { .kinds = { construct::intern("guide.volume") },
                                                     .roles = { construct::intern("envelope") } }; }

        void expand(construct::expand_context& ctx, const construct::element& hull) override {
            const auto sampling = ctx.query().policy<construct::sampling_hook>(hull);
            for (const construct::station& s : sampling->stations_along(hull, /*axis*/ glm::vec3(0, 0, 1)))
                ctx.emit(hull, "station", s.index, construct::intern("ship.frame_station"))
                   .placement(s.plane_frame)
                   .shape(construct::frame_shape{ s.plane_frame });
        }
    };
}
```

## 7. Resolvers and joints (`resolve.h`)

Resolvers create joints, cuts and modifications where elements meet. The core finds **candidates** (broad phase over
element bounds and Context distance queries); resolvers handle them (narrow phase).

```cpp
namespace GLT::construct {

    // @brief Who adapts to a joint, and who builds the joint's own geometry. Chosen per joint
    struct joint_ownership {
        joint_side          adapts = joint_side::both;        // which sides change their geometry (extend, notch, pad)
        joint_side          owns_contact = joint_side::a;     // which side's output carries the contact geometry (partitions, LOD)
        bool                joint_realizer = false;           // true: a joint realizer builds extra geometry (bead, bracket, gusset)
        i32                 priority = 0;                     // conflicts between joints on one region: higher wins
    };

    enum class candidate_kind : u8 { element_element, element_volume, element_guide_region };

    struct candidate {
        candidate_kind      kind;
        element_handle      a, b;
        region_selector     region_b;                         // e.g. the interface face group that was hit
        geometry::aabb      overlap;
        f64                 distance;                         // < 0: penetration depth
    };

    struct resolver_desc {
        std::string                 name;
        matcher                     a, b;                     // both sides must match (order as given)
        f64                         reach = 0.0;              // also consider pairs closer than this (metres)
        std::string                 pass = "primary";
        bool                        global = false;           // true: resolve_all() once with every candidate
        std::function<std::unique_ptr<class resolver>()> create;
    };

    class resolver {
    public:
        virtual ~resolver() = default;
        virtual void resolve(resolve_context& ctx, const candidate& c) {}
        virtual void resolve_all(resolve_context& ctx, std::span<const candidate> cs) { for (const auto& c : cs) resolve(ctx, c); }
    };

    class resolve_context {
    public:
        [[nodiscard]] const context& query() const;

        // A joint between a and b. Ownership defaults come from the interface type, then joint_ownership_hook
        relation_builder joint(name_id type, const candidate& c, contact_geometry contact);
        // Material of [target] inside [cutter] is removed at Finish (assembly boolean), across all fragments of [target]
        relation_builder cut(element_handle cutter, element_handle target);
        // A new version of an element (trimmed profile, changed params). Counts as a change for iteration
        element_builder modify(element_handle e);
        void cull(element_handle e, std::string reason);
        void diagnose(severity s, element_handle on, std::string message);
    };

    // @brief Interfaces: typed connection offers on guide regions ("weld", "bolt", "root_anchor")
    struct interface_type_desc {
        std::string                 name;
        std::vector<std::string>    connects_to;              // compatible interface or element kinds; matching may be overridden
        joint_ownership             default_ownership;
        type_info                   params;                   // reflected parameter struct
    };

    // @brief Iteration of core.resolve (negotiation, G13)
    struct iteration_settings {
        u32     max_iterations = 8;
        bool    detect_cycles = true;     // a state hash seen before ends iteration with a warning (oscillation)
        // An iteration re-runs resolvers only for islands where an element was modified or culled in the previous one.
        // Converged: no modify / cull / new joint in an iteration. Packs can replace the test (iteration_hook)
    };
}
```

**Ownership in practice.**

| Joint | `adapts` | `owns_contact` | `joint_realizer` | Result |
|---|---|---|---|---|
| Rib welded to backbone | `both` | `a` (rib) | true | rib extends, backbone gets a pad, bead built separately |
| Stringer through rib | `a` (rib) | `a` | false | rib gets a slot, stringer unchanged |
| Pipe through bulkhead | `b` (bulkhead) | `b` | true | bulkhead cut, flange collar built by the joint realizer |
| Branch fork | `both` | `a` (parent) | false | parent and child blend in one SDF zone (§13) |
| Chair leg into seat (mortise) | `b` (seat) | `b` | false | seat gets the mortise, leg gets a tenon through `adapts` on `a` |

Each side reads its joints during realization and sees the ownership (`side_view::must_adapt()`).

## 8. Realizers and products (`realize.h`)

```cpp
namespace GLT::construct {

    struct realizer_desc {
        std::string                 name;
        matcher                     match;
        i32                         priority = 0;         // first matching realizer by priority realizes the element
        std::function<std::unique_ptr<class realizer>()> create;
    };

    class realizer {
    public:
        virtual ~realizer() = default;
        virtual void realize(realize_context& ctx, const element& e) = 0;
    };

    struct joint_realizer_desc {
        std::string                 name;
        std::vector<std::string>    joint_types;          // "ship.weld"
        std::function<std::unique_ptr<class joint_realizer>()> create;
    };

    class joint_realizer {
    public:
        virtual ~joint_realizer() = default;
        virtual void realize(realize_context& ctx, const relation& joint, const element& a, const element& b) = 0;
    };

    // @brief One side's view of a joint while realizing
    class side_view {
    public:
        [[nodiscard]] const relation& joint() const;
        [[nodiscard]] const element& other() const;
        [[nodiscard]] bool must_adapt() const;            // this side is in ownership.adapts
        [[nodiscard]] bool owns_contact() const;
    };

    struct lod_request { u32 level = 0; f32 error = 0.0f; std::span<const pass_id> dropped_passes; };

    class realize_context {
    public:
        [[nodiscard]] const context& query() const;
        [[nodiscard]] geometry::kernel& geo();                       // §13, emits lineage for attribute propagation
        [[nodiscard]] std::span<const side_view> joints() const;    // joints touching the element being realized
        [[nodiscard]] std::span<const relation* const> cuts() const;
        [[nodiscard]] const lod_request& lod() const;
        [[nodiscard]] rng random(u64 salt = 0) const;

        // Products. Every product carries the element's path and a product name ("web", "flange", "bead")
        void mesh(std::string_view name, geometry::mesh fragment, material_binding materials);
        void instances(std::string_view name, asset::asset_ref mesh, std::span<const frame> placements);
        void kit_part(std::string_view name, const kit_part_ref& part, const frame& placement);   // §20
        void collider(std::string_view name, collider_shape shape);
        void marker(std::string_view name, const frame& at, attribute_layers tags);
        void decal(std::string_view name, const decal_desc& decal);
        // Nested construct in a local frame, with its own packs and policies (cargo module, flower)
        void nested(std::string_view name, const nested_construct& c, const frame& at);

        void diagnose(severity s, std::string message);
    };
}
```

## 9. Finishers and outputs (`output.h`)

Finishers post-process products across elements (assembly booleans for cuts, weld beads along all weld joints, rivets,
weathering). Outputs decide what the products **become**. A construct may have any number of outputs at once, each with
its own topology and purpose.

```cpp
namespace GLT::construct {

    struct finisher_desc {
        std::string                 name;
        std::vector<std::string>    after, before;        // order among finishers ("core.assembly_booleans" is one)
        std::function<std::unique_ptr<class finisher>()> create;
    };

    class finisher {
    public:
        virtual ~finisher() = default;
        virtual void finish(finish_context& ctx) = 0;     // reads and rewrites products; parallelises itself via ctx
    };

    enum class topology : u8 {
        as_realized,          // fragments kept as emitted (may intersect, may be open)
        shells_per_element,   // one closed or open shell per element, no merging across elements
        merged_shells,        // fragments of one group welded where they share vertices, intersections kept
        watertight,           // boolean union of the group into one closed 2-manifold (exact or SDF backend)
        open_surface,         // single-sided surfaces (leaf cards, decals, cavity walls without thickness)
    };

    enum class output_purpose : u8 { render, collision, physics_proxy, navigation, occlusion, export_only };

    // @brief One output of a construct. outputs_hook returns the list; packs and elements can add their own
    struct output_desc {
        std::string                 name;                 // "hull_exterior", "interior", "collision", "leaves"
        output_purpose              purpose = output_purpose::render;
        product_filter              include;              // by product name, element kind, tag, pass
        topology                    topo = topology::merged_shells;
        name_id                     backend = {};         // watertight: "exact", "sdf", pack backend ({} = meshing_hook)
        bool                        partitioned = true;   // split by partition cells (§17)
        bool                        lods = true;          // run lod_hook for this output
        std::optional<u32>          max_triangles;        // collision / navigation simplification target
    };
}
```

Examples of what packs choose:

- **Spaceship**: `hull_exterior` (render, `merged_shells`), `interior` (render, `shells_per_element`), `collision`
  (collision, `watertight`, `max_triangles = 20 000`), `navigation` (navigation, `open_surface` of floor products).
- **Plant**: `bark` (render, `watertight` via SDF), `leaves` (render, `open_surface`), `collision` (capsules from
  branch elements, `as_realized`).
- **Cutlery**: one `body` (render, `watertight`, exact) and its own collision.

Render outputs go to the procedural mesh store (one mesh per output × cell × material group). Collision outputs are kept
for physics. Navigation outputs are exported as marker data until the engine has navigation.

## 10. Policies (`policy.h`)

A **hook** is a named decision point with an interface type. A **policy** implements that interface and is bound to a
**scope**. When pack code asks for a hook, the core collects every policy whose scope covers the asking element and
applies the hook's **composition**.

```cpp
namespace GLT::construct {

    enum class scope_level : u8 {       // most specific first
        element,        // a single element path (user override)
        region,         // a guide region (face group, curve span)
        attribute,      // elements / regions carrying a tag value
        kind,           // an element kind
        pack,           // the pack's default
        construct,      // the construct asset's setting
        global,         // the core default
    };

    struct policy_scope {
        scope_level                 level;
        std::optional<element_path> path;
        std::optional<region_selector> region;
        std::optional<std::pair<attribute_id, attribute_value>> attribute;
        std::optional<kind_id>      kind;
        std::string                 pack;
    };

    enum class composition : u8 {
        first_match,    // most specific policy only
        chain,          // most specific first, each may call next (wrapping)
        merge,          // all matching policies combined by the hook's merge function
        all,            // all matching policies are called (validation, diagnostics)
    };

    // @brief A hook declaration. Every hook is a struct with these members
    //     struct sampling_hook {
    //         using interface = i_sampling;
    //         static constexpr std::string_view name = "core.sampling";
    //         static constexpr composition default_composition = composition::chain;
    //         static std::unique_ptr<i_sampling> merge(std::span<const i_sampling* const>);   // only for merge
    //     };
    template<typename H>
    concept hook = requires { typename H::interface; { H::name } -> std::convertible_to<std::string_view>;
                              { H::default_composition } -> std::convertible_to<composition>; };

    struct policy_desc {
        std::string                 name;
        std::string                 hook;                 // H::name
        policy_scope                scope;
        std::function<std::shared_ptr<const void>()> create;   // returns the H::interface implementation
    };

    // Packs can change how a hook composes, for scopes they own
    struct composition_override_desc { std::string hook; policy_scope scope; composition mode; };

    // @brief What a caller gets back: the composed policy, ready to call
    template<hook H>
    class policy_view {
    public:
        [[nodiscard]] const typename H::interface& operator*() const;      // composed result
        [[nodiscard]] const typename H::interface* operator->() const;
        [[nodiscard]] std::span<const typename H::interface* const> matches() const;   // raw list, most specific first
        [[nodiscard]] std::span<const policy_scope> scopes() const;                    // for the editor's policy inspector
    };

    // @brief For chain composition: every interface method gets the next policy in the chain
    template<typename I>
    class next_policy {
    public:
        [[nodiscard]] const I* get() const noexcept;          // null at the end of the chain (the global default answers)
        [[nodiscard]] explicit operator bool() const noexcept;
    };

    // Packs can declare new hooks the same way; their productions ask for them like core hooks
    template<hook H> void declare_hook(std::string_view owner);
}
```

Core hooks and their defaults. The interface of hook `core.<x>` is `i_<x>` (`i_sampling`, `i_outputs`, ...), its hook
struct `<x>_hook`.

| Hook | Interface (main method) | Default composition | Core default |
|---|---|---|---|
| `core.classification` | `role_id classify(const guide&)` | first_match | explicit role, else none |
| `core.propagation` | `propagate(attribute_desc, lineage_event, attribute_io&, next)` (§11) | chain | the attribute's mode |
| `core.sampling` | `stations_along`, `scatter_on`, `scatter_in` | chain | regular spacing, uniform scatter |
| `core.seed` | `u64 seed(const element_path&, u64 construct_seed, u64 salt)` | first_match | hash(path, seed, salt) |
| `core.conflict` | `conflict_action decide(const candidate&)` | first_match | report, no action |
| `core.interface_match` | `bool compatible(...)`, `joint_ownership ownership(...)` | merge | interface type tables |
| `core.joint_ownership` | `joint_ownership adjust(const relation&, joint_ownership)` | chain | unchanged |
| `core.meshing` | `name_id backend_for(operation, const element&)` | first_match | exact for booleans, sdf for blends |
| `core.outputs` | `void outputs(const construct_info&, std::vector<output_desc>&)` | **all** (each scope adds) | one render output, `merged_shells` |
| `core.partition` | `cell_key cell_of(...)`, `cell_layout layout(...)` (§17) | first_match | 64 m cells in large constructs |
| `core.uv` | `void unwrap(geometry::mesh&, const element&)` (§14) | chain | from surface parameterization |
| `core.lod` | `lod_plan plan(const output_desc&, const construct_info&)` | chain | store's automatic LODs |
| `core.iteration` | `bool converged(const iteration_state&)` | first_match | §7 rule |
| `core.constraint_solver` | `name_id solver_for(const constraint_island&)` | first_match | `core.gauss_seidel` |
| `core.validation` | `void validate(const element&, diagnostics&)` | **all** | none |

## 11. Attribute propagation (`propagation.h`)

Geometry operations never write attributes. They emit **lineage events**; the evaluator runs the `core.propagation`
hook for every affected attribute layer.

```cpp
namespace GLT::construct {

    enum class lineage_kind : u8 { split, merge, extrude, subdivide, mirror, instance, boolean, derive, transfer, remesh };

    struct lineage_source { element_handle element; attribute_domain domain; u32 index; f32 weight = 1.0f; };
    struct lineage_target { attribute_domain domain; u32 index; };

    struct lineage_event {
        lineage_kind                    kind;
        std::span<const lineage_source> sources;          // parents (with weights: area, length, barycentric)
        std::span<const lineage_target> targets;          // children
        std::optional<glm::vec3>        barycentric;      // split / subdivide: position in the parent
        std::optional<f64>              curve_t;          // split / derive along a curve
        std::optional<frame>            transform;        // mirror / instance
        std::optional<u8>               boolean_operand;  // boolean: which operand a target came from
        bool                            is_seam = false;  // target is a new seam edge / vertex
        u32                             depth_delta = 0;  // derive: production depth step
    };

    // Where geometry operations report lineage (the kernel passes one to every backend call)
    class lineage_sink {
    public:
        void report(const lineage_event& e);
    };

    // Read the sources' values, write the targets'
    class attribute_io {
    public:
        [[nodiscard]] attribute_value source(u32 i) const;
        void write(u32 target, attribute_value v);
        void leave_default(u32 target);
    };

    class i_propagation {
    public:
        virtual ~i_propagation() = default;
        virtual void propagate(const attribute_desc& attribute, const lineage_event& event, attribute_io& io,
                               const next_policy<i_propagation>& next) const = 0;
    };

    struct propagation_hook {
        using interface = i_propagation;
        static constexpr std::string_view name = "core.propagation";
        static constexpr composition default_composition = composition::chain;
    };

    // Built-in modes, registered as named policies; attributes refer to them by name in their schema
    //   copy, interpolate, reduce_min, reduce_max, reduce_sum, reduce_avg, decay, boundary, drop
    // Packs register more modes the same way:
    struct propagation_mode_desc { std::string name; std::function<std::shared_ptr<const i_propagation>()> create; };
}
```

Resolution per (attribute, event): the hook is looked up with the scope of the element being changed, so a tag-bound
policy ("faces tagged `armor` never interpolate") or an element override wins over the attribute's schema mode, which
wins over the pack default.

## 12. Context (`context.h`)

```cpp
namespace GLT::construct {

    struct query_filter {
        std::vector<role_id> roles; std::vector<kind_id> kinds; std::vector<attribute_id> require_present;
        bool include_guides = true, include_derived = true;
    };

    struct hit { glm::dvec3 position; glm::vec3 normal; element_handle element; region_selector region; f64 distance; };

    // @brief Read-only, thread-safe. Every query is recorded (query kind + region) for incremental invalidation
    class context {
    public:
        // Space
        [[nodiscard]] f64 distance(const glm::dvec3& p, const query_filter& f = {}) const;    // signed, orientation-aware
        [[nodiscard]] bool inside(const glm::dvec3& p, const query_filter& f = {}) const;
        [[nodiscard]] std::optional<hit> closest(const glm::dvec3& p, const query_filter& f = {}) const;
        [[nodiscard]] std::vector<hit> raycast(const ray& r, const query_filter& f = {}, u32 max_hits = 1) const;
        [[nodiscard]] std::vector<geometry::contour> slice(const plane& pl, const query_filter& f = {}) const;  // per segment: owner + region

        // Elements and relations
        [[nodiscard]] const element& get(element_handle h) const;
        [[nodiscard]] std::optional<element_handle> find(const element_path& p) const;
        [[nodiscard]] std::vector<element_handle> elements(const matcher& m) const;              // earlier passes / stages only
        [[nodiscard]] std::vector<element_handle> elements_in(const geometry::aabb& box, const matcher& m) const;
        [[nodiscard]] std::vector<const relation*> relations(element_handle h, relation_id kind = {}) const;
        [[nodiscard]] std::vector<element_handle> children(element_handle h) const;

        // Curves and surfaces
        [[nodiscard]] curve_sample sample_curve(element_handle h, f64 t) const;     // position, RMF frame, interpolated attributes
        [[nodiscard]] surface_sample sample_surface(element_handle h, glm::dvec2 uv) const;

        // Attributes (typed)
        template<auto Member> [[nodiscard]] auto attribute(const element& e, region_selector where = whole_element{}) const;
        [[nodiscard]] attribute_value attribute(const element& e, attribute_id id, region_selector where = whole_element{}) const;

        // Fields (§16): SDFs of roles, pack fields, external providers (terrain)
        [[nodiscard]] f64 field(name_id name, const glm::dvec3& p) const;
        [[nodiscard]] glm::dvec3 field_vector(name_id name, const glm::dvec3& p) const;

        // Services
        [[nodiscard]] const i_space& space() const;                      // §15
        [[nodiscard]] const i_kit_library& kits() const;                 // §20
        [[nodiscard]] const i_solvers& solvers() const;                  // §19

        // Policies
        template<hook H> [[nodiscard]] policy_view<H> policy(const element& e, region_selector where = whole_element{}) const;

        // Parameters of the construct instance (exposed parameters with world overrides applied)
        [[nodiscard]] attribute_value parameter(std::string_view name, attribute_value fallback) const;
        [[nodiscard]] u64 construct_seed() const noexcept;
    };
}
```

## 13. Geometry kernel and backends (`geometry.h`)

The kernel is a set of operations on four representations. Operations that need a robust boolean or meshing step go
through a **backend**, chosen per call. The core registers two backends; packs can register more.

```cpp
namespace GLT::construct::geometry {

    class mesh;       // half-edge, attribute layers per domain, face groups. f32 positions in the owning element's frame
    class curve;      // polyline / Bezier / Catmull-Rom / NURBS-evaluated; rotation-minimizing frames; attributes along t
    class patch;      // plane, bilinear / bicubic quad patch, trimmed patch, Coons patch from 4 curves
    class sdf;        // expression tree: primitives, transforms, (smooth) union / subtract / intersect, displacement, mesh-as-sdf
    struct cage { mesh base; };   // a volume guide after its edit stack; subdivision cage when meshing_hook says so

    enum class boolean_op : u8 { unite, subtract, intersect };

    // @brief Which backend runs an operation
    struct backend_choice {
        name_id name = {};       // "exact" (Manifold), "sdf" (in-house), pack backends; {} = ask core.meshing for this element
        static backend_choice exact()  { return { intern("exact") }; }
        static backend_choice sdf()    { return { intern("sdf") }; }
        static backend_choice policy() { return {}; }
    };

    enum class backend_caps : u32 {
        exact_boolean  = 1 << 0,   // robust, manifold-preserving booleans on triangle meshes
        smooth_blend   = 1 << 1,   // blends with a radius
        sharp_features = 1 << 2,   // meshing keeps creases and corners
        offset         = 1 << 3,
        displacement   = 1 << 4,
        adaptive       = 1 << 5,   // resolution follows curvature / error
    };

    struct sdf_meshing { f32 cell_size = 0.01f; f32 max_error = 0.001f; name_id mesher = intern("dual_contouring"); };   // or "surface_nets", "marching_cubes"

    // @brief A geometry backend. Registered with backend_desc { name, caps, create }
    class i_backend {
    public:
        virtual ~i_backend() = default;
        [[nodiscard]] virtual backend_caps caps() const = 0;
        virtual std::expected<mesh, geometry_error> boolean(const mesh& a, const mesh& b, boolean_op op, lineage_sink& lineage) = 0;
        virtual std::expected<mesh, geometry_error> mesh_sdf(const sdf& s, const aabb& bounds, const sdf_meshing& m, lineage_sink& lineage) = 0;
        virtual std::expected<mesh, geometry_error> offset(const mesh& m, f32 distance, lineage_sink& lineage) = 0;
    };

    // @brief The operations realizers and stages call. Every call reports lineage to the propagation system
    class kernel {
    public:
        // Construction
        mesh primitive(primitive_kind kind, const primitive_params& p);
        mesh extrude_profile(const contour& profile, f32 depth, const extrude_settings& s);
        mesh sweep(const contour& profile, const curve& path, const sweep_settings& s);        // radius / twist / profile from curve attributes
        mesh loft(std::span<const contour> sections, const curve& spine, const loft_settings& s);
        mesh lathe(const curve& profile, const glm::vec3& axis, u32 segments);
        mesh tessellate(const patch& p, const tessellation& t);

        // Editing (the guide edit stack uses the same calls)
        mesh loop_cut(const mesh& m, u32 edge_ring, std::span<const f32> positions);
        mesh extrude_faces(const mesh& m, const face_set& faces, f32 distance);
        mesh inset(const mesh& m, const face_set& faces, f32 amount);
        mesh bevel(const mesh& m, const edge_set& edges, const bevel_settings& s);
        mesh subdivide(const mesh& cage, u32 levels, const subdivision_settings& s);           // Catmull-Clark, creases from attributes
        mesh mirror(const mesh& m, const glm::vec4& plane, bool weld = true);
        mesh weld(const mesh& m, f32 tolerance);
        mesh smooth(const mesh& m, const smooth_settings& s);
        mesh displace(const mesh& m, const displacement& d);                                   // by function of position or surface param
        mesh remesh(const mesh& m, const remesh_settings& s);
        mesh decimate(const mesh& m, const decimate_settings& s);                              // keeps locked borders (§17)

        // Robust operations: backend per call
        mesh boolean(const mesh& a, const mesh& b, boolean_op op, backend_choice with = backend_choice::policy());
        mesh offset_shell(const mesh& surface, f32 thickness, shell_side side, backend_choice with = backend_choice::policy());
        mesh mesh_sdf(const sdf& s, const aabb& bounds, const sdf_meshing& m, backend_choice with = backend_choice::sdf());

        // Hybrid: an SDF-meshed blend zone stitched into explicit meshes (branch forks, organic junctions, welds)
        mesh blend_zone(std::span<const mesh* const> explicit_parts, const sdf& blend, const blend_zone_settings& s);

        // Queries
        std::vector<contour> slice(const mesh& m, const plane& p);
        sdf to_sdf(const mesh& closed);                                                         // generalized winding number + BVH
        aabb bounds(const mesh& m);
    };

    // @brief How blend_zone works
    struct blend_zone_settings {
        aabb        zone;                 // only here is the SDF meshed
        f32         radius = 0.05f;       // smooth-union radius
        sdf_meshing meshing;
        f32         stitch_tolerance = 0.0f;   // 0: derived from meshing cell size
        // Steps: (1) cut every explicit part at the zone boundary, keeping the outside; (2) mesh the SDF of the parts
        // plus [blend] inside the zone, clipped to the zone; (3) resample both boundary loops to a common vertex set
        // and weld them, so the seam is crack-free; (4) lineage: zone faces are 'boolean' targets of the parts they
        // came from (nearest operand), seam vertices are marked is_seam
    };
}
```

**Which backend where (G3).** `core.meshing` answers per operation and element. Typical pack choices: the Spaceship
pack uses `exact` for every boolean and plates; the Plant pack uses `blend_zone` at every `plant.fork` joint and exact
sweeps elsewhere; the Cutlery pack meshes the whole body with `sdf` and `sharp_features`, then `subdivide`s nothing.
An output with `topology::watertight` names its own backend (§9). A pack may register, for example, a voxel backend for
destruction-style shapes and select it only for elements tagged `damaged`.

## 14. Skins, panels and surface parameterization (`surface.h`)

**Skins (G2).** The outer surface of an envelope is just another element. A production in the pack emits a `skin`
element (shape `surface`) from an envelope guide; productions then emit **seam curves** on it (panel lines, plank
edges); the skin realizer cuts and thickens along them.

```cpp
namespace GLT::construct {

    // Built-in element kinds the core provides for packs to use (packs may also define their own)
    //   core.skin        surface derived from a volume guide (its outside, or inside for cavities)
    //   core.seam        curve on a skin (shape curve, attribute core.seam_on -> skin path)
    //   core.panel       region of a skin between seams (emitted by core.panelize)

    struct panelize_settings {
        f32 gap = 0.004f;                 // gap between panels
        f32 thickness = 0.01f;
        shell_side side = shell_side::inward;     // cavity skins: outward
        f32 edge_bevel = 0.002f;
        bool emit_panel_elements = true;  // true: each panel becomes a core.panel element (own tags, own realizer)
    };

    // Kernel-level operations used by skin realizers
    class surface_ops {
    public:
        // Split a surface along seam curves into panels (each panel keeps its parameterization)
        std::vector<geometry::mesh> panelize(const geometry::mesh& surface, std::span<const geometry::curve> seams,
                                             const panelize_settings& s);
        // Seam layouts as helpers for productions: iso-lines of the surface parameterization, plane sections, geodesics
        std::vector<geometry::curve> iso_seams(const geometry::mesh& surface, axis_uv axis, std::span<const f32> values);
        std::vector<geometry::curve> section_seams(const geometry::mesh& surface, std::span<const plane> planes);
    };
}
```

Envelopes defined by **section curves** (aircraft fuselage, boat hull, fork handle) are lofted: a production collects
the `section` curve guides along a `skeleton` / `spine` curve guide and emits one `core.skin` with `kernel::loft`.

**Surface parameterization (G15).** Every realized fragment can carry a `core.param` corner attribute (u, v in metres
along the surface). It is the basis for UVs and for detail operators.

```cpp
namespace GLT::construct {

    enum class param_method : u8 {
        from_construction,   // sweeps: (arc length, angle); lofts: (spine t, section t); patches: patch uv. Free and exact
        planar, box, cylindrical, spherical,
        conformal,           // LSCM per chart, charts split at seams / hard edges
    };

    void parameterize(geometry::mesh& m, param_method method, const param_settings& s);

    // Detail operators (finishers use them; realizers may too)
    struct groove      { std::vector<geometry::curve> curves_uv; f32 width, depth; };          // panel lines, engraving
    struct emboss      { geometry::sdf shape_uv; f32 height; };                               // logos, ornaments
    struct displacement_map { std::function<f32(glm::vec2 uv, glm::vec3 p)> height; f32 scale; };   // bark, dents (pure fn)

    geometry::mesh apply_groove(const geometry::mesh& m, const groove& g);
    geometry::mesh apply_emboss(const geometry::mesh& m, const emboss& e);
    geometry::mesh apply_displacement(const geometry::mesh& m, const displacement_map& d);
}
```

`core.uv` turns `core.param` into texture UVs. The core default copies it scaled by a texel density. Pack choices are, for
example, trim-sheet mapping for hull panels (rows chosen from tags) or a per-branch cylindrical atlas.

## 15. Space: voids, rooms and routing (`space.h`)

The free space of a construct is a service in the Context (G6). It is built after `core.interpret` from the volumes and
reservations, and refreshed when a stage marks it dirty. Packs can replace the whole service.

```cpp
namespace GLT::construct {

    // @brief Sparse voxel view of free space. Cost and labels come from attributes of the volumes that bound it
    struct void_settings { f32 voxel = 0.25f; std::vector<role_id> blocking = { intern("reservation") }; };

    struct partition_request {
        element_handle              region;               // a void / envelope to partition
        std::vector<element_handle> seeds;                // room seeds (point guides) or empty: from walls only
        std::vector<element_handle> walls;                // surface guides / elements that separate rooms
        f32                         min_room_volume = 4.0f;
        name_id                     method = intern("watershed");   // "watershed", "bsp", pack methods
    };
    struct partition_result { std::vector<element_handle> rooms; std::vector<std::pair<u32, u32>> adjacency; };   // rooms are emitted as core.room elements

    struct route_request {
        glm::dvec3                  from, to;             // or sockets / markers resolved by the caller
        f32                         clearance = 0.1f;     // radius kept free
        f32                         min_bend_radius = 0.0f;
        name_id                     style = intern("any_angle");    // "manhattan" (pipes, cable trays), "any_angle", "octilinear"
        std::function<f32(const glm::dvec3& p, const void_cell& cell)> cost;     // extra cost, pure (prefer ceilings, avoid rooms)
        i32                         priority = 0;         // lower priority routes avoid higher priority ones
        bool                        reserve = true;       // the result blocks space for later routes
        element_path                path;                 // stable identity: reservation order is (priority, path)
    };
    struct route { geometry::curve centre; f64 length; u32 bends; };

    class i_space {
    public:
        virtual ~i_space() = default;
        [[nodiscard]] virtual const void_grid& voids(const void_settings& s) const = 0;
        [[nodiscard]] virtual std::expected<partition_result, space_error> partition(const partition_request& r) const = 0;
        [[nodiscard]] virtual std::expected<route, space_error> route(const route_request& r) const = 0;
    };

    struct space_service_desc { std::string name; std::function<std::unique_ptr<i_space>()> create; };   // replaces "core.space"
}
```

Routing is deterministic: all route requests of a pass are collected, sorted by (priority, path) and solved in that
order on worker threads with a snapshot per priority level. A route that cannot be found is a diagnostic on the element
that asked; the pipe or corridor element is culled or kept as a straight placeholder, per pack choice.

## 16. Field providers (`field.h`)

Fields are functions of world space. The core registers the SDFs of every role; packs register their own fields; **other
plugins register external fields** (G8).

```cpp
namespace GLT::construct {

    enum class field_type : u8 { scalar, vector, sdf };

    class i_field_provider {
    public:
        virtual ~i_field_provider() = default;
        [[nodiscard]] virtual field_type type() const = 0;
        [[nodiscard]] virtual f64 sample(const glm::dvec3& p) const = 0;
        [[nodiscard]] virtual glm::dvec3 sample_vector(const glm::dvec3& p) const { return {}; }
        [[nodiscard]] virtual std::optional<geometry::aabb> bounds() const { return std::nullopt; }
        [[nodiscard]] virtual u64 revision() const = 0;       // bump when the field changed: dependent elements re-evaluate
        // Batched sampling for speed (default loops over sample())
        virtual void sample_many(std::span<const glm::dvec3> p, std::span<f64> out) const;
    };

    enum class field_scope : u8 {
        construct,   // created per evaluation by a pack (light, stress) from the construct's own data
        world,       // provided by another plugin for constructs placed in a world (terrain height)
    };

    struct field_provider_desc {
        std::string name;                 // "plant.light", "terrain.height", "terrain.ground_sdf"
        field_scope scope;
        std::function<std::shared_ptr<const i_field_provider>(const field_request&)> create;   // request: construct, world, bounds
    };
}
```

**Terrain integration.** The terrain plugin registers `terrain.height` (scalar: ground height at x, z) and
`terrain.ground_sdf` (signed distance to the ground surface) as `world` fields, by sampling its graph at arbitrary
points with its existing region evaluator (1 × 1 regions batched through `sample_many`). Its `revision()` is the graph
revision plus the seed and overrides of the world's terrain component. This is an addition to
[terrain_generation.md](terrain_generation.md): a small public header and one registration call, no change to its
evaluation model. A construct that reads a `world` field without being placed in a world gets the provider's fallback
(flat ground at y = 0) and a diagnostic.

## 17. Partitioning and large constructs (`partition.h`)

```cpp
namespace GLT::construct {

    struct cell_key { i32 x = 0, y = 0, z = 0; u8 level = 0; auto operator<=>(const cell_key&) const = default; };

    struct cell_layout {
        f64                         cell_size = 64.0;     // metres; 0: no partitioning (one cell)
        glm::dvec3                  origin{ 0.0 };
        bool                        split_fragments = true;   // fragments crossing cells are cut at cell borders
    };

    class i_partition {
    public:
        virtual ~i_partition() = default;
        [[nodiscard]] virtual cell_layout layout(const construct_info& c) const = 0;
        // Cell of a whole product (used when split_fragments is false, or for instances / markers)
        [[nodiscard]] virtual cell_key cell_of(const product& p, const element& owner) const = 0;
    };

    struct partition_hook {
        using interface = i_partition;
        static constexpr std::string_view name = "core.partition";
        static constexpr composition default_composition = composition::first_match;
    };
}
```

Rules (G9):

- **Precision.** Element frames are f64 world placements; geometry inside is f32 relative to the element. At output,
  geometry is re-expressed relative to its **cell origin**; each cell's procedural mesh sits on an entity at that
  origin. A 5 km bridge never stores large f32 coordinates.
- **Joints across cells.** Joint geometry goes to the cell of the side named in `ownership.owns_contact`; with
  `joint_realizer`, the joint's own geometry goes to the cell containing the joint's contact centre.
- **Cut fragments.** With `split_fragments`, the kernel cuts a fragment at the cell planes. Vertices on a cut get the
  `core.cell_border` attribute; both sides get identical positions.
- **Crack-free LODs.** `kernel::decimate` and the store's automatic LOD build lock border vertices (the store already
  locks open borders). Pack-provided LODs (re-realizing with dropped passes, `lod_request`) must keep `core.cell_border`
  vertices; `core.validation` checks it in debug builds.
- **Streaming.** Cells are independent outputs, so a world can load and unload construct cells by distance like terrain
  chunks. Live constructs evaluate cells near the camera first.

## 18. Constraints (`constraint.h`)

Constraints (G10) are relations of kind `core.constraint`. They are solved in `core.interpret` on guides and, if a pass
asks for it, on derived elements after that pass (`pass_desc` gets `solve_constraints = true`). Constraint types and
solvers are registered; the solver per island is chosen by `core.constraint_solver`.

```cpp
namespace GLT::construct {

    // Degrees of freedom a constraint may move: guide transforms, control points, element params
    struct dof_ref { element_handle element; dof_kind kind; u32 index; };     // kind: position, rotation, scale, control_point, param

    class i_constraint_type {
    public:
        virtual ~i_constraint_type() = default;
        // Residuals (0 when satisfied) and the dofs they depend on
        virtual void collect(const relation& r, const context& ctx, std::vector<dof_ref>& dofs) const = 0;
        virtual void residuals(const relation& r, const dof_values& x, std::span<f64> out) const = 0;
        [[nodiscard]] virtual u32 residual_count(const relation& r) const = 0;
        // Optional analytic projection (position-based solvers use it; default: numeric gradient)
        virtual bool project(const relation& r, dof_values& x) const { return false; }
    };
    struct constraint_type_desc { std::string name; std::function<std::shared_ptr<const i_constraint_type>()> create; };

    struct solve_settings { u32 max_iterations = 64; f64 tolerance = 1e-6; };
    struct solve_result   { bool converged; u32 iterations; f64 residual; std::vector<element_handle> unsatisfied; };

    class i_constraint_solver {
    public:
        virtual ~i_constraint_solver() = default;
        virtual solve_result solve(constraint_island& island, const solve_settings& s) const = 0;
    };
    struct constraint_solver_desc { std::string name; std::function<std::shared_ptr<const i_constraint_solver>()> create; };
}
```

Built-in types: `fixed`, `coincident`, `distance`, `equal_length`, `parallel`, `perpendicular`, `tangent`,
`symmetric` (about a plane), `on_curve`, `on_surface`, `aligned`. Built-in solvers: `core.gauss_seidel` (projective,
fast, the default) and `core.levenberg_marquardt` (for stiff islands). Islands are solved independently and in parallel;
inside an island, constraints are processed in path order. Unsatisfied constraints mark their elements and the
construct still evaluates with the best result.

## 19. Iterative solvers (`solver.h`)

Physical-looking shapes (G11): sagging cables, cushions, draped cloth, relaxed meshes. A solver is a deterministic
iterative operator with a fixed iteration order and an explicit stop rule.

```cpp
namespace GLT::construct {

    struct solver_limits { u32 max_iterations = 200; f64 tolerance = 1e-5; };

    class i_solver {
    public:
        virtual ~i_solver() = default;
        [[nodiscard]] virtual std::unique_ptr<solver_state> init(const solver_input& in, const context& ctx) const = 0;
        // One iteration. Returns the change measure (max displacement); the core stops at tolerance or max_iterations
        virtual f64 step(solver_state& s, u32 iteration) const = 0;
        [[nodiscard]] virtual solver_output result(const solver_state& s) const = 0;
    };
    struct solver_desc { std::string name; std::function<std::shared_ptr<const i_solver>()> create; };

    class i_solvers {
    public:
        virtual ~i_solvers() = default;
        [[nodiscard]] virtual solver_output run(std::string_view solver, const solver_input& in, const solver_limits& l) const = 0;
    };
}
```

Built-ins:

| Solver | Input | Output | Typical use |
|---|---|---|---|
| `core.catenary` | two anchors, length or sag, segment count | curve | cables, chains (links instanced along it), hanging ropes |
| `core.relax` | mesh or curve, pinned vertices, iterations | mesh / curve | fairing lofts, smoothing routed pipes |
| `core.inflate` | closed mesh, pressure, edge stiffness, pinned seams | mesh | cushions, inflatables, padding |
| `core.drape` | mesh, colliders from the Context, gravity | mesh | cloth over furniture, tarps, sails |
| `core.space_colonization` | attractor points, start nodes, step | graph | tree branching, roots, veins |

Determinism: single-threaded inside one solver run, or parallel with fixed reduction order; results cached by
(element path, input hash). Solvers run inside productions or realizers (`ctx.query().solvers().run(...)`), so a cable
is solved when its element is expanded or realized, never globally.

## 20. Kits (`kit.h`)

Kits (G7) are libraries of mesh assets with **sockets**: named frames where other parts attach, with tags saying what
fits. They exist as an asset type (`glt.construct_kit`, authored in the editor by placing socket gizmos on meshes) and as
code (`kit_desc` from a pack).

```cpp
namespace GLT::construct {

    struct kit_socket {
        std::string                 name;                 // "foot", "rail_left", "top"
        frame                       local;                // in the part's frame; +Z points out of the part
        std::vector<name_id>        provides;             // "ship.rail_mount"
        std::vector<name_id>        accepts;              // tags a mating socket must provide
        f32                         snap_tolerance = 0.01f;
        bool                        allow_rotation = true;    // around the socket's Z axis
    };

    struct kit_part {
        std::string                 name;                 // "railing_post_1m"
        asset::asset_ref            mesh;
        std::vector<kit_socket>     sockets;
        geometry::aabb              bounds;
        attribute_layers            tags;                 // style, size class, material set
        std::vector<std::string>    variants;             // interchangeable parts (chosen by rng / tags)
        std::optional<asset::asset_ref> collider;
    };

    struct kit_desc { std::string name; std::vector<kit_part> parts; };

    struct kit_part_ref { std::shared_ptr<const kit_desc> kit; u32 part = 0; };

    class i_kit_library {
    public:
        virtual ~i_kit_library() = default;
        [[nodiscard]] virtual std::vector<kit_part_ref> find(std::string_view kit, const part_query& q) const = 0;   // by tags, size, socket provides
        // Placement that mates [socket] of [part] onto a target frame (another part's socket or a frame on an element)
        [[nodiscard]] virtual std::optional<frame> mate(const kit_part_ref& part, std::string_view socket,
                                                         const frame& target, const mate_settings& s) const = 0;
    };
}
```

Kit parts are emitted with `realize_context::kit_part`. A production can instead emit `core.kit_instance` elements
(params: kit part, socket mating); the core realizes them into kit-part products, and until then resolvers can see them
like any element (a railing post that collides with a pipe can be moved or culled). They become instances (no unique geometry), keep their own LODs
and colliders, and count as products for outputs and partitioning. An output with `topology::watertight` can include
kit parts; they are then merged as geometry (the instance is expanded for that output only).

## 21. Editor extensions for packs (`editor.h`)

Packs control how their guides are authored. Everything here is optional and only registered when an editor exists
(`editor::get()` is non-null), like the terrain plugin's editor part.

```cpp
namespace GLT::construct {

    class i_guide_tool {                                  // a tool in the construct editor's toolbar
    public:
        virtual ~i_guide_tool() = default;
        [[nodiscard]] virtual std::string_view name() const = 0;
        virtual void draw_options() {}
        virtual bool on_input(const editor::viewport_input& in, guide_edit_session& session) = 0;   // creates / edits guides
        virtual void draw_overlay(render::i_debug_draw& draw, const context& ctx) {}
    };

    struct editor_extension_desc {
        std::string                 name;
        std::vector<std::function<std::unique_ptr<i_guide_tool>()>> tools;      // e.g. "Draw branch", "Place leaf plane"
        std::function<void(const element&, render::i_debug_draw&)> draw_element;   // gizmo for a pack kind
        std::function<void(guide&, guide_edit_session&)> details;               // extra details panel UI for a pack role
        std::vector<std::string>    role_colours;         // "envelope=#4a90d9", ...
    };
}
```

## 22. The plugin as seen by packs (`construct.h`)

```cpp
namespace GLT::construct {

    // @brief The construct plugin: plugin_manager::get_plugin_base("construct")->as<i_construct>()
    class i_construct {
    public:
        virtual ~i_construct() = default;

        [[nodiscard]] virtual i_registry& registry() = 0;

        // Evaluate a construct outside a world (tests, tools, runtime generation from game code)
        [[nodiscard]] virtual std::shared_ptr<evaluation> evaluate(evaluation_request request) = 0;
    };

    struct evaluation_request {
        construct_data              data;                 // loaded .glt_construct, or built in code
        u64                         seed = 0;
        std::unordered_map<std::string, attribute_value> parameters;
        std::vector<std::string>    outputs;              // empty: all
        std::optional<lod_request>  lod;
        world::i_world*             world = nullptr;      // for world fields; null: fallbacks
        i32                         priority = 0;
    };

    class evaluation {
    public:
        [[nodiscard]] bool done() const noexcept;
        void cancel() noexcept;
        void wait();
        [[nodiscard]] const evaluation_result& result() const;   // outputs, markers, colliders, diagnostics, stats per stage
        // Publish render outputs to the procedural mesh store (what the world component does)
        [[nodiscard]] std::vector<render::procedural_mesh_id> publish(render::procedural_mesh_store& store, const publish_settings& s);
    };
}
```

## 23. A pack, end to end

The skeleton of a minimal plant pack, showing which pieces a pack writes.

```cpp
// <project>/plugins/construct_pack_plant/src/plugin.cpp
namespace plant_pack {

    struct [[=construct::tag_schema{ "plant" }]] tags {
        [[=construct::attr{ .domains = construct::attribute_domain::curve, .propagation = construct::intern("interpolate") }]] f32 radius = 0.1f;
        [[=construct::attr{ .propagation = construct::intern("decay"), .rate = 0.8f }]] f32 vigor = 1.0f;
        [[=construct::attr{ .domains = construct::attribute_domain::curve }]] f32 twig_density = 2.0f;   // per metre
    };

    struct [[=construct::kind_info{ "plant.branch", construct::shape_kind::curve }]] branch { [[=construct::param{ "Sides" }]] i32 sides = 8; };

    // Spline guides with role "skeleton" become branches; snapped start points become forks
    struct [[=construct::production_info{ .name = "plant.branches_from_splines", .pass = "primary" }]]
    branches_from_splines final : construct::production { /* expand(): emit plant.branch per skeleton guide, relate child_of */ };

    // Twigs along drawn branches (procedural fill), recursive up to depth 3
    struct [[=construct::production_info{ .name = "plant.twigs", .pass = "secondary" }]]
    twigs final : construct::production { /* sampling_hook: stochastic, density = twig_density * vigor */ };

    // Leaf planes: scatter leaf-card kit parts over the plane, facing its normal
    struct [[=construct::production_info{ .name = "plant.leaves", .pass = "detail" }]]
    leaves final : construct::production { /* emit core.kit_instance elements */ };

    // Forks: joint with both sides adapting, no joint realizer; realized as a blend zone
    struct [[=construct::resolver_info{ .name = "plant.forks" }]] forks final : construct::resolver { /* joint("plant.fork", ...) */ };

    // Branch realizer: sweep with the radius curve attribute; at forks owned by this side, blend_zone with the child
    struct [[=construct::realizer_info{ .name = "plant.branch_tube" }]] branch_tube final : construct::realizer { /* geo().sweep / blend_zone */ };

    // Policies: interpolate along curves, inherit vigor through child_of with decay; outputs: bark (watertight, sdf) + leaves (open)
    struct [[=construct::policy_info{ .hook = "core.outputs", .level = construct::scope_level::pack }]]
    outputs final : construct::i_outputs { /* bark + leaves + capsule collision */ };
}

void plugin::on_load() {
    construct::register_pack<^^plant_pack>({ .name = "plant", .version = { 0, 1, 0 } });
}
```

## Gap coverage

Every gap from [procedural_construction.md](procedural_construction.md) mapped to the interface that closes it.

| # | Gap | Interface |
|---|---|---|
| G1 | Guides of every dimension | §3 `shape_kind`, `guide`, `region_selector` |
| G2 | Outer skins, plating, lofted shells | §14 `core.skin`, `core.seam`, `panelize`, §13 `loft`, `offset_shell` |
| G3 | Smooth single bodies and blends | §13 backends per operation, `blend_zone`, `sdf`, `subdivide`, `core.meshing` |
| G4 | Recursion, grammars | §5 passes with `max_depth`, §6 productions (`per_element`, `batch`, priority, exclusive) |
| G5 | Openings through several elements | §7 `cut`, §9 finisher `core.assembly_booleans` |
| G6 | Rooms and routing | §15 `i_space`: void grid, `partition`, `route` with reservation order |
| G7 | Reusable parts | §20 kits with sockets, `kit_part` products |
| G8 | World context | §16 `world` field providers; terrain registers `terrain.height` / `terrain.ground_sdf` |
| G9 | Huge constructs | §17 cells, cell-relative coordinates, joint ownership across cells, locked borders |
| G10 | Parametric relationships | §18 constraint types and solvers, islands, `core.constraint_solver` |
| G11 | Physical shapes | §19 iterative solvers (catenary, relax, inflate, drape, space colonization) |
| G12 | Non-mesh products | §8 instances, colliders, markers, decals, nested constructs |
| G13 | Negotiation between elements | §7 `iteration_settings`, `core.iteration`, cycle detection |
| G14 | Cavities | §3 `orientation`, §13 `offset_shell` side, §14 skins of cavities |
| G15 | Surface detail | §14 `core.param`, `parameterize`, grooves, emboss, displacement, `core.uv` |
| G16 | Variants | §12 `parameter`, `construct_seed`, §10 `core.seed` |
