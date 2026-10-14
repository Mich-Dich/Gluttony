
#pragma once

#include <asset/material.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static FORCE_INLINE void extract_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static FORCE_INLINE void extract_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]) {

        const glm::mat4 t = glm::transpose(view_projection);
        planes[0] = t[3] + t[0];   // left
        planes[1] = t[3] - t[0];   // right
        planes[2] = t[3] + t[1];   // bottom
        planes[3] = t[3] - t[1];   // top
        planes[4] = t[3] + t[2];   // near
        planes[5] = t[3] - t[2];   // far

        for (auto& plane : planes) {
            const f32 length = glm::length(glm::vec3(plane));
            if (length > 0.0f)
                plane /= length;
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    bool renderer::load_mesh(const std::filesystem::path& content_relative_path) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        auto handle = registry->load(content_relative_path);
        VALIDATE(handle.has_value(), return false, "", "Failed to load mesh [{}]", (content_relative_path.generic_string()))

        return load_mesh(*handle);
    }


    bool renderer::load_mesh(const GLT::asset::handle handle) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        if (find_slot(handle))
            return true;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(handle);
        VALIDATE(mesh, return false, "", "Asset [{}] is not a mesh_asset", (registry->info(handle).name))

        reserve_mesh_space(static_cast<u32>(mesh->vertices.size()), static_cast<u32>(mesh->indices.size()));

        u32 slot_idx;
        if (!m_free_slots.empty()) {

            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();

        } else {
            
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot.asset = handle;
        slot.vertex_offset = m_vertex_used;
        slot.vertex_count = static_cast<u32>(mesh->vertices.size());
        slot.index_offset = m_index_used;
        slot.index_count = static_cast<u32>(mesh->indices.size());
        slot.alive = true;

        upload_mesh_slice(slot, *mesh);
        pack_mesh_into_ssbo(slot, *mesh);

        m_vertex_used += slot.vertex_count;
        m_index_used += slot.index_count;
        return true;
    }


    void renderer::unload_mesh(GLT::asset::handle handle) {

        u32 slot_index = UINT32_MAX;
        for (u32 index = 0; index < m_mesh_slots.size(); ++index) {
            if (m_mesh_slots[index].alive && m_mesh_slots[index].asset == handle) {
                slot_index = index;
                break;
            }
        }

        VALIDATE(slot_index != UINT32_MAX, return, "", "handle not loaded")

        m_device.waitIdle();

        mesh_slot& slot = m_mesh_slots[slot_index];
        clear_mesh_ssbo_entry(slot);

        slot.alive = false;
        slot.asset = INVALID_HANDLE;
        slot.vertex_offset = 0;
        slot.vertex_count = 0;
        slot.index_offset = 0;
        slot.index_count = 0;

        m_free_slots.push_back(slot_index);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    bool renderer::reserve_mesh_space(u32 vertex_count, u32 index_count) {

        const u32 need_v = m_vertex_used + vertex_count;
        const u32 need_i = m_index_used  + index_count;
        bool grew = false;

        if (need_v > m_vertex_capacity) {
            m_vertex_capacity = std::max(need_v, m_vertex_capacity + std::max(m_vertex_capacity / 2, VERTEX_HEADROOM_MIN));
            grew = true;
        }
        if (need_i > m_index_capacity) {
            m_index_capacity = std::max(need_i, m_index_capacity + std::max(m_index_capacity / 2, INDEX_HEADROOM_MIN));
            grew = true;
        }
        if (!grew)
            return false;

        const auto usage_v = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
        const auto usage_i = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer;

        auto new_vb = m_dev->create_buffer(m_vertex_capacity * sizeof(GLT::asset::mesh::vertex), usage_v, 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        auto new_ib = m_dev->create_buffer(m_index_capacity * sizeof(u32), usage_i,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memset(m_dev->map_buffer(new_vb), 0, m_vertex_capacity * sizeof(GLT::asset::mesh::vertex));
        std::memset(m_dev->map_buffer(new_ib), 0, m_index_capacity  * sizeof(u32));
        m_dev->unmap_buffer(new_vb);
        m_dev->unmap_buffer(new_ib);

        if (m_vertex_used) {
            const void* src = m_dev->map_buffer(m_vertex_buffer);
            void* dst = m_dev->map_buffer(new_vb);
            std::memcpy(dst, src, m_vertex_used * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(m_vertex_buffer);
            m_dev->unmap_buffer(new_vb);
        }
        if (m_index_used) {
            const void* src = m_dev->map_buffer(m_index_buffer);
            void* dst = m_dev->map_buffer(new_ib);
            std::memcpy(dst, src, m_index_used * sizeof(u32));
            m_dev->unmap_buffer(m_index_buffer);
            m_dev->unmap_buffer(new_ib);
        }

        std::swap(m_vertex_buffer, new_vb);
        std::swap(m_index_buffer, new_ib);

        m_device.waitIdle();
        if (new_vb.buffer)
            m_dev->destroy_buffer(new_vb);

        if (new_ib.buffer)
            m_dev->destroy_buffer(new_ib);

        return true;
    }


    void renderer::upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh) {

        {
            auto* dst = static_cast<GLT::asset::mesh::vertex*>(m_dev->map_buffer(m_vertex_buffer));
            std::memcpy(dst + slot.vertex_offset, mesh.vertices.data(), mesh.vertices.size() * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(m_vertex_buffer);
        }
        {
            auto* dst = static_cast<u32*>(m_dev->map_buffer(m_index_buffer));
            std::memcpy(dst + slot.index_offset, mesh.indices.data(), mesh.indices.size() * sizeof(u32));
            m_dev->unmap_buffer(m_index_buffer);
        }
    }


    renderer::mesh_slot* renderer::find_slot(GLT::asset::handle handle) noexcept {

        for (auto& slot : m_mesh_slots)
            if (slot.alive && slot.asset == handle)
                return &slot;
        return nullptr;
    }


    const renderer::mesh_slot* renderer::find_slot(GLT::asset::handle handle) const noexcept { 

        return const_cast<renderer*>(this)->find_slot(handle);
    }

    // mesh resources --------------------------------------------------------------------------------------------------

    void renderer::create_mesh_resources() {

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // Descriptor set layout:
        std::vector<util::descriptor_item> descriptor_layout_items;
        descriptor_layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);     // 0  Camera UBO
        descriptor_layout_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, nullptr);                       // 1  Instance SSBO
        descriptor_layout_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);                     // 2  Lights SSBO
        descriptor_layout_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);                     // 3  Light grid SSBO
        descriptor_layout_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);                     // 4  Light index list
        descriptor_layout_items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);                      // 5  Shadow map
        descriptor_layout_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);                     // 6  Shadow views
        descriptor_layout_items.emplace_back(7, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);                      // 7  Shadow atlas
        descriptor_layout_items.emplace_back(8, VKDT::eSampler, VKSF::eFragment, 1, nullptr);                           // 8  shadow comparison sampler

        m_mesh_set_layout = m_dev->create_descriptor_set_layout(descriptor_layout_items);
        m_live_descriptor_set_count++;
        
        // set 0 = per frame, set 1 = materials + bindless textures (created by create_material_resources())
        const std::array<vk::DescriptorSetLayout, 2> set_layouts = { 
            m_mesh_set_layout,
            m_material_set_layout
        };

        // Pipeline layout unchanged (no push constants; everything via descriptors)
        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = static_cast<u32>(set_layouts.size());
        pipeline_layout_info.pSetLayouts = set_layouts.data();
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges = nullptr;
        m_mesh_pipeline_layout = m_device.createPipelineLayout(pipeline_layout_info);

        // Per-frame camera UBO + instance SSBO. The descriptor buffer is created lazily in
        // update_mesh_descriptors() once the cluster buffers exist
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            m_camera_ubos[frame_index] = m_dev->create_buffer(sizeof(camera_ubo_data), vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            m_instance_buffers[frame_index] = m_dev->create_buffer(MAX_INSTANCES * sizeof(gpu_instance_data),
                vk::BufferUsageFlagBits::eStorageBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            // per-instance-per-submesh material slots (MAX_INSTANCES * MAX_SUBMESHES_PER_MESH u32s = 512 KB at current values)
            m_instance_override_buffers[frame_index] = m_dev->create_buffer(MAX_INSTANCES * MAX_SUBMESHES_PER_MESH * sizeof(u32),
                vk::BufferUsageFlagBits::eStorageBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            m_live_buffer_count++;
        }

        // Persistent mesh-side SSBOs, read only by the cull compute shader --------------------------------------------
        m_mesh_data_buffer = m_dev->create_buffer(MAX_MESHES * sizeof(gpu_mesh_data), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        m_submesh_data_buffer = m_dev->create_buffer(MAX_SUBMESHES * sizeof(gpu_submesh_data), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        // Zero the whole thing so unloaded slots read as submesh_count = 0
        std::memset(m_dev->map_buffer(m_mesh_data_buffer), 0, MAX_MESHES * sizeof(gpu_mesh_data));
        m_dev->unmap_buffer(m_mesh_data_buffer);
        std::memset(m_dev->map_buffer(m_submesh_data_buffer), 0, MAX_SUBMESHES * sizeof(gpu_submesh_data));
        m_dev->unmap_buffer(m_submesh_data_buffer);

        // Pre-reserve buffer headroom ----------------------------------------------------------------------------------
        reserve_mesh_space(VERTEX_HEADROOM_MIN, INDEX_HEADROOM_MIN);
        create_cull_pipeline();
    }


    void renderer::destroy_mesh_resources() {

        m_device.waitIdle();
        
        destroy_cull_pipeline();

        if (m_mesh_data_buffer.buffer)
            m_dev->destroy_buffer(m_mesh_data_buffer);

        if (m_submesh_data_buffer.buffer)
            m_dev->destroy_buffer(m_submesh_data_buffer);

        for (auto& descriptor_buffer : m_frame_desc_buffers)
            if (descriptor_buffer.buffer.buffer)
                m_dev->destroy_buffer(descriptor_buffer.buffer);

        for (auto& instance_buffer : m_instance_buffers)
            if (instance_buffer.buffer)
                m_dev->destroy_buffer(instance_buffer);

        for (auto& override_buffer : m_instance_override_buffers)
            if (override_buffer.buffer)
                m_dev->destroy_buffer(override_buffer);

        for (auto& camera_buffer : m_camera_ubos)
            if (camera_buffer.buffer)
                m_dev->destroy_buffer(camera_buffer);

        if (m_vertex_buffer.buffer)
            m_dev->destroy_buffer(m_vertex_buffer);

        if (m_index_buffer.buffer)
            m_dev->destroy_buffer(m_index_buffer);

        if (m_mesh_pipeline_layout)
            m_device.destroyPipelineLayout(m_mesh_pipeline_layout);

        if (m_mesh_set_layout)
            m_device.destroyDescriptorSetLayout(m_mesh_set_layout);

        m_mesh_pipeline_layout = nullptr;
        m_mesh_set_layout = nullptr;
    }


    void renderer::update_camera_ubo() {

        camera_ubo_data camera_data{};

        const glm::mat4 projection = camera_projection();
        const glm::mat4 view_projection = projection * m_active_camera.view;

        camera_data.view = m_active_camera.view;
        camera_data.view_proj = view_projection;
        camera_data.camera_pos = glm::vec4(m_active_camera.position, 1.0f);
        camera_data.projection = projection;
        camera_data.inv_projection = glm::inverse(projection);
        camera_data.cluster_dims = glm::uvec4(m_cluster_params.grid_x, m_cluster_params.grid_y, m_cluster_params.grid_z, 0u);
        camera_data.cluster_depth = glm::vec4(
            m_active_camera.near_plane,
            m_active_camera.far_plane,
            static_cast<f32>(m_render_size.x),
            static_cast<f32>(m_render_size.y));

        extract_frustum_planes(view_projection, camera_data.frustum_planes);
        camera_data.light_view_proj = m_shadow.view_proj;
        camera_data.shadow_params = m_shadow.params;

        for (auto& plane : camera_data.frustum_planes) {
            const f32 normal_length = glm::length(glm::vec3(plane));
            if (normal_length > 0.0f)
                plane /= normal_length;
        }

        std::memcpy(m_dev->map_buffer(m_camera_ubos[m_current_frame]), &camera_data, sizeof(camera_data));
        m_dev->unmap_buffer(m_camera_ubos[m_current_frame]);
    }


    void renderer::update_mesh_descriptors() {

        const u32 frame_index = m_current_frame;

        if (!m_light_grid_handles[frame_index].is_valid() || !m_light_index_list_handles[frame_index].is_valid())
            return;

        util::allocated_buffer* grid_allocation = m_graph->buffer_allocation(m_light_grid_handles[frame_index]);
        util::allocated_buffer* list_allocation = m_graph->buffer_allocation(m_light_index_list_handles[frame_index]);
        ASSERT(grid_allocation && list_allocation, "", "Graph failed to allocate mesh light-culling buffers");

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

        std::vector<util::descriptor_item> descriptor_items;
        descriptor_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &m_camera_ubos[frame_index]);
        descriptor_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, &m_instance_buffers[frame_index]);
        descriptor_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_light_buffers[frame_index]);
        descriptor_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eFragment, 1, grid_allocation);
        descriptor_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eFragment, 1, list_allocation);
        descriptor_items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, &shadow_image);
        descriptor_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_shadow_view_buffers[frame_index]);
        descriptor_items.emplace_back(7, VKDT::eSampledImage, VKSF::eFragment, 1, &atlas_image);
        descriptor_items.emplace_back(8, VKDT::eSampler, VKSF::eFragment, 1, &shadow_sampler_image);

        constexpr auto buffer_type = util::descriptor_buffer_type::combined;

        if (m_frame_desc_buffers[frame_index].buffer.buffer == nullptr)
            m_frame_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_mesh_set_layout, descriptor_items, buffer_type, 1);

        m_dev->compute_descriptor_binding_offsets(m_mesh_set_layout, descriptor_items);
        m_dev->update_descriptor_buffer(m_frame_desc_buffers[frame_index], descriptor_items, buffer_type);

        // Directional shadow-pass set: binding 5+ are not written (shadow_map is the attachment; the depth shaders never read them)
        descriptor_items.erase(descriptor_items.begin() + 5, descriptor_items.end());
        descriptor_items[0].p_resources = &m_shadow_camera_ubos[frame_index];

        if (m_shadow_desc_buffers[frame_index].buffer.buffer == nullptr)
            m_shadow_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_mesh_set_layout, descriptor_items, buffer_type, 1);

        m_dev->update_descriptor_buffer(m_shadow_desc_buffers[frame_index], descriptor_items, buffer_type);
    }

}
