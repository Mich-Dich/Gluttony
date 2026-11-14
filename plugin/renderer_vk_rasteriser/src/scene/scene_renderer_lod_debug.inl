#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Layout matches PushConstants in lod_debug.vert.glsl
    struct lod_debug_push_constants {

        u32                                             source = 0;             // 0 = instance LOD buffer, 1 = procedural
        u32                                             _pad = 0;
    };
    static_assert(sizeof(lod_debug_push_constants) == 8, "lod_debug_push_constants layout mismatch");

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

    // Render mode "Level of Detail": every mesh unshaded, coloured by the LOD the main-view cull picked for its instance
    // (cull.comp writes it to the per-instance LOD buffer, binding 9). Uses the same indirect commands as the lit path,
    // so what you see is exactly what the lit path draws

    void scene_renderer::create_lod_debug_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::create_lod_debug_pipeline");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        auto* dev = m_plugin.get_dev();

        // 0 camera | 1 instances | 2 per-instance LOD
        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex, 1, nullptr);
        layout_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, nullptr);
        layout_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eVertex, 1, nullptr);
        m_lod_debug_set_layout = dev->create_descriptor_set_layout(layout_items);

        vk::PushConstantRange push_range{};
        push_range.stageFlags = VKSF::eVertex;
        push_range.offset = 0;
        push_range.size = sizeof(lod_debug_push_constants);

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_lod_debug_set_layout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &push_range;
        m_lod_debug_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pli);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "lod_debug.vert.glsl");
        auto fs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "lod_debug.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile LOD debug shaders")

        vk::ShaderModule vs = dev->create_shader_module(vs_spv);
        vk::ShaderModule fs = dev->create_shader_module(fs_spv);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
        stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

        // Shared mesh vertex buffer: position + normal only
        vk::VertexInputBindingDescription binding{ 0, sizeof(GLT::asset::mesh::vertex), vk::VertexInputRate::eVertex };
        std::array<vk::VertexInputAttributeDescription, 2> attributes{};
        attributes[0] = { 0, 0, vk::Format::eR32G32B32Sfloat, static_cast<u32>(offsetof(GLT::asset::mesh::vertex, position)) };
        attributes[1] = { 1, 0, vk::Format::eR32G32B32Sfloat, static_cast<u32>(offsetof(GLT::asset::mesh::vertex, normal)) };

        vk::PipelineVertexInputStateCreateInfo vi{};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = static_cast<u32>(attributes.size());
        vi.pVertexAttributeDescriptions = attributes.data();

        vk::PipelineInputAssemblyStateCreateInfo ia{};
        ia.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1; vp.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rs{};
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eNone;          // double-sided materials exist; the depth test sorts it out
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo ds{};
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = vk::CompareOp::eLessOrEqual;

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
        ri.depthAttachmentFormat = vk::Format::eD32Sfloat;              // depth_target

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
        pi.layout = m_lod_debug_pipeline_layout;

        auto [result, pipeline] = m_plugin.get_vk_device().createGraphicsPipeline(nullptr, pi);
        ASSERT(result == vk::Result::eSuccess, "", "LOD debug pipeline creation failed")
        m_lod_debug_pipeline = pipeline;

        m_plugin.get_vk_device().destroyShaderModule(vs);
        m_plugin.get_vk_device().destroyShaderModule(fs);

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index)
            m_lod_debug_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_lod_debug_set_layout, layout_items,
                util::descriptor_buffer_type::resource, 1);
    }


    void scene_renderer::destroy_lod_debug_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_lod_debug_pipeline");

        auto* dev = m_plugin.get_dev();
        for (auto& d : m_lod_debug_desc_buffers)
            if (d.buffer.buffer) {
                dev->destroy_buffer(d.buffer);
                d = {};
            }

        if (m_lod_debug_pipeline) {
            m_plugin.get_vk_device().destroyPipeline(m_lod_debug_pipeline);
            m_lod_debug_pipeline = nullptr;
        }
        if (m_lod_debug_pipeline_layout) {
            m_plugin.get_vk_device().destroyPipelineLayout(m_lod_debug_pipeline_layout);
            m_lod_debug_pipeline_layout = nullptr;
        }
        if (m_lod_debug_set_layout) {
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_lod_debug_set_layout);
            m_lod_debug_set_layout = nullptr;
        }
    }


    void scene_renderer::add_lod_debug_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_lod_debug_pass");

        m_graph->add_pass("lod_debug",
            [&](graph::pass_builder& b) {
                b.color_attachment(m_scene_color, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32, 4>{ m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a }));
                b.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                b.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                b.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
                b.read(m_instance_lod_handles[m_current_frame], graph::buffer_usage::storage_read);
                b.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                // Always begin/end: clears colour + depth (debug draw reads the depth afterwards) even for an empty scene
                ctx.begin_rendering();

                util::allocated_buffer* lods = m_graph->buffer_allocation(m_instance_lod_handles[m_current_frame]);
                if (m_instance_count == 0 || !lods || !m_instance_buffers[m_current_frame].buffer) {
                    ctx.end_rendering();
                    return;
                }

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1, &m_camera_ubos[m_current_frame]);
                items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 1, &m_instance_buffers[m_current_frame]);
                items.emplace_back(2, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 1, lods);

                auto& desc = m_lod_debug_desc_buffers[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_lod_debug_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc, items, util::descriptor_buffer_type::resource);

                const vk::Extent2D extent{ m_extent.x, m_extent.y };
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_lod_debug_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_lod_debug_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                auto push = [&](u32 source) {
                    const lod_debug_push_constants pc{ source, 0 };
                    ctx.cmd.pushConstants(m_lod_debug_pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(pc), &pc);
                };

                auto bind_shared_geometry = [&]() {
                    ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                    ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);
                };

                // ---- Asset-backed opaque draws: the cull's per-material command ranges, one pipeline for all -----
                bind_shared_geometry();
                push(0);
                const vk::Buffer cmd_buf = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                const vk::Buffer cnt_buf = ctx.get_buffer(m_draw_count_handles[m_current_frame]);
                constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);
                for (u32 slot : m_scene_materials) {
                    const glm::uvec2 range = m_material_ranges[slot];
                    if (range.y == 0)
                        continue;
                    ctx.cmd.drawIndexedIndirectCount(
                        cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                        cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32),
                        range.y, stride);
                }

                // ---- Asset-backed translucents (CPU list, already at the LOD the cull picked) ---------------------
                for (const translucent_draw& draw : m_translucent_draws) {
                    if (draw.procedural)
                        continue;                                   // drawn with the procedurals below
                    const mesh_slot* slot = m_shared->get_mesh(draw.mesh);
                    if (!slot || !slot->alive || draw.submesh >= slot->submesh_ranges.size())
                        continue;
                    const glm::uvec2 range = slot->submesh_ranges[draw.submesh];
                    if (range.y < 3)
                        continue;
                    ctx.cmd.drawIndexed(range.y, 1, slot->index_offset + range.x, static_cast<i32>(slot->vertex_offset), draw.instance);
                }

                // ---- Procedural meshes (no LOD chain) ----------------------------------------------------------
                push(1);
                for (size_t i = 0; i < m_procedural_draw_slots.size(); ++i) {
                    const auto& pm = m_procedural_meshes[m_procedural_draw_slots[i]];
                    if (!pm.alive || !pm.vertex_buffer.buffer || !pm.index_buffer.buffer || pm.index_count == 0)
                        continue;
                    ctx.cmd.bindVertexBuffers(0, { pm.vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(pm.index_buffer.buffer, 0, vk::IndexType::eUint32);
                    ctx.cmd.drawIndexed(pm.index_count, 1, 0, 0, m_asset_instance_count + static_cast<u32>(i));
                }

                ctx.end_rendering();
            });
    }

}
