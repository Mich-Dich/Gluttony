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

    void scene_renderer::draw_line(const GLT::render::debug_line_primitive& line) {
        std::lock_guard lock(m_debug_mutex);
        debug_primitive_record rec;
        rec.type = debug_primitive_record::kind::line;
        rec.verts[0] = { line.from, 0.0f, line.color };
        rec.verts[1] = { line.to,   0.0f, line.color };
        rec.vertex_count = 2;
        rec.remaining_lifetime = line.lifetime;
        rec.depth_test = line.depth_test;
        m_debug_primitives.push_back(rec);
    }


    void scene_renderer::draw_triangle(const GLT::render::debug_triangle_primitive& tri) {
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


    void scene_renderer::draw_aabb(const GLT::render::debug_aabb_primitive& box) {
        const glm::vec3 c[8] = {
            { box.min.x, box.min.y, box.min.z }, { box.max.x, box.min.y, box.min.z },
            { box.max.x, box.max.y, box.min.z }, { box.min.x, box.max.y, box.min.z },
            { box.min.x, box.min.y, box.max.z }, { box.max.x, box.min.y, box.max.z },
            { box.max.x, box.max.y, box.max.z }, { box.min.x, box.max.y, box.max.z },
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


    void scene_renderer::clear() {
        std::lock_guard lock(m_debug_mutex);
        m_debug_primitives.clear();
        m_watched_meshes.clear();
        m_watched_entities.clear();
    }


    void scene_renderer::set_draw_mesh_aabb(GLT::asset::handle mesh_handle, const glm::vec4& color,
        const f32 lifetime, const bool depth_test) {

        if (mesh_handle == INVALID_HANDLE) return;
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


    void scene_renderer::highlight_entity(std::span<const GLT::world::entity_id> ids,
        const glm::vec4& color, const f32 lifetime, const bool depth_test) {

        if (ids.empty()) return;
        std::lock_guard lock(m_debug_mutex);
        for (const auto& id : ids) {
            if (!id.is_valid()) continue;
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
            if (found) continue;
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

    // per-frame CPU work ----------------------------------------------------------------------------------------------

    void scene_renderer::process_debug_primitives() {

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
                if (p.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER) return false;
                p.remaining_lifetime -= delta;
                return p.remaining_lifetime <= 0.0f;
            });
            std::erase_if(m_watched_meshes, [delta](watched_mesh& w) {
                if (w.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER) return false;
                w.remaining_lifetime -= delta;
                return w.remaining_lifetime <= 0.0f;
            });
            std::erase_if(m_watched_entities, [delta](watched_entity& w) {
                if (w.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER) return false;
                w.remaining_lifetime -= delta;
                return w.remaining_lifetime <= 0.0f;
            });

            watched_meshes_snapshot = m_watched_meshes;
            watched_entities_snapshot = m_watched_entities;
        }

        auto emit_line = [&pending](const glm::vec3& a, const glm::vec3& b,
            const glm::vec4& color, bool depth_test) {
            debug_primitive_record rec;
            rec.type = debug_primitive_record::kind::line;
            rec.verts[0] = { a, 0.0f, color };
            rec.verts[1] = { b, 0.0f, color };
            rec.vertex_count = 2;
            rec.remaining_lifetime = 0.0f;
            rec.depth_test = depth_test;
            pending.push_back(rec);
        };

        auto emit_aabb = [&](const glm::vec3& lmin, const glm::vec3& lmax,
            const glm::mat4& xform, const glm::vec4& color, bool depth_test) {
            glm::vec3 c[8] = {
                { lmin.x, lmin.y, lmin.z }, { lmax.x, lmin.y, lmin.z },
                { lmax.x, lmax.y, lmin.z }, { lmin.x, lmax.y, lmin.z },
                { lmin.x, lmin.y, lmax.z }, { lmax.x, lmin.y, lmax.z },
                { lmax.x, lmax.y, lmax.z }, { lmin.x, lmax.y, lmax.z },
            };
            for (auto& v : c) v = glm::vec3(xform * glm::vec4(v, 1.0f));
            static constexpr u32 e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
            for (auto& [a, b] : e)
                emit_line(c[a], c[b], color, depth_test);
        };

        auto emit_cross = [&](const glm::vec3& p, f32 size, const glm::vec4& color, bool dt) {
            emit_line(p - glm::vec3(size, 0, 0), p + glm::vec3(size, 0, 0), color, dt);
            emit_line(p - glm::vec3(0, size, 0), p + glm::vec3(0, size, 0), color, dt);
            emit_line(p - glm::vec3(0, 0, size), p + glm::vec3(0, 0, size), color, dt);
        };

        auto emit_sphere = [&](const glm::vec3& center, f32 radius, const glm::vec4& color, bool dt) {
            if (radius <= 0.f) return;
            constexpr u32 seg = 32;
            const f32 two_pi = 6.2831853071795864f;
            for (u32 i = 0; i < seg; ++i) {
                const f32 a = static_cast<f32>(i)     / seg * two_pi;
                const f32 b = static_cast<f32>(i + 1) / seg * two_pi;
                emit_line(center + glm::vec3(std::cos(a), std::sin(a), 0.f) * radius,
                          center + glm::vec3(std::cos(b), std::sin(b), 0.f) * radius, color, dt);
                emit_line(center + glm::vec3(std::cos(a), 0.f, std::sin(a)) * radius,
                          center + glm::vec3(std::cos(b), 0.f, std::sin(b)) * radius, color, dt);
                emit_line(center + glm::vec3(0.f, std::cos(a), std::sin(a)) * radius,
                          center + glm::vec3(0.f, std::cos(b), std::sin(b)) * radius, color, dt);
            }
        };

        auto emit_arrow = [&](const glm::vec3& from, const glm::vec3& to, const glm::vec4& color,
            bool dt, f32 head) {
            const glm::vec3 axis = to - from;
            const f32 length = glm::length(axis);
            if (length < 1e-6f) return;
            const glm::vec3 dir = axis / length;
            const glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f)
                ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const glm::vec3 s1 = glm::normalize(glm::cross(dir, up));
            const glm::vec3 s2 = glm::cross(dir, s1);
            const glm::vec3 back = to - dir * head;
            const glm::vec3 barb = s1 * (head * 0.4f);
            emit_line(from, to, color, dt);
            emit_line(to, back + barb, color, dt);
            emit_line(to, back - barb, color, dt);
            emit_line(to, back + s2 * (head * 0.4f), color, dt);
            emit_line(to, back - s2 * (head * 0.4f), color, dt);
        };

        auto emit_cone = [&](const glm::vec3& apex, const glm::vec3& dir_in, f32 angle,
            f32 distance, const glm::vec4& color, bool dt) {
            const f32 len = glm::length(dir_in);
            if (len < 1e-6f || distance <= 0.f || angle <= 0.f || angle >= 1.5707963f) return;
            const glm::vec3 dir = dir_in / len;
            const glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f)
                ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const glm::vec3 p1 = glm::normalize(glm::cross(dir, up));
            const glm::vec3 p2 = glm::cross(dir, p1);
            const f32 br = distance * std::tan(angle);
            const glm::vec3 bc = apex + dir * distance;
            constexpr u32 seg = 32;
            const f32 two_pi = 6.2831853071795864f;
            for (u32 i = 0; i < seg; ++i) {
                const f32 t1 = static_cast<f32>(i)     / seg * two_pi;
                const f32 t2 = static_cast<f32>(i + 1) / seg * two_pi;
                const glm::vec3 r1 = p1 * std::cos(t1) + p2 * std::sin(t1);
                const glm::vec3 r2 = p1 * std::cos(t2) + p2 * std::sin(t2);
                emit_line(bc + r1 * br, bc + r2 * br, color, dt);
            }
            for (u32 i = 0; i < seg; i += seg / 4) {
                const f32 t = static_cast<f32>(i) / seg * two_pi;
                const glm::vec3 r = p1 * std::cos(t) + p2 * std::sin(t);
                emit_line(apex, bc + r * br, color, dt);
            }
        };

        auto emit_light = [&](const GLT::world::light_snapshot& L, const glm::vec4& color, bool dt) {
            const f32 cross = (L.type == GLT::world::light_type::directional)
                ? 0.5f : std::clamp(L.radius * 0.05f, 0.1f, 1.0f);
            emit_cross(L.position, cross, color, dt);
            switch (L.type) {
                case GLT::world::light_type::point:
                    emit_sphere(L.position, L.radius, color, dt);
                    break;
                case GLT::world::light_type::spot:
                    emit_cone(L.position, L.direction, L.outer_cone_angle, L.radius, color, dt);
                    break;
                case GLT::world::light_type::directional: {
                    const glm::vec3 tip = L.position + glm::normalize(L.direction) * 2.0f;
                    emit_arrow(L.position, tip, color, dt, 0.3f);
                    break;
                }
            }
        };

        // Path 1: watched mesh assets (set_draw_mesh_aabb) -----------------------------------------------------------------------
    
        for (const auto& watch : watched_meshes_snapshot) {
            auto rit = m_resident_meshes.find(watch.handle);
            if (rit == m_resident_meshes.end()) continue;
            const mesh_slot* slot = m_shared->get_mesh(rit->second);
            if (!slot || !slot->alive) continue;

            for (const auto& inst : m_scene_instances) {
                if (inst.mesh != watch.handle) continue;
                emit_aabb(slot->local_min, slot->local_max, inst.transform, watch.color, watch.depth_test);
            }
        }

        // Path 2: watched entities (highlight_entity) ----------------------------------------------------------------------------
    
        for (const auto& watch : watched_entities_snapshot) {

            for (const auto& inst : m_scene_instances) {
                if (inst.entity != watch.id) continue;
                auto rit = m_resident_meshes.find(inst.mesh);
                if (rit == m_resident_meshes.end()) continue;
                const mesh_slot* slot = m_shared->get_mesh(rit->second);
                if (!slot || !slot->alive) continue;
                emit_aabb(slot->local_min, slot->local_max, inst.transform, watch.color, watch.depth_test);
            }

            for (const auto& L : m_submitted_lights) {
                if (L.entity != watch.id) continue;
                emit_light(L, watch.color, watch.depth_test);
            }
        }

        // bucket into lines / tris × depth / overlay ------------------------------------------------------------------------------
    
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
                for (u8 i = 0; i < p.vertex_count; ++i) vertices.push_back(p.verts[i]);
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
        m_plugin.get_dev()->update_buffer(buffer, vertices.data(), vertices.size() * sizeof(debug_vertex));
    }

    // GPU resources ------------------------------------------------------------------------------------------------------------------

    void scene_renderer::create_debug_draw_resources() {

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex, 1, nullptr);
        m_debug_set_layout = m_plugin.get_dev()->create_descriptor_set_layout(layout_items);

        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_debug_set_layout;
        m_debug_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pli);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto vs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "debug_line.vert.glsl");
        auto fs_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "debug_line.frag.glsl");
        ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile debug draw shaders");

        vk::ShaderModule vs = m_plugin.get_dev()->create_shader_module(vs_spv);
        vk::ShaderModule fs = m_plugin.get_dev()->create_shader_module(fs_spv);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
        stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

        vk::VertexInputBindingDescription binding{ 0, sizeof(debug_vertex), vk::VertexInputRate::eVertex };
        std::array<vk::VertexInputAttributeDescription, 2> attrs{};
        attrs[0] = { 0, 0, vk::Format::eR32G32B32Sfloat,    offsetof(debug_vertex, position) };
        attrs[1] = { 1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(debug_vertex, color)    };

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
        vp.viewportCount = 1; vp.scissorCount = 1;

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

        vk::PipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        cb.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        cb.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.attachmentCount = 1; cbs.pAttachments = &cb;

        vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dyn_state{};
        dyn_state.dynamicStateCount = 2; dyn_state.pDynamicStates = dyn;

        const vk::Format color_format = vk::Format::eR8G8B8A8Unorm;
        const vk::Format depth_format = vk::Format::eD32Sfloat;
        vk::PipelineRenderingCreateInfo ri{};
        ri.colorAttachmentCount = 1;
        ri.pColorAttachmentFormats = &color_format;
        ri.depthAttachmentFormat = depth_format;

        auto make_pipeline = [&](const vk::PipelineInputAssemblyStateCreateInfo& ia,
                                 const vk::PipelineDepthStencilStateCreateInfo& ds) -> vk::Pipeline {
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
            pi.layout = m_debug_pipeline_layout;
            auto r = m_plugin.get_vk_device().createGraphicsPipeline(nullptr, pi);
            if (r.result != vk::Result::eSuccess)
                return {};
            return r.value;
        };

        m_debug_pipeline_line_depth = make_pipeline(ia_lines, ds_depth);
        m_debug_pipeline_line_overlay = make_pipeline(ia_lines, ds_overlay);
        m_debug_pipeline_tri_depth = make_pipeline(ia_tris,  ds_depth);
        m_debug_pipeline_tri_overlay = make_pipeline(ia_tris,  ds_overlay);

        m_plugin.get_vk_device().destroyShaderModule(vs);
        m_plugin.get_vk_device().destroyShaderModule(fs);

        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
            m_debug_vertex_buffers[i] = m_plugin.get_dev()->create_buffer(MAX_DEBUG_VERTICES * sizeof(debug_vertex),
                vk::BufferUsageFlagBits::eVertexBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex, 1, &m_camera_ubos[i]);

            m_debug_desc_buffers[i] = m_plugin.get_dev()->create_descriptor_buffer(m_debug_set_layout, items,
                util::descriptor_buffer_type::resource, 1);
            m_plugin.get_dev()->update_descriptor_buffer(m_debug_desc_buffers[i], items, util::descriptor_buffer_type::resource);
        }
    }


    void scene_renderer::destroy_debug_draw_resources() {

        m_plugin.get_vk_device().waitIdle();

        for (auto& db : m_debug_desc_buffers) {
            if (db.buffer.buffer) m_plugin.get_dev()->destroy_buffer(db.buffer);
            db = {};
        }
        for (auto& vb : m_debug_vertex_buffers) {
            if (vb.buffer) m_plugin.get_dev()->destroy_buffer(vb);
            vb = {};
        }

        if (m_debug_pipeline_line_depth)
            m_plugin.get_vk_device().destroyPipeline(m_debug_pipeline_line_depth);
        if (m_debug_pipeline_line_overlay)
            m_plugin.get_vk_device().destroyPipeline(m_debug_pipeline_line_overlay);
        if (m_debug_pipeline_tri_depth)
            m_plugin.get_vk_device().destroyPipeline(m_debug_pipeline_tri_depth);
        if (m_debug_pipeline_tri_overlay)
            m_plugin.get_vk_device().destroyPipeline(m_debug_pipeline_tri_overlay);
        m_debug_pipeline_line_depth = m_debug_pipeline_line_overlay = nullptr;
        m_debug_pipeline_tri_depth = m_debug_pipeline_tri_overlay = nullptr;

        if (m_debug_pipeline_layout) {
            m_plugin.get_vk_device().destroyPipelineLayout(m_debug_pipeline_layout);
            m_debug_pipeline_layout = nullptr;
        }
        if (m_debug_set_layout) {
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_debug_set_layout);
            m_debug_set_layout = nullptr;
        }

        std::lock_guard lock(m_debug_mutex);
        m_debug_primitives.clear();
        m_watched_meshes.clear();
        m_watched_entities.clear();
    }

    // pass ---------------------------------------------------------------------------------------------------------------------------

    void scene_renderer::add_debug_draw_pass() {

        const frame_debug_layout& layout = m_debug_layouts[m_current_frame];
        const u32 total = layout.line_depth_count + layout.line_overlay_count +
                          layout.tri_depth_count  + layout.tri_overlay_count;
        if (total == 0) return;

        m_graph->add_pass("debug_draw",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_color, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.set_side_effects(true);
            },
            [this, layout](graph::pass_context& ctx) {

                ctx.begin_rendering();

                ctx.cmd.bindVertexBuffers(0, { m_debug_vertex_buffers[m_current_frame].buffer }, { 0 });
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f,
                    static_cast<f32>(m_extent.x), static_cast<f32>(m_extent.y), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, vk::Extent2D{ m_extent.x, m_extent.y }));

                m_plugin.get_dev()->bind_descriptor_buffer({ m_debug_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_debug_pipeline_layout, 0, 0, 0, ctx.cmd,
                    vk::PipelineBindPoint::eGraphics);

                if (layout.line_depth_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_line_depth);
                    ctx.cmd.draw(layout.line_depth_count, 1, layout.line_depth_offset, 0);
                }
                if (layout.line_overlay_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_line_overlay);
                    ctx.cmd.draw(layout.line_overlay_count, 1, layout.line_overlay_offset, 0);
                }
                if (layout.tri_depth_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_tri_depth);
                    ctx.cmd.draw(layout.tri_depth_count, 1, layout.tri_depth_offset, 0);
                }
                if (layout.tri_overlay_count) {
                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debug_pipeline_tri_overlay);
                    ctx.cmd.draw(layout.tri_overlay_count, 1, layout.tri_overlay_offset, 0);
                }

                ctx.end_rendering();
            });
    }

}
