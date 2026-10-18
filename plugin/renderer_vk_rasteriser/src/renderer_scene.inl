
#pragma once


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

    void renderer::submit_scene(std::span<const GLT::asset::mesh::instance> instances) {

        m_scene_instances.assign(instances.begin(), instances.end());
        m_scene_meshes.clear();
        m_scene_meshes.reserve(instances.size());

        for (const auto& instance : instances) {
            if (instance.mesh == INVALID_HANDLE)
                continue;
            m_scene_meshes.insert(instance.mesh);
        }

        // Schedule loads
        for (auto mesh : m_scene_meshes) {

            if (m_failed_mesh_loads.contains(mesh))
                continue;

            if (find_slot(mesh))
                continue;

            std::erase(m_pending_unloads, mesh);
            if (std::ranges::contains(m_pending_loads, mesh))
                continue;

            m_pending_loads.push_back(mesh);
        }

        // Schedule unloads
        for (auto& slot : m_mesh_slots) {

            if (!slot.alive)
                continue;
            if (m_scene_meshes.contains(slot.asset))
                continue;
            if (m_retained_meshes.contains(slot.asset))
                continue;

            std::erase(m_pending_loads, slot.asset);
            if (std::ranges::contains(m_pending_unloads, slot.asset))
                continue;

            m_pending_unloads.push_back(slot.asset);
        }
    }


    void renderer::retain_mesh(GLT::asset::handle mesh) {

        if (mesh == INVALID_HANDLE)
            return;

        m_retained_meshes.insert(mesh);
        std::erase(m_pending_unloads, mesh);

        if (!find_slot(mesh))
            m_pending_loads.push_back(mesh);
    }


    void renderer::release_mesh(GLT::asset::handle mesh) {

        if (mesh == INVALID_HANDLE)
            return;

        m_retained_meshes.erase(mesh);

        if (!m_scene_meshes.contains(mesh) && find_slot(mesh))
            m_pending_unloads.push_back(mesh);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::process_pending_meshes() {

        for (auto mesh : m_pending_unloads)
            unload_mesh(mesh);
        m_pending_unloads.clear();

        for (auto mesh : m_pending_loads) {
            if (find_slot(mesh))
                continue;
            load_mesh(mesh);
        }
        m_pending_loads.clear();
    }


    void renderer::add_depth_prepass_pass() {

        m_graph->add_pass("depth_prepass",
            [this](graph::pass_builder& builder) {
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame],    graph::buffer_usage::indirect);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.begin_rendering();
                ctx.cmd.bindVertexBuffers(0, { m_vertex_buffer.buffer }, { 0 });
                ctx.cmd.bindIndexBuffer(m_index_buffer.buffer, 0, vk::IndexType::eUint32);
                m_dev->bind_descriptor_buffer({
                    m_frame_desc_buffers[m_current_frame],
                    m_material_desc_buffers[m_current_frame]
                }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, /*bufferIndex=*/0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                const vk::Buffer cmd_buffer = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                const vk::Buffer count_buffer = ctx.get_buffer(m_draw_count_handles[m_current_frame]);
                for (u32 mat_slot : m_scene_materials) {

                    const material_slot& entry = m_material_slots[mat_slot];
                    if (!entry.alive || !entry.pipeline)
                        continue;

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.depth_pipeline);
                    const u32 descriptor_offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(mat_slot);
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 1, 1, descriptor_offset, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                    ctx.cmd.drawIndexedIndirectCount(
                        cmd_buffer,
                        static_cast<vk::DeviceSize>(mat_slot) * MAX_COMMANDS_PER_MATERIAL * sizeof(vk::DrawIndexedIndirectCommand),
                        count_buffer,
                        static_cast<vk::DeviceSize>(mat_slot) * sizeof(u32),
                        MAX_COMMANDS_PER_MATERIAL,
                        sizeof(vk::DrawIndexedIndirectCommand));
                    m_frame_draw_calls++;
                }

                ctx.end_rendering();
                m_frame_render_passes++;
            });
    }


    void renderer::add_scene_pass() {

        m_graph->add_pass("scene",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_color, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a}));
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_light_grid_handles[m_current_frame], graph::buffer_usage::storage_read);
                builder.read(m_light_index_list_handles[m_current_frame], graph::buffer_usage::storage_read);
                if (m_shadow.active)
                    builder.read(m_shadow_map, graph::texture_usage::depth_sampled);
                if (m_local_shadow_view_count > 0)
                    builder.read(m_shadow_atlas, graph::texture_usage::depth_sampled);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.begin_rendering();

                ctx.cmd.bindVertexBuffers(0, { m_vertex_buffer.buffer }, { 0 });
                ctx.cmd.bindIndexBuffer(m_index_buffer.buffer, 0, vk::IndexType::eUint32);

                m_dev->bind_descriptor_buffer({
                    m_frame_desc_buffers[m_current_frame],
                    m_material_desc_buffers[m_current_frame]
                }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, /*bufferIndex=*/0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                const vk::Buffer cmd_buffer = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                const vk::Buffer count_buffer = ctx.get_buffer(m_draw_count_handles[m_current_frame]);

                for (u32 mat_slot : m_scene_materials) {

                    const material_slot& entry = m_material_slots[mat_slot];
                    if (!entry.alive || !entry.pipeline)
                        continue;

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);

                    const u32 descriptor_offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(mat_slot);
                    // binding index 0 in the array = the material descriptor buffer we just bound
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 1, 1, descriptor_offset, ctx.cmd,
                        vk::PipelineBindPoint::eGraphics);

                    ctx.cmd.drawIndexedIndirectCount(
                        cmd_buffer,
                        static_cast<vk::DeviceSize>(mat_slot) * MAX_COMMANDS_PER_MATERIAL * sizeof(vk::DrawIndexedIndirectCommand),
                        count_buffer,
                        static_cast<vk::DeviceSize>(mat_slot) * sizeof(u32),
                        MAX_COMMANDS_PER_MATERIAL,
                        sizeof(vk::DrawIndexedIndirectCommand));
                    m_frame_draw_calls++;
                }

                ctx.end_rendering();
                m_frame_render_passes++;
            });
    }

}
