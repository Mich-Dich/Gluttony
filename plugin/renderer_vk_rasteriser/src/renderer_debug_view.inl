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

    void renderer::create_debug_view_resources() {

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer,        VKSF::eFragment, 1, nullptr);
        layout_items.emplace_back(1, VKDT::eStorageBuffer,        VKSF::eFragment, 1, nullptr);
        layout_items.emplace_back(2, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);

        m_light_cull_debug_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_light_cull_debug_set_layout;
        m_light_cull_debug_pipeline_layout = m_device.createPipelineLayout(pli);

        // Shaders -----------------------------------------------------------------------------------------------
        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "light_cull_debug.vert.glsl");
        auto fs_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "light_cull_debug.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile light cull debug shaders");

        vk::ShaderModule vs = m_dev->create_shader_module(vs_spv);
        vk::ShaderModule fs = m_dev->create_shader_module(fs_spv);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
        stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

        // No vertex input — the vertex shader synthesizes a fullscreen triangle
        vk::PipelineVertexInputStateCreateInfo vi{};
        vk::PipelineInputAssemblyStateCreateInfo ia{};
        ia.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rs{};
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eNone;   // double-sided triangle covers the screen either winding
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo ds{};
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.attachmentCount = 1;
        cbs.pAttachments = &cb;

        vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dyn_state{};
        dyn_state.dynamicStateCount = 2;
        dyn_state.pDynamicStates = dyn;

        const vk::Format color_format = vk::Format::eR8G8B8A8Unorm;

        vk::PipelineRenderingCreateInfo ri{};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_format;
        ri.depthAttachmentFormat = vk::Format::eUndefined;

        vk::GraphicsPipelineCreateInfo pi{};
        pi.pNext = &ri;
        pi.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &ds;
        pi.pColorBlendState = &cbs;
        pi.pDynamicState = &dyn_state;
        pi.layout = m_light_cull_debug_pipeline_layout;
        pi.renderPass = nullptr;

        auto result = m_device.createGraphicsPipeline(nullptr, pi);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create light cull debug pipeline");
        m_light_cull_debug_pipeline = result.value;
        m_live_pipeline_count++;

        m_device.destroyShaderModule(vs);
        m_device.destroyShaderModule(fs);

        // Lazily created in update_debug_view_descriptors() — the grid handle isn't valid until
        // the first begin_frame() call has run update_cluster_dimensions()
    }


    void renderer::destroy_debug_view_resources() {

        m_device.waitIdle();

        for (auto& db : m_light_cull_debug_desc_buffers) {
            if (db.buffer.buffer)
                m_dev->destroy_buffer(db.buffer);
            db = {};
        }

        if (m_light_cull_debug_pipeline)
            m_device.destroyPipeline(m_light_cull_debug_pipeline);

        if (m_light_cull_debug_pipeline_layout)
            m_device.destroyPipelineLayout(m_light_cull_debug_pipeline_layout);

        if (m_light_cull_debug_set_layout)
            m_device.destroyDescriptorSetLayout(m_light_cull_debug_set_layout);

        m_light_cull_debug_pipeline = nullptr;
        m_light_cull_debug_pipeline_layout = nullptr;
        m_light_cull_debug_set_layout = nullptr;
    }


    void renderer::update_debug_view_descriptors() {

        if (m_cluster_params.cluster_count == 0)
            return;

        const u32 frame_index = m_current_frame;

        util::allocated_buffer* grid_allocation = m_graph->buffer_allocation(m_light_grid_handles[frame_index]);
        ASSERT(grid_allocation, "", "Graph failed to allocate light grid for debug view");

        const vk::ImageView depth_view = m_graph->texture_default_view(m_depth_target);
        ASSERT(depth_view && m_depth_sampler, "", "Depth view/sampler not ready for debug view");

        util::accessible_image depth_image{};
        depth_image.view = depth_view;
        depth_image.sampler = m_depth_sampler;
        depth_image.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> items;
        items.emplace_back(0, VKDT::eUniformBuffer,        VKSF::eFragment, 1, &m_camera_ubos[frame_index]);
        items.emplace_back(1, VKDT::eStorageBuffer,        VKSF::eFragment, 1, grid_allocation);
        items.emplace_back(2, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, &depth_image);

        if (m_light_cull_debug_desc_buffers[frame_index].buffer.buffer == nullptr)
            m_light_cull_debug_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(
                m_light_cull_debug_set_layout, items, util::descriptor_buffer_type::combined, 1);

        m_dev->compute_descriptor_binding_offsets(m_light_cull_debug_set_layout, items);
        m_dev->update_descriptor_buffer(m_light_cull_debug_desc_buffers[frame_index], items,
            util::descriptor_buffer_type::combined);
    }

}
