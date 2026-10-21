
#pragma once

#include <asset/material.h>
#include <asset/mesh.h>
#include <asset/texture.h>

namespace GLT::renderer::vk_rasterizer {

    constexpr u32                       PREVIEW_GRID_CLUSTERS = 4096;   // >= max of main grid_x*grid_y*grid_z

    // ================================================================================================================
    // interface methods
    // ================================================================================================================

    GLT::render::preview_id renderer::create(const GLT::render::preview_desc& desc, const glm::uvec2& size) {

        if (size.x == 0 || size.y == 0)
            return GLT::render::INVALID_PREVIEW_ID;

        const auto id = m_next_preview_id++;
        auto& state = m_previews[id];

        state.desc = desc;
        state.settings = {};
        state.camera = {};
        state.active = true;

        state.target = GLT::create_ref<image>();
        state.target->resize({ size.x, size.y, 1 });

        using BU = vk::BufferUsageFlagBits;
        for (u32 frame = 0; frame < MAX_CONCURRENT_FRAMES; ++frame) {

            state.camera_ubos[frame] = m_dev->create_buffer(sizeof(camera_ubo_data), BU::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            state.instance_ssbos[frame] = m_dev->create_buffer(sizeof(gpu_instance_data), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            // Directional light lives at slot 0. Size the buffer like the main path so no shader that reads unconditionally
            // can go out of bounds
            state.light_ssbos[frame] = m_dev->create_buffer(MAX_LIGHTS * sizeof(gpu_light), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            std::memset(m_dev->map_buffer(state.light_ssbos[frame]), 0, MAX_LIGHTS * sizeof(gpu_light));
            m_dev->unmap_buffer(state.light_ssbos[frame]);

            state.light_grid_ssbos[frame] = m_dev->create_buffer(PREVIEW_GRID_CLUSTERS * sizeof(glm::uvec2), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            {
                auto* grid = static_cast<glm::uvec2*>(m_dev->map_buffer(state.light_grid_ssbos[frame]));
                std::fill_n(grid, PREVIEW_GRID_CLUSTERS, glm::uvec2{ 0u, 1u });   // {offset, count}
                m_dev->unmap_buffer(state.light_grid_ssbos[frame]);
            }

            // One-entry index list
            state.light_index_ssbos[frame] = m_dev->create_buffer(sizeof(u32), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            state.globals_ubos[frame] = m_dev->create_buffer(sizeof(gpu_globals), BU::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        }
            
        if (desc.source == GLT::render::preview_source::mesh_asset && desc.mesh != INVALID_HANDLE)
            retain_mesh(desc.mesh);

        return id;
    }


    void renderer::destroy(GLT::render::preview_id id) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        m_device.waitIdle();

        auto& state = it->second;

        for (u32 frame = 0; frame < MAX_CONCURRENT_FRAMES; ++frame) {
            if (state.camera_ubos[frame].buffer)
                m_dev->destroy_buffer(state.camera_ubos[frame]);
            if (state.instance_ssbos[frame].buffer)
                m_dev->destroy_buffer(state.instance_ssbos[frame]);
            if (state.light_ssbos[frame].buffer)
                m_dev->destroy_buffer(state.light_ssbos[frame]);
            if (state.light_grid_ssbos[frame].buffer)
                m_dev->destroy_buffer(state.light_grid_ssbos[frame]);
            if (state.light_index_ssbos[frame].buffer)
                m_dev->destroy_buffer(state.light_index_ssbos[frame]);
            if (state.globals_ubos[frame].buffer)
                m_dev->destroy_buffer(state.globals_ubos[frame]);
            if (state.desc_buffers[frame].buffer.buffer)
                m_dev->destroy_buffer(state.desc_buffers[frame].buffer);
        }

        if (state.resolved_mesh != INVALID_HANDLE)
            release_mesh(state.resolved_mesh);

        state.target.reset();
        m_previews.erase(it);
    }


    void renderer::set_active(GLT::render::preview_id id, bool active) {

        const auto it = m_previews.find(id);
        if (it != m_previews.end())
            it->second.active = active;
    }


    void renderer::set_desc(GLT::render::preview_id id, const GLT::render::preview_desc& desc) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        auto& state = it->second;
        const GLT::asset::handle previous = state.desc.mesh;
        state.desc = desc;

        if (desc.source == GLT::render::preview_source::mesh_asset && desc.mesh != previous) {

            if (state.resolved_mesh != INVALID_HANDLE)
                release_mesh(state.resolved_mesh);
            state.resolved_mesh = INVALID_HANDLE;
            state.material_slots.clear();

            if (desc.mesh != INVALID_HANDLE)
                retain_mesh(desc.mesh);
        }
    }


    void renderer::set_settings(GLT::render::preview_id id, const GLT::render::preview_settings& settings) {

        const auto it = m_previews.find(id);
        if (it != m_previews.end())
            it->second.settings = settings;
    }


    void renderer::set_camera(GLT::render::preview_id id, const GLT::render::preview_camera& camera) {

        const auto it = m_previews.find(id);
        if (it != m_previews.end())
            it->second.camera = camera;
    }


    void renderer::set_material_overrides(GLT::render::preview_id id, const GLT::render::preview_material_overrides& overrides) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        // TODO: clone the target material into a preview-private slot, apply packed_params to the clone's UBO, and swap texture slots
        it->second.overrides = overrides;
    }


    void renderer::set_auto_rotate(GLT::render::preview_id id, bool enabled, f32 speed_rad_per_sec) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        it->second.auto_rotate = enabled;
        it->second.rotate_speed = speed_rad_per_sec;
    }


    void renderer::set_highlight_submeshes(GLT::render::preview_id id, std::span<const u32> submesh_indices) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        // TODO: add an outline/stencil pass using the highlight list
        it->second.highlight_submeshes.assign(submesh_indices.begin(), submesh_indices.end());
    }


    void* renderer::get_render_target(GLT::render::preview_id id) const {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return nullptr;
        return static_cast<void*>(it->second.target->get_descriptor_set());
    }


    glm::uvec2 renderer::get_size(GLT::render::preview_id id) const {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return { 0, 0 };
        return it->second.target->get_size();
    }


    void renderer::set_size(GLT::render::preview_id id, const glm::uvec2& size) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return;

        if (size.x == 0 || size.y == 0)
            return;

        const auto current = it->second.target->get_size();
        if (current.x == size.x && current.y == size.y)
            return;

        m_device.waitIdle();
        it->second.target->resize({ size.x, size.y, 1 });
    }


