
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

    glm::mat4 scene_renderer::camera_projection() const {
        GLT_PROFILE_SCOPE("scene_renderer::camera_projection");

        const f32 aspect = static_cast<f32>(m_extent.x) / static_cast<f32>(m_extent.y);
        glm::mat4 p = glm::perspective(glm::radians(m_active_camera.fov), aspect,
            m_active_camera.near_plane, m_active_camera.far_plane);
        p[1][1] *= -1.0f;
        return p;
    }

    bool scene_renderer::ensure_host_buffer(util::allocated_buffer& buffer, vk::DeviceSize bytes, vk::BufferUsageFlags usage) {
        GLT_PROFILE_SCOPE("scene_renderer::ensure_host_buffer");

        if (buffer.buffer && buffer.size >= bytes)
            return false;

        const vk::DeviceSize capacity = std::bit_ceil(std::max<vk::DeviceSize>(bytes, 1024));
        if (buffer.buffer)
            m_shared->retire_buffer(buffer);           // the previous frame using this slot is done, but be safe & uniform

        buffer = m_plugin.get_dev()->create_buffer(capacity, usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        ASSERT(buffer.buffer, "", "Failed to grow host buffer to {} bytes", (capacity))
        return true;
    }

    // per-frame CPU work (update()) -----------------------------------------------------------------------------------

    void scene_renderer::ensure_scene_meshes() {
        GLT_PROFILE_SCOPE("scene_renderer::ensure_scene_meshes");

        std::unordered_set<GLT::asset::handle> current;
        for (const auto& inst : m_scene_instances)
            if (inst.mesh != INVALID_HANDLE)
                current.insert(inst.mesh);

        // Release meshes no longer used
        for (auto it = m_resident_meshes.begin(); it != m_resident_meshes.end(); ) {
            if (!current.contains(it->first)) {
                m_shared->release_mesh(it->second);
                it = m_resident_meshes.erase(it);
            } else
                it++;
        }

        // Ensure every current mesh has a reference
        for (auto h : current) {
            if (m_resident_meshes.contains(h))
                continue;
            auto r = m_shared->ensure_mesh(h);
            if (r) m_resident_meshes.emplace(h, *r);
        }

        // Pick up in-place asset edits (LOD regeneration in the mesh editor, reloads). One registry lookup per resident
        // mesh, so only every few frames
        if ((m_mesh_refresh_tick++ & 3u) == 0u)
            for (const auto& [handle, id] : m_resident_meshes)
                if (m_shared->refresh_mesh(id))
                    ++m_mesh_generation;            // invalidates the cached shadow maps
    }


    void scene_renderer::update_camera_ubo() {
        GLT_PROFILE_SCOPE("scene_renderer::update_camera_ubo");

        camera_ubo_data cam{};
        const glm::mat4 proj = camera_projection();
        cam.view = m_active_camera.view;
        cam.view_proj = proj * m_active_camera.view;
        cam.camera_pos = glm::vec4(m_active_camera.position, 1.0f);
        cam.projection = proj;
        cam.inv_projection = glm::inverse(proj);
        cam.cluster_dims = glm::uvec4(m_cluster_params.grid_x, m_cluster_params.grid_y, m_cluster_params.grid_z,
            CLUSTER_TILE_SIZE_X | (CLUSTER_TILE_SIZE_Y << 16));
        cam.cluster_depth = glm::vec4(m_active_camera.near_plane, m_active_camera.far_plane,
            static_cast<f32>(m_extent.x), static_cast<f32>(m_extent.y));
        cam.light_view_proj = m_shadow.view_proj;
        cam.shadow_params = m_shadow.params;
        cam.flags.x = m_asset_instance_count;   // cull.comp: only the asset-backed prefix of this frame's instance SSBO is valid
        cam.flags.y = 1u;                       // cull.comp: main view writes the draw statistics (shadow cull does not)

        const auto& env = m_environment;
        const f32 ambient = std::max(env.ambient_intensity, 0.f);
        if (env.has_sky) {
            // Image based lighting from the sky cubemaps (the radiance already carries sky intensity and sun colour)
            cam.ambient_sky = glm::vec4(glm::vec3(ambient), 1.f);
            cam.ambient_ground = glm::vec4(0.f, 0.f, 0.f, static_cast<f32>(ENV_SPECULAR_MIPS - 1));
        } else {
            cam.ambient_sky = glm::vec4(env.ambient_sky_color * ambient, 0.f);
            cam.ambient_ground = glm::vec4(env.ambient_ground_color * ambient, 0.f);
        }

        // Frustum planes
        const glm::mat4 t = glm::transpose(cam.view_proj);
        glm::vec4 p[6] = { t[3] + t[0], t[3] - t[0], t[3] + t[1], t[3] - t[1], t[3] + t[2], t[3] - t[2] };
        for (u32 i = 0; i < 6; i++) {
            const f32 len = glm::length(glm::vec3(p[i]));
            cam.frustum_planes[i] = (len > 0.f) ? p[i] / len : p[i];
        }

        m_plugin.get_dev()->update_buffer(m_camera_ubos[m_current_frame], &cam, sizeof(cam));
    }


    // Translucent candidates -> visible list sorted back-to-front. Bounds are the whole mesh's AABB (per-submesh bounds
    // are not stored), so all submeshes of one instance share a sort depth and keep their submesh order
    void scene_renderer::prepare_translucent_draws() {
        GLT_PROFILE_SCOPE("scene_renderer::prepare_translucent_draws");

        m_translucent_draws.clear();
        m_translucent_span = glm::vec2(0.f);
        if (m_translucent_candidates.empty())
            return;

        f32 span_min = std::numeric_limits<f32>::max();
        f32 span_max = 0.f;

        glm::vec4 planes[6];
        extract_frustum_planes(camera_projection() * m_active_camera.view, planes);

        const glm::mat4& view = m_active_camera.view;
        const glm::vec3 camera_pos = m_active_camera.position;
        const glm::vec3 camera_forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

        m_translucent_draws.reserve(m_translucent_candidates.size());
        for (translucent_draw draw : m_translucent_candidates) {

            glm::vec3 local_min, local_max;
            if (draw.procedural) {
                const auto& pm = m_procedural_meshes[draw.mesh];
                local_min = pm.local_min;
                local_max = pm.local_max;
            } else {
                const mesh_slot* slot = m_shared->get_mesh(draw.mesh);
                if (!slot || !slot->alive || draw.submesh >= slot->submesh_ranges.size())
                    continue;
                local_min = slot->local_min;
                local_max = slot->local_max;
            }

            // World AABB of the transformed local box
            const glm::mat4& model = m_packed_instances[draw.instance].model;
            glm::vec3 world_min(std::numeric_limits<f32>::max());
            glm::vec3 world_max(std::numeric_limits<f32>::lowest());
            for (u32 corner = 0; corner < 8; ++corner) {
                const glm::vec3 local((corner & 1u) ? local_max.x : local_min.x,
                                      (corner & 2u) ? local_max.y : local_min.y,
                                      (corner & 4u) ? local_max.z : local_min.z);
                const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.f));
                world_min = glm::min(world_min, world);
                world_max = glm::max(world_max, world);
            }

            bool visible = true;
            for (const glm::vec4& plane : planes) {
                const glm::vec3 positive(plane.x >= 0.f ? world_max.x : world_min.x,
                                         plane.y >= 0.f ? world_max.y : world_min.y,
                                         plane.z >= 0.f ? world_max.z : world_min.z);
                if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.f) {
                    visible = false;
                    break;
                }
            }
            if (!visible)
                continue;

            // Linear view depth range of the box, for light culling (translucents are not in the depth buffer)
            for (u32 corner = 0; corner < 8; ++corner) {
                const glm::vec3 world((corner & 1u) ? world_max.x : world_min.x,
                                      (corner & 2u) ? world_max.y : world_min.y,
                                      (corner & 4u) ? world_max.z : world_min.z);
                const f32 depth = glm::dot(world - camera_pos, camera_forward);
                span_min = std::min(span_min, depth);
                span_max = std::max(span_max, depth);
            }

            draw.sort_depth = glm::dot((world_min + world_max) * 0.5f - camera_pos, camera_forward);
            m_translucent_draws.push_back(draw);
        }

        if (!m_translucent_draws.empty() && span_max > 0.f) {
            const f32 near_z = m_active_camera.near_plane;
            const f32 far_z = m_active_camera.far_plane;
            m_translucent_span = glm::vec2(std::clamp(span_min, near_z, far_z), std::clamp(span_max, near_z, far_z));
        }

        // Far -> near. Stable so submeshes of one instance (equal depth) keep their authored order
        std::stable_sort(m_translucent_draws.begin(), m_translucent_draws.end(),
            [](const translucent_draw& a, const translucent_draw& b) { return a.sort_depth > b.sort_depth; });
    }


    void scene_renderer::update_globals() {
        GLT_PROFILE_SCOPE("scene_renderer::update_globals");

        const glm::mat4 proj = camera_projection();
        const glm::mat4 view = m_active_camera.view;
        const glm::mat4 vp = proj * view;

        gpu_globals g{};
        g.time = 0.f;   // populated from application elsewhere if needed
        g.delta_time = m_delta_time;
        g.frame_index = m_frame_counter;
        g.viewport_size = glm::vec2(m_extent);
        g.render_size = glm::vec2(m_extent);
        g.camera_position = m_active_camera.position;
        g.near_plane = m_active_camera.near_plane;
        g.far_plane = m_active_camera.far_plane;
        g.camera_right = glm::vec3(view[0][0], view[1][0], view[2][0]);
        g.camera_up = glm::vec3(view[0][1], view[1][1], view[2][1]);
        g.camera_forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
        g.view = view;
        g.projection = proj;
        g.view_projection = vp;
        g.inv_view = glm::inverse(view);
        g.inv_projection = glm::inverse(proj);
        g.inv_view_projection = glm::inverse(vp);

        m_plugin.get_dev()->update_buffer(m_globals_ubos[m_current_frame], &g, sizeof(g));
    }

    // init / teardown -------------------------------------------------------------------------------------------------

    void scene_renderer::init_gpu_state() {
        GLT_PROFILE_SCOPE("scene_renderer::init_gpu_state");

        auto* dev = m_plugin.get_dev();

        m_material_ranges.assign(MAX_MATERIALS, glm::uvec2(0u));
        m_material_draw_counts.assign(MAX_MATERIALS, 0u);

        using BU = vk::BufferUsageFlagBits;

        // Per-frame buffers
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
            m_camera_ubos[i] = dev->create_buffer(sizeof(camera_ubo_data), BU::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_globals_ubos[i] = dev->create_buffer(sizeof(gpu_globals), BU::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            // Instance + material-table buffers start small and grow on demand (ensure_host_buffer)
            m_instance_buffers[i] = dev->create_buffer(INITIAL_INSTANCE_CAPACITY * sizeof(gpu_instance_data), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_material_table_buffers[i] = dev->create_buffer(INITIAL_DRAW_CAPACITY * sizeof(u32), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_material_range_buffers[i] = dev->create_buffer(MAX_MATERIALS * sizeof(glm::uvec2), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            std::memset(dev->map_buffer(m_material_range_buffers[i]), 0, MAX_MATERIALS * sizeof(glm::uvec2));
            dev->unmap_buffer(m_material_range_buffers[i]);
            m_light_buffers[i] = dev->create_buffer(MAX_LIGHTS * sizeof(gpu_light), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            std::memset(dev->map_buffer(m_light_buffers[i]), 0, MAX_LIGHTS * sizeof(gpu_light));
            dev->unmap_buffer(m_light_buffers[i]);
            m_light_meta_ubos[i] = dev->create_buffer(sizeof(glm::uvec4), BU::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        }

        create_mesh_material_resources();
        create_cluster_resources();
        create_light_culling_pipeline();
        create_cull_pipeline();
        create_shadow_resources();
        create_local_shadow_resources();
        if (!m_offscreen)
            create_blit_pipeline();
        create_tonemap_pipeline();
        create_light_cull_debug_pipeline();
        create_environment_resources();
        create_debug_draw_resources();
    }


    void scene_renderer::destroy_gpu_state() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_gpu_state");

        auto* dev = m_plugin.get_dev();

        destroy_debug_draw_resources();
        destroy_environment_resources();
        destroy_tonemap_pipeline();
        destroy_light_cull_debug_pipeline();
        if (!m_offscreen)
            destroy_blit_pipeline();
        destroy_local_shadow_resources();
        destroy_shadow_resources();
        destroy_cull_pipeline();
        destroy_light_culling_pipeline();
        destroy_cluster_resources();
        destroy_mesh_material_resources();

        for (auto& b : m_camera_ubos)
            if (b.buffer)
                dev->destroy_buffer(b);
        for (auto& b : m_globals_ubos)
            if (b.buffer)
                dev->destroy_buffer(b);
        for (auto& b : m_instance_buffers)
            if (b.buffer)
                dev->destroy_buffer(b);
        for (auto& b : m_material_table_buffers)
            if (b.buffer)
                dev->destroy_buffer(b);
        for (auto& b : m_material_range_buffers)
            if (b.buffer)
                dev->destroy_buffer(b);
        for (auto& b : m_light_buffers)
            if (b.buffer)
                dev->destroy_buffer(b);
        for (auto& b : m_light_meta_ubos)
            if (b.buffer)
                dev->destroy_buffer(b);

        // Procedural meshes
        for (auto& pm : m_procedural_meshes) {
            if (!pm.alive)
                continue;
            if (pm.vertex_buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(pm.vertex_buffer);
            if (pm.index_buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(pm.index_buffer);
            if (pm.material_slot != DEFAULT_MATERIAL_SLOT)
                m_shared->release_material(pm.material_slot);
        }
        m_procedural_meshes.clear();
        m_free_procedural_slots.clear();
        m_procedural_draw_slots.clear();

        m_camera_ubos.fill({});
        m_globals_ubos.fill({});
        m_instance_buffers.fill({});
        m_material_table_buffers.fill({});
        m_material_range_buffers.fill({});
        m_light_buffers.fill({});
        m_light_meta_ubos.fill({});
    }

    // per-frame GPU work (render()) -----------------------------------------------------------------------------------

    void scene_renderer::import_frame_textures() {
        GLT_PROFILE_SCOPE("scene_renderer::import_frame_textures");

        using VKIU = vk::ImageUsageFlagBits;

        // Backbuffer (surface only) -------------------------------------------------
        if (!m_offscreen) {
            const vk::ImageLayout initial_layout =
                m_swapchain_images_layout[m_current_swapchain_image];
            m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::eUndefined;

            graph::texture_desc d{};
            d.name = "backbuffer";
            d.format = m_swapchain.swapchain_format;
            d.width = m_swapchain.swapchain_extent.width;
            d.height = m_swapchain.swapchain_extent.height;
            d.usage = VKIU::eColorAttachment | VKIU::eTransferDst;
            d.persistent = true;

            m_backbuffer_handle = m_graph->create_texture(d);
            m_graph->import_texture(m_backbuffer_handle,
                m_swapchain.swapchain_images[m_current_swapchain_image],
                m_swapchain.swapchain_image_views[m_current_swapchain_image],
                { initial_layout, {}, vk::PipelineStageFlagBits2::eNone });
        }

        // Scene color ---------------------------------------------------------------
        graph::texture_desc sc{};
        sc.name = "scene_color";
        sc.format = vk::Format::eR8G8B8A8Unorm;
        sc.width = m_extent.x;
        sc.height = m_extent.y;
        sc.usage = VKIU::eSampled | VKIU::eColorAttachment | VKIU::eTransferSrc | VKIU::eTransferDst;
        sc.persistent = true;
        m_scene_color = m_graph->create_texture(sc);

        if (m_offscreen) {
            // The UI of the previous frame (an earlier submission on the same queue) may still be sampling this image, and
            // window targets render after offscreen ones. Wait on all prior work (write-after-read: execution dependency
            // only) instead of claiming nothing used it (eNone)
            m_graph->import_texture(m_scene_color,
                m_output_image->get_allocated_image_ref().image,
                m_output_image->get_accessible_image_ref().view,
                { m_output_image->get_accessible_image_ref().layout, {}, vk::PipelineStageFlagBits2::eAllCommands });
        }

        // HDR scene colour (lit passes render here, the tonemap pass resolves it into scene_color) -------------
        graph::texture_desc hdr{};
        hdr.name = "scene_hdr";
        hdr.format = HDR_COLOR_FORMAT;
        hdr.width = m_extent.x;
        hdr.height = m_extent.y;
        hdr.usage = VKIU::eColorAttachment | VKIU::eSampled;
        hdr.persistent = true;
        m_scene_hdr = m_graph->create_texture(hdr);

        // Depth ---------------------------------------------------------------------
        graph::texture_desc dd{};
        dd.name = "scene_depth";
        dd.format = vk::Format::eD32Sfloat;
        dd.width = m_extent.x;
        dd.height = m_extent.y;
        dd.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        dd.persistent = true;
        m_depth_target = m_graph->create_texture(dd);

        // Directional shadow map ----------------------------------------------------
        graph::texture_desc sd{};
        sd.name = "shadow_map";
        sd.format = vk::Format::eD32Sfloat;
        sd.width = m_shadow_settings.resolution;
        sd.height = m_shadow_settings.resolution;
        sd.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        sd.persistent = true;
        m_shadow_map = m_graph->create_texture(sd);

        // Local shadow atlas --------------------------------------------------------
        graph::texture_desc ad{};
        ad.name = "shadow_atlas";
        ad.format = vk::Format::eD32Sfloat;
        ad.width = m_shadow_settings.atlas_size;
        ad.height = m_shadow_settings.atlas_size;
        ad.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        ad.persistent = true;
        m_shadow_atlas = m_graph->create_texture(ad);
    }


    void scene_renderer::update_mesh_descriptors() {
        GLT_PROFILE_SCOPE("scene_renderer::update_mesh_descriptors");

        const u32 frame = m_current_frame;
        if (!m_light_grid_handles[frame].is_valid() || !m_light_index_list_handles[frame].is_valid())
            return;

        auto* grid = m_graph->buffer_allocation(m_light_grid_handles[frame]);
        auto* list = m_graph->buffer_allocation(m_light_index_list_handles[frame]);
        ASSERT(grid && list, "", "Graph failed to allocate light culling buffers");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        util::accessible_image shadow_img{};
        shadow_img.view = m_graph->texture_default_view(m_shadow_map);
        shadow_img.sampler = m_shared->get_shadow_sampler();
        shadow_img.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        util::accessible_image atlas_img{};
        atlas_img.view = m_graph->texture_default_view(m_shadow_atlas);
        atlas_img.sampler = m_shared->get_shadow_sampler();
        atlas_img.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        util::accessible_image shadow_sampler_image{};
        shadow_sampler_image.sampler = m_shared->get_shadow_sampler();

        util::accessible_image env_specular_img{};
        env_specular_img.view = m_graph->texture_default_view(m_env_specular);
        env_specular_img.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        util::accessible_image env_sampler_image{};
        env_sampler_image.sampler = m_shared->get_material_sampler();      // trilinear across the roughness mips

        auto* env_sh = m_graph->buffer_allocation(m_env_sh);
        ASSERT(env_sh, "", "Graph failed to allocate the environment SH buffer");

        std::vector<util::descriptor_item> items;
        items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &m_camera_ubos[frame]);
        items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, &m_instance_buffers[frame]);
        items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_light_buffers[frame]);
        items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eFragment, 1, grid);
        items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eFragment, 1, list);
        items.emplace_back(9, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &m_globals_ubos[frame]);
        items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, &shadow_img);
        items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_shadow_view_buffers[frame]);
        items.emplace_back(7, VKDT::eSampledImage, VKSF::eFragment, 1, &atlas_img);
        items.emplace_back(8, VKDT::eSampler, VKSF::eFragment, 1, &shadow_sampler_image);
        items.emplace_back(10, VKDT::eSampledImage, VKSF::eFragment, 1, &env_specular_img);
        items.emplace_back(11, VKDT::eStorageBuffer, VKSF::eFragment, 1, env_sh);
        items.emplace_back(12, VKDT::eSampler, VKSF::eFragment, 1, &env_sampler_image);

        constexpr auto bt = util::descriptor_buffer_type::combined;

        if (!m_frame_desc_buffers[frame].buffer.buffer)
            m_frame_desc_buffers[frame] = m_plugin.get_dev()->create_descriptor_buffer(
                m_shared->get_mesh_set_layout(), items, bt, 1);

        m_plugin.get_dev()->compute_descriptor_binding_offsets(m_shared->get_mesh_set_layout(), items);
        m_plugin.get_dev()->update_descriptor_buffer(m_frame_desc_buffers[frame], items, bt);

        // Shadow-pass set: only bindings 0-4 and 9 (camera swapped for the shadow camera). Filter by binding number: the
        // old positional erase dropped binding 9 (globals), which sits at index 5
        std::vector<util::descriptor_item> shadow_items;
        for (const auto& item : items)
            if (item.binding <= 4 || item.binding == 9)
                shadow_items.push_back(item);
        shadow_items[0].p_resources = &m_shadow_camera_ubos[frame];

        if (!m_shadow_desc_buffers[frame].buffer.buffer)
            m_shadow_desc_buffers[frame] = m_plugin.get_dev()->create_descriptor_buffer(m_shared->get_mesh_set_layout(), shadow_items,
                bt, 1);

        m_plugin.get_dev()->update_descriptor_buffer(m_shadow_desc_buffers[frame], shadow_items, bt);
    }

    // pipelines + resources -------------------------------------------------------------------------------------------

    void scene_renderer::create_mesh_material_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_mesh_material_resources");

        auto* dev = m_plugin.get_dev();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // Per-frame material descriptor buffers (layout is on the shared manager)
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; i++) {

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);
            for (u32 t = 0; t < 6; ++t)
                items.emplace_back(1 + t, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);

            items.emplace_back(7, VKDT::eSampler, VKSF::eFragment, 1, nullptr);
            m_material_desc_buffers[i] = dev->create_descriptor_buffer(m_shared->get_material_set_layout(), items,
                util::descriptor_buffer_type::combined, MAX_MATERIALS);
        }
    }


    void scene_renderer::destroy_mesh_material_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_mesh_material_resources");

        auto* dev = m_plugin.get_dev();
        for (auto& d : m_material_desc_buffers)
            if (d.buffer.buffer) {
                dev->destroy_buffer(d.buffer);
                d = {};
            }

        for (auto& d : m_frame_desc_buffers)
            if (d.buffer.buffer) {
                dev->destroy_buffer(d.buffer);
                d = {};
            }
    }

}
