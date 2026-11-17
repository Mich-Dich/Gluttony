
#include "util/pch.h"
#include "node_registry.h"



namespace GLT::terrain {

    void node_registry::add(std::string_view owner, node_type type) {

        VALIDATE(!type.type_name.empty() && type.create && type.apply.size() == type.params.size(), return, "",
            "terrain node type from [{}] is incomplete (name, create or parameters missing)", (owner))

        std::unique_lock lock(m_mutex);
        std::string name = type.type_name;
        if (m_types.contains(name))
            LOG(warn, "terrain node type [{}] registered again (by [{}]): replacing it", name, owner)
        m_types[std::move(name)] = entry{ std::string(owner), std::make_shared<const node_type>(std::move(type)) };
        m_generation.fetch_add(1, std::memory_order_acq_rel);
    }


    void node_registry::remove_owner(std::string_view owner) {

        std::unique_lock lock(m_mutex);
        std::erase_if(m_types, [owner](const auto& item) { return item.second.owner == owner; });
        m_generation.fetch_add(1, std::memory_order_acq_rel);
    }


    std::shared_ptr<const node_type> node_registry::find(std::string_view type_name) const {

        std::shared_lock lock(m_mutex);
        if (auto it = m_types.find(type_name); it != m_types.end())
            return it->second.type;
        return nullptr;
    }


    std::vector<std::shared_ptr<const node_type>> node_registry::all() const {

        std::shared_lock lock(m_mutex);
        std::vector<std::shared_ptr<const node_type>> out;
        out.reserve(m_types.size());
        for (const auto& [name, item] : m_types)
            out.push_back(item.type);
        return out;
    }


    void node_registry::clear() {

        std::unique_lock lock(m_mutex);
        m_types.clear();
        m_generation.fetch_add(1, std::memory_order_acq_rel);
    }

}
