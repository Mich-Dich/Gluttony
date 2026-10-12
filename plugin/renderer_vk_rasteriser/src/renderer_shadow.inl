#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const glm::vec3 SHADOW_FACE_DIRECTIONS[6] = {
        { 1.f,  0.f,  0.f},
        {-1.f,  0.f,  0.f},
        { 0.f,  1.f,  0.f},
        { 0.f, -1.f,  0.f},
        { 0.f,  0.f,  1.f},
        { 0.f,  0.f, -1.f}
    };  // +X -X +Y -Y +Z -Z. The order must match point_face() in mesh.frag

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static glm::mat4 shadow_look_view(const glm::vec3& position, const glm::vec3& forward);

    // Conservative: a view is skipped only if all 8 corners of its (true, unguarded) frustum are behind one camera plane
    static bool shadow_view_visible(const glm::mat4& view, const glm::vec3& position, f32 half_tan, f32 near_d, f32 far_d,
        const glm::vec4 (&camera_planes)[6]);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static glm::mat4 shadow_look_view(const glm::vec3& position, const glm::vec3& forward) {

        const glm::vec3 up = std::abs(forward.y) > 0.99f ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
        return glm::lookAt(position, position + forward, up);
    }


    static bool shadow_view_visible(const glm::mat4& view, const glm::vec3& position, f32 half_tan, f32 near_d, f32 far_d, 
        const glm::vec4 (&camera_planes)[6]) {

        const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
        const glm::vec3 forward(-view[0][2], -view[1][2], -view[2][2]);

        glm::vec3 corners[8];
        for (u32 i = 0; i < 8; ++i) {
            const f32 d = (i & 4) ? far_d : near_d;
            const f32 x = (i & 1) ? 1.f : -1.f;
            const f32 y = (i & 2) ? 1.f : -1.f;
            corners[i] = position + forward * d + right * (x * half_tan * d) + up * (y * half_tan * d);
        }

        for (const auto& plane : camera_planes) {
            bool all_outside = true;
            for (const auto& corner : corners)
                if (glm::dot(glm::vec3(plane), corner) + plane.w >= 0.f) { all_outside = false; break; }
            if (all_outside)
                return false;
        }
        return true;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::create_shadow_resources() {

        // Comparison sampler: each tap is a hardware 2x2 PCF. Border = 1.0, so anything outside the map reads as lit
        vk::SamplerCreateInfo sampler_info{};
        sampler_info.magFilter = vk::Filter::eLinear;
        sampler_info.minFilter = vk::Filter::eLinear;
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
        sampler_info.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        sampler_info.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        sampler_info.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        sampler_info.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        sampler_info.compareEnable = VK_TRUE;
        sampler_info.compareOp = vk::CompareOp::eLessOrEqual;
        sampler_info.maxLod = 0.0f;
        m_shadow_sampler = m_device.createSampler(sampler_info);

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            m_shadow_camera_ubos[frame_index] = m_dev->create_buffer(sizeof(camera_ubo_data),
                vk::BufferUsageFlagBits::eUniformBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            graph::buffer_desc commands_desc{};
            commands_desc.name = "shadow_draw_commands_" + std::to_string(frame_index);
            commands_desc.size = MAX_DRAW_COMMANDS * sizeof(vk::DrawIndexedIndirectCommand);
            commands_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            commands_desc.persistent = true;
            m_shadow_draw_commands_handles[frame_index] = m_graph->create_buffer(commands_desc);

            graph::buffer_desc count_desc{};
            count_desc.name = "shadow_draw_count_" + std::to_string(frame_index);
            count_desc.size = sizeof(u32);
            count_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer
                             | vk::BufferUsageFlagBits::eTransferDst;
            count_desc.persistent = true;
            m_shadow_draw_count_handles[frame_index] = m_graph->create_buffer(count_desc);

            util::allocated_buffer* commands_allocation = m_graph->buffer_allocation(m_shadow_draw_commands_handles[frame_index]);
            util::allocated_buffer* count_allocation = m_graph->buffer_allocation(m_shadow_draw_count_handles[frame_index]);
            ASSERT(commands_allocation && count_allocation, "", "Graph failed to allocate shadow draw buffers");

            // Same layout + pipeline as the camera cull; only the outputs (3, 4) and the "camera" (5) differ
            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame_index]);
            items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_mesh_data_buffer);
            items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_submesh_data_buffer);
            items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, commands_allocation);
            items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, count_allocation);
            items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_shadow_camera_ubos[frame_index]);
            items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_override_buffers[frame_index]);

            m_shadow_cull_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_cull_set_layout, items,
                util::descriptor_buffer_type::resource, 1);
            m_dev->update_descriptor_buffer(m_shadow_cull_desc_buffers[frame_index], items, util::descriptor_buffer_type::resource);
            m_live_buffer_count++;
        }
    }


    void renderer::destroy_shadow_resources() {

        m_device.waitIdle();

        for (auto& desc : m_shadow_desc_buffers) {
            if (desc.buffer.buffer) m_dev->destroy_buffer(desc.buffer);
            desc = {};
        }
        for (auto& desc : m_shadow_cull_desc_buffers) {
            if (desc.buffer.buffer) m_dev->destroy_buffer(desc.buffer);
            desc = {};
        }
        for (auto& ubo : m_shadow_camera_ubos) {
            if (ubo.buffer) m_dev->destroy_buffer(ubo);
            ubo = {};
        }
        if (m_shadow_sampler) {
            m_device.destroySampler(m_shadow_sampler);
            m_shadow_sampler = nullptr;
        }
        // shadow draw buffers + shadow map are graph-owned (persistent)
    }


    // Fits an orthographic light projection around a bounding sphere of the camera frustum slice [near, distance].
    // Sphere radius is rotation-invariant and the centre is snapped to whole texels, so shadows don't shimmer.
    void renderer::update_shadow() {

        if (!m_shadow.active)
            return;

        const shadow_settings& settings = m_shadow_settings;

        // Camera slice corners in world space (built from fov/aspect, so it doesn't depend on GLM's depth convention)
        const f32 aspect = static_cast<f32>(m_render_size.x) / static_cast<f32>(m_render_size.y);
        const f32 slice_near = m_active_camera.near_plane;
        const f32 slice_far = std::min(m_active_camera.far_plane, settings.distance);
        const f32 tan_half_y = std::tan(glm::radians(m_active_camera.fov) * 0.5f);
        const f32 tan_half_x = tan_half_y * aspect;
        const glm::mat4 inv_view = glm::inverse(m_active_camera.view);

        std::array<glm::vec3, 8> corners{};
        glm::vec3 center{0.f};
        for (u32 i = 0; i < 8; ++i) {
            const f32 d = (i & 4) ? slice_far : slice_near;
            const f32 x = (i & 1) ? 1.f : -1.f;
            const f32 y = (i & 2) ? 1.f : -1.f;
            corners[i] = glm::vec3(inv_view * glm::vec4(x * tan_half_x * d, y * tan_half_y * d, -d, 1.f));
            center += corners[i];
        }
        center /= 8.f;

        f32 radius = 0.f;
        for (const auto& corner : corners)
            radius = std::max(radius, glm::length(corner - center));
        radius = std::ceil(radius * 16.f) / 16.f;

        // Light rotation, centred at the origin so snapping happens in a fixed frame
        const glm::vec3 dir = m_shadow.direction;
        const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(1.f, 0.f, 0.f) : glm::vec3(0.f, 1.f, 0.f);
        const glm::mat4 light_rotation = glm::lookAt(glm::vec3(0.f), dir, up);

        glm::vec3 center_ls = glm::vec3(light_rotation * glm::vec4(center, 1.f));
        const f32 texel_size = (2.f * radius) / static_cast<f32>(settings.resolution);
        center_ls.x = std::floor(center_ls.x / texel_size) * texel_size;
        center_ls.y = std::floor(center_ls.y / texel_size) * texel_size;

        // Eye sits (radius + caster_extension) behind the centre, so casters between the sun and the slice are inside the volume
        const f32 extension = settings.caster_extension;
        const glm::mat4 light_view = glm::translate(glm::mat4(1.f),
            -glm::vec3(center_ls.x, center_ls.y, center_ls.z + radius + extension)) * light_rotation;

        glm::mat4 light_projection = glm::ortho(-radius, radius, -radius, radius, 0.f, 2.f * radius + extension);
        light_projection[1][1] *= -1.0f;    // Vulkan Y flip: keeps winding identical to the camera, so the same CCW/back-cull pipeline works

        m_shadow.view_proj = light_projection * light_view;
        m_shadow.params = glm::vec4(texel_size, settings.normal_bias, settings.depth_bias, 1.f / static_cast<f32>(settings.resolution));

        // The shadow "camera" UBO: consumed by cull.comp (frustum_planes) and mesh.vert (view_proj)
        camera_ubo_data shadow_camera{};
        shadow_camera.view = light_view;
        shadow_camera.view_proj = m_shadow.view_proj;
        extract_frustum_planes(m_shadow.view_proj, shadow_camera.frustum_planes);

        std::memcpy(m_dev->map_buffer(m_shadow_camera_ubos[m_current_frame]), &shadow_camera, sizeof(shadow_camera));
        m_dev->unmap_buffer(m_shadow_camera_ubos[m_current_frame]);
    }

    // local shadows ---------------------------------------------------------------------------------------------------

    void renderer::create_local_shadow_resources() {

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // Cull pipeline ------------------------------------------------------------------------------------------------
        std::vector<util::descriptor_item> layout_items;
        for (u32 binding = 0; binding < 5; binding++)
            layout_items.emplace_back(binding, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(5, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);

        m_local_shadow_cull_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_local_shadow_cull_set_layout;
        m_local_shadow_cull_pipeline_layout = m_device.createPipelineLayout(pipeline_layout_info);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "shadow_cull.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile shadow cull compute shader");

        vk::ShaderModule compute_shader = m_dev->create_shader_module(compute_spv);
        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = VKSF::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.layout = m_local_shadow_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = m_device.createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create shadow cull pipeline");
        m_local_shadow_cull_pipeline = result.value;
        m_live_pipeline_count++;
        m_device.destroyShaderModule(compute_shader);

        // Per-frame resources ------------------------------------------------------------------------------------------
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; frame_index++) {

            // Views buffer: read as an SSBO by the cull + fragment shaders, and as 256-byte UBO slices by mesh.vert (one per view)
            m_shadow_view_buffers[frame_index] = m_dev->create_buffer(MAX_SHADOW_VIEWS * SHADOW_VIEW_STRIDE, 
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eUniformBuffer, 
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;
            std::memset(m_dev->map_buffer(m_shadow_view_buffers[frame_index]), 0, MAX_SHADOW_VIEWS * SHADOW_VIEW_STRIDE);
            m_dev->unmap_buffer(m_shadow_view_buffers[frame_index]);

            graph::buffer_desc commands_desc{};
            commands_desc.name = "local_shadow_commands_" + std::to_string(frame_index);
            commands_desc.size = MAX_SHADOW_VIEWS * SHADOW_COMMANDS_PER_VIEW * sizeof(vk::DrawIndexedIndirectCommand);
            commands_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            commands_desc.persistent = true;
            m_local_shadow_commands_handles[frame_index] = m_graph->create_buffer(commands_desc);

            graph::buffer_desc counts_desc{};
            counts_desc.name = "local_shadow_counts_" + std::to_string(frame_index);
            counts_desc.size = MAX_SHADOW_VIEWS * sizeof(u32);
            counts_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst;
            counts_desc.persistent = true;
            m_local_shadow_counts_handles[frame_index] = m_graph->create_buffer(counts_desc);

            util::allocated_buffer* commands_allocation = m_graph->buffer_allocation(m_local_shadow_commands_handles[frame_index]);
            util::allocated_buffer* counts_allocation = m_graph->buffer_allocation(m_local_shadow_counts_handles[frame_index]);
            ASSERT(commands_allocation && counts_allocation, "", "Graph failed to allocate local shadow buffers");

            // Cull descriptors
            std::vector<util::descriptor_item> cull_items;
            cull_items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame_index]);
            cull_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_mesh_data_buffer);
            cull_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_submesh_data_buffer);
            cull_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, commands_allocation);
            cull_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, counts_allocation);
            cull_items.emplace_back(5, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shadow_view_buffers[frame_index]);

            m_local_shadow_cull_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_local_shadow_cull_set_layout,
                cull_items, util::descriptor_buffer_type::resource, 1);
            m_dev->update_descriptor_buffer(m_local_shadow_cull_desc_buffers[frame_index], cull_items, util::descriptor_buffer_type::resource);
            m_live_buffer_count++;

            // Draw descriptors: MAX_SHADOW_VIEWS sets using the mesh layout. Set v has binding 0 = view v's UBO slice, so the
            // unchanged mesh.vert + m_depth_pipeline render any view just by switching the descriptor offset. Written once at init
            std::array<util::allocated_buffer, MAX_SHADOW_VIEWS> slices{};
            for (u32 view = 0; view < MAX_SHADOW_VIEWS; view++) {
                slices[view] = m_shadow_view_buffers[frame_index];
                slices[view].dev_address += static_cast<vk::DeviceAddress>(view) * SHADOW_VIEW_STRIDE;
                slices[view].size = SHADOW_VIEW_STRIDE;
            }

            std::vector<util::descriptor_item> draw_items;
            draw_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &slices[0]);
            draw_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, &m_instance_buffers[frame_index]);

            constexpr auto buffer_type = util::descriptor_buffer_type::combined;
            m_local_shadow_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_mesh_set_layout, draw_items, buffer_type,
                MAX_SHADOW_VIEWS);
            m_live_buffer_count++;

            m_dev->compute_descriptor_binding_offsets(m_mesh_set_layout, draw_items);
            for (u32 view = 0; view < MAX_SHADOW_VIEWS; view++) {
                draw_items[0].p_resources = &slices[view];
                m_dev->update_descriptor_buffer(m_local_shadow_desc_buffers[frame_index], draw_items, buffer_type, view);
            }
        }
    }


    void renderer::destroy_local_shadow_resources() {

        m_device.waitIdle();

        for (auto& desc : m_local_shadow_desc_buffers) {
            if (desc.buffer.buffer)
                m_dev->destroy_buffer(desc.buffer);
            desc = {};
        }
        for (auto& desc : m_local_shadow_cull_desc_buffers) {
            if (desc.buffer.buffer)
                m_dev->destroy_buffer(desc.buffer);
            desc = {};
        }
        for (auto& buffer : m_shadow_view_buffers) {
            if (buffer.buffer)
                m_dev->destroy_buffer(buffer);
            buffer = {};
        }

        if (m_local_shadow_cull_pipeline)
            m_device.destroyPipeline(m_local_shadow_cull_pipeline);

        if (m_local_shadow_cull_pipeline_layout)
            m_device.destroyPipelineLayout(m_local_shadow_cull_pipeline_layout);

        if (m_local_shadow_cull_set_layout)
            m_device.destroyDescriptorSetLayout(m_local_shadow_cull_set_layout);

        m_local_shadow_cull_pipeline = nullptr;
        m_local_shadow_cull_pipeline_layout = nullptr;
        m_local_shadow_cull_set_layout = nullptr;
    }


    void renderer::update_local_shadows() {

        m_local_shadow_view_count = 0;
        m_local_shadow_tiles.clear();
        m_packed_shadow_views.clear();

        const shadow_settings& settings = m_shadow_settings;
        if (!settings.enabled || settings.max_local_lights == 0)
            return;

        glm::vec4 camera_planes[6];
        extract_frustum_planes(camera_projection() * m_active_camera.view, camera_planes);
        const glm::vec3 camera_position = m_active_camera.position;

        // Candidates: point/spot lights that touch the camera frustum, ranked by screen footprint
        struct candidate { u32 light; f32 score; f32 ratio; };
        std::vector<candidate> candidates;

        for (u32 index = 0; index < m_packed_lights.size(); index++) {

            const gpu_light& light = m_packed_lights[index];
            const u32 type = static_cast<u32>(light.params.y);
            const f32 radius = light.position_radius.w;
            if ((type != 1 && type != 2) || radius <= 0.f)
                continue;                                   // (add a cast_shadows check here if light_snapshot has one)

            const glm::vec3 position(light.position_radius);

            bool outside = false;
            for (const auto& plane : camera_planes)
                if (glm::dot(glm::vec3(plane), position) + plane.w < -radius) { outside = true; break; }
            if (outside)
                continue;

            const f32 distance = glm::distance(camera_position, position);
            if (distance - radius > settings.local_distance)
                continue;

            const f32 ratio = radius / std::max(distance, 1.f);
            candidates.push_back({ index, ratio * std::sqrt(std::max(light.color_intensity.a, 0.01f)), ratio });
        }

        std::sort(candidates.begin(), candidates.end(), [](const candidate& a, const candidate& b) { return a.score > b.score; });

        // Plans: tile size per light + which faces are visible
        struct light_plan { u32 light; u32 type; u32 first_view; u32 face_count; u32 size; u32 visible_mask; f32 half_tan; f32 near_d; };
        std::vector<light_plan> plans;
        u32 view_total = 0;

        for (const auto& c : candidates) {

            if (plans.size() >= settings.max_local_lights)
                break;

            const gpu_light& light = m_packed_lights[c.light];
            const u32 type = static_cast<u32>(light.params.y);
            const u32 face_count = (type == 1) ? 6u : 1u;
            if (view_total + face_count > MAX_SHADOW_VIEWS)
                continue;

            const glm::vec3 position(light.position_radius);
            const f32 radius = light.position_radius.w;
            const f32 near_d = std::max(0.05f, radius * 0.02f);
            const f32 half_tan = (type == 1) ? 1.f : std::tan(std::min(std::acos(std::clamp(light.params.x, -1.f, 1.f)), glm::radians(80.f)));

            u32 visible_mask = 0;
            for (u32 face = 0; face < face_count; face++) {
                const glm::vec3 forward = (type == 1) ? SHADOW_FACE_DIRECTIONS[face] : glm::normalize(glm::vec3(light.direction_inner_cos));
                if (shadow_view_visible(shadow_look_view(position, forward), position, half_tan, near_d, radius, camera_planes))
                    visible_mask |= (1u << face);
            }
            if (visible_mask == 0)
                continue;

            // ratio >= 0.5 -> full size, then one halving per octave, clamped to 3 halvings
            const i32 tier = std::clamp(static_cast<i32>(std::floor(std::log2(0.5f / std::max(c.ratio, 1e-4f)))), 0, 3);
            const u32 max_tile = (type == 1) ? SHADOW_POINT_TILE_MAX : SHADOW_SPOT_TILE_MAX;
            const u32 size = std::max(SHADOW_TILE_MIN, max_tile >> tier);

            plans.push_back({ c.light, type, view_total, face_count, size, visible_mask, half_tan, near_d });
            view_total += face_count;
        }

        if (plans.empty())
            return;

        // Pack tiles into the atlas. Sizes are powers of two sorted descending, so a shelf packer is near-optimal
        // If it doesn't fit, halve every light's tile size and retry; whatever still doesn't fit stays unshadowed
        struct tile_request { u32 plan; u32 face; u32 size; u32 x; u32 y; bool placed; };
        std::vector<tile_request> requests;
        const u32 atlas_size = settings.atlas_size;

        for (u32 attempt = 0; ; attempt++) {

            requests.clear();
            for (u32 p = 0; p < plans.size(); p++)
                for (u32 face = 0; face < plans[p].face_count; face++)
                    if (plans[p].visible_mask & (1u << face))
                        requests.push_back({ p, face, plans[p].size, 0, 0, false });

            std::stable_sort(requests.begin(), requests.end(), [](const tile_request& a, const tile_request& b) { return a.size > b.size; });

            u32 x = 0, y = 0, row_height = 0;
            bool all_placed = true;
            for (auto& r : requests) {
                if (x + r.size > atlas_size) {
                    x = 0; y += row_height; 
                    row_height = 0; 
                }
                if (y + r.size > atlas_size) {
                    r.placed = false; all_placed = false; 
                    continue; 
                }
                r.x = x; r.y = y; r.placed = true;
                x += r.size;
                row_height = std::max(row_height, r.size);
            }

            if (all_placed || attempt == 3)
                break;

            for (auto& plan : plans)
                plan.size = std::max(SHADOW_TILE_MIN, plan.size / 2);
        }

        // Build the views
        m_packed_shadow_views.assign(view_total, gpu_shadow_view{});
        const f32 atlas_f = static_cast<f32>(atlas_size);

        for (const auto& plan : plans) {

            gpu_light& light = m_packed_lights[plan.light];
            light.params.z = static_cast<f32>(plan.first_view + 1);         // 0 = unshadowed; the shader adds the face for points

            const glm::vec3 position(light.position_radius);
            const f32 radius = light.position_radius.w;
            const f32 size = static_cast<f32>(plan.size);

            // Widen the frustum so taps near the tile edge still hit real depth instead of clamping onto the border
            const f32 guard_tan = plan.half_tan * size / (size - 2.f * SHADOW_TILE_GUARD);
            glm::mat4 projection = glm::perspective(2.f * std::atan(guard_tan), 1.f, plan.near_d, radius);
            projection[1][1] *= -1.0f;

            for (u32 face = 0; face < plan.face_count; face++) {

                const glm::vec3 forward = (plan.type == 1) ? SHADOW_FACE_DIRECTIONS[face] : glm::normalize(glm::vec3(light.direction_inner_cos));

                gpu_shadow_view& view = m_packed_shadow_views[plan.first_view + face];
                view.view_proj = projection * shadow_look_view(position, forward);
                view.params = glm::vec4(2.f * guard_tan / size, settings.local_normal_bias, settings.local_bias, 1.f / atlas_f);
                view.sphere = glm::vec4(position, radius);
                extract_frustum_planes(view.view_proj, view.planes);
            }
        }

        for (const auto& request : requests) {

            if (!request.placed)
                continue;

            const u32 view_index = plans[request.plan].first_view + request.face;
            m_packed_shadow_views[view_index].atlas_rect = glm::vec4(request.x / atlas_f, request.y / atlas_f, request.size / atlas_f, request.size / atlas_f);
            m_local_shadow_tiles.push_back({ view_index, request.x, request.y, request.size });
        }

        if (m_local_shadow_tiles.empty())
            return;

        m_local_shadow_view_count = view_total;
        std::memcpy(m_dev->map_buffer(m_shadow_view_buffers[m_current_frame]), m_packed_shadow_views.data(),
            m_packed_shadow_views.size() * sizeof(gpu_shadow_view));
        m_dev->unmap_buffer(m_shadow_view_buffers[m_current_frame]);
    }

}