    // ================================================================================================================
    // internal
    // ================================================================================================================

    void renderer::create_preview_resources() { /* no global resources needed */ }


    void renderer::destroy_preview_resources() { m_previews.clear(); }


    void renderer::add_preview_passes() {

        if (m_previews.empty())
            return;

        const u32 frame = m_current_frame;
        const auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        for (auto& [id, state] : m_previews) {

            if (!state.active || !state.target)
                continue;

            // ---- resolve content ----
            if (state.desc.source != GLT::render::preview_source::mesh_asset)
                continue;                       // TODO: primitives need a built-in mesh generator

            if (state.resolved_mesh != state.desc.mesh) {
                if (state.resolved_mesh != INVALID_HANDLE)
                    release_mesh(state.resolved_mesh);
                state.resolved_mesh = state.desc.mesh;
                state.material_slots.clear();
                if (state.resolved_mesh != INVALID_HANDLE)
                    retain_mesh(state.resolved_mesh);
            }

            if (state.resolved_mesh == INVALID_HANDLE)
                continue;

            const mesh_slot* slot = find_slot(state.resolved_mesh);
            if (!slot || !slot->alive)
                continue;

            const auto* mesh_asset = registry->data_as<GLT::asset::mesh::mesh_asset>(state.resolved_mesh);
            if (!mesh_asset)
                continue;

            const auto size = state.target->get_size();
            if (size.x == 0 || size.y == 0)
                continue;

            // ---- advance animation ----
            if (state.auto_rotate)
                state.accumulated_yaw_rad += static_cast<f64>(state.rotate_speed) *
                    static_cast<f64>(GLT::application::get().get_delta_time());

            // ---- upload per-frame buffers + rebuild descriptor ----
            state.material_slots = slot->material_slots;
            update_preview_resources(id, state, frame);

            // ---- graph textures ----
            graph::texture_desc depth_desc{};
            depth_desc.name = "preview_depth_" + std::to_string(id);
            depth_desc.format = vk::Format::eD32Sfloat;
            depth_desc.width = size.x;
            depth_desc.height = size.y;
            depth_desc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
            depth_desc.persistent = true;
            state.depth_handle = m_graph->create_texture(depth_desc);

            graph::texture_desc color_desc{};
            color_desc.name = "preview_color_" + std::to_string(id);
            color_desc.format = vk::Format::eR8G8B8A8Unorm;
            color_desc.width = size.x;
            color_desc.height = size.y;
            color_desc.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
            color_desc.persistent = true;
            state.color_handle = m_graph->create_texture(color_desc);
            m_graph->import_texture(state.color_handle,
                state.target->get_allocated_image_ref().image,
                state.target->get_accessible_image_ref().view,
                { state.target->get_accessible_image_ref().layout, {}, vk::PipelineStageFlagBits2::eNone });

            // ---- snapshot for the lambdas ----
            const GLT::asset::handle mesh = state.resolved_mesh;
            const std::vector<u32> material_slots = state.material_slots;
            const auto clear = state.settings.background;
            const graph::texture_handle depth = state.depth_handle;
            const graph::texture_handle color = state.color_handle;

            const std::string depth_name = "preview_depth_pass_" + std::to_string(id);
            const std::string scene_name = "preview_scene_pass_" + std::to_string(id);

            // ---- depth prepass (the material main pipeline uses eEqual, so depth must be filled first) ----
            m_graph->add_pass(depth_name,
                [depth](graph::pass_builder& builder) {
                    builder.depth_attachment(depth, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                    builder.set_side_effects(true);
                },
                [this, frame, mesh, material_slots, id](graph::pass_context& ctx) {

                    const auto& s = m_previews.at(id);
                    const mesh_slot* slot = find_slot(mesh);
                    if (!slot || !slot->alive)
                        return;

                    const auto reg = GLT::asset::registry::get_ref();
                    const auto* asset = reg ? reg->data_as<GLT::asset::mesh::mesh_asset>(mesh) : nullptr;
                    if (!asset)
                        return;

                    ctx.begin_rendering();

                    ctx.cmd.bindVertexBuffers(0, { m_vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(m_index_buffer.buffer, 0, vk::IndexType::eUint32);

                    m_dev->bind_descriptor_buffer({
                        s.desc_buffers[frame],
                        m_material_desc_buffers[frame]
                    }, ctx.cmd);
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                    const u32 count = static_cast<u32>(std::min(material_slots.size(), asset->submeshes.size()));
                    for (u32 sub = 0; sub < count; ++sub) {

                        const u32 mat_slot = material_slots[sub];
                        const material_slot& entry = m_material_slots[mat_slot];
                        if (!entry.alive || !entry.depth_pipeline)
                            continue;

                        ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.depth_pipeline);
                        m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 1, 1, 
                            m_material_desc_buffers[frame].get_offset_to_set(mat_slot), ctx.cmd, vk::PipelineBindPoint::eGraphics);

                        const auto& submesh = asset->submeshes[sub];
                        ctx.cmd.drawIndexed(submesh.index_count, 1, slot->index_offset + submesh.first_index,
                            static_cast<i32>(slot->vertex_offset), 0);
                        m_frame_draw_calls++;
                    }

                    ctx.end_rendering();
                    m_frame_render_passes++;
                });

            // ---- main colour pass ----
            m_graph->add_pass(scene_name,
                [color, depth, clear](graph::pass_builder& builder) {
                    builder.color_attachment(color, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                        vk::ClearColorValue(std::array<f32,4>{ clear.r, clear.g, clear.b, clear.a }));
                    builder.depth_attachment(depth, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                    builder.set_side_effects(true);
                },
                [this, frame, mesh, material_slots, id](graph::pass_context& ctx) {

                    const auto& s = m_previews.at(id);
                    const mesh_slot* slot = find_slot(mesh);
                    if (!slot || !slot->alive)
                        return;

                    const auto reg = GLT::asset::registry::get_ref();
                    const auto* asset = reg ? reg->data_as<GLT::asset::mesh::mesh_asset>(mesh) : nullptr;
                    if (!asset)
                        return;

                    ctx.begin_rendering();

                    ctx.cmd.bindVertexBuffers(0, { m_vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(m_index_buffer.buffer, 0, vk::IndexType::eUint32);

                    m_dev->bind_descriptor_buffer({
                        s.desc_buffers[frame],
                        m_material_desc_buffers[frame]
                    }, ctx.cmd);
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, /*bufferIndex=*/0, 0,
                        ctx.cmd, vk::PipelineBindPoint::eGraphics);

                    const u32 count = static_cast<u32>(std::min(material_slots.size(), asset->submeshes.size()));
                    for (u32 sub = 0; sub < count; ++sub) {

                        const u32 mat_slot = material_slots[sub];
                        const material_slot& entry = m_material_slots[mat_slot];
                        if (!entry.alive || !entry.pipeline)
                            continue;

                        ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);
                        m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 1, /*bufferIndex=*/1,
                            m_material_desc_buffers[frame].get_offset_to_set(mat_slot),
                            ctx.cmd, vk::PipelineBindPoint::eGraphics);

                        const auto& submesh = asset->submeshes[sub];
                        ctx.cmd.drawIndexed(submesh.index_count, 1,
                            slot->index_offset + submesh.first_index,
                            static_cast<i32>(slot->vertex_offset), 0);
                        m_frame_draw_calls++;
                    }

                    ctx.end_rendering();
                    m_frame_render_passes++;
                });
        }
    }


    void renderer::update_preview_resources(GLT::render::preview_id /*id*/, preview_state& state, u32 frame) {

        const auto size = state.target->get_size();
        if (size.x == 0 || size.y == 0)
            return;

        const mesh_slot* slot = find_slot(state.resolved_mesh);
        if (!slot)
            return;

        // ---- orbit camera ----
        const glm::vec3 target = state.camera.target;
        const f32 yaw = state.camera.yaw_rad + static_cast<f32>(state.accumulated_yaw_rad);
        const f32 pitch = state.camera.pitch_rad;
        const f32 distance = state.camera.distance;

        const glm::vec3 offset(
            distance * std::cos(pitch) * std::sin(yaw),
            distance * std::sin(pitch),
            distance * std::cos(pitch) * std::cos(yaw));
        const glm::vec3 position = target + offset;

        const glm::mat4 view = glm::lookAt(position, target, glm::vec3(0.f, 1.f, 0.f));
        const f32 aspect = static_cast<f32>(size.x) / static_cast<f32>(size.y);
        glm::mat4 projection = glm::perspective(glm::radians(state.camera.fov_deg), aspect, 0.1f, 1000.f);
        projection[1][1] *= -1.0f;
        const glm::mat4 view_projection = projection * view;

        // ---- camera UBO ----
        camera_ubo_data cam{};
        cam.view = view;
        cam.view_proj = view_projection;
        cam.camera_pos = glm::vec4(position, 1.f);
        cam.projection = projection;
        cam.inv_projection = glm::inverse(projection);
        cam.cluster_dims = glm::uvec4(1u, 1u, 1u, 0u);
        cam.cluster_depth = glm::vec4(0.1f, 100.f, static_cast<f32>(size.x), static_cast<f32>(size.y));
        cam.light_view_proj = glm::mat4(1.f);
        cam.shadow_params = glm::vec4(0.f);                 // disables shadow sampling in the material shader
        extract_frustum_planes(view_projection, cam.frustum_planes);
        m_dev->update_buffer(state.camera_ubos[frame], &cam, sizeof(cam));

        // ---- globals UBO ----
        const glm::vec3 forward = glm::normalize(target - position);
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.f, 1.f, 0.f)));
        const glm::vec3 up = glm::cross(right, forward);

        auto& app = GLT::application::get();
        gpu_globals globals{};
        globals.time = app.get_total_time();
        globals.delta_time = app.get_delta_time();
        globals.frame_index = m_frame_counter;
        globals.viewport_size = glm::vec2(size);
        globals.render_size = glm::vec2(size);
        globals.camera_position = position;
        globals.near_plane = 0.1f;
        globals.far_plane = 100.f;
        globals.camera_forward = forward;
        globals.camera_right = right;
        globals.camera_up = up;
        globals.view = view;
        globals.projection = projection;
        globals.view_projection = view_projection;
        globals.inv_view = glm::inverse(view);
        globals.inv_projection = glm::inverse(projection);
        globals.inv_view_projection = glm::inverse(view_projection);
        m_dev->update_buffer(state.globals_ubos[frame], &globals, sizeof(globals));

        // ---- single instance ----
        const u32 mesh_index = static_cast<u32>(slot - m_mesh_slots.data());
        gpu_instance_data instance{};
        instance.model = glm::mat4(1.f);
        instance.normal_matrix = glm::mat4(1.f);
        instance.mesh_index = mesh_index;
        instance.material_index = 0u;
        m_dev->update_buffer(state.instance_ssbos[frame], &instance, sizeof(instance));

        // ---- camera-relative key light ----
        // TODO: restore camera-relative once direction sanity-checked; for now fixed in world space
        const glm::vec3 light_dir_world = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

        gpu_light light{};
        light.direction_inner_cos = glm::vec4(light_dir_world, 1.0f);
        light.color_intensity = glm::vec4(1.0f, 0.98f, 0.94f, 10.0f);   // temporarily 10
        light.params = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
        m_dev->update_buffer(state.light_ssbos[frame], &light, sizeof(gpu_light));

        // ---- light grid + index: every cluster -> {offset 0, count 1}; index list -> light 0 ----
        // Rewritten each frame for this frame's buffers; cheap, and removes any init-once pitfall
        {
            auto* grid = static_cast<glm::uvec2*>(m_dev->map_buffer(state.light_grid_ssbos[frame]));
            std::fill_n(grid, PREVIEW_GRID_CLUSTERS, glm::uvec2{ 0u, 1u });
            m_dev->unmap_buffer(state.light_grid_ssbos[frame]);

            auto* idx = static_cast<u32*>(m_dev->map_buffer(state.light_index_ssbos[frame]));
            idx[0] = 0u;
            m_dev->unmap_buffer(state.light_index_ssbos[frame]);
        }

        // ---- descriptor ----
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        util::accessible_image shadow_image{};
        shadow_image.view = m_graph->texture_default_view(m_shadow_map);
        shadow_image.sampler = m_shadow_sampler;
        shadow_image.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        util::accessible_image atlas_image{};
        atlas_image.view = m_graph->texture_default_view(m_shadow_atlas);
        atlas_image.sampler = m_shadow_sampler;
        atlas_image.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        util::accessible_image shadow_sampler_image{};
        shadow_sampler_image.sampler = m_shadow_sampler;

        std::vector<util::descriptor_item> items;
        items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &state.camera_ubos[frame]);
        items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, &state.instance_ssbos[frame]);
        items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eFragment, 1, &state.light_ssbos[frame]);
        items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eFragment, 1, &state.light_grid_ssbos[frame]);
        items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eFragment, 1, &state.light_index_ssbos[frame]);
        items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, &shadow_image);
        items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_shadow_view_buffers[frame]);
        items.emplace_back(7, VKDT::eSampledImage, VKSF::eFragment, 1, &atlas_image);
        items.emplace_back(8, VKDT::eSampler, VKSF::eFragment, 1, &shadow_sampler_image);
        items.emplace_back(9, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &state.globals_ubos[frame]);

        constexpr auto buffer_type = util::descriptor_buffer_type::combined;

        if (state.desc_buffers[frame].buffer.buffer == nullptr)
            state.desc_buffers[frame] = m_dev->create_descriptor_buffer(m_mesh_set_layout, items, buffer_type, 1);

        m_dev->compute_descriptor_binding_offsets(m_mesh_set_layout, items);
        m_dev->update_descriptor_buffer(state.desc_buffers[frame], items, buffer_type);
    }


    void renderer::sync_preview_images_layout() {

        for (auto& [id, state] : m_previews)
            if (state.target && state.color_handle.is_valid())
                state.target->get_accessible_image_ref().layout = m_graph->texture_layout(state.color_handle);
    }

}
