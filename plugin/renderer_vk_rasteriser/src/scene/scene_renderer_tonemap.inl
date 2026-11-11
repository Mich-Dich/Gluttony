#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Push constants of tonemap.frag.glsl
    struct tonemap_push_constants {

        glm::vec4                                       clear_color{0.f};       // display-referred, used for background pixels
        f32                                             exposure = 1.f;         // linear multiplier (2^EV)
        u32                                             tonemapper = 0;
        u32                                             background_mode = 0;    // 1 = sky drew a HDR background
        u32                                             _pad1 = 0;
    };
    static_assert(sizeof(tonemap_push_constants) == 32, "tonemap_push_constants layout mismatch");

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

    // Fullscreen triangle (blit.vert) -> tonemap.frag. Renders into scene_color (R8G8B8A8_UNORM, sRGB-encoded values),
    // which is the offscreen output image or the source of the backbuffer blit
    void scene_renderer::create_tonemap_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::create_tonemap_pipeline");

        auto* dev = m_plugin.get_dev();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);    // HDR colour
        layout_items.emplace_back(1, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);    // depth (background mask)
        m_tonemap_set_layout = dev->create_descriptor_set_layout(layout_items);

        vk::PushConstantRange push_range{};
        push_range.stageFlags = VKSF::eFragment;
        push_range.offset = 0;
        push_range.size = sizeof(tonemap_push_constants);

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_tonemap_set_layout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &push_range;
        m_tonemap_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pli);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "blit.vert.glsl");
        auto fs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "tonemap.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile tonemap shaders");

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
        pi.layout = m_tonemap_pipeline_layout;

        auto [result, pipeline] = m_plugin.get_vk_device().createGraphicsPipeline(nullptr, pi);
        ASSERT(result == vk::Result::eSuccess, "", "tonemap pipeline creation failed");
        m_tonemap_pipeline = pipeline;

        m_plugin.get_vk_device().destroyShaderModule(vs);
        m_plugin.get_vk_device().destroyShaderModule(fs);

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index)
            m_tonemap_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_tonemap_set_layout, layout_items,
                util::descriptor_buffer_type::combined, 1);
    }


    void scene_renderer::destroy_tonemap_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_tonemap_pipeline");

        auto* dev = m_plugin.get_dev();
        for (auto& d : m_tonemap_desc_buffers)
            if (d.buffer.buffer) {
                dev->destroy_buffer(d.buffer);
                d = {};
            }

        if (m_tonemap_pipeline) {
            m_plugin.get_vk_device().destroyPipeline(m_tonemap_pipeline);
            m_tonemap_pipeline = nullptr;
        }

        if (m_tonemap_pipeline_layout) {
            m_plugin.get_vk_device().destroyPipelineLayout(m_tonemap_pipeline_layout);
            m_tonemap_pipeline_layout = nullptr;
        }

        if (m_tonemap_set_layout) {
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_tonemap_set_layout);
            m_tonemap_set_layout = nullptr;
        }
    }

    // passes ----------------------------------------------------------------------------------------------------------

    void scene_renderer::add_tonemap_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_tonemap_pass");

        m_graph->add_pass("tonemap",
            [&](graph::pass_builder& b) {
                b.color_attachment(m_scene_color, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore);
                b.read(m_scene_hdr, graph::texture_usage::sampled);
                b.read(m_depth_target, graph::texture_usage::depth_sampled);
                b.set_side_effects(true);       // offscreen targets: nothing in the graph reads scene_color afterwards
            },
            [this](graph::pass_context& ctx) {

                // Descriptors point at this frame's views (the targets can be recreated on resize)
                util::accessible_image hdr{};
                hdr.view = m_graph->texture_default_view(m_scene_hdr);
                hdr.sampler = m_shared->get_material_sampler();             // texelFetch: filtering is irrelevant
                hdr.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

                util::accessible_image depth{};
                depth.view = m_graph->texture_default_view(m_depth_target);
                depth.sampler = m_shared->get_depth_sampler();
                depth.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &hdr);
                items.emplace_back(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &depth);

                auto& desc = m_tonemap_desc_buffers[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_tonemap_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc, items, util::descriptor_buffer_type::combined);

                const settings::visual& visual = m_plugin.visual_settings();
                tonemap_push_constants push{};
                push.clear_color = m_clear_color;
                push.exposure = std::exp2(std::clamp(visual.exposure_ev, -20.0f, 20.0f));
                push.tonemapper = std::min(visual.tonemapper, 3u);
                push.background_mode = m_env.enabled ? 1u : 0u;
                if (debug_view_active()) {
                    // Debug colours are display values: no exposure / curve, flat dark background instead of the sky
                    push.exposure = 1.0f;
                    push.tonemapper = 3u;
                    push.background_mode = 0u;
                    push.clear_color = glm::vec4(0.06f, 0.06f, 0.07f, 1.0f);
                }

                const vk::Extent2D extent{ m_extent.x, m_extent.y };

                ctx.begin_rendering();
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_tonemap_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_tonemap_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                ctx.cmd.pushConstants(m_tonemap_pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(push), &push);
                ctx.cmd.draw(3, 1, 0, 0);
                ctx.end_rendering();
            });
    }

}
