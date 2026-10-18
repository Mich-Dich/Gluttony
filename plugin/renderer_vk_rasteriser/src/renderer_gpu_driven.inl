
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
        VALIDATE(mesh_index < MAX_MESHES, return, "", "Mesh SSBO capacity exceeded")

        // Compute the local-space AABB from the mesh's vertices
        glm::vec3 local_min(std::numeric_limits<f32>::max());
        glm::vec3 local_max(std::numeric_limits<f32>::lowest());
        for (const auto& vertex : mesh_asset.vertices) {
            local_min = glm::min(local_min, vertex.position);
            local_max = glm::max(local_max, vertex.position);
        }

        // Claim a contiguous run of submesh slots
        const u32 submesh_count = static_cast<u32>(std::min<size_t>(mesh_asset.submeshes.size(), MAX_SUBMESHES_PER_MESH));
        VALIDATE(m_submesh_used + submesh_count <= MAX_SUBMESHES, return, "", "Submesh SSBO capacity exceeded")

        // Resolve every submesh's material. First use uploads its textures (immediate_submit), so do it before mapping the SSBO
        std::vector<u32> material_slots(submesh_count);
        for (u32 i = 0; i < submesh_count; ++i)
            material_slots[i] = acquire_material(mesh_asset.submeshes[i].material);
        mesh_slot.material_slots = material_slots;

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

        for (const u32 material_slot : mesh_slot.material_slots)
            release_material(material_slot);
        mesh_slot.material_slots.clear();

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

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);

        m_cull_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_cull_set_layout;
        m_cull_pipeline_layout = m_device.createPipelineLayout(pipeline_layout_info);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "cull.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile cull compute shader")

        vk::ShaderModule compute_shader = m_dev->create_shader_module(compute_spv);

        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = vk::ShaderStageFlagBits::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.layout = m_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = m_device.createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create cull pipeline")
        m_cull_pipeline = result.value;
        m_live_pipeline_count++;

        m_device.destroyShaderModule(compute_shader);

        // Per-frame draw buffers owned by the graph. The graph handles barriers between the cull pass (which writes them) and the draw passes
        // (which read them as indirect). One buffer per in-flight frame so frames N-1 and N-2 don't clobber the one the GPU is still reading
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            graph::buffer_desc cmd_desc{};
            cmd_desc.name = "draw_commands_" + std::to_string(frame_index);
            cmd_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * MAX_COMMANDS_PER_MATERIAL * sizeof(vk::DrawIndexedIndirectCommand);
            cmd_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            cmd_desc.persistent = true;
            m_draw_commands_handles[frame_index] = m_graph->create_buffer(cmd_desc);

            graph::buffer_desc count_desc{};
            count_desc.name = "draw_count_" + std::to_string(frame_index);
            count_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * sizeof(u32);
            count_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer
                            | vk::BufferUsageFlagBits::eIndirectBuffer
                            | vk::BufferUsageFlagBits::eTransferDst;
            count_desc.persistent = true;
            m_draw_count_handles[frame_index] = m_graph->create_buffer(count_desc);

            // Build the cull descriptor set for this frame. The descriptor items point into the graph's buffers via the graph's own lookup helper
            util::allocated_buffer* draw_commands_allocation = m_graph->buffer_allocation(m_draw_commands_handles[frame_index]);
            util::allocated_buffer* draw_count_allocation = m_graph->buffer_allocation(m_draw_count_handles[frame_index]);
            ASSERT(draw_commands_allocation && draw_count_allocation, "", "Graph failed to allocate draw buffers")

            std::vector<util::descriptor_item> descriptor_items;
            descriptor_items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame_index]);
            descriptor_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_mesh_data_buffer);
            descriptor_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_submesh_data_buffer);
            descriptor_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, draw_commands_allocation);
            descriptor_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, draw_count_allocation);
            descriptor_items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_camera_ubos[frame_index]);
            descriptor_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_override_buffers[frame_index]);

            m_cull_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_cull_set_layout, descriptor_items,
                util::descriptor_buffer_type::resource, 1);

            m_dev->update_descriptor_buffer(m_cull_desc_buffers[frame_index], descriptor_items,
                util::descriptor_buffer_type::resource);

            m_live_buffer_count++;
        }
    }


    void renderer::destroy_cull_pipeline() {

        m_device.waitIdle();

        for (auto& descriptor_buffer : m_cull_desc_buffers)
            if (descriptor_buffer.buffer.buffer)
                m_dev->destroy_buffer(descriptor_buffer.buffer);

        // The draw command / draw count buffers are graph-owned (persistent) and will be destroyed by ~render_graph. Nothing to do here

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

    // Pack every visible scene instance into a single flat array. The GPU groups by mesh during culling; the CPU no longer batches anything
    void renderer::build_instance_buffer() {

        m_packed_instances.clear();
        m_scene_materials.clear();

        // empty scene: release everything we were holding
        if (m_scene_instances.empty()) {
            for (u32 slot : m_instance_material_slots)
                release_material(slot);
            m_instance_material_slots.clear();
            m_instance_material_handles.clear();
            m_instance_count = 0;
            return;
        }

        m_packed_instances.reserve(m_scene_instances.size());

        std::vector<GLT::asset::handle> desired_handles;
        std::vector<u32> desired_slots;
        desired_handles.reserve(m_scene_instances.size() * MAX_SUBMESHES_PER_MESH);
        desired_slots.reserve(m_scene_instances.size() * MAX_SUBMESHES_PER_MESH);

        for (const auto& scene_instance : m_scene_instances) {

            if (scene_instance.mesh == INVALID_HANDLE)
                continue;

            const mesh_slot* mesh_slot = find_slot(scene_instance.mesh);
            if (!mesh_slot || !mesh_slot->alive)
                continue;

            VALIDATE(m_packed_instances.size() < MAX_INSTANCES, break,
                "", "Instance SSBO capacity exceeded ({} instances); scene truncated", (MAX_INSTANCES))

            const u32 mesh_index = static_cast<u32>(mesh_slot - m_mesh_slots.data());
            const u32 instance_index = static_cast<u32>(m_packed_instances.size());
            const glm::mat3 upper_left(scene_instance.transform);

            gpu_instance_data instance_data{};
            instance_data.model = scene_instance.transform;
            instance_data.normal_matrix = glm::mat4(glm::transpose(glm::inverse(upper_left)));
            instance_data.mesh_index = mesh_index;
            instance_data.material_index = 0;
            m_packed_instances.push_back(instance_data);

            // resolve per-submesh materials for this instance
            const u32 submesh_count = static_cast<u32>(mesh_slot->material_slots.size());

            for (u32 sm = 0; sm < MAX_SUBMESHES_PER_MESH; ++sm) {

                GLT::asset::handle desired = INVALID_HANDLE;

                if (sm < submesh_count) {

                    const bool has_override =
                        sm < scene_instance.material_overrides.size() &&
                        scene_instance.material_overrides[sm] != INVALID_HANDLE;

                    if (has_override)
                        desired = scene_instance.material_overrides[sm];
                    else
                        desired = m_material_slots[mesh_slot->material_slots[sm]].asset;    // Default: the handle behind the slot we acquired at mesh load time
                }

                const u32 flat_index = instance_index * MAX_SUBMESHES_PER_MESH + sm;
                desired_handles.push_back(desired);

                // Change detection against last frame's state
                const bool unchanged =
                    flat_index < m_instance_material_handles.size() &&
                    m_instance_material_handles[flat_index] == desired;

                if (unchanged)
                    desired_slots.push_back(m_instance_material_slots[flat_index]);
                else {
                    if (flat_index < m_instance_material_slots.size())
                        release_material(m_instance_material_slots[flat_index]);
                    desired_slots.push_back(acquire_material(desired));
                }

                // dedup material slot set
                if (std::find(m_scene_materials.begin(), m_scene_materials.end(), desired_slots.back()) == m_scene_materials.end())
                    m_scene_materials.push_back(desired_slots.back());
            }
        }

        // release tail slots if the scene shrank this frame
        for (size_t i = desired_handles.size(); i < m_instance_material_slots.size(); ++i)
            release_material(m_instance_material_slots[i]);

        m_instance_material_handles = std::move(desired_handles);
        m_instance_material_slots = std::move(desired_slots);
        m_instance_count = static_cast<u32>(m_packed_instances.size());

        if (m_packed_instances.empty())
            return;

        // upload instance SSBO
        {
            void* mapped = m_dev->map_buffer(m_instance_buffers[m_current_frame]);
            std::memcpy(mapped, m_packed_instances.data(), m_packed_instances.size() * sizeof(gpu_instance_data));
            m_dev->unmap_buffer(m_instance_buffers[m_current_frame]);
        }

        // upload per-instance material slot SSBO
        if (!m_instance_material_slots.empty()) {

            const vk::DeviceSize bytes = m_instance_material_slots.size() * sizeof(u32);
            util::allocated_buffer& buffer = m_instance_override_buffers[m_current_frame];

            // Sanity: buffer was pre-allocated at MAX_INSTANCES * MAX_SUBMESHES_PER_MESH
            VALIDATE(bytes <= MAX_INSTANCES * MAX_SUBMESHES_PER_MESH * sizeof(u32), return, "", "Instance material override buffer overflow")

            void* mapped = m_dev->map_buffer(buffer);
            std::memcpy(mapped, m_instance_material_slots.data(), bytes);
            m_dev->unmap_buffer(buffer);
        }
    }


    void renderer::add_reset_draw_count_pass() {

        m_graph->add_pass("reset_draw_count",
            [this](graph::pass_builder& builder) {
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::transfer_dst);
            },
            [this](graph::pass_context& ctx) {
                ctx.cmd.fillBuffer(ctx.get_buffer(m_draw_count_handles[m_current_frame]),
                    0, MAX_MATERIALS * sizeof(u32), 0);
            });
    }


    void renderer::add_cull_pass() {

        // Side effects so the culler keeps it even if nothing downstream reads its outputs
        m_graph->add_pass("cull",
            [this](graph::pass_builder& builder) {
                builder.write(m_draw_commands_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_draw_count_handles[m_current_frame],    graph::buffer_usage::storage_readwrite);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cull_pipeline);
                m_dev->bind_descriptor_buffer({ m_cull_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);

                const u32 dispatch_count = (m_instance_count + 63) / 64;
                ctx.cmd.dispatch(dispatch_count, 1, 1);
            });
    }

}
