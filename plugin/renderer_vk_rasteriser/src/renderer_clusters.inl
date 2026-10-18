
#pragma once

#include <glm/glm.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // Grid + index list + global counter
    //
    // Layout:
    //   light_grid[N]        = { offset, count }  uvec2   -> start into the index list + how many entries
    //   light_index_list[M]  = u32                -> packed light indices, global atomic cursor
    //   light_counter[1]     = u32                -> next free slot in the index list
    //
    // Semantics:
    //   * The clear pass zeroes the counter, and the grid. It does NOT zero the index list — the culling
    //     shader only reads list entries that it wrote this frame, so stale tail entries are never observed
    //   * Every cluster must have a thread that writes its grid slot (even { 0, 0 } for empty clusters),
    //     otherwise the frame reads a leftover entry from the previous clear

    void renderer::create_cluster_resources() {

        const u32 cluster_count = m_cluster_params.cluster_count;
        if (cluster_count == 0)
            return;

        const vk::DeviceSize grid_bytes =
            static_cast<vk::DeviceSize>(cluster_count) * sizeof(glm::uvec2);

        // Ideal size if every cluster saturated its per-cluster ceiling, capped at a hard upper bound
        const vk::DeviceSize ideal_index_bytes =
            static_cast<vk::DeviceSize>(cluster_count) * MAX_LIGHTS_PER_CLUSTER * sizeof(u32);
        const vk::DeviceSize index_bytes = std::min<vk::DeviceSize>(
            ideal_index_bytes,
            static_cast<vk::DeviceSize>(MAX_CLUSTER_INDEX_ENTRIES) * sizeof(u32));

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            // Persistent handles are stable; the graph rebuilds them in place on resize (see create_buffer fix)
            graph::buffer_desc grid_desc{};
            grid_desc.name = "light_grid_" + std::to_string(frame_index);
            grid_desc.size = grid_bytes;
            grid_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            grid_desc.persistent = true;
            m_light_grid_handles[frame_index] = m_graph->create_buffer(grid_desc);

            graph::buffer_desc list_desc{};
            list_desc.name = "light_index_list_" + std::to_string(frame_index);
            list_desc.size = index_bytes;
            list_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            list_desc.persistent = true;
            m_light_index_list_handles[frame_index] = m_graph->create_buffer(list_desc);

            graph::buffer_desc counter_desc{};
            counter_desc.name = "light_counter_" + std::to_string(frame_index);
            counter_desc.size = sizeof(u32);
            counter_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            counter_desc.persistent = true;
            m_light_counter_handles[frame_index] = m_graph->create_buffer(counter_desc);
        }

        m_cluster_index_capacity = static_cast<u32>(index_bytes / sizeof(u32));

        LOG(trace, "Cluster grid: {}x{}x{} = {} clusters (grid {} B, list {} B)",
            m_cluster_params.grid_x, m_cluster_params.grid_y, m_cluster_params.grid_z, cluster_count, grid_bytes, index_bytes);
    }


    void renderer::destroy_cluster_resources() {

        // Handles are owned by the render graph; nothing to release here.
        // Just forget them so a later frame doesn't try to bind dangling slots.
        m_light_grid_handles.fill({});
        m_light_index_list_handles.fill({});
        m_light_counter_handles.fill({});
        m_cluster_params = {};
    }


    void renderer::update_cluster_dimensions() {

        const u32 grid_x = (static_cast<u32>(m_render_size.x) + CLUSTER_TILE_SIZE_X - 1) / CLUSTER_TILE_SIZE_X;
        const u32 grid_y = (static_cast<u32>(m_render_size.y) + CLUSTER_TILE_SIZE_Y - 1) / CLUSTER_TILE_SIZE_Y;
        const u32 grid_z = CLUSTER_DEPTH_SLICES;
        const u32 count = grid_x * grid_y * grid_z;

        if (count == m_cluster_params.cluster_count &&
            grid_x == m_cluster_params.grid_x &&
            grid_y == m_cluster_params.grid_y &&
            grid_z == m_cluster_params.grid_z)
            return;

        m_cluster_params = { grid_x, grid_y, grid_z, count };
        create_cluster_resources();
    }


    void renderer::add_clear_light_grid_pass() {

        // Clears the grid (small) and the global counter. The index list is not cleared - culling only reads offsets it wrote this frame
        m_graph->add_pass("clear_light_grid",
            [this](graph::pass_builder& builder) {
                builder.write(m_light_grid_handles[m_current_frame],    graph::buffer_usage::transfer_dst);
                builder.write(m_light_counter_handles[m_current_frame], graph::buffer_usage::transfer_dst);
            },
            [this](graph::pass_context& ctx) {

                if (m_cluster_params.cluster_count == 0)
                    return;

                const vk::DeviceSize grid_bytes =
                    static_cast<vk::DeviceSize>(m_cluster_params.cluster_count) * sizeof(glm::uvec2);

                ctx.cmd.fillBuffer(ctx.get_buffer(m_light_grid_handles[m_current_frame]),    0, grid_bytes,     0);
                ctx.cmd.fillBuffer(ctx.get_buffer(m_light_counter_handles[m_current_frame]), 0, sizeof(u32),     0);
            });
    }

}
