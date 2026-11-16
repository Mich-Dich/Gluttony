#include "util/pch.h"
#include <debug/trace.h>
#include "util/util_pch.h"
#include "shared_resource_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // Staging allocations start at this alignment (memcpy speed only: buffer copies have no alignment requirement)
    static constexpr u64                                STAGING_ALIGNMENT = 16;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // How mesh data reaches the GPU
    //
    // Every mesh buffer (vertex / index / submesh / cluster pools, the two mesh tables) is device-local: on a discrete GPU
    // that is VRAM, the fastest memory for drawing, which the CPU cannot map. The CPU writes upload data into host-visible
    // staging blocks (stage()) and the copies into the mesh buffers are recorded into one upload command buffer per frame.
    // Pool growth and compaction are recorded into the same command buffer, in order, as buffer-to-buffer copies.
    //
    // flush_uploads() submits that command buffer on the graphics queue, signalling the shared retire timeline. Scene
    // renderers call it right before their own submission and wait for the returned value, so the copies are complete
    // and visible before any draw reads them.
    //
    // Hazards, and why they cannot happen:
    //  - Writes into pool ranges: always into freshly allocated ranges. Freed ranges only return to the pool through the
    //    retire queue, i.e. after the GPU finished every frame that could draw them (and while a recording is open,
    //    retire() holds callbacks back until that recording is submitted).
    //  - Two writes to the same range within one recording: pools as above (a range is freed and reused at most once per
    //    submission); the mesh tables are written from their CPU copies once per flush (dirty list).
    //  - In-place mesh table writes (refresh of an existing slot, compaction) while earlier frames still read the table:
    //    upload_war_barrier() first, which waits for all earlier work on the queue. Only frames that rewrite a published
    //    entry pay for it.
    //  - Copies into a buffer that growth replaces later in the same recording: upload_transfer_barrier() before and after
    //    every buffer-to-buffer copy, so earlier staged writes are in the old buffer before it is copied.
    //  - Growth / compaction reading what an earlier upload submission wrote: every recording starts with a transfer
    //    barrier, whose first scope includes the transfers of earlier submissions on the queue.

    // CLASS PUBLIC ====================================================================================================

    void shared_resource_manager::retire(std::function<void()>&& fn) {

        if (!fn)
            return;

        // The open upload command buffer may reference the object (e.g. the old buffer of a pool that just grew is the
        // source of a recorded copy). Its submission has no retire value yet, so tagging now would be too early
        if (m_upload_recording) {
            m_upload_deferred_retires.push_back(std::move(fn));
            return;
        }
        m_retire.retire(std::move(fn));
    }


    u64 shared_resource_manager::flush_uploads() {
        GLT_PROFILE_SCOPE("shared_resource_manager::flush_uploads");

        if (!m_upload_recording)
            return m_last_upload_value;

        emit_mesh_table_uploads();

        // Make the host writes into the staging blocks visible to the device (no-op on coherent memory)
        for (auto& block : m_staging_blocks)
            if (block.in_recording && block.buffer.allocation)
                vmaFlushAllocation(m_dev->get_allocator(), block.buffer.allocation, 0, VK_WHOLE_SIZE);

        m_upload_recording.end();

        const u64 value = m_retire.next_signal_value();

        vk::CommandBufferSubmitInfo cbsi{};
        cbsi.commandBuffer = m_upload_recording;

        vk::SemaphoreSubmitInfo signal{};
        signal.semaphore = m_retire.timeline();
        signal.value = value;
        signal.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

        vk::SubmitInfo2 si{};
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cbsi;
        si.signalSemaphoreInfoCount = 1;
        si.pSignalSemaphoreInfos = &signal;

        bool submitted = true;
        try {
            m_graphics_queue.submit2(si);
        } catch (const std::exception& e) {
            // Not recoverable: slots are already published and buffers swapped, frames would draw from VRAM that never got
            // its data. In practice this is a lost device
            submitted = false;
            LOG(fatal, "mesh upload submission failed (device lost?): {}", e.what());
        }

        m_upload_commands[m_upload_recording_index].value = value;
        m_upload_recording = nullptr;

        // Staging blocks: free for reuse once [value] is reached. Dedicated ones are destroyed then
        m_staging_current = -1;
        for (size_t i = 0; i < m_staging_blocks.size(); ) {

            auto& block = m_staging_blocks[i];
            if (!block.in_recording) {
                ++i;
                continue;
            }
            block.in_recording = false;
            block.last_use = value;
            if (!block.dedicated) {
                ++i;
                continue;
            }

            m_retire.retire([dev = m_dev, dead = block.buffer]() mutable {
                dev->unmap_buffer(dead);
                dev->destroy_buffer(dead);
            });
            m_staging_blocks.erase(m_staging_blocks.begin() + static_cast<std::ptrdiff_t>(i));
        }

        // Everything retired while recording: now tagged with this submission (or a later one)
        for (auto& fn : m_upload_deferred_retires)
            m_retire.retire(std::move(fn));
        m_upload_deferred_retires.clear();

        GLT_PROFILE_PLOT("Mesh upload (KB)", static_cast<int64_t>(m_upload_bytes_this_frame / 1024));
        m_upload_bytes_this_frame = 0;

        // Table entries that could not be staged (out of staging memory) go out with the next flush. They may be
        // in-place rewrites, so that flush waits for the frames in flight
        if (!m_mesh_table_dirty.empty()) {
            (void)upload_cmd();
            m_upload_war_pending = true;
        }

        if (submitted)
            m_last_upload_value = value;
        return m_last_upload_value;
    }

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::create_upload_context() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_upload_context");

        vk::CommandPoolCreateInfo cpi{};
        cpi.queueFamilyIndex = m_graphics_family;
        cpi.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_upload_pool = m_device.createCommandPool(cpi);
    }


    void shared_resource_manager::destroy_upload_context() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_upload_context");

        // Called with the device idle. A recording that was never submitted is simply dropped (nothing reads it)
        if (m_upload_recording) {
            m_upload_recording.end();
            m_upload_recording = nullptr;
        }
        for (auto& fn : m_upload_deferred_retires)
            m_retire.retire(std::move(fn));
        m_upload_deferred_retires.clear();

        for (auto& block : m_staging_blocks) {
            m_dev->unmap_buffer(block.buffer);
            m_dev->destroy_buffer(block.buffer);
        }
        m_staging_blocks.clear();
        m_staging_current = -1;

        if (m_upload_pool) {
            m_device.destroyCommandPool(m_upload_pool);     // frees the command buffers
            m_upload_pool = nullptr;
        }
        m_upload_commands.clear();
        m_last_upload_value = 0;
    }


    vk::CommandBuffer shared_resource_manager::upload_cmd() {

        if (m_upload_recording)
            return m_upload_recording;

        // A command buffer whose last submission has completed, or a new one
        const u64 done = m_retire.completed_value();
        size_t index = m_upload_commands.size();
        for (size_t i = 0; i < m_upload_commands.size(); ++i)
            if (m_upload_commands[i].value <= done) {
                index = i;
                break;
            }

        if (index == m_upload_commands.size()) {
            vk::CommandBufferAllocateInfo cai{};
            cai.commandPool = m_upload_pool;
            cai.level = vk::CommandBufferLevel::ePrimary;
            cai.commandBufferCount = 1;
            m_upload_commands.push_back({ m_device.allocateCommandBuffers(cai)[0], 0 });
        }

        m_upload_recording_index = index;
        m_upload_recording = m_upload_commands[index].cmd;
        m_upload_recording.reset();
        m_upload_recording.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        // Earlier upload submissions may still be writing (nothing orders two submissions on a queue by itself). Growth and
        // compaction below read what they wrote: this barrier's first scope covers all earlier submitted transfers
        upload_transfer_barrier();
        return m_upload_recording;
    }


    std::byte* shared_resource_manager::stage(const util::allocated_buffer& dst, vk::DeviceSize dst_offset, vk::DeviceSize size) {

        if (size == 0 || !dst.buffer)
            return nullptr;

        VALIDATE(dst.size == 0 || dst_offset + size <= dst.size, return nullptr, "",
            "staged upload of {} bytes at {} exceeds the destination buffer ({} bytes)", (size, dst_offset, dst.size))

        const vk::CommandBuffer cmd = upload_cmd();
        const u64 done = m_retire.completed_value();

        auto create_block = [this](u64 capacity, bool dedicated) -> i32 {
            staging_block block{};
            block.buffer = m_dev->create_buffer(capacity, vk::BufferUsageFlagBits::eTransferSrc,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, 0, nullptr, VMA_MEMORY_USAGE_AUTO);
            if (!block.buffer.buffer)
                return -1;
            block.mapped = static_cast<std::byte*>(m_dev->map_buffer(block.buffer));
            if (!block.mapped) {
                m_dev->destroy_buffer(block.buffer);
                return -1;
            }
            block.capacity = capacity;
            block.dedicated = dedicated;
            m_staging_blocks.push_back(std::move(block));
            return static_cast<i32>(m_staging_blocks.size() - 1);
        };

        i32 index = -1;
        u64 offset = 0;

        if (size > STAGING_BLOCK_SIZE / 2) {
            index = create_block(size, true);       // large upload (world load, big mesh): its own buffer
        } else {

            // Continue the current block, else reuse a free one, else make a new one
            if (m_staging_current >= 0) {
                auto& block = m_staging_blocks[static_cast<size_t>(m_staging_current)];
                const u64 aligned = util::align_up(block.offset, STAGING_ALIGNMENT);
                if (aligned + size <= block.capacity) {
                    index = m_staging_current;
                    offset = aligned;
                }
            }
            if (index < 0) {
                for (size_t i = 0; i < m_staging_blocks.size(); ++i) {
                    auto& block = m_staging_blocks[i];
                    if (block.dedicated || block.in_recording || block.last_use > done)
                        continue;
                    block.offset = 0;
                    index = static_cast<i32>(i);
                    break;
                }
                if (index < 0)
                    index = create_block(STAGING_BLOCK_SIZE, false);
                m_staging_current = index;
            }
        }

        if (index < 0) {
            LOG(error, "no staging memory for a mesh upload of {} bytes: the mesh will be missing geometry", size);
            return nullptr;
        }

        auto& block = m_staging_blocks[static_cast<size_t>(index)];
        block.in_recording = true;
        block.offset = offset + size;
        m_upload_bytes_this_frame += size;

        vk::BufferCopy region{};
        region.srcOffset = offset;
        region.dstOffset = dst_offset;
        region.size = size;
        cmd.copyBuffer(block.buffer.buffer, dst.buffer, region);

        return block.mapped + offset;
    }


    void shared_resource_manager::upload_transfer_barrier() {

        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;

        vk::DependencyInfo dependency{};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        upload_cmd().pipelineBarrier2(dependency);
    }


    void shared_resource_manager::upload_war_barrier() {

        // Execution dependency on everything submitted earlier on this queue (frames in flight reading the tables). A
        // write-after-read hazard needs no memory dependency, only the wait
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;

        vk::DependencyInfo dependency{};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        upload_cmd().pipelineBarrier2(dependency);
    }


    void shared_resource_manager::trim_staging_blocks() {

        if (m_staging_blocks.size() <= STAGING_MAX_CACHED_BLOCKS)
            return;

        const u64 done = m_retire.completed_value();
        u32 kept = 0;
        for (size_t i = 0; i < m_staging_blocks.size(); ) {

            auto& block = m_staging_blocks[i];
            const bool idle = !block.in_recording && block.last_use <= done && static_cast<i32>(i) != m_staging_current;
            if (!idle || kept < STAGING_MAX_CACHED_BLOCKS) {
                if (idle)
                    ++kept;
                ++i;
                continue;
            }
            m_dev->unmap_buffer(block.buffer);
            m_dev->destroy_buffer(block.buffer);
            m_staging_blocks.erase(m_staging_blocks.begin() + static_cast<std::ptrdiff_t>(i));
            if (m_staging_current > static_cast<i32>(i))
                --m_staging_current;
        }
    }


    void shared_resource_manager::log_buffer_memory(std::string_view name, const util::allocated_buffer& buffer) const {

        if (!buffer.allocation)
            return;

        VkMemoryPropertyFlags flags = 0;
        vmaGetAllocationMemoryProperties(m_dev->get_allocator(), buffer.allocation, &flags);

        const bool device_local = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        const bool host_visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        const f64 mb = static_cast<f64>(buffer.size) / (1024.0 * 1024.0);

        if (device_local && !host_visible)
            LOG(info, "mesh buffer '{}': {:.1f} MB in VRAM", name, mb)
        else if (device_local)
            LOG(info, "mesh buffer '{}': {:.1f} MB in device-local, CPU-mappable memory (unified memory or ReBAR)", name, mb)
        else
            LOG(warn, "mesh buffer '{}': {:.1f} MB in system RAM, the GPU reads it over the bus (VRAM full?)", name, mb)
    }

}
