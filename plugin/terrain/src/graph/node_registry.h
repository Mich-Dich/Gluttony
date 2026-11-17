
#pragma once

#include <terrain/node.h>



namespace GLT::terrain {

    // @brief The plugin's node type registry (built-in nodes + node packs)
    class node_registry final : public i_node_registry {
    public:

        void add(std::string_view owner, node_type type) override;

        void remove_owner(std::string_view owner) override;

        [[nodiscard]] std::shared_ptr<const node_type> find(std::string_view type_name) const override;

        [[nodiscard]] std::vector<std::shared_ptr<const node_type>> all() const override;

        [[nodiscard]] u64 generation() const noexcept override { return m_generation.load(std::memory_order_acquire); }

        void clear();

    private:

        std::atomic<u64>                                m_generation{ 1 };

        struct entry {
            std::string                                 owner;
            std::shared_ptr<const node_type>            type;
        };

        mutable std::shared_mutex                       m_mutex{};
        std::map<std::string, entry, std::less<>>       m_types{};      // by type_name
    };

}
