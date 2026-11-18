#include "procedural/construct/registry.h"



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    std::string_view desc_name(const any_desc& d) noexcept {

        return std::visit([](const auto& x) -> std::string_view {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, composition_override_desc>)    return x.hook;
            else                                                            return x.name;
        }, d);
    }

    namespace {

        template<typename D>
        void put(std::vector<registered<D>>& list, const D& desc, std::string_view owner, std::string_view pack, u64 order) {

            const std::string name(desc_name(any_desc(desc)));      // owned: the any_desc is a temporary
            for (registered<D>& r : list)
                if (r.pack == pack && std::string(desc_name(any_desc(*r.desc))) == name) {
                    r.desc = std::make_shared<const D>(desc);
                    r.owner = std::string(owner);
                    r.order = order;
                    return;
                }
            list.push_back({ std::make_shared<const D>(desc), std::string(owner), std::string(pack), order });
        }

        template<typename D>
        void drop_owner(std::vector<registered<D>>& list, std::string_view owner) {
            std::erase_if(list, [&](const registered<D>& r) { return r.owner == owner; });
        }

    }

    // CLASS IMPLEMENTATION ============================================================================================

    const pack_desc* registry_snapshot::find_pack(std::string_view name) const noexcept {

        for (const auto& p : packs)
            if (p.desc->name == name)
                return p.desc.get();
        return nullptr;
    }


    bool registry_snapshot::extends(std::string_view a, std::string_view b) const {

        if (a == b)
            return false;
        std::vector<std::string_view> todo{ a };
        std::vector<std::string_view> seen;
        while (!todo.empty()) {
            const std::string_view cur = todo.back();
            todo.pop_back();
            if (std::ranges::find(seen, cur) != seen.end())
                continue;
            seen.push_back(cur);
            if (const pack_desc* p = find_pack(cur))
                for (const std::string& e : p->extends) {
                    if (e == b)
                        return true;
                    todo.push_back(e);
                }
        }
        return false;
    }


    const registered<generator_desc>* registry_snapshot::find_generator(std::string_view name) const noexcept {

        const registered<generator_desc>* best = nullptr;
        for (const auto& g : generators)
            if (g.desc->name == name && (!best || g.order > best->order))
                best = &g;
        return best;
    }


    const role_desc* registry_snapshot::find_role(role_id role) const noexcept {

        const role_desc* best = nullptr;
        u64 order = 0;
        for (const auto& r : roles)
            if (role.str() == r.desc->name && (!best || r.order > order)) {
                best = r.desc.get();
                order = r.order;
            }
        return best;
    }


    const attribute_desc* registry_snapshot::find_attribute(attribute_id id) const noexcept {

        for (const auto& s : tag_schemas)
            for (const attribute_desc& a : s.desc->attributes)
                if (a.id == id)
                    return &a;
        return nullptr;
    }


    void registry::add_pack(std::string_view owner, pack_desc desc) {

        std::scoped_lock lock(m_mutex);
        const std::string pack = desc.name;
        for (auto& p : m_data.packs)
            if (p.desc->name == pack) {
                p.desc = std::make_shared<const pack_desc>(std::move(desc));
                p.owner = std::string(owner);
                ++m_data.generation;
                m_cached.reset();
                return;
            }
        m_data.packs.push_back({ std::make_shared<const pack_desc>(std::move(desc)), std::string(owner), pack, m_order++ });
        ++m_data.generation;
        m_cached.reset();
    }


    void registry::add(std::string_view owner, std::string_view pack, any_desc desc) {

        std::scoped_lock lock(m_mutex);
        const u64 order = m_order++;
        std::visit([&](const auto& d) {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, tag_schema_desc>)               put(m_data.tag_schemas, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, kind_desc>)                put(m_data.kinds, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, pass_desc>)                put(m_data.passes, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, stage_desc>)               put(m_data.stages, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, production_desc>)          put(m_data.productions, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, realizer_desc>)            put(m_data.realizers, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, finisher_desc>)            put(m_data.finishers, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, policy_desc>)              put(m_data.policies, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, composition_override_desc>) put(m_data.compositions, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, propagation_mode_desc>)    put(m_data.propagation_modes, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, generator_desc>)           put(m_data.generators, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, role_desc>)                put(m_data.roles, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, guide_template_desc>)      put(m_data.guide_templates, d, owner, pack, order);
            else if constexpr (std::is_same_v<T, preset_desc>)              put(m_data.presets, d, owner, pack, order);
        }, desc);
        ++m_data.generation;
        m_cached.reset();
    }


    void registry::remove_owner(std::string_view owner) {

        std::scoped_lock lock(m_mutex);
        drop_owner(m_data.packs, owner);
        drop_owner(m_data.tag_schemas, owner);
        drop_owner(m_data.kinds, owner);
        drop_owner(m_data.passes, owner);
        drop_owner(m_data.stages, owner);
        drop_owner(m_data.productions, owner);
        drop_owner(m_data.realizers, owner);
        drop_owner(m_data.finishers, owner);
        drop_owner(m_data.policies, owner);
        drop_owner(m_data.compositions, owner);
        drop_owner(m_data.propagation_modes, owner);
        drop_owner(m_data.generators, owner);
        drop_owner(m_data.roles, owner);
        drop_owner(m_data.guide_templates, owner);
        drop_owner(m_data.presets, owner);
        ++m_data.generation;
        m_cached.reset();
    }


    std::shared_ptr<const registry_snapshot> registry::snapshot() const {

        std::scoped_lock lock(m_mutex);
        if (!m_cached)
            m_cached = std::make_shared<const registry_snapshot>(m_data);
        return m_cached;
    }


    u64 registry::generation() const noexcept {

        std::scoped_lock lock(m_mutex);
        return m_data.generation;
    }

}
