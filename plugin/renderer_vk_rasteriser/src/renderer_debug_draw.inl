
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

    void renderer::draw_line(const GLT::render::debug_line_primitive& line) {

        std::lock_guard lock(m_debug_mutex);

        debug_primitive_record rec;
        rec.type = debug_primitive_record::kind::line;
        rec.verts[0] = { line.from, 0.0f, line.color };
        rec.verts[1] = { line.to, 0.0f, line.color };
        rec.vertex_count = 2;
        rec.remaining_lifetime = line.lifetime;
        rec.depth_test = line.depth_test;
        m_debug_primitives.push_back(rec);
    }


    void renderer::draw_triangle(const GLT::render::debug_triangle_primitive& tri) {

        std::lock_guard lock(m_debug_mutex);

        debug_primitive_record rec;
        rec.type = debug_primitive_record::kind::triangle;
        rec.verts[0] = { tri.a, 0.0f, tri.color };
        rec.verts[1] = { tri.b, 0.0f, tri.color };
        rec.verts[2] = { tri.c, 0.0f, tri.color };
        rec.vertex_count = 3;
        rec.remaining_lifetime = tri.lifetime;
        rec.depth_test = tri.depth_test;
        m_debug_primitives.push_back(rec);
    }


    void renderer::draw_aabb(const GLT::render::debug_aabb_primitive& box) {

        const glm::vec3 c[8] = {
            { box.min.x, box.min.y, box.min.z },
            { box.max.x, box.min.y, box.min.z },
            { box.max.x, box.max.y, box.min.z },
            { box.min.x, box.max.y, box.min.z },
            { box.min.x, box.min.y, box.max.z },
            { box.max.x, box.min.y, box.max.z },
            { box.max.x, box.max.y, box.max.z },
            { box.min.x, box.max.y, box.max.z },
        };
        static constexpr u32 e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };

        std::lock_guard lock(m_debug_mutex);

        for (auto& [a, b] : e) {

            debug_primitive_record rec;
            rec.type = debug_primitive_record::kind::line;
            rec.verts[0] = { c[a], 0.0f, box.color };
            rec.verts[1] = { c[b], 0.0f, box.color };
            rec.vertex_count = 2;
            rec.remaining_lifetime = box.lifetime;
            rec.depth_test = box.depth_test;
            m_debug_primitives.push_back(rec);
        }
    }


    void renderer::clear() {

        std::lock_guard lock(m_debug_mutex);
        m_debug_primitives.clear();
        m_watched_meshes.clear();
        m_watched_entities.clear();
    }


    void renderer::set_draw_mesh_aabb(GLT::asset::handle mesh_handle, const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        if (mesh_handle == INVALID_HANDLE)
            return;

        std::lock_guard lock(m_debug_mutex);

        for (auto& entry : m_watched_meshes) {
            if (entry.handle == mesh_handle) {
                entry.color = color;
                entry.remaining_lifetime = lifetime;
                entry.depth_test = depth_test;
                return;
            }
        }

        watched_mesh w{};
        w.handle = mesh_handle;
        w.color = color;
        w.remaining_lifetime = lifetime;
        w.depth_test = depth_test;
        m_watched_meshes.push_back(w);
    }


    void renderer::draw_mesh_aabb(std::span<const GLT::world::entity_id> ids, const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        if (ids.empty())
            return;

        std::lock_guard lock(m_debug_mutex);

        for (const auto& id : ids) {

            if (!id.is_valid())
                continue;

            // Update an existing entry rather than stacking duplicates - matches set_draw_mesh_aabb()'s contract
            bool found = false;
            for (auto& entry : m_watched_entities) {
                if (entry.id == id) {
                    entry.color = color;
                    entry.remaining_lifetime = lifetime;
                    entry.depth_test = depth_test;
                    found = true;
                    break;
                }
            }
            if (found)
                continue;

            watched_entity w{};
            w.id = id;
            w.color = color;
            w.remaining_lifetime = lifetime;
            w.depth_test = depth_test;
            m_watched_entities.push_back(w);
        }
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::process_debug_primitives() {

        const f64 now = std::chrono::duration<f64>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const f32 delta = (m_debug_last_frame_time > 0.0) ? static_cast<f32>(now - m_debug_last_frame_time) : 0.0f;
        m_debug_last_frame_time = now;

        std::vector<debug_primitive_record> pending;
        std::vector<watched_mesh> watched_meshes_snapshot;
        std::vector<watched_entity> watched_entities_snapshot;

        {
            std::lock_guard lock(m_debug_mutex);

            pending = m_debug_primitives;

            std::erase_if(m_debug_primitives, [delta](debug_primitive_record& p) {
                if (p.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER)
                    return false;
                p.remaining_lifetime -= delta;
                return p.remaining_lifetime <= 0.0f;
            });
            std::erase_if(m_watched_meshes, [delta](watched_mesh& w) {
                if (w.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER)
                    return false;
                w.remaining_lifetime -= delta;
                return w.remaining_lifetime <= 0.0f;
            });
            std::erase_if(m_watched_entities, [delta](watched_entity& w) {
                if (w.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER)
                    return false;
                w.remaining_lifetime -= delta;
                return w.remaining_lifetime <= 0.0f;
            });

            watched_meshes_snapshot   = m_watched_meshes;
            watched_entities_snapshot = m_watched_entities;
        }

        static constexpr u32 e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };

        auto emit_aabb = [&](const glm::vec3& local_min, const glm::vec3& local_max, const glm::mat4& transform,
            const glm::vec4& color, const bool depth_test) {

            glm::vec3 corners[8] = {
                { local_min.x, local_min.y, local_min.z },
                { local_max.x, local_min.y, local_min.z },
                { local_max.x, local_max.y, local_min.z },
                { local_min.x, local_max.y, local_min.z },
                { local_min.x, local_min.y, local_max.z },
                { local_max.x, local_min.y, local_max.z },
                { local_max.x, local_max.y, local_max.z },
                { local_min.x, local_max.y, local_max.z },
            };
            for (auto& v : corners)
                v = glm::vec3(transform * glm::vec4(v, 1.0f));

            for (auto& [a, b] : e) {
                debug_primitive_record rec;
                rec.type = debug_primitive_record::kind::line;
                rec.verts[0] = { corners[a], 0.0f, color };
                rec.verts[1] = { corners[b], 0.0f, color };
                rec.vertex_count = 2;
                rec.remaining_lifetime = 0.0f;      // transient, rebuilt each frame from the watch lists
                rec.depth_test = depth_test;
                pending.push_back(rec);
            }
        };

        // Path 1: every instance of a given mesh handle (set_draw_mesh_aabb)
        for (const auto& watch : watched_meshes_snapshot) {

            const mesh_slot* slot = find_slot(watch.handle);
            if (!slot || !slot->alive)
                continue;

            for (const auto& inst : m_scene_instances) {
                if (inst.mesh != watch.handle)
                    continue;
                emit_aabb(slot->local_min, slot->local_max, inst.transform, watch.color, watch.depth_test);
            }
        }

        // Path 2: instances selected by entity id (draw_mesh_aabb) ------------------------------------------------
        // No world access is required: m_scene_instances is the copy handed to us via submit_scene(), and every
        // instance that matters here carries its entity id. Instances of an entity whose mesh was unloaded this
        // frame (find_slot returns nullptr) are silently skipped.
        for (const auto& watch : watched_entities_snapshot) {
            for (const auto& inst : m_scene_instances) {

                if (inst.entity != watch.id)
                    continue;

                const mesh_slot* slot = find_slot(inst.mesh);
                if (!slot || !slot->alive)
                    continue;

                emit_aabb(slot->local_min, slot->local_max, inst.transform, watch.color, watch.depth_test);
            }
        }

        // ---- bucket building unchanged -------------------------------------------------
        frame_debug_layout& layout = m_debug_layouts[m_current_frame];
        layout = {};

        std::vector<debug_vertex> vertices;
        vertices.reserve(pending.size() * 3);

        auto append_bucket = [&](debug_primitive_record::kind kind, bool depth) -> std::pair<u32, u32> {
            const u32 offset = static_cast<u32>(vertices.size());
            u32 count = 0;
            for (const auto& p : pending) {
                if (p.type != kind || p.depth_test != depth)
                    continue;
                for (u8 i = 0; i < p.vertex_count; ++i)
                    vertices.push_back(p.verts[i]);
                count += p.vertex_count;
            }
            return { offset, count };
        };

        std::tie(layout.line_depth_offset, layout.line_depth_count) = append_bucket(debug_primitive_record::kind::line, true);
        std::tie(layout.line_overlay_offset, layout.line_overlay_count) = append_bucket(debug_primitive_record::kind::line, false);
        std::tie(layout.tri_depth_offset, layout.tri_depth_count) = append_bucket(debug_primitive_record::kind::triangle, true);
        std::tie(layout.tri_overlay_offset, layout.tri_overlay_count) = append_bucket(debug_primitive_record::kind::triangle, false);

        if (vertices.empty())
            return;

        if (vertices.size() > MAX_DEBUG_VERTICES) {
            LOG(warn, "Debug draw budget exceeded ({} > {}), skipping frame", vertices.size(), MAX_DEBUG_VERTICES);
            layout = {};
            return;
        }

        util::allocated_buffer& buffer = m_debug_vertex_buffers[m_current_frame];
        VALIDATE(buffer.buffer, return, "", "Debug vertex buffer not initialised")

        void* mapped = m_dev->map_buffer(buffer);
        std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug_vertex));
        m_dev->unmap_buffer(buffer);
    }


    void renderer::add_debug_draw_pass() {

        if (!m_world_render_enabled)
            return;

        const frame_debug_layout& layout = m_debug_layouts[m_current_frame];
        const u32 total = layout.line_depth_count + layout.line_overlay_count +
                          layout.tri_depth_count  + layout.tri_overlay_count;
        if (total == 0)
            return;

        m_graph->add_pass("debug_draw",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_color, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.set_side_effects(true);
            },
            [this, layout](graph::pass_context& ctx) {

                ctx.begin_rendering();

                ctx.cmd.bindVertexBuffers(0, { m_debug_vertex_buffers[m_current_frame].buffer }, { 0 });
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(m_render_size.x), static_cast<f32>(m_render_size.y), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({0, 0}, {static_cast<u32>(m_render_size.x), static_cast<u32>(m_render_size.y)}));

                m_dev->bind_descriptor_buffer({ m_debug_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_debug_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                if (layout.line_depth_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_line_depth);
                    ctx.cmd.draw(layout.line_depth_count, 1, layout.line_depth_offset, 0);
                    m_frame_draw_calls++;
                }
                if (layout.line_overlay_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_line_overlay);
                    ctx.cmd.draw(layout.line_overlay_count, 1, layout.line_overlay_offset, 0);
                    m_frame_draw_calls++;
                }
                if (layout.tri_depth_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_tri_depth);
                    ctx.cmd.draw(layout.tri_depth_count, 1, layout.tri_depth_offset, 0);
                    m_frame_draw_calls++;
                }
                if (layout.tri_overlay_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_tri_overlay);
                    ctx.cmd.draw(layout.tri_overlay_count, 1, layout.tri_overlay_offset, 0);
                    m_frame_draw_calls++;
                }

                ctx.end_rendering();
                m_frame_render_passes++;
            });
    }


    void renderer::create_debug_draw_resources() {

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // ---- Descriptor set + layout --------------------------------------------------------------------------------
        // Single UBO binding pointing at the per-frame camera buffer. The shader only reads `view_proj` (offset 0),
        // so the 496-byte struct shrinks comfortably to a 64-byte declaration

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex, 1, nullptr);
        m_debug_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_debug_set_layout;
        m_debug_pipeline_layout = m_device.createPipelineLayout(pli);

        // ---- Shaders ------------------------------------------------------------------------------------------------

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "debug_line.vert.glsl");
        auto fs_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "debug_line.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile debug draw shaders");

        vk::ShaderModule vs = m_dev->create_shader_module(vs_spv);
        vk::ShaderModule fs = m_dev->create_shader_module(fs_spv);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
        stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

        // ---- Vertex input -------------------------------------------------------------------------------------------

        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(debug_vertex);
        binding.inputRate = vk::VertexInputRate::eVertex;

        std::array<vk::VertexInputAttributeDescription, 2> attrs{};
        attrs[0].location = 0;
        attrs[0].binding  = 0;
        attrs[0].format   = vk::Format::eR32G32B32Sfloat;
        attrs[0].offset   = offsetof(debug_vertex, position);
        attrs[1].location = 1;
        attrs[1].binding  = 0;
        attrs[1].format   = vk::Format::eR32G32B32A32Sfloat;
        attrs[1].offset   = offsetof(debug_vertex, color);

        vk::PipelineVertexInputStateCreateInfo vi{};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = static_cast<u32>(attrs.size());
        vi.pVertexAttributeDescriptions = attrs.data();

        vk::PipelineInputAssemblyStateCreateInfo ia_lines{};
        ia_lines.topology = vk::PrimitiveTopology::eLineList;

        vk::PipelineInputAssemblyStateCreateInfo ia_tris{};
        ia_tris.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rs{};
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = vk::CullModeFlagBits::eNone;
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo ds_depth{};
        ds_depth.depthTestEnable = VK_TRUE;
        ds_depth.depthWriteEnable = VK_FALSE;
        ds_depth.depthCompareOp = vk::CompareOp::eLessOrEqual;

        vk::PipelineDepthStencilStateCreateInfo ds_overlay{};
        ds_overlay.depthTestEnable = VK_FALSE;
        ds_overlay.depthWriteEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        cb.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        cb.colorBlendOp        = vk::BlendOp::eAdd;
        cb.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        cb.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        cb.alphaBlendOp        = vk::BlendOp::eAdd;
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
        const vk::Format depth_format = vk::Format::eD32Sfloat;

        vk::PipelineRenderingCreateInfo ri{};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_format;
        ri.depthAttachmentFormat = depth_format;

        auto make_pipeline = [&](const vk::PipelineInputAssemblyStateCreateInfo& ia,
                                 const vk::PipelineDepthStencilStateCreateInfo& ds) -> vk::Pipeline {

            vk::GraphicsPipelineCreateInfo pi{};
            pi.pNext  = &ri;
            pi.flags  = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
            pi.stageCount = 2;
            pi.pStages = stages;
            pi.pVertexInputState   = &vi;
            pi.pInputAssemblyState = &ia;
            pi.pViewportState      = &vp;
            pi.pRasterizationState = &rs;
            pi.pMultisampleState   = &ms;
            pi.pDepthStencilState  = &ds;
            pi.pColorBlendState    = &cbs;
            pi.pDynamicState       = &dyn_state;
            pi.layout     = m_debug_pipeline_layout;
            pi.renderPass = nullptr;

            auto result = m_device.createGraphicsPipeline(nullptr, pi);
            if (result.result != vk::Result::eSuccess) {
                LOG(error, "Failed to create debug pipeline");
                return {};
            }
            return result.value;
        };

        m_debug_pipeline_line_depth   = make_pipeline(ia_lines, ds_depth);
        m_debug_pipeline_line_overlay = make_pipeline(ia_lines, ds_overlay);
        m_debug_pipeline_tri_depth    = make_pipeline(ia_tris,  ds_depth);
        m_debug_pipeline_tri_overlay  = make_pipeline(ia_tris,  ds_overlay);
        m_live_pipeline_count += 4;

        m_device.destroyShaderModule(vs);
        m_device.destroyShaderModule(fs);

        // ---- Per-frame vertex buffers + descriptor sets -------------------------------------------------------------

        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {

            m_debug_vertex_buffers[i] = m_dev->create_buffer(
                MAX_DEBUG_VERTICES * sizeof(debug_vertex),
                vk::BufferUsageFlagBits::eVertexBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex, 1, &m_camera_ubos[i]);

            m_debug_desc_buffers[i] = m_dev->create_descriptor_buffer(
                m_debug_set_layout, items, util::descriptor_buffer_type::resource, 1);

            m_dev->update_descriptor_buffer(m_debug_desc_buffers[i], items,
                util::descriptor_buffer_type::resource);
        }
    }


    void renderer::destroy_debug_draw_resources() {

        m_device.waitIdle();

        for (auto& db : m_debug_desc_buffers) {
            if (db.buffer.buffer)
                m_dev->destroy_buffer(db.buffer);
            db = {};
        }

        for (auto& vb : m_debug_vertex_buffers) {
            if (vb.buffer)
                m_dev->destroy_buffer(vb);
            vb = {};
        }

        if (m_debug_pipeline_line_depth)   { m_device.destroyPipeline(m_debug_pipeline_line_depth);   m_debug_pipeline_line_depth   = nullptr; }
        if (m_debug_pipeline_line_overlay) { m_device.destroyPipeline(m_debug_pipeline_line_overlay); m_debug_pipeline_line_overlay = nullptr; }
        if (m_debug_pipeline_tri_depth)    { m_device.destroyPipeline(m_debug_pipeline_tri_depth);    m_debug_pipeline_tri_depth    = nullptr; }
        if (m_debug_pipeline_tri_overlay)  { m_device.destroyPipeline(m_debug_pipeline_tri_overlay);  m_debug_pipeline_tri_overlay  = nullptr; }

        if (m_debug_pipeline_layout) {
            m_device.destroyPipelineLayout(m_debug_pipeline_layout);
            m_debug_pipeline_layout = nullptr;
        }
        if (m_debug_set_layout) {
            m_device.destroyDescriptorSetLayout(m_debug_set_layout);
            m_debug_set_layout = nullptr;
        }

        // Watched mesh handles are just ints — nothing else to release
        {
            std::lock_guard lock(m_debug_mutex);
            m_debug_primitives.clear();
            m_watched_meshes.clear();
        }
    }

}
