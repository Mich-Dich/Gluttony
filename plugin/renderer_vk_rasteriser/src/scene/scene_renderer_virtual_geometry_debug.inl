
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // Virtual geometry debug views
    //
    // Render modes of the category "Virtual Geometry" (modes::Vg_*). While one is active the lit path still runs (the
    // material shaders draw flat grey, camera flags.z = 2), and one pass at the end replaces the image: it redraws
    // exactly the commands the lit path drew (culled LOD draws, cluster draws, translucents, procedural meshes) with one
    // pipeline that colours them per cluster, per hierarchy level, per triangle or by projected error. Depth is cleared
    // and written again, so the debug lines drawn afterwards are tested against what the view shows.
    //
    // A cluster draw finds its cluster through gl_DrawID and the list virtual_geometry_cull.comp writes parallel to its
    // commands (m_vg_draw_cluster_handles). Nothing here runs or is bound while another render mode is active

    // CONSTANTS =======================================================================================================

    // virtual_geometry_debug.vert.glsl: PushConstants::view
    constexpr u32                                       VIRTUAL_DEBUG_VIEW_NONE = 0u;
    constexpr u32                                       VIRTUAL_DEBUG_VIEW_CLUSTERS = 1u;
    constexpr u32                                       VIRTUAL_DEBUG_VIEW_LEVELS = 2u;
    constexpr u32                                       VIRTUAL_DEBUG_VIEW_TRIANGLES = 3u;
    constexpr u32                                       VIRTUAL_DEBUG_VIEW_ERROR = 4u;

    // virtual_geometry_debug.vert.glsl: PushConstants::kind
    constexpr u32                                       VIRTUAL_DEBUG_KIND_MESH = 0u;
    constexpr u32                                       VIRTUAL_DEBUG_KIND_CLUSTERS = 1u;
    constexpr u32                                       VIRTUAL_DEBUG_KIND_PROCEDURAL = 2u;

    constexpr u32                                       VIRTUAL_DEBUG_BINDINGS = 5;     // 0 camera | 1 instances | 2 mesh cluster table | 3 clusters | 4 cluster per command

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Layout matches PushConstants in virtual_geometry_debug.vert.glsl
    struct virtual_geometry_debug_push {

        u32                                             view = VIRTUAL_DEBUG_VIEW_CLUSTERS;
        u32                                             kind = VIRTUAL_DEBUG_KIND_MESH;
        u32                                             command_base = 0;
        f32                                             error_threshold = 0.0f;
        f32                                             projection_scale = 0.0f;
        f32                                             near_plane = 0.0f;
    };
    static_assert(sizeof(virtual_geometry_debug_push) == 24, "virtual_geometry_debug_push layout mismatch");

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

    u32 scene_renderer::virtual_geometry_debug_view() const {

        if (m_active_mode == modes::Vg_clusters.id)     return VIRTUAL_DEBUG_VIEW_CLUSTERS;
        if (m_active_mode == modes::Vg_levels.id)       return VIRTUAL_DEBUG_VIEW_LEVELS;
        if (m_active_mode == modes::Vg_triangles.id)    return VIRTUAL_DEBUG_VIEW_TRIANGLES;
        if (m_active_mode == modes::Vg_error.id)        return VIRTUAL_DEBUG_VIEW_ERROR;
        return VIRTUAL_DEBUG_VIEW_NONE;
    }


    // Optional like the cluster path itself: without the pipeline the four render modes show the flat grey scene
    void scene_renderer::create_virtual_geometry_debug_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_virtual_geometry_debug_resources");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        auto* dev = m_plugin.get_dev();
        auto device = m_plugin.get_vk_device();

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto& compiler = m_plugin.get_shader_compiler_ref();
        auto vs_spv = compiler.compile_glsl_to_spirv(shader_dir / "virtual_geometry_debug.vert.glsl");
        auto fs_spv = compiler.compile_glsl_to_spirv(shader_dir / "virtual_geometry_debug.frag.glsl");
        VALIDATE(!vs_spv.empty() && !fs_spv.empty(), return, "", "Failed to compile the virtual geometry debug shaders - the debug views are unavailable")

        // gl_PrimitiveID in a fragment shader declares the SPIR-V Geometry capability, which needs the geometryShader
        // device feature (enabled when the device has it, util/builders.cpp). Without it: cluster colours in that view
        std::vector<u32> triangle_fs_spv;
        if (m_plugin.get_physical_device().getFeatures().geometryShader)
            triangle_fs_spv = compiler.compile_glsl_to_spirv(shader_dir / "virtual_geometry_debug_triangles.frag.glsl");
        else
            LOG(info, "Virtual geometry debug view 'Triangles': no geometryShader feature, showing cluster colours instead")

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);
        for (u32 binding = 1; binding < VIRTUAL_DEBUG_BINDINGS; ++binding)
            layout_items.emplace_back(binding, VKDT::eStorageBuffer, VKSF::eVertex, 1, nullptr);
        m_vg_debug_set_layout = dev->create_descriptor_set_layout(layout_items);

        vk::PushConstantRange push_range{};
        push_range.stageFlags = VKSF::eVertex | VKSF::eFragment;
        push_range.offset = 0;
        push_range.size = sizeof(virtual_geometry_debug_push);

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_vg_debug_set_layout;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_range;
        m_vg_debug_pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

        // ---- fixed state: shared mesh vertex buffer (position only), no culling, own depth test + write ----------------
        vk::VertexInputBindingDescription binding{ 0, sizeof(GLT::asset::mesh::vertex), vk::VertexInputRate::eVertex };
        vk::VertexInputAttributeDescription position{ 0, 0, vk::Format::eR32G32B32Sfloat,
            static_cast<u32>(offsetof(GLT::asset::mesh::vertex, position)) };
        vk::PipelineVertexInputStateCreateInfo vi{};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 1;
        vi.pVertexAttributeDescriptions = &position;

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

        vk::PipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.attachmentCount = 1;
        cbs.pAttachments = &blend;

        vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dyn_state{};
        dyn_state.dynamicStateCount = 2;
        dyn_state.pDynamicStates = dyn;

        const vk::Format color_format = vk::Format::eR8G8B8A8Unorm;     // scene_color
        vk::PipelineRenderingCreateInfo ri{};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_format;
        ri.depthAttachmentFormat = vk::Format::eD32Sfloat;              // scene_depth

        vk::ShaderModule vs = dev->create_shader_module(vs_spv);

        auto make = [&](const std::vector<u32>& fragment_spv) -> vk::Pipeline {

            vk::ShaderModule fs = dev->create_shader_module(fragment_spv);

            vk::PipelineShaderStageCreateInfo stages[2]{};
            stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
            stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

            vk::GraphicsPipelineCreateInfo pi{};
            pi.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;    // bound via descriptor buffers (spec: mandatory)
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
            pi.layout = m_vg_debug_pipeline_layout;
            auto [result, pipeline] = device.createGraphicsPipeline(nullptr, pi);
            device.destroyShaderModule(fs);
            return result == vk::Result::eSuccess ? pipeline : vk::Pipeline{};
        };

        m_vg_debug_pipeline = make(fs_spv);
        if (!triangle_fs_spv.empty())
            m_vg_debug_triangle_pipeline = make(triangle_fs_spv);
        device.destroyShaderModule(vs);

        VALIDATE(m_vg_debug_pipeline, , "", "Virtual geometry debug pipeline creation failed - the debug views are unavailable")

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index)
            m_vg_debug_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_vg_debug_set_layout, layout_items,
                util::descriptor_buffer_type::resource, 1);
    }


    void scene_renderer::destroy_virtual_geometry_debug_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_virtual_geometry_debug_resources");

        auto* dev = m_plugin.get_dev();
        auto device = m_plugin.get_vk_device();
        device.waitIdle();

        for (auto& descriptor_buffer : m_vg_debug_desc_buffers)
            if (descriptor_buffer.buffer.buffer) {
                dev->destroy_buffer(descriptor_buffer.buffer);
                descriptor_buffer = {};
            }

        for (vk::Pipeline* pipeline : { &m_vg_debug_pipeline, &m_vg_debug_triangle_pipeline })
            if (*pipeline) {
                device.destroyPipeline(*pipeline);
                *pipeline = nullptr;
            }
        if (m_vg_debug_pipeline_layout) {
            device.destroyPipelineLayout(m_vg_debug_pipeline_layout);
            m_vg_debug_pipeline_layout = nullptr;
        }
        if (m_vg_debug_set_layout) {
            device.destroyDescriptorSetLayout(m_vg_debug_set_layout);
            m_vg_debug_set_layout = nullptr;
        }
    }


    // After the tonemap pass, before the debug lines. Replaces scene_color and scene_depth
    void scene_renderer::add_virtual_geometry_debug_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_virtual_geometry_debug_pass");

        const u32 view = virtual_geometry_debug_view();
        if (!m_vg_debug_pipeline || view == VIRTUAL_DEBUG_VIEW_NONE)
            return;

        m_graph->add_pass("virtual_geometry_debug",
            [this](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_color, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32, 4>{ m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a }));
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
                read_virtual_geometry_commands(builder);
                if (m_vg_active)
                    builder.read(m_vg_draw_cluster_handles[m_current_frame], graph::buffer_usage::storage_read);
                builder.set_side_effects(true);
            },
            [this, view](graph::pass_context& ctx) {

                // Always begin / end: clears colour + depth even for an empty scene
                ctx.begin_rendering();
                if (m_instance_count == 0 || !m_instance_buffers[m_current_frame].buffer) {
                    ctx.end_rendering();
                    return;
                }

                // Cluster data is only read by cluster draws. Without any (no instance on the path this frame) the three
                // bindings still need a buffer: the instance buffer stands in, nothing reads it through them
                util::allocated_buffer* placeholder = &m_instance_buffers[m_current_frame];
                util::allocated_buffer* mesh_clusters = &m_shared->get_mesh_cluster_buffer_alloc_ref();
                util::allocated_buffer* clusters = &m_shared->get_cluster_data_buffer_alloc_ref();
                util::allocated_buffer* draw_clusters = m_vg_active ? m_graph->buffer_allocation(m_vg_draw_cluster_handles[m_current_frame]) : nullptr;
                const bool draw_cluster_commands = m_vg_active && draw_clusters && draw_clusters->buffer && mesh_clusters->buffer && clusters->buffer;

                using VKDT = vk::DescriptorType;
                using VKSF = vk::ShaderStageFlagBits;
                std::vector<util::descriptor_item> items;
                items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &m_camera_ubos[m_current_frame]);
                items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, &m_instance_buffers[m_current_frame]);
                items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eVertex, 1, draw_cluster_commands ? mesh_clusters : placeholder);
                items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eVertex, 1, draw_cluster_commands ? clusters : placeholder);
                items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eVertex, 1, draw_cluster_commands ? draw_clusters : placeholder);

                auto& desc_buffer = m_vg_debug_desc_buffers[m_current_frame];
                m_plugin.get_dev()->compute_descriptor_binding_offsets(m_vg_debug_set_layout, items);
                m_plugin.get_dev()->update_descriptor_buffer(desc_buffer, items, util::descriptor_buffer_type::resource);

                const vk::Pipeline pipeline = (view == VIRTUAL_DEBUG_VIEW_TRIANGLES && m_vg_debug_triangle_pipeline)
                    ? m_vg_debug_triangle_pipeline : m_vg_debug_pipeline;

                const vk::Extent2D extent{ m_extent.x, m_extent.y };
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ desc_buffer }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_vg_debug_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                // The selection thresholds of this frame's cull pass: the error view shows the numbers that pass compared
                const virtual_geometry_cull_push cull = virtual_geometry_cull_constants();
                virtual_geometry_debug_push push{};
                push.view = view;
                push.error_threshold = cull.error_threshold;
                push.projection_scale = cull.projection_scale;
                push.near_plane = cull.near_plane;
                auto push_kind = [&](u32 kind, u32 command_base) {
                    push.kind = kind;
                    push.command_base = command_base;
                    ctx.cmd.pushConstants(m_vg_debug_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                        0, sizeof(push), &push);
                };

                ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);
                constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);

                // ---- meshes drawn with their discrete LODs: the instance cull's commands, all material ranges -----------------
                push_kind(VIRTUAL_DEBUG_KIND_MESH, 0);
                {
                    const vk::Buffer cmd_buf = ctx.get_buffer(m_draw_commands_handles[m_current_frame]);
                    const vk::Buffer cnt_buf = ctx.get_buffer(m_draw_count_handles[m_current_frame]);
                    for (u32 slot : m_scene_materials) {
                        const glm::uvec2 range = m_material_ranges[slot];
                        if (range.y == 0)
                            continue;
                        ctx.cmd.drawIndexedIndirectCount(cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                            cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32), range.y, stride);
                    }
                }

                // ---- translucent asset draws (CPU list, same LOD as the translucent pass) -------------------------------------
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

                // ---- virtual geometry: one indirect call per material range. gl_DrawID counts from the range's first command,
                //      command_base makes it an index into the cluster list ----------------------------------------------------
                if (draw_cluster_commands) {
                    const vk::Buffer cmd_buf = ctx.get_buffer(m_vg_draw_commands_handles[m_current_frame]);
                    const vk::Buffer cnt_buf = ctx.get_buffer(m_vg_draw_count_handles[m_current_frame]);
                    for (u32 slot : m_scene_materials) {
                        const glm::uvec2 range = m_vg_material_ranges[slot];
                        if (range.y == 0)
                            continue;
                        push_kind(VIRTUAL_DEBUG_KIND_CLUSTERS, range.x);
                        ctx.cmd.drawIndexedIndirectCount(cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                            cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32), range.y, stride);
                    }
                }

                // ---- procedural meshes (own buffers; the instance SSBO holds them after the asset instances) -----------------
                push_kind(VIRTUAL_DEBUG_KIND_PROCEDURAL, 0);
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
