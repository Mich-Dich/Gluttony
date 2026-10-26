
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

    // cull pipeline ---------------------------------------------------------------------------------------------------

    void scene_renderer::create_cull_pipeline() {

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

        m_cull_set_layout = m_plugin.get_dev()->create_descriptor_set_layout(layout_items);

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_cull_set_layout;
        m_cull_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pipeline_layout_info);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "cull.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile cull compute shader")

        vk::ShaderModule compute_shader = m_plugin.get_dev()->create_shader_module(compute_spv);

        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = vk::ShaderStageFlagBits::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.layout = m_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = m_plugin.get_vk_device().createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create cull pipeline")
        m_cull_pipeline = result.value;

        m_plugin.get_vk_device().destroyShaderModule(compute_shader);

        // Per-frame draw buffers owned by the graph. The graph handles barriers between the cull pass (which writes them) and
        // the draw passes (which read them as indirect). One buffer per in-flight frame so frames N-1 and N-2 don't clobber the
        // one the GPU is still reading
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            using BU = vk::BufferUsageFlagBits;

            graph::buffer_desc cmd_desc{};
            cmd_desc.name = "draw_commands_" + std::to_string(frame_index);
            cmd_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * MAX_COMMANDS_PER_MATERIAL * sizeof(vk::DrawIndexedIndirectCommand);
            cmd_desc.usage = BU::eStorageBuffer | BU::eIndirectBuffer;
            cmd_desc.persistent = true;
            m_draw_commands_handles[frame_index] = m_graph->create_buffer(cmd_desc);

            graph::buffer_desc count_desc{};
            count_desc.name = "draw_count_" + std::to_string(frame_index);
            count_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * sizeof(u32);
            count_desc.usage = BU::eStorageBuffer | BU::eIndirectBuffer | BU::eTransferDst;
            count_desc.persistent = true;
            m_draw_count_handles[frame_index] = m_graph->create_buffer(count_desc);

            // Build the cull descriptor set for this frame. The descriptor items point into the graph's buffers via the graph's
            // own lookup helper
            util::allocated_buffer* draw_commands_allocation = m_graph->buffer_allocation(m_draw_commands_handles[frame_index]);
            util::allocated_buffer* draw_count_allocation = m_graph->buffer_allocation(m_draw_count_handles[frame_index]);
            ASSERT(draw_commands_allocation && draw_count_allocation, "", "Graph failed to allocate draw buffers")

            std::vector<util::descriptor_item> descriptor_items;
            descriptor_items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame_index]);
            descriptor_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_mesh_data_buffer_alloc_ref());
            descriptor_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_submesh_data_buffer_alloc_ref());
            descriptor_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, draw_commands_allocation);
            descriptor_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, draw_count_allocation);
            descriptor_items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_camera_ubos[frame_index]);
            descriptor_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_override_buffers[frame_index]);

            m_cull_desc_buffers[frame_index] = m_plugin.get_dev()->create_descriptor_buffer(m_cull_set_layout, descriptor_items,
                util::descriptor_buffer_type::resource, 1);

            m_plugin.get_dev()->update_descriptor_buffer(m_cull_desc_buffers[frame_index], descriptor_items,
                util::descriptor_buffer_type::resource);
        }
    }


    void scene_renderer::destroy_cull_pipeline() {

        m_plugin.get_vk_device().waitIdle();

        for (auto& descriptor_buffer : m_cull_desc_buffers)
            if (descriptor_buffer.buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(descriptor_buffer.buffer);

        // The draw command / draw count buffers are graph-owned (persistent) and will be destroyed by ~render_graph. Nothing to do here

        if (m_cull_pipeline)
            m_plugin.get_vk_device().destroyPipeline(m_cull_pipeline);

        if (m_cull_pipeline_layout)
            m_plugin.get_vk_device().destroyPipelineLayout(m_cull_pipeline_layout);

        if (m_cull_set_layout)
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_cull_set_layout);

        m_cull_pipeline = nullptr;
        m_cull_pipeline_layout = nullptr;
        m_cull_set_layout = nullptr;
    }

    // frame-time operations -------------------------------------------------------------------------------------------

    // Pack every scene instance into the SSBO. Asset-backed instances come first ([0, m_asset_instance_count)); procedural
    // instances append after that. Cull only dispatches over the asset-backed prefix, so procedural draws are handled by a
    // manual loop in add_depth_prepass_pass / add_scene_pass
    void scene_renderer::build_instance_buffer() {

        m_packed_instances.clear();
        m_scene_materials.clear();
        m_procedural_draw_slots.clear();
        m_asset_instance_count = 0;

        constexpr u32 MAX_SUB = 32;

        // Grow-only reserve; previous sizes were released when their (h, id) left m_resident_meshes.
        std::vector<GLT::asset::handle> desired_handles;
        std::vector<u32> desired_slots;
        desired_handles.reserve(m_scene_instances.size() * MAX_SUB);
        desired_slots.reserve(m_scene_instances.size() * MAX_SUB);

        // ---- Pass 1: asset-backed instances -------------------------------------------------------
        for (const auto& scene_instance : m_scene_instances) {

            if (scene_instance.procedural_mesh != 0)
                continue;
            if (scene_instance.mesh == INVALID_HANDLE)
                continue;

            auto rit = m_resident_meshes.find(scene_instance.mesh);
            if (rit == m_resident_meshes.end())
                continue;
            const mesh_slot* slot = m_shared->get_mesh(rit->second);
            if (!slot || !slot->alive)
                continue;

            VALIDATE(m_packed_instances.size() < MAX_INSTANCES, break,
                "", "Instance SSBO capacity exceeded ({}), truncating scene", (MAX_INSTANCES))

            const u32 instance_idx = static_cast<u32>(m_packed_instances.size());
            const glm::mat3 ul(scene_instance.transform);

            gpu_instance_data inst{};
            inst.model = scene_instance.transform;
            inst.normal_matrix = glm::mat4(glm::transpose(glm::inverse(ul)));
            inst.mesh_index = rit->second;
            m_packed_instances.push_back(inst);

            const u32 submesh_count = static_cast<u32>(slot->material_slots.size());

            for (u32 sm = 0; sm < MAX_SUB; ++sm) {

                GLT::asset::handle desired = INVALID_HANDLE;
                if (sm < submesh_count) {
                    const bool override = sm < scene_instance.material_overrides.size() && scene_instance.material_overrides[sm] != INVALID_HANDLE;
                    desired = override ? scene_instance.material_overrides[sm] : m_shared->get_material(slot->material_slots[sm]).asset;
                }

                const u32 flat = instance_idx * MAX_SUB + sm;
                desired_handles.push_back(desired);

                const bool unchanged = flat < m_instance_material_handles.size() && m_instance_material_handles[flat] == desired;

                if (unchanged)
                    desired_slots.push_back(m_instance_material_slots[flat]);
                else {
                    if (flat < m_instance_material_slots.size())
                        m_shared->release_material(m_instance_material_slots[flat]);
                    desired_slots.push_back(m_shared->acquire_material(desired));
                }

                if (!std::contains(m_scene_materials, desired_slots.back()))
                    m_scene_materials.push_back(desired_slots.back());
            }
        }

        m_asset_instance_count = static_cast<u32>(m_packed_instances.size());

        // ---- Pass 2: procedural instances ---------------------------------------------------------
        for (const auto& scene_instance : m_scene_instances) {

            if (scene_instance.procedural_mesh == 0)
                continue;
            const u32 id = scene_instance.procedural_mesh;
            if (id > m_procedural_meshes.size())
                continue;

            const auto& pm = m_procedural_meshes[id - 1];
            if (!pm.alive || pm.vertex_count == 0 || pm.index_count == 0)
                continue;

            VALIDATE(m_packed_instances.size() < MAX_INSTANCES, break, "", "Instance SSBO capacity exceeded (procedural), truncating", ())

            const glm::mat3 ul(scene_instance.transform);
            gpu_instance_data inst{};
            inst.model = scene_instance.transform;
            inst.normal_matrix = glm::mat4(glm::transpose(glm::inverse(ul)));
            inst.mesh_index = 0;
            m_packed_instances.push_back(inst);
            m_procedural_draw_slots.push_back(id - 1);

            if (!std::contains(m_scene_materials, pm.material_slot))
                m_scene_materials.push_back(pm.material_slot);
        }

        // ---- Release tail material slots from a shrunken scene -----------------------------------
        for (size_t i = desired_handles.size(); i < m_instance_material_slots.size(); ++i)
            m_shared->release_material(m_instance_material_slots[i]);

        m_instance_material_handles = std::move(desired_handles);
        m_instance_material_slots = std::move(desired_slots);
        m_instance_count = static_cast<u32>(m_packed_instances.size());

        if (m_packed_instances.empty())
            return;

        m_plugin.get_dev()->update_buffer(m_instance_buffers[m_current_frame],
            m_packed_instances.data(), m_packed_instances.size() * sizeof(gpu_instance_data));

        if (!m_instance_material_slots.empty()) {
            const vk::DeviceSize bytes = m_instance_material_slots.size() * sizeof(u32);
            VALIDATE(bytes <= MAX_INSTANCES * MAX_SUB * sizeof(u32), return, "", "Instance override buffer overflow")
            m_plugin.get_dev()->update_buffer(m_instance_override_buffers[m_current_frame], m_instance_material_slots.data(), bytes);
        }
    }


    void scene_renderer::add_reset_draw_count_pass() {

        m_graph->add_pass("reset_draw_count",
            [this](graph::pass_builder& builder) {
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::transfer_dst);
            },
            [this](graph::pass_context& ctx) {
                ctx.cmd.fillBuffer(ctx.get_buffer(m_draw_count_handles[m_current_frame]),
                    0, MAX_MATERIALS * sizeof(u32), 0);
            });
    }


    void scene_renderer::add_cull_pass() {

        m_graph->add_pass("cull",
            [this](graph::pass_builder& builder) {
                builder.write(m_draw_commands_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
            },
            [this](graph::pass_context& ctx) {

                if (m_asset_instance_count == 0)
                    return;

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cull_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ m_cull_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);

                // Only the asset-backed prefix
                const u32 dispatch_count = (m_asset_instance_count + 63) / 64;
                ctx.cmd.dispatch(dispatch_count, 1, 1);
            });
    }

}
