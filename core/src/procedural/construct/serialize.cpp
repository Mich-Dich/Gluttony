#include "procedural/construct/serialize.h"



namespace GLT::construct {

    // CLASS DECLARATION ===============================================================================================

    namespace {

        constexpr u32                                   MAGIC = 0x54534e43;     // "CNST"
        constexpr u32                                   MAX_COUNT = 1u << 24;   // sanity limit for any list

        class writer {
        public:

            template<typename T>
            requires std::is_trivially_copyable_v<T>
            void raw(const T& v) {
                const auto* p = reinterpret_cast<const std::byte*>(&v);
                m_out.insert(m_out.end(), p, p + sizeof(T));
            }

            void u8v(u8 v)                  { raw(v); }
            void u32v(u32 v)                { raw(v); }
            void count(size_t n)            { raw(static_cast<u32>(n)); }
            void str(std::string_view s)    { count(s.size()); m_out.insert(m_out.end(), reinterpret_cast<const std::byte*>(s.data()), reinterpret_cast<const std::byte*>(s.data()) + s.size()); }
            void name(name_id n)            { str(n.str()); }
            void path(const element_path& p){ str(p.to_string()); }

            void value(const attribute_value& v) {
                u8v(static_cast<u8>(v.index()));
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::monostate>)    {}
                    else if constexpr (std::is_same_v<T, bool>)         u8v(x ? 1 : 0);
                    else if constexpr (std::is_same_v<T, name_id>)      name(x);
                    else if constexpr (std::is_same_v<T, std::string>)  str(x);
                    else                                                raw(x);
                }, v);
            }

            void region(const region_selector& r) {
                u8v(static_cast<u8>(r.index()));
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, face_group>)        name(x.group);
                    else if constexpr (std::is_same_v<T, curve_span>)   { raw(x.t0); raw(x.t1); }
                    else if constexpr (std::is_same_v<T, point_index>)  raw(x.index);
                }, r);
            }

            void attributes(const attribute_set& s) {
                count(s.entries().size());
                for (const auto& e : s.entries()) {
                    name(e.id);
                    region(e.region);
                    value(e.value);
                }
            }

            void placement(const frame& f) { raw(f.origin); raw(f.rotation); raw(f.scale); }

            void selection(const face_selection& s) {
                u8v(s.group ? 1 : 0);   if (s.group) name(*s.group);
                u8v(s.normal ? 1 : 0);  if (s.normal) raw(*s.normal);
                raw(s.max_angle_degrees);
                u8v(s.inside ? 1 : 0);  if (s.inside) { raw(s.inside->first); raw(s.inside->second); }
            }

            void scope(const policy_scope& s) {
                u8v(static_cast<u8>(s.level));
                path(s.path);
                region(s.region);
                name(s.attribute);
                value(s.value);
                name(s.kind);
            }

            void setting(const setting_value& v) {
                u8v(static_cast<u8>(v.index()));
                std::visit([&](const auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, bool>)                  u8v(x ? 1 : 0);
                    else if constexpr (std::is_same_v<T, std::string>)      str(x);
                    else if constexpr (std::is_same_v<T, profile_curve>)    { count(x.points.size()); for (const glm::vec2& p : x.points) raw(p); }
                    else                                                    raw(x);
                }, v);
            }

            [[nodiscard]] std::vector<std::byte> take() { return std::move(m_out); }

        private:

            std::vector<std::byte>                      m_out{};
        };


        struct read_error { std::string message; };

        class reader {
        public:

            explicit reader(std::span<const std::byte> in) noexcept : m_in(in) {}

            template<typename T>
            requires std::is_trivially_copyable_v<T>
            T raw() {
                if (m_pos + sizeof(T) > m_in.size())
                    throw read_error{ std::format("unexpected end of data at byte {}", m_pos) };
                T v{};
                std::memcpy(&v, m_in.data() + m_pos, sizeof(T));
                m_pos += sizeof(T);
                return v;
            }

            u8 u8v()    { return raw<u8>(); }
            u32 u32v()  { return raw<u32>(); }
            u32 count() {
                const u32 n = raw<u32>();
                if (n > MAX_COUNT)
                    throw read_error{ std::format("implausible count {} at byte {}", n, m_pos - 4) };
                return n;
            }
            std::string str() {
                const u32 n = count();
                if (m_pos + n > m_in.size())
                    throw read_error{ "string runs past the end of the data" };
                std::string s(reinterpret_cast<const char*>(m_in.data() + m_pos), n);
                m_pos += n;
                return s;
            }
            name_id name() { return intern(str()); }
            element_path path() {
                const std::string s = str();
                if (s.empty())
                    return {};
                auto p = element_path::parse(s);
                if (!p)
                    throw read_error{ std::format("bad element path [{}]: {}", s, p.error().message) };
                return *p;
            }

            setting_value setting() {
                switch (u8v()) {
                    case 0:  return raw<f32>();
                    case 1:  return raw<i32>();
                    case 2:  return u8v() != 0;
                    case 3:  return raw<glm::vec2>();
                    case 4:  return raw<glm::vec3>();
                    case 5: {
                        profile_curve c{};
                        c.points.clear();
                        for (u32 i = 0, n = count(); i < n; ++i)
                            c.points.push_back(raw<glm::vec2>());
                        c.sanitize();
                        return c;
                    }
                    case 6:  return str();
                    default: throw read_error{ "unknown stage setting type" };
                }
            }

            attribute_value value() {
                switch (u8v()) {
                    case 0:  return std::monostate{};
                    case 1:  return u8v() != 0;
                    case 2:  return raw<i32>();
                    case 3:  return raw<i64>();
                    case 4:  return raw<f32>();
                    case 5:  return raw<f64>();
                    case 6:  return raw<glm::vec2>();
                    case 7:  return raw<glm::vec3>();
                    case 8:  return raw<glm::vec4>();
                    case 9:  return raw<glm::quat>();
                    case 10: return name();
                    case 11: return str();
                    default: throw read_error{ "unknown attribute value type" };
                }
            }

            region_selector region() {
                switch (u8v()) {
                    case 0:  return whole_element{};
                    case 1:  return face_group{ name() };
                    case 2:  { curve_span s{}; s.t0 = raw<f64>(); s.t1 = raw<f64>(); return s; }
                    case 3:  return point_index{ raw<u32>() };
                    default: throw read_error{ "unknown region type" };
                }
            }

            attribute_set attributes() {
                attribute_set s{};
                const u32 n = count();
                for (u32 i = 0; i < n; ++i) {
                    const name_id id = name();
                    const region_selector r = region();
                    s.set(id, value(), r);
                }
                return s;
            }

            frame placement() {
                frame f{};
                f.origin = raw<glm::dvec3>();
                f.rotation = raw<glm::quat>();
                f.scale = raw<glm::vec3>();
                return f;
            }

            face_selection selection() {
                face_selection s{};
                if (u8v()) s.group = name();
                if (u8v()) s.normal = raw<glm::vec3>();
                s.max_angle_degrees = raw<f32>();
                if (u8v()) {
                    const glm::vec3 lo = raw<glm::vec3>();
                    const glm::vec3 hi = raw<glm::vec3>();
                    s.inside = std::pair{ lo, hi };
                }
                return s;
            }

            policy_scope scope() {
                policy_scope s{};
                s.level = static_cast<scope_level>(u8v());
                s.path = path();
                s.region = region();
                s.attribute = name();
                s.value = value();
                s.kind = name();
                return s;
            }

            [[nodiscard]] bool done() const noexcept { return m_pos == m_in.size(); }

        private:

            std::span<const std::byte>                  m_in;
            size_t                                      m_pos = 0;
        };

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::vector<std::byte> serialize(const construct_data& data) {

        writer w;
        w.u32v(MAGIC);
        w.u32v(CONSTRUCT_DATA_VERSION);

        w.count(data.packs.size());
        for (const std::string& p : data.packs)
            w.str(p);

        w.count(data.guides.size());
        for (const guide& g : data.guides) {
            w.str(g.name);
            w.raw(g.index);
            w.name(g.role);
            w.placement(g.placement);

            w.u8v(static_cast<u8>(g.shape.index()));
            std::visit([&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, volume_guide>) {
                    w.u8v(static_cast<u8>(s.primitive));
                    w.raw(s.params.size);
                    w.raw(s.params.radius);
                    w.raw(s.params.height);
                    w.raw(s.params.segments);
                    w.raw(s.params.rings);
                    w.raw(s.params.divisions);
                    w.u8v(s.params.caps ? 1 : 0);
                    w.u8v(static_cast<u8>(s.side));
                } else if constexpr (std::is_same_v<T, curve_guide>) {
                    w.u8v(static_cast<u8>(s.type));
                    w.count(s.points.size());
                    for (const glm::vec3& p : s.points) w.raw(p);
                    w.u8v(s.closed ? 1 : 0);
                    w.count(s.point_attributes.size());
                    for (const auto& [id, values] : s.point_attributes) {
                        w.name(id);
                        w.count(values.size());
                        for (const attribute_value& v : values) w.value(v);
                    }
                } else if constexpr (std::is_same_v<T, surface_guide>) {
                    w.raw(s.size);
                    w.raw(s.divisions);
                } else {
                    w.count(s.points.size());
                    for (const glm::vec3& p : s.points) w.raw(p);
                }
            }, g.shape);

            w.count(g.edits.size());
            for (const edit_step& e : g.edits) {
                w.u8v(static_cast<u8>(e.index()));
                std::visit([&](const auto& s) {
                    using T = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<T, edit_transform>) {
                        w.raw(s.matrix);
                    } else if constexpr (std::is_same_v<T, edit_loop_cut>) {
                        w.raw(s.near_point);
                        w.count(s.positions.size());
                        for (const f32 t : s.positions) w.raw(t);
                    } else if constexpr (std::is_same_v<T, edit_assign_group>) {
                        w.selection(s.faces);
                        w.name(s.group);
                    } else {
                        w.selection(s.faces);
                        w.name(s.id);
                        w.value(s.value);
                        w.raw(static_cast<u16>(s.domain));
                    }
                }, e);
            }

            w.u8v(g.modifiers.mirror_plane ? 1 : 0);
            if (g.modifiers.mirror_plane)
                w.raw(*g.modifiers.mirror_plane);
            w.attributes(g.attributes);
        }

        w.count(data.parameters.size());
        for (const exposed_parameter& p : data.parameters) {
            w.str(p.name);
            w.value(p.value);
        }

        w.count(data.overrides.size());
        for (const element_override& o : data.overrides) {
            w.path(o.path);
            w.u8v(o.hidden ? 1 : 0);
            w.attributes(o.attributes);
        }

        w.count(data.policies.size());
        for (const policy_binding& b : data.policies) {
            w.str(b.policy);
            w.u8v(b.scope ? 1 : 0);
            if (b.scope)
                w.scope(*b.scope);
        }

        w.count(data.compositions.size());
        for (const auto& [hook, mode] : data.compositions) {
            w.str(hook);
            w.u8v(static_cast<u8>(mode));
        }

        w.raw(data.origin);

        // version 2: stages
        w.count(data.generators.size());
        for (const generator_instance& g : data.generators) {
            w.u32v(g.id);
            w.str(g.type);
            w.str(g.label);
            w.u8v(g.enabled ? 1 : 0);
            w.count(g.inputs.size());
            for (const u32 i : g.inputs)
                w.u32v(i);
            w.u32v(g.seed);
            w.count(g.settings.size());
            for (const auto& [name, value] : g.settings) {
                w.str(name);
                w.setting(value);
            }
        }
        return w.take();
    }


    std::expected<construct_data, std::string> deserialize(std::span<const std::byte> bytes) {

        reader r(bytes);
        construct_data data{};
        try {
            if (r.u32v() != MAGIC)
                return std::unexpected(std::string("not construct data (bad magic)"));
            const u32 version = r.u32v();
            if (version == 0 || version > CONSTRUCT_DATA_VERSION)
                return std::unexpected(std::format("unsupported construct data version {} (this build reads up to {})", version, CONSTRUCT_DATA_VERSION));

            for (u32 i = 0, n = r.count(); i < n; ++i)
                data.packs.push_back(r.str());

            for (u32 i = 0, n = r.count(); i < n; ++i) {
                guide g{};
                g.name = r.str();
                g.index = r.raw<i64>();
                g.role = r.name();
                g.placement = r.placement();

                switch (r.u8v()) {
                    case 0: {
                        volume_guide v{};
                        v.primitive = static_cast<geometry::primitive_kind>(r.u8v());
                        v.params.size = r.raw<glm::vec3>();
                        v.params.radius = r.raw<f32>();
                        v.params.height = r.raw<f32>();
                        v.params.segments = r.raw<u32>();
                        v.params.rings = r.raw<u32>();
                        v.params.divisions = r.raw<glm::uvec2>();
                        v.params.caps = r.u8v() != 0;
                        v.side = static_cast<orientation>(r.u8v());
                        g.shape = v;
                        break;
                    }
                    case 1: {
                        curve_guide c{};
                        c.type = static_cast<geometry::curve_type>(r.u8v());
                        for (u32 k = 0, m = r.count(); k < m; ++k)
                            c.points.push_back(r.raw<glm::vec3>());
                        c.closed = r.u8v() != 0;
                        for (u32 k = 0, m = r.count(); k < m; ++k) {
                            const name_id id = r.name();
                            std::vector<attribute_value> values;
                            for (u32 j = 0, q = r.count(); j < q; ++j)
                                values.push_back(r.value());
                            c.point_attributes.emplace_back(id, std::move(values));
                        }
                        g.shape = std::move(c);
                        break;
                    }
                    case 2: {
                        surface_guide s{};
                        s.size = r.raw<glm::vec2>();
                        s.divisions = r.raw<glm::uvec2>();
                        g.shape = s;
                        break;
                    }
                    case 3: {
                        point_guide p{};
                        p.points.clear();
                        for (u32 k = 0, m = r.count(); k < m; ++k)
                            p.points.push_back(r.raw<glm::vec3>());
                        g.shape = std::move(p);
                        break;
                    }
                    default:
                        return std::unexpected(std::format("guide [{}]: unknown shape", g.name));
                }

                for (u32 k = 0, m = r.count(); k < m; ++k) {
                    switch (r.u8v()) {
                        case 0: g.edits.push_back(edit_transform{ r.raw<glm::mat4>() }); break;
                        case 1: {
                            edit_loop_cut e{};
                            e.near_point = r.raw<glm::vec3>();
                            e.positions.clear();
                            for (u32 j = 0, q = r.count(); j < q; ++j)
                                e.positions.push_back(r.raw<f32>());
                            g.edits.push_back(std::move(e));
                            break;
                        }
                        case 2: {
                            edit_assign_group e{};
                            e.faces = r.selection();
                            e.group = r.name();
                            g.edits.push_back(std::move(e));
                            break;
                        }
                        case 3: {
                            edit_set_attribute e{};
                            e.faces = r.selection();
                            e.id = r.name();
                            e.value = r.value();
                            e.domain = static_cast<attribute_domain>(r.raw<u16>());
                            g.edits.push_back(std::move(e));
                            break;
                        }
                        default:
                            return std::unexpected(std::format("guide [{}]: unknown edit step", g.name));
                    }
                }

                if (r.u8v())
                    g.modifiers.mirror_plane = r.raw<glm::vec4>();
                g.attributes = r.attributes();
                data.guides.push_back(std::move(g));
            }

            for (u32 i = 0, n = r.count(); i < n; ++i) {
                exposed_parameter p{};
                p.name = r.str();
                p.value = r.value();
                data.parameters.push_back(std::move(p));
            }

            for (u32 i = 0, n = r.count(); i < n; ++i) {
                element_override o{};
                o.path = r.path();
                o.hidden = r.u8v() != 0;
                o.attributes = r.attributes();
                data.overrides.push_back(std::move(o));
            }

            for (u32 i = 0, n = r.count(); i < n; ++i) {
                policy_binding b{};
                b.policy = r.str();
                if (r.u8v())
                    b.scope = r.scope();
                data.policies.push_back(std::move(b));
            }

            for (u32 i = 0, n = r.count(); i < n; ++i) {
                std::string hook = r.str();
                data.compositions.emplace_back(std::move(hook), static_cast<composition>(r.u8v()));
            }

            data.origin = r.raw<glm::dvec3>();

            if (version >= 2)
                for (u32 i = 0, n = r.count(); i < n; ++i) {
                    generator_instance g{};
                    g.id = r.u32v();
                    g.type = r.str();
                    g.label = r.str();
                    g.enabled = r.u8v() != 0;
                    for (u32 k = 0, m = r.count(); k < m; ++k)
                        g.inputs.push_back(r.u32v());
                    g.seed = r.u32v();
                    for (u32 k = 0, m = r.count(); k < m; ++k) {
                        std::string name = r.str();
                        g.settings.emplace_back(std::move(name), r.setting());
                    }
                    data.generators.push_back(std::move(g));
                }
            if (!r.done())
                return std::unexpected(std::string("trailing bytes after the construct data"));
        } catch (const read_error& e) {
            return std::unexpected(e.message);
        }
        return data;
    }


    u64 content_hash(const construct_data& data) {

        const std::vector<std::byte> bytes = serialize(data);
        return fnv1a_64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

}
