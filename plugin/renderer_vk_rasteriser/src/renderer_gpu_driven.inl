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

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // mesh ssbo packing -----------------------------------------------------------------------------------------------

    void renderer::pack_mesh_into_ssbo(mesh_slot& mesh_slot, const GLT::asset::mesh::mesh_asset& mesh_asset) {

        // mesh_slot.vertex_offset and .index_offset are absolute offsets into the shared vertex/index buffers; the shader uses them directly
        u32 mesh_index = static_cast<u32>(&mesh_slot - m_mesh_slots.data());
        VALIDATE(mesh_index < MAX_MESHES, return, "", "Mesh SSBO capacity exceeded");

        // Compute the local-space AABB from the mesh's vertices
        glm::vec3 local_min(std::numeric_limits<f32>::max());
        glm::vec3 local_max(std::numeric_limits<f32>::lowest());
        for (const auto& vertex : mesh_asset.vertices) {
            local_min = glm::min(local_min, vertex.position);
            local_max = glm::max(local_max, vertex.position);
        }

        // Claim a contiguous run of submesh slots
        const u32 submesh_count = static_cast<u32>(std::min<size_t>(mesh_asset.submeshes.size(), MAX_SUBMESHES_PER_MESH));
        VALIDATE(m_submesh_used + submesh_count <= MAX_SUBMESHES, return, "", "Submesh SSBO capacity exceeded");

        const u32 first_submesh = m_submesh_used;
        m_submesh_used += submesh_count;

        // Write the per-submesh entries
        auto* submesh_mapped = static_cast<gpu_submesh_data*>(m_dev->map_buffer(m_submesh_data_buffer));
        for (u32 i = 0; i < submesh_count; ++i) {
            submesh_mapped[first_submesh + i].first_index = mesh_asset.submeshes[i].first_index;
            submesh_mapped[first_submesh + i].index_count = mesh_asset.submeshes[i].index_count;
        }
        m_dev->unmap_buffer(m_submesh_data_buffer);

        // Write the per-mesh entry
        gpu_mesh_data mesh_data{};
        mesh_data.local_min = local_min;
        mesh_data.local_max = local_max;
        mesh_data.first_submesh = first_submesh;
        mesh_data.submesh_count = submesh_count;
        mesh_data.vertex_offset = mesh_slot.vertex_offset;
        mesh_data.index_offset = mesh_slot.index_offset;

        auto* mesh_mapped = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
        mesh_mapped[mesh_index] = mesh_data;
        m_dev->unmap_buffer(m_mesh_data_buffer);
    }


    void renderer::clear_mesh_ssbo_entry(mesh_slot& mesh_slot) {

        u32 mesh_index = static_cast<u32>(&mesh_slot - m_mesh_slots.data());
        if (mesh_index >= MAX_MESHES)
            return;

        // Zero the mesh entry so the cull shader sees submesh_count == 0 and emits nothing even if an instance somehow still references this slot
        gpu_mesh_data empty{};
        auto* mesh_mapped = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
        mesh_mapped[mesh_index] = empty;
        m_dev->unmap_buffer(m_mesh_data_buffer);

        // Submesh slots are not reclaimed. This is a hole — acceptable until mesh churn becomes heavy; then switch to a proper free list
    }

    // cull pipeline ---------------------------------------------------------------------------------------------------

    void renderer::create_cull_pipeline() {

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(2, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(3, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(4, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(5, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(6, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute, 1, nullptr);

        m_cull_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts    = &m_cull_set_layout;
        m_cull_pipeline_layout = m_device.createPipelineLayout(pipeline_layout_info);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "cull.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile cull compute shader");

        vk::ShaderModule compute_shader = m_dev->create_shader_module(compute_spv);

        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage  = vk::ShaderStageFlagBits::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName  = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.layout = m_cull_pipeline_layout;
        pipeline_info.stage  = shader_stage;

        auto result = m_device.createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create cull pipeline");
        m_cull_pipeline = result.value;
        m_live_pipeline_count++;

        m_device.destroyShaderModule(compute_shader);

        // Per-frame draw command + draw count buffers. Descriptor buffers are populated later, by refresh_hiz_descriptors(), 
        // once the pyramid's texture and views are known
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            m_draw_command_buffers[frame_index] = m_dev->create_buffer(
                MAX_DRAW_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            m_draw_count_buffers[frame_index] = m_dev->create_buffer(
                sizeof(u32),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst,
                0);
            m_live_buffer_count++;
        }
    }


    void renderer::destroy_cull_pipeline() {

        m_device.waitIdle();

        for (auto& draw_command_buffer : m_draw_command_buffers)
            if (draw_command_buffer.buffer)
                m_dev->destroy_buffer(draw_command_buffer);

        for (auto& draw_count_buffer : m_draw_count_buffers)
            if (draw_count_buffer.buffer)
                m_dev->destroy_buffer(draw_count_buffer);

        if (m_cull_pipeline)
            m_device.destroyPipeline(m_cull_pipeline);

        if (m_cull_pipeline_layout)
            m_device.destroyPipelineLayout(m_cull_pipeline_layout);

        if (m_cull_set_layout)
            m_device.destroyDescriptorSetLayout(m_cull_set_layout);

        m_cull_pipeline = nullptr;
        m_cull_pipeline_layout = nullptr;
        m_cull_set_layout = nullptr;
    }

    // frame-time operations -------------------------------------------------------------------------------------------

    void renderer::build_instance_buffer() {

        if (m_scene_instances.empty())
            return;

        std::vector<gpu_instance_data> packed;
        packed.reserve(std::min<size_t>(m_scene_instances.size(), MAX_INSTANCES));

        for (const auto& scene_instance : m_scene_instances) {

            if (scene_instance.mesh == INVALID_HANDLE)
                continue;

            const mesh_slot* mesh_slot = find_slot(scene_instance.mesh);
            if (!mesh_slot || !mesh_slot->alive)
                continue;

            if (packed.size() >= MAX_INSTANCES) {
                LOG(warn, "Instance SSBO capacity exceeded ({} instances); scene truncated", MAX_INSTANCES);
                break;
            }

            const u32 mesh_index = static_cast<u32>(mesh_slot - m_mesh_slots.data());

            gpu_instance_data instance_data{};
            instance_data.model = scene_instance.transform;

            const glm::mat3 upper_left(scene_instance.transform);
            instance_data.normal_matrix = glm::mat4(glm::transpose(glm::inverse(upper_left)));
            instance_data.mesh_index = mesh_index;
            instance_data.material_index = 0;

            packed.push_back(instance_data);
        }

        m_instance_count = static_cast<u32>(packed.size());

        if (packed.empty())
            return;

        void* mapped = m_dev->map_buffer(m_instance_buffers[m_current_frame]);
        std::memcpy(mapped, packed.data(), packed.size() * sizeof(gpu_instance_data));
        m_dev->unmap_buffer(m_instance_buffers[m_current_frame]);
    }


    void renderer::dispatch_culling(vk::CommandBuffer cmd) {

        auto& draw_count_buffer = m_draw_count_buffers[m_current_frame];

        cmd.fillBuffer(draw_count_buffer.buffer, 0, sizeof(u32), 0);

        // Barrier: fill (transfer) -> dispatch (compute). This is intra-pass, the graph can't express it, so we emit it manually
        {
            vk::MemoryBarrier2 barrier{};
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
            barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;

            vk::DependencyInfo dependency_info{};
            dependency_info.memoryBarrierCount = 1;
            dependency_info.pMemoryBarriers = &barrier;
            cmd.pipelineBarrier2(dependency_info);
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cull_pipeline);
        m_dev->bind_descriptor_buffer({ m_cull_desc_buffers[m_current_frame] }, cmd);
        m_dev->bind_descriptor_set(m_cull_pipeline_layout, 0, 0, 0, cmd, vk::PipelineBindPoint::eCompute);

        const u32 workgroup_size = 64;
        const u32 dispatch_count = (m_instance_count + workgroup_size - 1) / workgroup_size;
        cmd.dispatch(dispatch_count, 1, 1);
    }


    void renderer::draw_scene_indirect(vk::CommandBuffer cmd) {

        if (m_instance_count == 0)
            return;

        cmd.bindVertexBuffers(0, { m_vertex_buffer.buffer }, { 0 });
        cmd.bindIndexBuffer(m_index_buffer.buffer, 0, vk::IndexType::eUint32);

        cmd.drawIndexedIndirectCount(
            m_draw_command_buffers[m_current_frame].buffer, 0,
            m_draw_count_buffers[m_current_frame].buffer, 0,
            MAX_DRAW_COMMANDS, sizeof(vk::DrawIndexedIndirectCommand));

        m_frame_draw_calls++;
    }

}
