#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    constexpr vk::Format                                TRI_DENSITY_FORMAT = vk::Format::eR16Sfloat;   // blendable everywhere

    // TYPES ===========================================================================================================

    // Push constants of tri_density_blur.frag.glsl
    struct tri_density_blur_push {

        i32                                             radius = 0;
        i32                                             vertical = 0;
        f32                                             max_density = 1.0f;
        f32                                             _pad = 0.0f;
    };
    static_assert(sizeof(tri_density_blur_push) == 16, "tri_density_blur_push layout mismatch");

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // Render mode "Triangle Density" (modes::Tri_density). The material shaders draw the world flat grey (camera
    // flags.z = 2), then:
    //   1. count:  every visible mesh draw again as a POINT list into an R16F target, 1/3 per corner, additive. A
    //              triangle adds exactly 1 to the pixel(s) of its corners, whatever its size -> triangles per pixel
    //              (corners hidden behind the visible surface are rejected against the depth buffer)
    //   2. blur H: gaussian, into a second R16F target
    //   3. blur V: gaussian + log heat ramp, alpha-blended over scene_color (after tonemapping)
    // Only built while the mode is active; costs nothing otherwise

    void scene_renderer::create_tri_density_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_tri_density_resources");

        auto* dev = m_plugin.get_dev();
        auto device = m_plugin.get_vk_device();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto& compiler = m_plugin.get_shader_compiler_ref();
        auto count_vs_spv = compiler.compile_glsl_to_spirv(shader_dir / "tri_density.vert.glsl");
        auto count_fs_spv = compiler.compile_glsl_to_spirv(shader_dir / "tri_density.frag.glsl");
        auto fullscreen_spv = compiler.compile_glsl_to_spirv(shader_dir / "light_cull_debug.vert.glsl");
        auto blur_fs_spv = compiler.compile_glsl_to_spirv(shader_dir / "tri_density_blur.frag.glsl");
        VALIDATE(!count_vs_spv.empty() && !count_fs_spv.empty() && !fullscreen_spv.empty() && !blur_fs_spv.empty(), return,
            "", "Failed to compile the triangle density shaders - render mode unavailable")

        // ---- layouts ----------------------------------------------------------------------------------------------------
        std::vector<util::descriptor_item> count_items;
        count_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);   // camera
        count_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, nullptr);                     // instances
        count_items.emplace_back(2, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);            // scene depth
        m_tri_density_count_set_layout = dev->create_descriptor_set_layout(count_items);

        std::vector<util::descriptor_item> blur_items;
        blur_items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);
        m_tri_density_blur_set_layout = dev->create_descriptor_set_layout(blur_items);

        {
            vk::PipelineLayoutCreateInfo pli{};
            pli.setLayoutCount = 1;
            pli.pSetLayouts = &m_tri_density_count_set_layout;
            m_tri_density_count_pipeline_layout = device.createPipelineLayout(pli);
        }
        {
            vk::PushConstantRange range{ VKSF::eFragment, 0, sizeof(tri_density_blur_push) };
            vk::PipelineLayoutCreateInfo pli{};
            pli.setLayoutCount = 1;
            pli.pSetLayouts = &m_tri_density_blur_set_layout;
            pli.pushConstantRangeCount = 1;
            pli.pPushConstantRanges = &range;
            m_tri_density_blur_pipeline_layout = device.createPipelineLayout(pli);
        }

        vk::ShaderModule count_vs = dev->create_shader_module(count_vs_spv);
        vk::ShaderModule count_fs = dev->create_shader_module(count_fs_spv);
        vk::ShaderModule fullscreen_vs = dev->create_shader_module(fullscreen_spv);
        vk::ShaderModule blur_fs = dev->create_shader_module(blur_fs_spv);

        // ---- shared fixed state -----------------------------------------------------------------------------------------
        vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1; vp.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rs{};
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eNone;
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo ds{};           // no depth attachment: occlusion is tested in the shader

        vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dyn_state{};
        dyn_state.dynamicStateCount = 2;
        dyn_state.pDynamicStates = dyn;

        const auto all_channels = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        auto make = [&](vk::ShaderModule vs, vk::ShaderModule fs, const vk::PipelineVertexInputStateCreateInfo& vi,
            vk::PrimitiveTopology topology, vk::Format format, const vk::PipelineColorBlendAttachmentState& blend,
            vk::PipelineLayout layout) -> vk::Pipeline {

            vk::PipelineShaderStageCreateInfo stages[2]{};
            stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
            stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

            vk::PipelineInputAssemblyStateCreateInfo ia{};
            ia.topology = topology;

            vk::PipelineColorBlendStateCreateInfo cbs{};
            cbs.attachmentCount = 1;
            cbs.pAttachments = &blend;

            vk::PipelineRenderingCreateInfo ri{};
            ri.colorAttachmentCount = 1;
            ri.pColorAttachmentFormats = &format;

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
            pi.layout = layout;
            auto [result, pipeline] = device.createGraphicsPipeline(nullptr, pi);
            return result == vk::Result::eSuccess ? pipeline : vk::Pipeline{};
        };

        // 1. count: mesh positions only, index buffer reinterpreted as a point list, additive
        {
            vk::VertexInputBindingDescription binding{ 0, sizeof(GLT::asset::mesh::vertex), vk::VertexInputRate::eVertex };
            vk::VertexInputAttributeDescription position{ 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, position) };
            vk::PipelineVertexInputStateCreateInfo vi{};
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &binding;
            vi.vertexAttributeDescriptionCount = 1;
            vi.pVertexAttributeDescriptions = &position;

            vk::PipelineColorBlendAttachmentState add{};
            add.blendEnable = VK_TRUE;
            add.srcColorBlendFactor = vk::BlendFactor::eOne;
            add.dstColorBlendFactor = vk::BlendFactor::eOne;
            add.colorBlendOp = vk::BlendOp::eAdd;
            add.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            add.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            add.alphaBlendOp = vk::BlendOp::eAdd;
            add.colorWriteMask = vk::ColorComponentFlagBits::eR;

            m_tri_density_count_pipeline = make(count_vs, count_fs, vi, vk::PrimitiveTopology::ePointList,
                TRI_DENSITY_FORMAT, add, m_tri_density_count_pipeline_layout);
        }

        // 2. horizontal blur (R16F, no blend) / 3. vertical blur + heat colour, blended over scene_color
        {
            vk::PipelineVertexInputStateCreateInfo vi{};

            vk::PipelineColorBlendAttachmentState opaque{};
            opaque.colorWriteMask = all_channels;
            m_tri_density_blur_h_pipeline = make(fullscreen_vs, blur_fs, vi, vk::PrimitiveTopology::eTriangleList,
                TRI_DENSITY_FORMAT, opaque, m_tri_density_blur_pipeline_layout);

            vk::PipelineColorBlendAttachmentState over{};
            over.blendEnable = VK_TRUE;
            over.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            over.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            over.colorBlendOp = vk::BlendOp::eAdd;
            over.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            over.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            over.alphaBlendOp = vk::BlendOp::eAdd;
            over.colorWriteMask = all_channels;
            m_tri_density_blur_v_pipeline = make(fullscreen_vs, blur_fs, vi, vk::PrimitiveTopology::eTriangleList,
                vk::Format::eR8G8B8A8Unorm, over, m_tri_density_blur_pipeline_layout);
        }

        device.destroyShaderModule(count_vs);
        device.destroyShaderModule(count_fs);
        device.destroyShaderModule(fullscreen_vs);
        device.destroyShaderModule(blur_fs);

        VALIDATE(m_tri_density_count_pipeline && m_tri_density_blur_h_pipeline && m_tri_density_blur_v_pipeline, ,
            "", "Triangle density pipeline creation failed - render mode unavailable")

        for (u32 frame = 0; frame < MAX_CONCURRENT_FRAMES; ++frame) {
            m_tri_density_count_desc[frame] = dev->create_descriptor_buffer(m_tri_density_count_set_layout, count_items,
                util::descriptor_buffer_type::combined, 1);
            m_tri_density_blur_h_desc[frame] = dev->create_descriptor_buffer(m_tri_density_blur_set_layout, blur_items,
                util::descriptor_buffer_type::combined, 1);
            m_tri_density_blur_v_desc[frame] = dev->create_descriptor_buffer(m_tri_density_blur_set_layout, blur_items,
                util::descriptor_buffer_type::combined, 1);
        }
    }


    void scene_renderer::destroy_tri_density_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_tri_density_resources");

        auto* dev = m_plugin.get_dev();
        auto device = m_plugin.get_vk_device();
        device.waitIdle();

        for (auto* set : { &m_tri_density_count_desc, &m_tri_density_blur_h_desc, &m_tri_density_blur_v_desc })
            for (auto& d : *set)
                if (d.buffer.buffer) {
                    dev->destroy_buffer(d.buffer);
                    d = {};
                }

        for (vk::Pipeline* p : { &m_tri_density_count_pipeline, &m_tri_density_blur_h_pipeline, &m_tri_density_blur_v_pipeline })
            if (*p) {
                device.destroyPipeline(*p);
                *p = nullptr;
            }
        for (vk::PipelineLayout* l : { &m_tri_density_count_pipeline_layout, &m_tri_density_blur_pipeline_layout })
            if (*l) {
                device.destroyPipelineLayout(*l);
                *l = nullptr;
            }
        for (vk::DescriptorSetLayout* l : { &m_tri_density_count_set_layout, &m_tri_density_blur_set_layout })
            if (*l) {
                device.destroyDescriptorSetLayout(*l);
                *l = nullptr;
            }
    }


    void scene_renderer::add_tri_density_passes() {
        GLT_PROFILE_SCOPE("scene_renderer::add_tri_density_passes");

        if (!m_tri_density_count_pipeline || !m_tri_density_blur_h_pipeline || !m_tri_density_blur_v_pipeline)
            return;

        using VKIU = vk::ImageUsageFlagBits;
        graph::texture_desc desc{};
        desc.format = TRI_DENSITY_FORMAT;
        desc.width = m_extent.x;
        desc.height = m_extent.y;
        desc.usage = VKIU::eColorAttachment | VKIU::eSampled;
        desc.persistent = true;
        desc.name = "tri_density";
        const graph::texture_handle density = m_graph->create_texture(desc);
        desc.name = "tri_density_blur";
        const graph::texture_handle blurred = m_graph->create_texture(desc);

        const settings::visual& visual = m_plugin.visual_settings();
        tri_density_blur_push push{};
        push.radius = static_cast<i32>(std::min(visual.tri_density_radius, 32u));
        push.max_density = std::isfinite(visual.tri_density_max) ? std::clamp(visual.tri_density_max, 0.01f, 16.0f) : 1.0f;

        auto set_viewport = [this](vk::CommandBuffer cmd) {
            const vk::Extent2D extent{ m_extent.x, m_extent.y };
            cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.f, 1.f));
            cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
        };

        // 1. count ---------------------------------------------------------------------------------------------------------
        m_graph->add_pass("tri_density_count",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(density, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32, 4>{ 0.f, 0.f, 0.f, 0.f }));
                builder.read(m_depth_target, graph::texture_usage::depth_sampled);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
            },
            [this, set_viewport](graph::pass_context& ctx) {

                util::accessible_image depth{};
                depth.view = m_graph->texture_default_view(m_depth_target);
                depth.sampler = m_shared->get_depth_sampler();
                depth.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    1, &m_camera_ubos[m_current_frame]);
                items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 1, &m_instance_buffers[m_current_frame]);
                items.emplace_back(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &depth);
                auto& desc_buffer = m_tri_density_count_desc[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_tri_density_count_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc_buffer, items, util::descriptor_buffer_type::combined);

                ctx.begin_rendering();
                if (m_instance_count == 0) {
                    ctx.end_rendering();
                    return;
                }
                set_viewport(ctx.cmd);
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_tri_density_count_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc_buffer }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_tri_density_count_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                // Culled asset draws: the main view's indirect commands, all material ranges, one pipeline
                ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);
                const vk::Buffer cmd_buf = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                const vk::Buffer cnt_buf = ctx.get_buffer(m_draw_count_handles[m_current_frame]);
                constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);
                for (u32 slot : m_scene_materials) {
                    const glm::uvec2 range = m_material_ranges[slot];
                    if (range.y == 0)
                        continue;
                    ctx.cmd.drawIndexedIndirectCount(cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                        cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32), range.y, stride);
                }

                // Translucent asset draws (CPU list, same LOD as the translucent pass)
                for (const translucent_draw& draw : m_translucent_draws) {
                    if (draw.procedural)
                        continue;
                    const mesh_slot* slot = m_shared->get_mesh(draw.mesh);
                    if (!slot || !slot->alive || draw.submesh >= slot->submesh_count)
                        continue;
                    const size_t index = static_cast<size_t>(draw.lod) * slot->submesh_count + draw.submesh;
                    if (index >= slot->submesh_ranges.size() || slot->submesh_ranges[index].y < 3)
                        continue;
                    const glm::uvec2 range = slot->submesh_ranges[index];
                    ctx.cmd.drawIndexed(range.y, 1, slot->index_offset + range.x, static_cast<i32>(slot->vertex_offset), draw.instance);
                }

                // Procedural meshes (own buffers; the instance SSBO holds them after the asset instances)
                for (size_t i = 0; i < m_procedural_draw_slots.size(); ++i) {
                    const auto& pm = m_procedural_meshes[m_procedural_draw_slots[i]];
                    if (!pm.alive || !pm.vertex_buffer.buffer || !pm.index_buffer.buffer)
                        continue;
                    ctx.cmd.bindVertexBuffers(0, { pm.vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(pm.index_buffer.buffer, 0, vk::IndexType::eUint32);
                    ctx.cmd.drawIndexed(pm.index_count, 1, 0, 0, m_asset_instance_count + static_cast<u32>(i));
                }

                ctx.end_rendering();
            });

        // 2. horizontal blur -------------------------------------------------------------------------------------------------
        m_graph->add_pass("tri_density_blur_h",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(blurred, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eStore);
                builder.read(density, graph::texture_usage::sampled);
            },
            [this, density, push, set_viewport](graph::pass_context& ctx) {

                util::accessible_image source{};
                source.view = m_graph->texture_default_view(density);
                source.sampler = m_shared->get_material_sampler();      // texelFetch: filtering irrelevant
                source.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &source);
                auto& desc_buffer = m_tri_density_blur_h_desc[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_tri_density_blur_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc_buffer, items, util::descriptor_buffer_type::combined);

                tri_density_blur_push p = push;
                p.vertical = 0;
                ctx.begin_rendering();
                set_viewport(ctx.cmd);
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_tri_density_blur_h_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc_buffer }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_tri_density_blur_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                ctx.cmd.pushConstants(m_tri_density_blur_pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(p), &p);
                ctx.cmd.draw(3, 1, 0, 0);
                ctx.end_rendering();
            });

        // 3. vertical blur + heat colour over scene_color ---------------------------------------------------------------------
        m_graph->add_pass("tri_density_overlay",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_color, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.read(blurred, graph::texture_usage::sampled);
                builder.set_side_effects(true);
            },
            [this, blurred, push, set_viewport](graph::pass_context& ctx) {

                util::accessible_image source{};
                source.view = m_graph->texture_default_view(blurred);
                source.sampler = m_shared->get_material_sampler();
                source.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &source);
                auto& desc_buffer = m_tri_density_blur_v_desc[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_tri_density_blur_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc_buffer, items, util::descriptor_buffer_type::combined);

                tri_density_blur_push p = push;
                p.vertical = 1;
                ctx.begin_rendering();
                set_viewport(ctx.cmd);
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_tri_density_blur_v_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc_buffer }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_tri_density_blur_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                ctx.cmd.pushConstants(m_tri_density_blur_pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(p), &p);
                ctx.cmd.draw(3, 1, 0, 0);
                ctx.end_rendering();
            });
    }

}
