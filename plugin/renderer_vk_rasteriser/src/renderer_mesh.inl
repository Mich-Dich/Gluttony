
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
        std::swap(m_index_buffer,  new_ib);

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

        // -------- Camera UBO + descriptor buffer (one per in-flight frame) --------
        {
            std::vector<util::descriptor_item> items_for_layout;
            items_for_layout.emplace_back(0, vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                1, nullptr);

            m_mesh_set_layout = m_dev->create_descriptor_set_layout(items_for_layout);
            m_live_descriptor_set_count++;

            vk::PushConstantRange push_range{};
            push_range.stageFlags = vk::ShaderStageFlagBits::eVertex;
            push_range.offset = 0;
            push_range.size = sizeof(glm::mat4);

            vk::PipelineLayoutCreateInfo plci{};
            plci.setLayoutCount = 1;
            plci.pSetLayouts = &m_mesh_set_layout;
            plci.pushConstantRangeCount = 1;
            plci.pPushConstantRanges = &push_range;
            m_mesh_pipeline_layout = m_device.createPipelineLayout(plci);

            for (u32 index = 0; index < MAX_CONCURRENT_FRAMES; ++index) {

                m_camera_ubos[index] = m_dev->create_buffer(
                    sizeof(camera_ubo_data),
                    vk::BufferUsageFlagBits::eUniformBuffer,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                m_live_buffer_count++;

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eUniformBuffer, 
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1, &m_camera_ubos[index]);

                m_camera_desc_buffers[index] = m_dev->create_descriptor_buffer(m_mesh_set_layout, items, 
                    util::descriptor_buffer_type::resource, 1);

                m_dev->update_descriptor_buffer(m_camera_desc_buffers[index], items, util::descriptor_buffer_type::resource);
                m_live_buffer_count++;
            }
        }

        // -------- Mesh graphics pipeline (dynamic rendering) --------
        {
            const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader";

            auto vs_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh.vert.glsl");
            auto fs_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh.frag.glsl");
            ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile mesh shaders");

            vk::ShaderModule vertex_shader = m_dev->create_shader_module(vs_spv);
            vk::ShaderModule fragment_shader = m_dev->create_shader_module(fs_spv);

            vk::PipelineShaderStageCreateInfo stages[2]{};
            stages[0].stage = vk::ShaderStageFlagBits::eVertex;
            stages[0].module = vertex_shader;
            stages[0].pName = "main";
            stages[1].stage = vk::ShaderStageFlagBits::eFragment;
            stages[1].module = fragment_shader;
            stages[1].pName = "main";

            // Vertex input — must match GLT::asset::mesh::vertex layout
            vk::VertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(GLT::asset::mesh::vertex);
            binding.inputRate = vk::VertexInputRate::eVertex;

            std::array<vk::VertexInputAttributeDescription, 4> attrs{};
            attrs[0] = {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, position)};
            attrs[1] = {1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, normal)};
            attrs[2] = {2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(GLT::asset::mesh::vertex, tangent)};
            attrs[3] = {3, 0, vk::Format::eR32G32Sfloat, offsetof(GLT::asset::mesh::vertex, uv0)};

            vk::PipelineVertexInputStateCreateInfo vi{};
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &binding;
            vi.vertexAttributeDescriptionCount = static_cast<u32>(attrs.size());
            vi.pVertexAttributeDescriptions = attrs.data();

            vk::PipelineInputAssemblyStateCreateInfo ia{};
            ia.topology = vk::PrimitiveTopology::eTriangleList;

            vk::PipelineViewportStateCreateInfo vp{};
            vp.viewportCount = 1;
            vp.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rs{};
            rs.polygonMode = vk::PolygonMode::eFill;
            rs.cullMode = vk::CullModeFlagBits::eBack;
            rs.frontFace = vk::FrontFace::eCounterClockwise;
            rs.lineWidth = 1.0f;

            vk::PipelineMultisampleStateCreateInfo ms{};
            ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

            vk::PipelineDepthStencilStateCreateInfo ds{};
            ds.depthTestEnable = VK_FALSE;
            ds.depthWriteEnable = VK_FALSE;

            vk::PipelineColorBlendAttachmentState cb{};
            cb.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB |
                                vk::ColorComponentFlagBits::eA;

            vk::PipelineColorBlendStateCreateInfo cbs{};
            cbs.attachmentCount = 1;
            cbs.pAttachments = &cb;

            vk::DynamicState dyn_states[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dyn{};
            dyn.dynamicStateCount = 2;
            dyn.pDynamicStates = dyn_states;

            vk::Format color_format = vk::Format::eR8G8B8A8Unorm;
            vk::PipelineRenderingCreateInfo rendering_info{};
            rendering_info.colorAttachmentCount    = 1;
            rendering_info.pColorAttachmentFormats = &color_format;

            vk::GraphicsPipelineCreateInfo gpci{};
            gpci.pNext = &rendering_info;
            gpci.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
            gpci.stageCount = 2;
            gpci.pStages = stages;
            gpci.pVertexInputState = &vi;
            gpci.pInputAssemblyState = &ia;
            gpci.pViewportState = &vp;
            gpci.pRasterizationState = &rs;
            gpci.pMultisampleState = &ms;
            gpci.pDepthStencilState = &ds;
            gpci.pColorBlendState = &cbs;
            gpci.pDynamicState = &dyn;
            gpci.layout = m_mesh_pipeline_layout;
            gpci.renderPass = nullptr;
            gpci.subpass = 0;

            auto result = m_device.createGraphicsPipeline(nullptr, gpci);
            VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create mesh pipeline");
            m_mesh_pipeline = result.value;
            m_live_pipeline_count++;

            m_device.destroyShaderModule(vertex_shader);
            m_device.destroyShaderModule(fragment_shader);
        }

        // -------- Pre-reserve buffer headroom --------
        reserve_mesh_space(VERTEX_HEADROOM_MIN, INDEX_HEADROOM_MIN);
    }


    void renderer::destroy_mesh_resources() {

        m_device.waitIdle();

        for (auto& b : m_camera_desc_buffers)
            if (b.buffer.buffer)
                m_dev->destroy_buffer(b.buffer);
        for (auto& u : m_camera_ubos)
            if (u.buffer)
                m_dev->destroy_buffer(u);

        if (m_vertex_buffer.buffer)
            m_dev->destroy_buffer(m_vertex_buffer);

        if (m_index_buffer.buffer)
            m_dev->destroy_buffer(m_index_buffer);

        if (m_mesh_pipeline)
            m_device.destroyPipeline(m_mesh_pipeline);

        if (m_mesh_pipeline_layout)
            m_device.destroyPipelineLayout(m_mesh_pipeline_layout);

        if (m_mesh_set_layout)
            m_device.destroyDescriptorSetLayout(m_mesh_set_layout);

        m_mesh_pipeline = nullptr;
        m_mesh_pipeline_layout = nullptr;
        m_mesh_set_layout = nullptr;
    }


    void renderer::update_camera_ubo() {

        camera_ubo_data data{};

        const f32 aspect = static_cast<f32>(m_render_size.x) / static_cast<f32>(m_render_size.y);
        glm::mat4 proj = glm::perspective(glm::radians(m_active_camera.fov), aspect, m_active_camera.near_plane,
            m_active_camera.far_plane);
        proj[1][1] *= -1.0f;   // Vulkan Y flip

        data.view = m_active_camera.view;
        data.view_proj = proj * m_active_camera.view;
        data.camera_pos = glm::vec4(m_active_camera.position, 1.0f);

        std::memcpy(m_dev->map_buffer(m_camera_ubos[m_current_frame]), &data, sizeof(data));
        m_dev->unmap_buffer(m_camera_ubos[m_current_frame]);
    }

}
