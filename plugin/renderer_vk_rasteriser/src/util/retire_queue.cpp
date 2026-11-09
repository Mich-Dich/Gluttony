#include "util/pch.h"
#include "util/retire_queue.h"



namespace GLT::renderer::vk_rasterizer::util {

    // CLASS PUBLIC ====================================================================================================

    retire_queue::~retire_queue() {

        if (m_timeline || !m_pending.empty())
            LOG(warn, "retire_queue destroyed without shutdown() ({} pending callbacks leaked)", m_pending.size())
    }


    void retire_queue::init(vk::Device device) {

        m_device = device;

        vk::SemaphoreTypeCreateInfo type_ci{};
        type_ci.semaphoreType = vk::SemaphoreType::eTimeline;
        type_ci.initialValue = 0;

        vk::SemaphoreCreateInfo sem_ci{};
        sem_ci.pNext = &type_ci;
        m_timeline = m_device.createSemaphore(sem_ci);
        m_last_submitted = 0;
    }


    void retire_queue::shutdown() {

        flush();
        if (m_timeline) {
            m_device.destroySemaphore(m_timeline);
            m_timeline = nullptr;
        }
        m_device = nullptr;
    }


    void retire_queue::retire(std::function<void()>&& fn) {

        if (!fn)
            return;

        // Nothing submitted yet -> nothing can reference the object
        if (!m_timeline || m_last_submitted == 0) {
            fn();
            return;
        }
        m_pending.push_back({ m_last_submitted, std::move(fn) });
    }


    void retire_queue::collect() {

        if (m_pending.empty())
            return;

        const u64 done = completed_value();
        while (!m_pending.empty() && m_pending.front().value <= done) {
            auto fn = std::move(m_pending.front().fn);
            m_pending.pop_front();
            fn();           // may call retire() again; it appends to the back, which is safe with a deque
        }
    }


    void retire_queue::flush() {

        while (!m_pending.empty()) {
            auto fn = std::move(m_pending.front().fn);
            m_pending.pop_front();
            fn();
        }
    }


    u64 retire_queue::completed_value() const {

        if (!m_timeline)
            return UINT64_MAX;
        return m_device.getSemaphoreCounterValue(m_timeline);
    }

}
