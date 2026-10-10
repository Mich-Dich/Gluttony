
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

    bool renderer::load_mesh(const std::filesystem::path& content_relative_path) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        auto handle = registry->load(content_relative_path);
        VALIDATE(handle.has_value(), return false, "", "Failed to load mesh [{}]", content_relative_path.generic_string());

        return load_mesh(*handle);
    }


    bool renderer::load_mesh(const GLT::asset::handle handle) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        if (find_slot(handle))
            return true;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(handle);
        VALIDATE(mesh, return false, "", "Asset [{}] is not a mesh_asset", registry->info(handle).name);

        reserve_mesh_space(static_cast<u32>(mesh->vertices.size()), static_cast<u32>(mesh->indices.size()));

        u32 slot_idx;
        if (!m_free_slots.empty()) {

            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();

        } else {
            
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot.asset = handle;
        slot.vertex_offset = m_vertex_used;
        slot.vertex_count = static_cast<u32>(mesh->vertices.size());
        slot.index_offset = m_index_used;
        slot.index_count = static_cast<u32>(mesh->indices.size());
        slot.alive = true;

        upload_mesh_slice(slot, *mesh);
        pack_mesh_into_ssbo(slot, *mesh);

        m_vertex_used += slot.vertex_count;
        m_index_used += slot.index_count;
        return true;
    }


    void renderer::unload_mesh(GLT::asset::handle handle) {

        u32 slot_index = UINT32_MAX;
        for (u32 index = 0; index < m_mesh_slots.size(); ++index) {
            if (m_mesh_slots[index].alive && m_mesh_slots[index].asset == handle) {
                slot_index = index;
                break;
            }
        }

        VALIDATE(slot_index != UINT32_MAX, return, "", "handle not loaded");

        m_device.waitIdle();

        mesh_slot& slot = m_mesh_slots[slot_index];
        clear_mesh_ssbo_entry(slot);

        slot.alive = false;
        slot.asset = INVALID_HANDLE;
        slot.vertex_offset = 0;
        slot.vertex_count = 0;
        slot.index_offset = 0;
        slot.index_count = 0;

        m_free_slots.push_back(slot_index);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    bool renderer::reserve_mesh_space(u32 vertex_count, u32 index_count) {

        const u32 need_v = m_vertex_used + vertex_count;
        const u32 need_i = m_index_used  + index_count;
        bool grew = false;

        if (need_v > m_vertex_capacity) {
            m_vertex_capacity = std::max(need_v, m_vertex_capacity + std::max(m_vertex_capacity / 2, VERTEX_HEADROOM_MIN));
            grew = true;
        }
        if (need_i > m_index_capacity) {
            m_index_capacity = std::max(need_i, m_index_capacity + std::max(m_index_capacity / 2, INDEX_HEADROOM_MIN));
            grew = true;
        }
        if (!grew)
            return false;

        const auto usage_v = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
        const auto usage_i = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer;

        auto new_vb = m_dev->create_buffer(m_vertex_capacity * sizeof(GLT::asset::mesh::vertex), usage_v, 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        auto new_ib = m_dev->create_buffer(m_index_capacity * sizeof(u32), usage_i,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memset(m_dev->map_buffer(new_vb), 0, m_vertex_capacity * sizeof(GLT::asset::mesh::vertex));
        std::memset(m_dev->map_buffer(new_ib), 0, m_index_capacity  * sizeof(u32));
        m_dev->unmap_buffer(new_vb);
        m_dev->unmap_buffer(new_ib);

        if (m_vertex_used) {
            const void* src = m_dev->map_buffer(m_vertex_buffer);
            void* dst = m_dev->map_buffer(new_vb);
            std::memcpy(dst, src, m_vertex_used * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(m_vertex_buffer);
            m_dev->unmap_buffer(new_vb);
        }
        if (m_index_used) {
            const void* src = m_dev->map_buffer(m_index_buffer);
            void* dst = m_dev->map_buffer(new_ib);
            std::memcpy(dst, src, m_index_used * sizeof(u32));
            m_dev->unmap_buffer(m_index_buffer);
            m_dev->unmap_buffer(new_ib);
        }

        std::swap(m_vertex_buffer, new_vb);
        std::swap(m_index_buffer, new_ib);

        m_device.waitIdle();
        if (new_vb.buffer)
            m_dev->destroy_buffer(new_vb);

        if (new_ib.buffer)
            m_dev->destroy_buffer(new_ib);

        return true;
    }


    void renderer::upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh) {

        {
            auto* dst = static_cast<GLT::asset::mesh::vertex*>(m_dev->map_buffer(m_vertex_buffer));
            std::memcpy(dst + slot.vertex_offset, mesh.vertices.data(), mesh.vertices.size() * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(m_vertex_buffer);
        }
        {
            auto* dst = static_cast<u32*>(m_dev->map_buffer(m_index_buffer));
            std::memcpy(dst + slot.index_offset, mesh.indices.data(), mesh.indices.size() * sizeof(u32));
            m_dev->unmap_buffer(m_index_buffer);
        }
    }


    renderer::mesh_slot* renderer::find_slot(GLT::asset::handle h) noexcept {

        for (auto& slot : m_mesh_slots)
            if (slot.alive && slot.asset == h)
                return &slot;
        return nullptr;
    }


    const renderer::mesh_slot* renderer::find_slot(GLT::asset::handle h) const noexcept { 

        return const_cast<renderer*>(this)->find_slot(h);
    }

    // mesh resources --------------------------------------------------------------------------------------------------

    void renderer::create_mesh_resources() {

        // Descriptor set layout: binding 0 = camera UBO, binding 1 = instance SSBO ------------------------------------
        std::vector<util::descriptor_item> descriptor_layout_items;
        descriptor_layout_items.emplace_back(0, vk::DescriptorType::eUniformBuffer, 
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1, nullptr);
        descriptor_layout_items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 1, nullptr);

        m_mesh_set_layout = m_dev->create_descriptor_set_layout(descriptor_layout_items);
        m_live_descriptor_set_count++;

        // No push constants — everything the shader needs is in the descriptor set
        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_mesh_set_layout;
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges = nullptr;
        m_mesh_pipeline_layout = m_device.createPipelineLayout(pipeline_layout_info);

        // Per-frame camera UBO + instance SSBO + a single descriptor buffer holding both -----------------------------
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            m_camera_ubos[frame_index] = m_dev->create_buffer(sizeof(camera_ubo_data), vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            m_instance_buffers[frame_index] = m_dev->create_buffer(MAX_INSTANCES * sizeof(gpu_instance_data),
                vk::BufferUsageFlagBits::eStorageBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            std::vector<util::descriptor_item> descriptor_items;
            descriptor_items.emplace_back(0, vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1, &m_camera_ubos[frame_index]);
            descriptor_items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 1,
                &m_instance_buffers[frame_index]);

            m_frame_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_mesh_set_layout, descriptor_items,
                util::descriptor_buffer_type::resource, 1);

            m_dev->update_descriptor_buffer(m_frame_desc_buffers[frame_index], descriptor_items, util::descriptor_buffer_type::resource);

            m_live_buffer_count++;
        }

        // Pipelines ----------------------------------------------------------------------------------------------------
        // (identical to before, minus the push constant range — the pipeline layout above already handles that)
        {
            const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";

            auto vertex_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh.vert.glsl");
            auto fragment_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh.frag.glsl");
            auto depth_fragment_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "depth_only.frag.glsl");
            ASSERT(!vertex_spv.empty() && !fragment_spv.empty() && !depth_fragment_spv.empty(), "", "Failed to compile mesh shaders");

            vk::ShaderModule vertex_shader = m_dev->create_shader_module(vertex_spv);
            vk::ShaderModule fragment_shader = m_dev->create_shader_module(fragment_spv);
            vk::ShaderModule depth_fragment_shader = m_dev->create_shader_module(depth_fragment_spv);

            vk::VertexInputBindingDescription vertex_binding{};
            vertex_binding.binding = 0;
            vertex_binding.stride = sizeof(GLT::asset::mesh::vertex);
            vertex_binding.inputRate = vk::VertexInputRate::eVertex;

            std::array<vk::VertexInputAttributeDescription, 4> vertex_attributes{};
            vertex_attributes[0] = {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, position)};
            vertex_attributes[1] = {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, normal)};
            vertex_attributes[2] = {2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(GLT::asset::mesh::vertex, tangent)};
            vertex_attributes[3] = {3, 0, vk::Format::eR32G32Sfloat, offsetof(GLT::asset::mesh::vertex, uv0)};

            vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
            vertex_input_state.vertexBindingDescriptionCount = 1;
            vertex_input_state.pVertexBindingDescriptions = &vertex_binding;
            vertex_input_state.vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size());
            vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.data();

            vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{};
            input_assembly_state.topology = vk::PrimitiveTopology::eTriangleList;

            vk::PipelineViewportStateCreateInfo viewport_state{};
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rasterization_state{};
            rasterization_state.polygonMode = vk::PolygonMode::eFill;
            rasterization_state.cullMode = vk::CullModeFlagBits::eBack;
            rasterization_state.frontFace = vk::FrontFace::eCounterClockwise;
            rasterization_state.lineWidth = 1.0f;

            vk::PipelineMultisampleStateCreateInfo multisample_state{};
            multisample_state.rasterizationSamples = vk::SampleCountFlagBits::e1;

            vk::DynamicState dynamic_states[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamic_state{};
            dynamic_state.dynamicStateCount = 2;
            dynamic_state.pDynamicStates = dynamic_states;

            const vk::Format color_format = vk::Format::eR8G8B8A8Unorm;
            const vk::Format depth_format = vk::Format::eD32Sfloat;

            // Depth-only pipeline --------------------------------------------------------------------------------------
            {
                vk::PipelineShaderStageCreateInfo shader_stages[2]{};
                shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
                shader_stages[0].module = vertex_shader;
                shader_stages[0].pName = "main";
                shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
                shader_stages[1].module = depth_fragment_shader;
                shader_stages[1].pName = "main";

                vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{};
                depth_stencil_state.depthTestEnable = VK_TRUE;
                depth_stencil_state.depthWriteEnable = VK_TRUE;
                depth_stencil_state.depthCompareOp = vk::CompareOp::eLess;
                depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
                depth_stencil_state.stencilTestEnable = VK_FALSE;

                vk::PipelineColorBlendStateCreateInfo color_blend_state{};
                color_blend_state.attachmentCount = 0;
                color_blend_state.pAttachments = nullptr;

                vk::PipelineRenderingCreateInfo rendering_info{};
                rendering_info.colorAttachmentCount = 0;
                rendering_info.pColorAttachmentFormats = nullptr;
                rendering_info.depthAttachmentFormat = depth_format;

                vk::GraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.pNext = &rendering_info;
                pipeline_info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
                pipeline_info.stageCount = 2;
                pipeline_info.pStages = shader_stages;
                pipeline_info.pVertexInputState = &vertex_input_state;
                pipeline_info.pInputAssemblyState = &input_assembly_state;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterization_state;
                pipeline_info.pMultisampleState = &multisample_state;
                pipeline_info.pDepthStencilState = &depth_stencil_state;
                pipeline_info.pColorBlendState = &color_blend_state;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = m_mesh_pipeline_layout;
                pipeline_info.renderPass = nullptr;
                pipeline_info.subpass = 0;

                auto result = m_device.createGraphicsPipeline(nullptr, pipeline_info);
                VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create depth-only pipeline");
                m_depth_pipeline = result.value;
                m_live_pipeline_count++;
            }

            // Forward shading pipeline ---------------------------------------------------------------------------------
            {
                vk::PipelineShaderStageCreateInfo shader_stages[2]{};
                shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
                shader_stages[0].module = vertex_shader;
                shader_stages[0].pName = "main";
                shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
                shader_stages[1].module = fragment_shader;
                shader_stages[1].pName = "main";

                vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{};
                depth_stencil_state.depthTestEnable = VK_TRUE;
                depth_stencil_state.depthWriteEnable = VK_FALSE;
                depth_stencil_state.depthCompareOp = vk::CompareOp::eEqual;
                depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
                depth_stencil_state.stencilTestEnable = VK_FALSE;

                vk::PipelineColorBlendAttachmentState color_blend_attachment{};
                color_blend_attachment.colorWriteMask =
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                    vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

                vk::PipelineColorBlendStateCreateInfo color_blend_state{};
                color_blend_state.attachmentCount = 1;
                color_blend_state.pAttachments = &color_blend_attachment;

                vk::PipelineRenderingCreateInfo rendering_info{};
                rendering_info.colorAttachmentCount = 1;
                rendering_info.pColorAttachmentFormats = &color_format;
                rendering_info.depthAttachmentFormat = depth_format;

                vk::GraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.pNext = &rendering_info;
                pipeline_info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
                pipeline_info.stageCount = 2;
                pipeline_info.pStages = shader_stages;
                pipeline_info.pVertexInputState = &vertex_input_state;
                pipeline_info.pInputAssemblyState = &input_assembly_state;
                pipeline_info.pViewportState = &viewport_state;
                pipeline_info.pRasterizationState = &rasterization_state;
                pipeline_info.pMultisampleState = &multisample_state;
                pipeline_info.pDepthStencilState = &depth_stencil_state;
                pipeline_info.pColorBlendState = &color_blend_state;
                pipeline_info.pDynamicState = &dynamic_state;
                pipeline_info.layout = m_mesh_pipeline_layout;
                pipeline_info.renderPass = nullptr;
                pipeline_info.subpass = 0;

                auto result = m_device.createGraphicsPipeline(nullptr, pipeline_info);
                VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create mesh pipeline");
                m_mesh_pipeline = result.value;
                m_live_pipeline_count++;
            }

            m_device.destroyShaderModule(vertex_shader);
            m_device.destroyShaderModule(fragment_shader);
            m_device.destroyShaderModule(depth_fragment_shader);
        }

        // Persistent mesh-side SSBOs, read only by the cull compute shader --------------------------------------------
        m_mesh_data_buffer = m_dev->create_buffer(MAX_MESHES * sizeof(gpu_mesh_data), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        m_submesh_data_buffer = m_dev->create_buffer(MAX_SUBMESHES * sizeof(gpu_submesh_data), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        // Zero the whole thing so unloaded slots read as submesh_count = 0
        std::memset(m_dev->map_buffer(m_mesh_data_buffer), 0, MAX_MESHES * sizeof(gpu_mesh_data));
        m_dev->unmap_buffer(m_mesh_data_buffer);
        std::memset(m_dev->map_buffer(m_submesh_data_buffer), 0, MAX_SUBMESHES * sizeof(gpu_submesh_data));
        m_dev->unmap_buffer(m_submesh_data_buffer);

        // Pre-reserve buffer headroom ----------------------------------------------------------------------------------
        reserve_mesh_space(VERTEX_HEADROOM_MIN, INDEX_HEADROOM_MIN);
        create_cull_pipeline();
    }


    void renderer::destroy_mesh_resources() {

        m_device.waitIdle();
        
        destroy_cull_pipeline();

        if (m_mesh_data_buffer.buffer)
            m_dev->destroy_buffer(m_mesh_data_buffer);

        if (m_submesh_data_buffer.buffer)
            m_dev->destroy_buffer(m_submesh_data_buffer);

        for (auto& descriptor_buffer : m_frame_desc_buffers)
            if (descriptor_buffer.buffer.buffer)
                m_dev->destroy_buffer(descriptor_buffer.buffer);

        for (auto& instance_buffer : m_instance_buffers)
            if (instance_buffer.buffer)
                m_dev->destroy_buffer(instance_buffer);

        for (auto& camera_buffer : m_camera_ubos)
            if (camera_buffer.buffer)
                m_dev->destroy_buffer(camera_buffer);

        if (m_vertex_buffer.buffer)
            m_dev->destroy_buffer(m_vertex_buffer);

        if (m_index_buffer.buffer)
            m_dev->destroy_buffer(m_index_buffer);

        if (m_depth_pipeline)
            m_device.destroyPipeline(m_depth_pipeline);

        if (m_mesh_pipeline)
            m_device.destroyPipeline(m_mesh_pipeline);

        if (m_mesh_pipeline_layout)
            m_device.destroyPipelineLayout(m_mesh_pipeline_layout);

        if (m_mesh_set_layout)
            m_device.destroyDescriptorSetLayout(m_mesh_set_layout);

        m_depth_pipeline = nullptr;
        m_mesh_pipeline = nullptr;
        m_mesh_pipeline_layout = nullptr;
        m_mesh_set_layout = nullptr;
    }


    void renderer::update_camera_ubo() {

        camera_ubo_data camera_data{};

        const f32 aspect_ratio = static_cast<f32>(m_render_size.x) / static_cast<f32>(m_render_size.y);
        glm::mat4 projection = glm::perspective(glm::radians(m_active_camera.fov), aspect_ratio,
            m_active_camera.near_plane, m_active_camera.far_plane);
        projection[1][1] *= -1.0f;   // Vulkan Y flip

        const glm::mat4 view_projection = projection * m_active_camera.view;
        const bool hiz_enabled = m_frame_counter >= 2;              // HiZ is valid from frame 2 onward (built from frame 1's depth)
        camera_data.view = m_active_camera.view;
        camera_data.view_proj = view_projection;
        camera_data.camera_pos = glm::vec4(m_active_camera.position, 1.0f);
        camera_data.flags = glm::uvec4(hiz_enabled ? 1u : 0u, 0u, 0u, 0u);

        // Gribb-Hartmann frustum extraction. Rows of view_projection map world space to clip space; the six planes fall out as signed
        // combinations of those rows
        const glm::mat4 view_projection_transposed = glm::transpose(view_projection);
        const glm::vec4 clip_row_0 = view_projection_transposed[0];
        const glm::vec4 clip_row_1 = view_projection_transposed[1];
        const glm::vec4 clip_row_2 = view_projection_transposed[2];
        const glm::vec4 clip_row_3 = view_projection_transposed[3];

        camera_data.frustum_planes[0] = clip_row_3 + clip_row_0;  // left
        camera_data.frustum_planes[1] = clip_row_3 - clip_row_0;  // right
        camera_data.frustum_planes[2] = clip_row_3 + clip_row_1;  // bottom
        camera_data.frustum_planes[3] = clip_row_3 - clip_row_1;  // top
        camera_data.frustum_planes[4] = clip_row_3 + clip_row_2;  // near
        camera_data.frustum_planes[5] = clip_row_3 - clip_row_2;  // far

        for (auto& plane : camera_data.frustum_planes) {
            const f32 normal_length = glm::length(glm::vec3(plane));
            if (normal_length > 0.0f)
                plane /= normal_length;
        }

        std::memcpy(m_dev->map_buffer(m_camera_ubos[m_current_frame]), &camera_data, sizeof(camera_data));
        m_dev->unmap_buffer(m_camera_ubos[m_current_frame]);
    }

}
