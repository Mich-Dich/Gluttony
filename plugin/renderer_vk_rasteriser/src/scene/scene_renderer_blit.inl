
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

    // pipelines + resources -------------------------------------------------------------------------------------------

    void scene_renderer::create_blit_pipeline() {

        auto* dev = m_plugin.get_dev();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // Descriptor set: combined image sampler
        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);
        m_blit_set_layout = dev->create_descriptor_set_layout(layout_items);

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_blit_set_layout;
        m_blit_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pli);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "blit.vert.glsl");
        auto fs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "blit.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile blit shaders");

        vk::ShaderModule vs = m_plugin.get_dev()->create_shader_module(vs_spv);
        vk::ShaderModule fs = m_plugin.get_dev()->create_shader_module(fs_spv);

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
        ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.attachmentCount = 1; cbs.pAttachments = &cb;

        vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dyn_state{};
        dyn_state.dynamicStateCount = 2; dyn_state.pDynamicStates = dyn;

        const vk::Format color_format = m_swapchain.swapchain_format;
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
        pi.layout = m_blit_pipeline_layout;

        auto [result, pipeline] = m_plugin.get_vk_device().createGraphicsPipeline(nullptr, pi);
        ASSERT(result == vk::Result::eSuccess, "", "blit pipeline creation failed");
        m_blit_pipeline = pipeline;

        m_plugin.get_vk_device().destroyShaderModule(vs);
        m_plugin.get_vk_device().destroyShaderModule(fs);
    }


    void scene_renderer::destroy_blit_pipeline() {

        auto* dev = m_plugin.get_dev();
        for (auto& d : m_blit_desc_buffers)
            if (d.buffer.buffer) { dev->destroy_buffer(d.buffer); d = {}; }

        if (m_blit_pipeline) {
            m_plugin.get_vk_device().destroyPipeline(m_blit_pipeline);
            m_blit_pipeline = nullptr;
        }

        if (m_blit_pipeline_layout) {
            m_plugin.get_vk_device().destroyPipelineLayout(m_blit_pipeline_layout);
            m_blit_pipeline_layout = nullptr;
        }

        if (m_blit_set_layout) {
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_blit_set_layout);
            m_blit_set_layout = nullptr;
        }
    }

    // passes ----------------------------------------------------------------------------------------------------------

    void scene_renderer::add_blit_to_backbuffer_pass() {

        m_graph->add_pass("blit_to_backbuffer",
            [&](graph::pass_builder& b) {
                b.color_attachment(m_backbuffer_handle, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{0.f,0.f,0.f,1.f}));
                b.read(m_scene_color, graph::texture_usage::sampled);
                b.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                util::accessible_image src{};
                src.view = m_graph->texture_default_view(m_scene_color);
                src.sampler = m_shared->get_material_sampler();
                src.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &src);

                auto& desc = m_blit_desc_buffers[m_current_frame];
                if (!desc.buffer.buffer)
                    desc = m_plugin.get_dev()->create_descriptor_buffer(m_blit_set_layout, items, util::descriptor_buffer_type::combined, 1);
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_blit_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc, items, util::descriptor_buffer_type::combined);

                ctx.begin_rendering();
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f,
                    static_cast<f32>(m_swapchain.swapchain_extent.width),
                    static_cast<f32>(m_swapchain.swapchain_extent.height), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({0,0}, m_swapchain.swapchain_extent));
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_blit_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({desc}, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_blit_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                ctx.cmd.draw(3, 1, 0, 0);
                ctx.end_rendering();

                // Transition swapchain image to ePresentSrcKHR
                vk::ImageMemoryBarrier2 b{};
                b.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                b.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                b.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
                b.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
                b.newLayout = vk::ImageLayout::ePresentSrcKHR;
                b.image = ctx.get_image(m_backbuffer_handle);
                b.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
                vk::DependencyInfo di{}; di.imageMemoryBarrierCount = 1; di.pImageMemoryBarriers = &b;
                ctx.cmd.pipelineBarrier2(di);
            });
    }

}
