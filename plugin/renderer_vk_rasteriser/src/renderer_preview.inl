
#pragma once

#include <asset/material.h>
#include <asset/mesh.h>
#include <asset/texture.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_debug_draw;
}

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    constexpr u32                       PREVIEW_GRID_CLUSTERS = 4096;   // >= max of main grid_x*grid_y*grid_z

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
        state.debug_draw = GLT::create_unique_ref<preview_debug_draw>(*this, id);

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
            
        return id;
    }


    void renderer::destroy(GLT::render::preview_id id) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        m_device.waitIdle();

        auto& state = it->second;

        for (u32 slot : state.material_slots)
            release_material(slot);

        if (state.vertex_buffer.buffer) { m_dev->destroy_buffer(state.vertex_buffer); state.vertex_buffer = {}; }
        if (state.index_buffer.buffer)  { m_dev->destroy_buffer(state.index_buffer);  state.index_buffer = {}; }

        for (u32 frame = 0; frame < MAX_CONCURRENT_FRAMES; ++frame) {
            if (state.camera_ubos[frame].buffer) {
                m_dev->destroy_buffer(state.camera_ubos[frame]);
                state.camera_ubos[frame] = {};
            }
            if (state.instance_ssbos[frame].buffer) {
                m_dev->destroy_buffer(state.instance_ssbos[frame]);
                state.instance_ssbos[frame] = {};
            }
            if (state.light_ssbos[frame].buffer) {
                m_dev->destroy_buffer(state.light_ssbos[frame]);
                state.light_ssbos[frame] = {};
            }
            if (state.light_grid_ssbos[frame].buffer) {
                m_dev->destroy_buffer(state.light_grid_ssbos[frame]);
                state.light_grid_ssbos[frame] = {};
            }
            if (state.light_index_ssbos[frame].buffer) {
                m_dev->destroy_buffer(state.light_index_ssbos[frame]);
                state.light_index_ssbos[frame] = {};
            }
            if (state.globals_ubos[frame].buffer) {
                m_dev->destroy_buffer(state.globals_ubos[frame]);
                state.globals_ubos[frame] = {};
            }
            if (state.desc_buffers[frame].buffer.buffer) {
                m_dev->destroy_buffer(state.desc_buffers[frame].buffer);
                state.desc_buffers[frame] = {};
            }
            if (state.debug_vertex_buffers[frame].buffer) {
                m_dev->destroy_buffer(state.debug_vertex_buffers[frame]);
                state.debug_vertex_buffers[frame]= {};
            }
            if (state.debug_desc_buffers[frame].buffer.buffer) {
                m_dev->destroy_buffer(state.debug_desc_buffers[frame].buffer);
                state.debug_desc_buffers[frame] = {};
            }
        }

        state.color_handle = {};
        state.depth_handle = {};
        state.debug_draw.reset();
        state.debug_primitives.clear();
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

        it->second.desc = desc;
        it->second.resolved_mesh = INVALID_HANDLE;   // force re-upload on next add_preview_passes
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


    GLT::render::i_debug_draw* renderer::get_debug_draw(GLT::render::preview_id id) {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.debug_draw)
            return nullptr;
        return it->second.debug_draw.get();
    }

    // preview_debug_draw ----------------------------------------------------------------------------------------------

    void renderer::preview_debug_draw::draw_line(const GLT::render::debug_line_primitive& line) {

        const auto it = m_owner.m_previews.find(m_id);
        if (it == m_owner.m_previews.end())
            return;

        auto& state = it->second;
        std::lock_guard lock(state.debug_mutex);

        debug_primitive_record rec;
        rec.type = debug_primitive_record::kind::line;
        rec.verts[0] = { line.from, 0.0f, line.color };
        rec.verts[1] = { line.to,   0.0f, line.color };
        rec.vertex_count = 2;
        rec.remaining_lifetime = line.lifetime;
        rec.depth_test = line.depth_test;
        state.debug_primitives.push_back(rec);
    }


    void renderer::preview_debug_draw::draw_triangle(const GLT::render::debug_triangle_primitive& tri) {

        const auto it = m_owner.m_previews.find(m_id);
        if (it == m_owner.m_previews.end())
            return;

        auto& state = it->second;
        std::lock_guard lock(state.debug_mutex);

        debug_primitive_record rec;
        rec.type = debug_primitive_record::kind::triangle;
        rec.verts[0] = { tri.a, 0.0f, tri.color };
        rec.verts[1] = { tri.b, 0.0f, tri.color };
        rec.verts[2] = { tri.c, 0.0f, tri.color };
        rec.vertex_count = 3;
        rec.remaining_lifetime = tri.lifetime;
        rec.depth_test = tri.depth_test;
        state.debug_primitives.push_back(rec);
    }


    void renderer::preview_debug_draw::draw_aabb(const GLT::render::debug_aabb_primitive& box) {

        const auto it = m_owner.m_previews.find(m_id);
        if (it == m_owner.m_previews.end())
            return;

        auto& state = it->second;
        std::lock_guard lock(state.debug_mutex);

        const glm::vec3 c[8] = {
            { box.min.x, box.min.y, box.min.z }, { box.max.x, box.min.y, box.min.z },
            { box.max.x, box.max.y, box.min.z }, { box.min.x, box.max.y, box.min.z },
            { box.min.x, box.min.y, box.max.z }, { box.max.x, box.min.y, box.max.z },
            { box.max.x, box.max.y, box.max.z }, { box.min.x, box.max.y, box.max.z },
        };
        static constexpr u32 e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };

        for (auto& [a, b] : e) {
            debug_primitive_record rec;
            rec.type = debug_primitive_record::kind::line;
            rec.verts[0] = { c[a], 0.0f, box.color };
            rec.verts[1] = { c[b], 0.0f, box.color };
            rec.vertex_count = 2;
            rec.remaining_lifetime = box.lifetime;
            rec.depth_test = box.depth_test;
            state.debug_primitives.push_back(rec);
        }
    }


    void renderer::preview_debug_draw::clear() {

        const auto it = m_owner.m_previews.find(m_id);
        if (it == m_owner.m_previews.end())
            return;

        std::lock_guard lock(it->second.debug_mutex);
        it->second.debug_primitives.clear();
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::create_preview_resources() { /* no global resources needed */ }


    void renderer::destroy_preview_resources() {

        std::vector<GLT::render::preview_id> ids;
        ids.reserve(m_previews.size());
        for (const auto& entry : m_previews)
            ids.push_back(entry.first);

        for (auto id : ids)
            destroy(id);
    }


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
                if (!upload_preview_mesh(state, state.desc.mesh))
                    continue;
            }

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
            if (state.auto_rotate)
                state.accumulated_yaw_rad += static_cast<f64>(state.rotate_speed) *
                    static_cast<f64>(GLT::application::get().get_delta_time());

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
            const vk::Buffer preview_vb = state.vertex_buffer.buffer;
            const vk::Buffer preview_ib = state.index_buffer.buffer;
            const std::string depth_name = "preview_depth_pass_" + std::to_string(id);
            const std::string scene_name = "preview_scene_pass_" + std::to_string(id);

            // ---- depth prepass (the material main pipeline uses eEqual, so depth must be filled first) ----
            m_graph->add_pass(depth_name,
                [depth](graph::pass_builder& b) {
                    b.depth_attachment(depth, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                    b.set_side_effects(true);
                },
                [this, frame, mesh, material_slots, preview_vb, preview_ib, id](graph::pass_context& ctx) {

                    const auto it = m_previews.find(id);
                    if (it == m_previews.end())
                        return;
                    const auto& s = it->second;

                    const auto reg = GLT::asset::registry::get_ref();
                    const auto* asset = reg ? reg->data_as<GLT::asset::mesh::mesh_asset>(mesh) : nullptr;
                    if (!asset)
                        return;

                    // Guard against a pass that outlived a mid-frame destroy / clear
                    if (s.desc_buffers[frame].buffer.buffer == nullptr)
                        return;

                    ctx.begin_rendering();
                    ctx.cmd.bindVertexBuffers(0, { preview_vb }, { 0 });
                    ctx.cmd.bindIndexBuffer(preview_ib, 0, vk::IndexType::eUint32);

                    m_dev->bind_descriptor_buffer({ s.desc_buffers[frame], m_material_desc_buffers[frame] }, ctx.cmd);
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

                        const auto& sm = asset->submeshes[sub];
                        ctx.cmd.drawIndexed(sm.index_count, 1, sm.first_index, 0, 0);
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
                [this, frame, mesh, material_slots, preview_vb, preview_ib, id](graph::pass_context& ctx) {

                    const auto& s = m_previews.at(id);
                    const auto reg = GLT::asset::registry::get_ref();
                    const auto* asset = reg ? reg->data_as<GLT::asset::mesh::mesh_asset>(mesh) : nullptr;
                    if (!asset)
                        return;

                    ctx.begin_rendering();
                    ctx.cmd.bindVertexBuffers(0, { preview_vb }, { 0 });
                    ctx.cmd.bindIndexBuffer(preview_ib, 0, vk::IndexType::eUint32);

                    m_dev->bind_descriptor_buffer({
                        s.desc_buffers[frame],
                        m_material_desc_buffers[frame]
                    }, ctx.cmd);
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                    const u32 count = static_cast<u32>(std::min(material_slots.size(), asset->submeshes.size()));
                    for (u32 sub = 0; sub < count; ++sub) {

                        const u32 mat_slot = material_slots[sub];
                        const material_slot& entry = m_material_slots[mat_slot];
                        if (!entry.alive || !entry.pipeline)
                            continue;

                        ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);
                        m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 1, 1, 
                            m_material_desc_buffers[frame].get_offset_to_set(mat_slot), ctx.cmd, vk::PipelineBindPoint::eGraphics);

                        const auto& submesh = asset->submeshes[sub];
                        ctx.cmd.drawIndexed(submesh.index_count, 1, submesh.first_index, 0, 0);
                        m_frame_draw_calls++;
                    }

                    ctx.end_rendering();
                    m_frame_render_passes++;
                });

            // ---- debug overlay pass (reuses the main debug pipelines, but with the preview's camera) ----
            add_preview_debug_pass(id, state, frame, color, depth);
        }
    }


    void renderer::sync_preview_images_layout() {

        for (auto& [id, state] : m_previews)
            if (state.target && state.color_handle.is_valid())
                state.target->get_accessible_image_ref().layout = m_graph->texture_layout(state.color_handle);
    }


    void renderer::update_preview_resources(GLT::render::preview_id /*id*/, preview_state& state, u32 frame) {

        const auto size = state.target->get_size();
        if (size.x == 0 || size.y == 0)
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

        camera_ubo_data cam{};
        cam.view = view;
        cam.view_proj = view_projection;
        cam.camera_pos = glm::vec4(position, 1.f);
        cam.projection = projection;
        cam.inv_projection = glm::inverse(projection);
        cam.cluster_dims = glm::uvec4(1u, 1u, 1u, 0u);
        cam.cluster_depth = glm::vec4(0.1f, 100.f, static_cast<f32>(size.x), static_cast<f32>(size.y));
        cam.light_view_proj = glm::mat4(1.f);
        cam.shadow_params = glm::vec4(0.f);
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

        // ---- single instance — mesh_index no longer addresses m_mesh_slots ----
        gpu_instance_data instance{};
        instance.model = glm::mat4(1.f);
        instance.normal_matrix = glm::mat4(1.f);
        instance.mesh_index = 0u;         // unused by the material shader path
        instance.material_index = 0u;
        m_dev->update_buffer(state.instance_ssbos[frame], &instance, sizeof(instance));

        // ---- camera-relative key light ----
        const glm::vec3 light_dir_world = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));

        gpu_light light{};
        light.direction_inner_cos = glm::vec4(light_dir_world, 1.0f);
        light.color_intensity = glm::vec4(1.0f, 0.98f, 0.94f, 10.0f);
        light.params = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
        m_dev->update_buffer(state.light_ssbos[frame], &light, sizeof(gpu_light));

        // ---- light grid + index ----
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


    bool renderer::upload_preview_mesh(preview_state& state, GLT::asset::handle mesh_handle) {

        for (u32 slot : state.material_slots)
            release_material(slot);
        state.material_slots.clear();

        if (state.vertex_buffer.buffer) { m_dev->destroy_buffer(state.vertex_buffer); state.vertex_buffer = {}; }
        if (state.index_buffer.buffer)  { m_dev->destroy_buffer(state.index_buffer);  state.index_buffer = {}; }

        state.resolved_mesh = INVALID_HANDLE;

        if (mesh_handle == INVALID_HANDLE)
            return false;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry) return false;

        const auto* asset = registry->data_as<GLT::asset::mesh::mesh_asset>(mesh_handle);
        if (!asset || asset->vertices.empty() || asset->indices.empty())
            return false;

        const u64 vb_bytes = asset->vertices.size() * sizeof(GLT::asset::mesh::vertex);
        const u64 ib_bytes = asset->indices.size()  * sizeof(u32);

        using BU = vk::BufferUsageFlagBits;
        state.vertex_buffer = m_dev->create_buffer(vb_bytes, BU::eVertexBuffer | BU::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        state.index_buffer = m_dev->create_buffer(ib_bytes, BU::eIndexBuffer  | BU::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        if (!state.vertex_buffer.buffer || !state.index_buffer.buffer) {
            if (state.vertex_buffer.buffer) { m_dev->destroy_buffer(state.vertex_buffer); state.vertex_buffer = {}; }
            if (state.index_buffer.buffer)  { m_dev->destroy_buffer(state.index_buffer);  state.index_buffer = {}; }
            return false;
        }

        // ---- upload vertices ----
        {
            void* dst = m_dev->map_buffer(state.vertex_buffer);
            if (!dst) {
                m_dev->destroy_buffer(state.vertex_buffer); state.vertex_buffer = {};
                m_dev->destroy_buffer(state.index_buffer);  state.index_buffer = {};
                return false;
            }
            std::memcpy(dst, asset->vertices.data(), vb_bytes);
            m_dev->unmap_buffer(state.vertex_buffer);
        }

        // ---- upload indices ----
        {
            void* dst = m_dev->map_buffer(state.index_buffer);
            if (!dst) {
                m_dev->destroy_buffer(state.vertex_buffer); state.vertex_buffer = {};
                m_dev->destroy_buffer(state.index_buffer);  state.index_buffer = {};
                return false;
            }
            std::memcpy(dst, asset->indices.data(), ib_bytes);
            m_dev->unmap_buffer(state.index_buffer);
        }

        for (const auto& submesh : asset->submeshes)
            state.material_slots.push_back(acquire_material(submesh.material));

        state.resolved_mesh = mesh_handle;
        return true;
    }


    void renderer::add_preview_debug_pass(GLT::render::preview_id id, preview_state& state, u32 frame, graph::texture_handle color,
        graph::texture_handle depth) {

        // snapshot + age
        std::vector<debug_primitive_record> pending;
        {
            const f32 dt = GLT::application::get().get_delta_time();
            std::lock_guard lock(state.debug_mutex);
            pending = state.debug_primitives;
            std::erase_if(state.debug_primitives, [dt](debug_primitive_record& p) {
                if (p.remaining_lifetime == GLT::render::DEBUG_DRAW_FOREVER)
                    return false;
                p.remaining_lifetime -= dt;
                return p.remaining_lifetime <= 0.0f;
            });
        }
        if (pending.empty())
            return;

        // bucket into lines/tris × depth/overlay
        frame_debug_layout layout{};
        std::vector<debug_vertex> vertices;
        vertices.reserve(pending.size() * 3);

        auto append_bucket = [&](debug_primitive_record::kind kind, bool depth_test) -> std::pair<u32, u32> {
            const u32 offset = static_cast<u32>(vertices.size());
            u32 count = 0;
            for (const auto& p : pending) {

                if (p.type != kind || p.depth_test != depth_test)
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

        if (vertices.size() > MAX_PREVIEW_DEBUG_VERTICES) {
            LOG(warn, "Preview {} debug draw budget exceeded ({} > {}), skipping frame", id, vertices.size(), MAX_PREVIEW_DEBUG_VERTICES);
            return;
        }

        // ---- lazy vertex buffer ----
        if (!state.debug_vertex_buffers[frame].buffer) {
            state.debug_vertex_buffers[frame] = m_dev->create_buffer(MAX_PREVIEW_DEBUG_VERTICES * sizeof(debug_vertex),
                vk::BufferUsageFlagBits::eVertexBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        }

        {
            void* mapped = m_dev->map_buffer(state.debug_vertex_buffers[frame]);
            if (!mapped)
                return;
            std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(debug_vertex));
            m_dev->unmap_buffer(state.debug_vertex_buffers[frame]);
        }

        // ---- lazy descriptor buffer, pointing at the preview's camera UBO ----
        // The debug shader only reads view_proj (offset 0) and the preview camera_ubo_data starts with
        // view_proj, so the same set layout + shader works unchanged
        if (state.debug_desc_buffers[frame].buffer.buffer == nullptr) {

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1, &state.camera_ubos[frame]);

            state.debug_desc_buffers[frame] = m_dev->create_descriptor_buffer(m_debug_set_layout, items,
                util::descriptor_buffer_type::resource, 1);
            m_dev->update_descriptor_buffer(state.debug_desc_buffers[frame], items, util::descriptor_buffer_type::resource);
        }

        // ---- snapshots for the pass ----
        const vk::Buffer vb = state.debug_vertex_buffers[frame].buffer;
        const util::descriptor_buffer* desc = &state.debug_desc_buffers[frame];
        const glm::uvec2 size = state.target->get_size();
        const u32 w = size.x;
        const u32 h = size.y;
        const std::string name = "preview_debug_pass_" + std::to_string(id);

        m_graph->add_pass(name,
            [color, depth](graph::pass_builder& b) {
                b.color_attachment(color, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                b.depth_attachment(depth, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                b.set_side_effects(true);
            },
            [this, vb, desc, layout, w, h](graph::pass_context& ctx) {

                ctx.begin_rendering();

                ctx.cmd.bindVertexBuffers(0, { vb }, { 0 });
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(w), static_cast<f32>(h), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, { w, h }));

                m_dev->bind_descriptor_buffer({ *desc }, ctx.cmd);
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

}
