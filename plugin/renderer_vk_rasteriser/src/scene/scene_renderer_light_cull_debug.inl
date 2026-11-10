#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // "Light Culling" render mode (modes::Light_cull): heatmap of lights per cluster, using the same cluster lookup as the
    // material shaders, so a mismatch between light culling and shading shows up directly. Replaces the tonemap pass

    void scene_renderer::create_light_cull_debug_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::create_light_cull_debug_pipeline");

        auto* dev = m_plugin.get_dev();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eFragment, 1, nullptr);           // camera
        layout_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);           // light grid
        layout_items.emplace_back(2, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);    // depth
        m_light_debug_set_layout = dev->create_descriptor_set_layout(layout_items);

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_light_debug_set_layout;
        m_light_debug_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pli);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "light_cull_debug.vert.glsl");
        auto fs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "light_cull_debug.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile light culling debug shaders");

        vk::ShaderModule vs = dev->create_shader_module(vs_spv);
        vk::ShaderModule fs = dev->create_shader_module(fs_spv);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
        stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

        vk::PipelineVertexInputStateCreateInfo vi{};
        vk::PipelineInputAssemblyStateCreateInfo ia{};
        ia.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1; vp.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rs{};
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eNone;
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo ds{};

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

        const vk::Format color_format = vk::Format::eR8G8B8A8Unorm;     // scene_color
        vk::PipelineRenderingCreateInfo ri{};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_format;

        vk::GraphicsPipelineCreateInfo pi{};
        pi.pNext = &ri;
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
        pi.layout = m_light_debug_pipeline_layout;

        auto [result, pipeline] = m_plugin.get_vk_device().createGraphicsPipeline(nullptr, pi);
        ASSERT(result == vk::Result::eSuccess, "", "light culling debug pipeline creation failed");
        m_light_debug_pipeline = pipeline;

        m_plugin.get_vk_device().destroyShaderModule(vs);
        m_plugin.get_vk_device().destroyShaderModule(fs);

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index)
            m_light_debug_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_light_debug_set_layout, layout_items,
                util::descriptor_buffer_type::combined, 1);
    }


    void scene_renderer::destroy_light_cull_debug_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_light_cull_debug_pipeline");

        auto* dev = m_plugin.get_dev();
        for (auto& d : m_light_debug_desc_buffers)
            if (d.buffer.buffer) {
                dev->destroy_buffer(d.buffer);
                d = {};
            }

        if (m_light_debug_pipeline) {
            m_plugin.get_vk_device().destroyPipeline(m_light_debug_pipeline);
            m_light_debug_pipeline = nullptr;
        }
        if (m_light_debug_pipeline_layout) {
            m_plugin.get_vk_device().destroyPipelineLayout(m_light_debug_pipeline_layout);
            m_light_debug_pipeline_layout = nullptr;
        }
        if (m_light_debug_set_layout) {
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_light_debug_set_layout);
            m_light_debug_set_layout = nullptr;
        }
    }


    void scene_renderer::add_light_cull_debug_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_light_cull_debug_pass");

        m_graph->add_pass("light_cull_debug",
            [&](graph::pass_builder& b) {
                b.color_attachment(m_scene_color, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore);
                b.read(m_depth_target, graph::texture_usage::depth_sampled);
                b.read(m_light_grid_handles[m_current_frame], graph::buffer_usage::storage_read);
                b.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                util::allocated_buffer* grid = m_graph->buffer_allocation(m_light_grid_handles[m_current_frame]);
                if (!grid)
                    return;

                util::accessible_image depth{};
                depth.view = m_graph->texture_default_view(m_depth_target);
                depth.sampler = m_shared->get_depth_sampler();
                depth.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1, &m_camera_ubos[m_current_frame]);
                items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1, grid);
                items.emplace_back(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &depth);

                auto& desc = m_light_debug_desc_buffers[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_light_debug_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc, items, util::descriptor_buffer_type::combined);

                const vk::Extent2D extent{ m_extent.x, m_extent.y };
                ctx.begin_rendering();
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_light_debug_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_light_debug_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                ctx.cmd.draw(3, 1, 0, 0);
                ctx.end_rendering();
            });
    }

}
