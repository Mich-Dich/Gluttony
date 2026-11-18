#include "procedural/construct/attributes.h"



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        template<typename T>
        u64 hash_bytes(const T& v) noexcept {
            return fnv1a_64(std::string_view(reinterpret_cast<const char*>(&v), sizeof(T)));
        }

    }


    u64 hash_value(const attribute_value& value) noexcept {

        const u64 h = std::visit([](const auto& v) -> u64 {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)                return 0;
            else if constexpr (std::is_same_v<T, name_id>)                  return v.hash;
            else if constexpr (std::is_same_v<T, std::string>)              return fnv1a_64(v);
            else                                                            return hash_bytes(v);
        }, value);
        return hash_combine(static_cast<u64>(value.index()), h);
    }


    u64 hash_region(const region_selector& region) noexcept {

        const u64 h = std::visit([](const auto& r) -> u64 {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, whole_element>)                 return 0;
            else if constexpr (std::is_same_v<T, face_group>)               return r.group.hash;
            else if constexpr (std::is_same_v<T, curve_span>)               return hash_combine(hash_bytes(r.t0), hash_bytes(r.t1));
            else                                                            return static_cast<u64>(r.index);
        }, region);
        return hash_combine(static_cast<u64>(region.index()), h);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    void attribute_set::set(attribute_id id, attribute_value value, const region_selector& region) {

        for (entry& e : m_entries)
            if (e.id == id && e.region == region) {
                e.value = std::move(value);
                return;
            }
        m_entries.push_back({ id, region, std::move(value) });
    }


    void attribute_set::erase(attribute_id id, const region_selector& region) {

        std::erase_if(m_entries, [&](const entry& e) { return e.id == id && e.region == region; });
    }


    const attribute_value* attribute_set::find(attribute_id id, const region_selector& region) const noexcept {

        const attribute_value* whole = nullptr;
        for (const entry& e : m_entries) {
            if (e.id != id)
                continue;
            if (e.region == region)
                return &e.value;
            if (std::holds_alternative<whole_element>(e.region))
                whole = &e.value;
        }
        return whole;
    }


    bool attribute_set::has(attribute_id id) const noexcept {

        return std::ranges::any_of(m_entries, [&](const entry& e) { return e.id == id; });
    }


    u64 attribute_set::hash() const noexcept {

        // Order-independent over entries (sum of entry hashes): two sets with the same content hash the same
        u64 sum = 0;
        for (const entry& e : m_entries)
            sum += hash_combine(hash_combine(e.id.hash, hash_region(e.region)), hash_value(e.value));
        return sum;
    }

}
