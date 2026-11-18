#include "procedural/construct/types.h"

#include <charconv>
#include <deque>
#include <glm/gtc/matrix_transform.hpp>



namespace GLT::construct {

    // STATIC VARIABLES ================================================================================================

    namespace {

        struct intern_table {

            std::shared_mutex                           mutex{};
            std::unordered_map<u64, const char*>        by_hash{};
            std::deque<std::string>                     storage{};          // deque: pointers stay valid
        };

        intern_table& table() {
            static intern_table instance{};
            return instance;
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    name_id intern(std::string_view name) {

        if (name.empty())
            return {};

        const u64 hash = fnv1a_64(name);
        intern_table& t = table();
        {
            std::shared_lock lock(t.mutex);
            if (auto it = t.by_hash.find(hash); it != t.by_hash.end())
                return { hash, it->second };
        }
        std::unique_lock lock(t.mutex);
        if (auto it = t.by_hash.find(hash); it != t.by_hash.end())
            return { hash, it->second };
        const char* text = t.storage.emplace_back(name).c_str();
        t.by_hash.emplace(hash, text);
        return { hash, text };
    }


    f64 as_number(const attribute_value& value) noexcept {

        return std::visit([](const auto& v) -> f64 {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>)                                   return v ? 1.0 : 0.0;
            else if constexpr (std::is_arithmetic_v<T>)                              return static_cast<f64>(v);
            else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
                                                                                     return static_cast<f64>(v.x);
            else                                                                     return 0.0;
        }, value);
    }


    std::string to_string(const attribute_value& value) {

        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)                         return "<none>";
            else if constexpr (std::is_same_v<T, bool>)                              return v ? "true" : "false";
            else if constexpr (std::is_arithmetic_v<T>)                              return std::format("{}", v);
            else if constexpr (std::is_same_v<T, glm::vec2>)                         return std::format("({}, {})", v.x, v.y);
            else if constexpr (std::is_same_v<T, glm::vec3>)                         return std::format("({}, {}, {})", v.x, v.y, v.z);
            else if constexpr (std::is_same_v<T, glm::vec4>)                         return std::format("({}, {}, {}, {})", v.x, v.y, v.z, v.w);
            else if constexpr (std::is_same_v<T, glm::quat>)                         return std::format("q({}, {}, {}, {})", v.w, v.x, v.y, v.z);
            else if constexpr (std::is_same_v<T, name_id>)                           return std::string(v.str());
            else                                                                     return v;
        }, value);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // frame -----------------------------------------------------------------------------------------------------------

    glm::mat4 frame::matrix_relative_to(const glm::dvec3& base) const noexcept {

        const glm::vec3 offset(origin - base);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), offset);
        m = m * glm::mat4_cast(rotation);
        m = glm::scale(m, scale);
        return m;
    }


    frame frame::compose(const frame& child) const noexcept {

        frame out{};
        out.origin = origin + glm::dvec3(rotation * (glm::vec3(child.origin) * scale));
        out.rotation = glm::normalize(rotation * child.rotation);
        out.scale = scale * child.scale;
        return out;
    }

    // element_path ----------------------------------------------------------------------------------------------------

    element_path element_path::root(std::string_view name, i64 index) {

        element_path p{};
        p.m_segments.push_back({ intern(name), index });
        p.rehash();
        return p;
    }


    element_path element_path::child(std::string_view name, i64 index) const { return child(intern(name), index); }


    element_path element_path::child(name_id name, i64 index) const {

        element_path p = *this;
        p.m_segments.push_back({ name, index });
        p.rehash();
        return p;
    }


    element_path element_path::parent() const {

        element_path p = *this;
        if (!p.m_segments.empty())
            p.m_segments.pop_back();
        p.rehash();
        return p;
    }


    bool element_path::is_prefix_of(const element_path& other) const noexcept {

        if (m_segments.size() > other.m_segments.size())
            return false;
        return std::equal(m_segments.begin(), m_segments.end(), other.m_segments.begin());
    }


    std::string element_path::to_string() const {

        std::string out;
        for (size_t i = 0; i < m_segments.size(); ++i) {
            if (i)
                out += '/';
            out += m_segments[i].name.str();
            if (m_segments[i].index >= 0)
                out += std::format("[{}]", m_segments[i].index);
        }
        return out;
    }


    std::expected<element_path, parse_error> element_path::parse(std::string_view text) {

        element_path p{};
        while (!text.empty()) {
            const size_t slash = text.find('/');
            std::string_view segment = text.substr(0, slash);
            text = slash == std::string_view::npos ? std::string_view() : text.substr(slash + 1);

            i64 index = -1;
            if (const size_t open = segment.find('['); open != std::string_view::npos) {
                if (segment.back() != ']')
                    return std::unexpected(parse_error{ std::format("missing ']' in segment [{}]", segment) });
                const std::string_view digits = segment.substr(open + 1, segment.size() - open - 2);
                const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
                if (ec != std::errc() || ptr != digits.data() + digits.size() || index < 0)
                    return std::unexpected(parse_error{ std::format("bad index in segment [{}]", segment) });
                segment = segment.substr(0, open);
            }
            if (segment.empty())
                return std::unexpected(parse_error{ "empty path segment" });
            p.m_segments.push_back({ intern(segment), index });
        }
        p.rehash();
        return p;
    }


    std::strong_ordering element_path::operator<=>(const element_path& other) const noexcept {

        const size_t n = std::min(m_segments.size(), other.m_segments.size());
        for (size_t i = 0; i < n; ++i) {
            const path_segment& a = m_segments[i];
            const path_segment& b = other.m_segments[i];
            if (a.name != b.name) {
                if (const auto c = a.name.str().compare(b.name.str()); c != 0)
                    return c < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
                return a.name.hash <=> b.name.hash;
            }
            if (a.index != b.index)
                return a.index <=> b.index;
        }
        return m_segments.size() <=> other.m_segments.size();
    }


    void element_path::rehash() noexcept {

        u64 h = 0x84222325cbf29ce4ull;
        for (const path_segment& s : m_segments)
            h = hash_combine(hash_combine(h, s.name.hash), static_cast<u64>(s.index + 1));
        m_hash = h;
    }

}
