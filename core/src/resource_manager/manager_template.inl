#pragma once

#include "util/pch.h"
#include "manager_template.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================
    
    template <typename resource_type, typename resource_specific_header, typename key_type>
    resource_manager<resource_type, resource_specific_header, key_type>::resource_manager(
        load_function loadFunc, unload_function unloadFunc, bool useBackgroundCleanup)
        : m_load(loadFunc), m_unload(unloadFunc), m_useBackgroundCleanup(useBackgroundCleanup) {}


    template <typename resource_type, typename resource_specific_header, typename key_type>
    resource_manager<resource_type, resource_specific_header, key_type>::~resource_manager() { shutdown(); }

    // TEMPLATE CLASS PUBLIC ===========================================================================================
    
    template <typename resource_type, typename resource_specific_header, typename key_type>
    void resource_manager<resource_type, resource_specific_header, key_type>::init() {

        if (m_useBackgroundCleanup && !m_running) {
            m_running = true;
            m_stop_requested = false;
            m_cleanup_thread = std::thread(&resource_manager::cleanup_thread_func, this);
        }
    }


    template <typename resource_type, typename resource_specific_header, typename key_type>
    void resource_manager<resource_type, resource_specific_header, key_type>::shutdown() {

        if (m_useBackgroundCleanup && m_running) {
            m_stop_requested = true;
            m_condition.notify_one();
            if (m_cleanup_thread.joinable())
                m_cleanup_thread.join();

            m_running = false;
        }
        release_all();
    }


    template <typename resource_type, typename resource_specific_header, typename key_type>
    typename resource_manager<resource_type, resource_specific_header, key_type>::resource_ref
    resource_manager<resource_type, resource_specific_header, key_type>::get(const key_type& key) {

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_resources.find(key);
        if (it != m_resources.end())
            return it->second;

        asset_file_header asset_header{};
        resource_specific_header specific_header{};
        resource_ref new_resource = m_load(key, asset_header, specific_header);

        if (new_resource)
            m_resources[key] = new_resource;

        return new_resource;
    }


    template <typename resource_type, typename resource_specific_header, typename key_type>
    key_type resource_manager<resource_type, resource_specific_header, key_type>::get_key_from_ref(resource_ref resource_ref) {

        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [key, resource] : m_resources)
            if (resource == resource_ref)
                return key;

        return {};
    }


    template <typename resource_type, typename resource_specific_header, typename key_type>
    bool resource_manager<resource_type, resource_specific_header, key_type>::contains(const key_type& key) const {

        std::lock_guard<std::mutex> lock(m_mutex);
        return m_resources.find(key) != m_resources.end();
    }


    template <typename resource_type, typename resource_specific_header, typename key_type>
    void resource_manager<resource_type, resource_specific_header, key_type>::release_all() {

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_unload)
            for (auto& [key, resource] : m_resources)
                m_unload(resource);

        m_resources.clear();
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    template <typename resource_type, typename resource_specific_header, typename key_type>
    void resource_manager<resource_type, resource_specific_header, key_type>::cleanup_unused() {

        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_resources.begin(); it != m_resources.end(); ) {
            if (it->second.use_count() == 1) {
                if (m_unload)
                    m_unload(it->second);

                it = m_resources.erase(it);
            } else
                ++it;
        }
    }


    template <typename resource_type, typename resource_specific_header, typename key_type>
    void resource_manager<resource_type, resource_specific_header, key_type>::cleanup_thread_func() {

        while (!m_stop_requested.load()) {
            cleanup_unused();
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait_for(lock, std::chrono::seconds(10), [this] {
                return m_stop_requested.load();
            });
        }
    }

}
