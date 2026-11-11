
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

    void scene_renderer::submit_scene(std::span<const GLT::asset::mesh::instance> instances) {

        m_scene_instances.assign(instances.begin(), instances.end());
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::add_depth_prepass_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_depth_prepass_pass");

        m_graph->add_pass("depth_prepass",
            [this](graph::pass_builder& builder) {
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
            },
            [this](graph::pass_context& ctx) {

                // Always begin/end so the attachments are cleared: the tonemap pass reads depth (background mask) and HDR
                // colour even when the scene is empty
                ctx.begin_rendering();
                if (m_instance_count == 0) {
                    ctx.end_rendering();
                    return;
                }
                ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);

                m_plugin.get_dev()->bind_descriptor_buffer({m_frame_desc_buffers[m_current_frame], m_material_desc_buffers[m_current_frame]},
                    ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 0, 0, 0, ctx.cmd,
                    vk::PipelineBindPoint::eGraphics);

                const vk::Buffer cmd_buf = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                const vk::Buffer cnt_buf = ctx.get_buffer(m_draw_count_handles[m_current_frame]);
                constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);

                // ---- Asset-backed (culled) draws -----------------------------------------------
                for (u32 slot : m_scene_materials) {
                    const glm::uvec2 range = m_material_ranges[slot];
                    if (range.y == 0)
                        continue;                               // no culled draws use this material (e.g. procedural only)

                    const auto& entry = m_shared->get_material(slot);
                    if (!entry.alive || !entry.depth_pipeline)
                        continue;

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.depth_pipeline);
                    const u32 offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(slot);
                    m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1, offset, ctx.cmd,
                        vk::PipelineBindPoint::eGraphics);
                    ctx.cmd.drawIndexedIndirectCount(
                        cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                        cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32),
                        range.y, stride);
                }

                // ---- Procedural draws ---------------------------------------------------------
                for (size_t i = 0; i < m_procedural_draw_slots.size(); ++i) {

                    const u32 pm_slot = m_procedural_draw_slots[i];
                    const auto& pm = m_procedural_meshes[pm_slot];
                    if (!pm.alive || !pm.vertex_buffer.buffer || !pm.index_buffer.buffer)
                        continue;

                    const auto& entry = m_shared->get_material(pm.material_slot);
                    if (!entry.alive || !entry.depth_pipeline || entry.is_translucent())
                        continue;                               // translucent procedurals: translucent pass

                    ctx.cmd.bindVertexBuffers(0, { pm.vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(pm.index_buffer.buffer, 0, vk::IndexType::eUint32);

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.depth_pipeline);
                    const u32 offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(pm.material_slot);
                    m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1, offset, ctx.cmd,
                        vk::PipelineBindPoint::eGraphics);

                    const u32 base_instance = m_asset_instance_count + static_cast<u32>(i);
                    ctx.cmd.drawIndexed(pm.index_count, 1, 0, 0, base_instance);
                }

                ctx.end_rendering();
            });
    }


    void scene_renderer::add_scene_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_scene_pass");

        m_graph->add_pass("scene",
            [&](graph::pass_builder& builder) {
                // Linear HDR target. Background pixels are replaced by the clear colour in the tonemap pass (depth == 1)
                // alpha cleared to 0: translucents accumulate coverage there, the tonemap pass composites with it
                builder.color_attachment(m_scene_hdr, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{0.f, 0.f, 0.f, 0.f}));
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_light_grid_handles[m_current_frame], graph::buffer_usage::storage_read);
                builder.read(m_light_index_list_handles[m_current_frame], graph::buffer_usage::storage_read);
                if (m_shadow.active)
                    builder.read(m_shadow_map, graph::texture_usage::depth_sampled);
                builder.read(m_env_specular, graph::texture_usage::sampled);      // image based lighting
                builder.read(m_env_sh, graph::buffer_usage::storage_read);
                if (m_local_shadow_view_count > 0)
                    builder.read(m_shadow_atlas, graph::texture_usage::depth_sampled);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                // Always begin/end so the attachments are cleared: the tonemap pass reads depth (background mask) and HDR
                // colour even when the scene is empty
                ctx.begin_rendering();
                if (m_instance_count == 0) {
                    ctx.end_rendering();
                    return;
                }
                ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);

                m_plugin.get_dev()->bind_descriptor_buffer({m_frame_desc_buffers[m_current_frame], m_material_desc_buffers[m_current_frame]},
                    ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                const vk::Buffer cmd_buf = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                const vk::Buffer cnt_buf = ctx.get_buffer(m_draw_count_handles[m_current_frame]);
                constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);

                // ---- Asset-backed (culled) draws -----------------------------------------------
                for (u32 slot : m_scene_materials) {
                    const glm::uvec2 range = m_material_ranges[slot];
                    if (range.y == 0)
                        continue;                               // no culled draws use this material (e.g. procedural only)

                    const auto& entry = m_shared->get_material(slot);
                    if (!entry.alive || !entry.pipeline)
                        continue;

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);
                    const u32 offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(slot);
                    m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1, offset, ctx.cmd,
                        vk::PipelineBindPoint::eGraphics);
                    ctx.cmd.drawIndexedIndirectCount(
                        cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                        cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32),
                        range.y, stride);
                }

                // ---- Procedural draws ---------------------------------------------------------
                for (size_t i = 0; i < m_procedural_draw_slots.size(); ++i) {

                    const u32 pm_slot = m_procedural_draw_slots[i];
                    const auto& pm = m_procedural_meshes[pm_slot];
                    if (!pm.alive || !pm.vertex_buffer.buffer || !pm.index_buffer.buffer)
                        continue;

                    const auto& entry = m_shared->get_material(pm.material_slot);
                    if (!entry.alive || !entry.pipeline || entry.is_translucent())
                        continue;                               // translucent procedurals: translucent pass

                    ctx.cmd.bindVertexBuffers(0, { pm.vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(pm.index_buffer.buffer, 0, vk::IndexType::eUint32);

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);
                    const u32 offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(pm.material_slot);
                    m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1, offset, ctx.cmd,
                        vk::PipelineBindPoint::eGraphics);

                    const u32 base_instance = m_asset_instance_count + static_cast<u32>(i);
                    ctx.cmd.drawIndexed(pm.index_count, 1, 0, 0, base_instance);
                }

                ctx.end_rendering();
            });
    }


    // Translucent geometry, after all opaque geometry and before tonemapping. Same lighting as the scene pass (same
    // descriptors, Forward+ clusters, shadows), blended into scene_hdr, depth tested against the opaque depth but not
    // written. Draws are the CPU-culled, back-to-front sorted list from prepare_translucent_draws()
    void scene_renderer::add_translucent_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_translucent_pass");

        if (m_translucent_draws.empty())
            return;

        m_graph->add_pass("translucent",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_hdr, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_light_grid_handles[m_current_frame], graph::buffer_usage::storage_read);
                builder.read(m_light_index_list_handles[m_current_frame], graph::buffer_usage::storage_read);
                if (m_shadow.active)
                    builder.read(m_shadow_map, graph::texture_usage::depth_sampled);
                builder.read(m_env_specular, graph::texture_usage::sampled);      // image based lighting
                builder.read(m_env_sh, graph::buffer_usage::storage_read);
                if (m_local_shadow_view_count > 0)
                    builder.read(m_shadow_atlas, graph::texture_usage::depth_sampled);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                ctx.begin_rendering();

                m_plugin.get_dev()->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame], m_material_desc_buffers[m_current_frame] },
                    ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                vk::Pipeline bound_pipeline = nullptr;
                u32 bound_material = UINT32_MAX;
                bool shared_geometry_bound = false;

                for (const translucent_draw& draw : m_translucent_draws) {

                    const auto& entry = m_shared->get_material(draw.material);
                    if (!entry.alive || !entry.pipeline)
                        continue;

                    if (entry.pipeline != bound_pipeline) {
                        ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);
                        bound_pipeline = entry.pipeline;
                    }
                    if (draw.material != bound_material) {
                        const u32 offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(draw.material);
                        m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1, offset, ctx.cmd,
                            vk::PipelineBindPoint::eGraphics);
                        bound_material = draw.material;
                    }

                    if (draw.procedural) {

                        const auto& pm = m_procedural_meshes[draw.mesh];
                        if (!pm.alive || !pm.vertex_buffer.buffer || !pm.index_buffer.buffer)
                            continue;
                        ctx.cmd.bindVertexBuffers(0, { pm.vertex_buffer.buffer }, { 0 });
                        ctx.cmd.bindIndexBuffer(pm.index_buffer.buffer, 0, vk::IndexType::eUint32);
                        shared_geometry_bound = false;
                        ctx.cmd.drawIndexed(pm.index_count, 1, 0, 0, draw.instance);

                    } else {

                        const mesh_slot* slot = m_shared->get_mesh(draw.mesh);
                        const size_t range_index = static_cast<size_t>(draw.lod) * (slot ? slot->submesh_count : 0u) + draw.submesh;
                        if (!slot || !slot->alive || draw.submesh >= slot->submesh_count || range_index >= slot->submesh_ranges.size())
                            continue;
                        const glm::uvec2 range = slot->submesh_ranges[range_index];        // {first_index, index_count}
                        if (range.y < 3)
                            continue;

                        if (!shared_geometry_bound) {
                            ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                            ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);
                            shared_geometry_bound = true;
                        }
                        ctx.cmd.drawIndexed(range.y, 1, slot->index_offset + range.x, static_cast<i32>(slot->vertex_offset),
                            draw.instance);
                    }
                }

                ctx.end_rendering();
            });
    }

}
