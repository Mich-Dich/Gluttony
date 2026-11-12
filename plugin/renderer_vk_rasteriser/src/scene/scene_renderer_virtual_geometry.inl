
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // Virtual geometry
    //
    // Meshes that opted in (asset/mesh_cluster.h, mesh editor "Virtual Geometry") carry a hierarchy of clusters next to
    // their discrete LODs. In the main view such an instance is not drawn per submesh by cull.comp: this file lists it as
    // work items of VIRTUAL_CLUSTERS_PER_ITEM clusters, virtual_geometry_cull.comp picks the clusters that are detailed
    // enough for the camera (and culls them), and every pass that draws the culled scene also draws the resulting
    // cluster commands with the same material pipelines.
    //
    // Deliberately self-contained: own work list, own per-material command ranges, own indirect buffers, own pass. When
    // no instance is on the path (m_vg_active == false) nothing here records anything, and shadow views never use it
    // (they draw the discrete levels of every mesh, as before)

    // CONSTANTS =======================================================================================================

    // Upper bound of cluster draw commands a frame may reserve (20 bytes each, per frame in flight). Instances that do
    // not fit any more stay on the LOD path for that frame, so the worst case is a missing optimisation, never a
    // missing object
    constexpr u32                                       MAX_VIRTUAL_DRAW_COMMANDS = 1u << 20;

    constexpr u32                                       VIRTUAL_CULL_BINDINGS = 12;     // 0..11, see create_virtual_geometry_resources()

    constexpr u32                                       VIRTUAL_CULL_CAMERA_BINDING = 6;

    // virtual_geometry_cull.comp.glsl: PushConstants::flags
    constexpr u32                                       VIRTUAL_CULL_FLAG_CONE_CULLING = 1u;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // virtual_geometry_cull_push (the shader's push constants): scene/type.h

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Backface cones are stored in mesh space. They stay valid under rotation, translation and uniform scale; non-uniform
    // scale or shear bends them, and a mirrored transform flips the winding
    static bool transform_keeps_cones(const glm::mat4& model) {

        const glm::vec3 x(model[0]), y(model[1]), z(model[2]);
        const f32 length_x = glm::length(x), length_y = glm::length(y), length_z = glm::length(z);
        const f32 longest = std::max({ length_x, length_y, length_z });
        const f32 shortest = std::min({ length_x, length_y, length_z });
        if (!(shortest > 0.0f) || longest > shortest * 1.001f)
            return false;

        const f32 tolerance = 1e-3f * longest * longest;
        if (std::abs(glm::dot(x, y)) > tolerance || std::abs(glm::dot(x, z)) > tolerance || std::abs(glm::dot(y, z)) > tolerance)
            return false;

        return glm::dot(glm::cross(x, y), z) > 0.0f;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    bool scene_renderer::virtual_geometry_enabled() const {

        return m_vg_cull_pipeline && m_plugin.visual_settings().virtual_geometry;
    }

    // pipeline + resources --------------------------------------------------------------------------------------------

    // Everything here is optional: if the shader or the pipeline cannot be created the path stays off
    // (m_vg_cull_pipeline == nullptr) and every mesh renders with its LODs
    void scene_renderer::create_virtual_geometry_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_virtual_geometry_resources");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        auto* dev = m_plugin.get_dev();
        auto device = m_plugin.get_vk_device();

        m_vg_material_ranges.assign(MAX_MATERIALS, glm::uvec2(0u));
        m_vg_material_draw_counts.assign(MAX_MATERIALS, 0u);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "virtual_geometry_cull.comp.glsl");
        VALIDATE(!compute_spv.empty(), return, "", "Failed to compile virtual_geometry_cull.comp.glsl - virtual geometry is off, meshes use their LODs")

        // 0 instances | 1 mesh table | 2 mesh cluster table | 3 clusters | 4 cluster commands (out) | 5 counts (out)
        // 6 camera | 7 material table | 8 cluster material ranges | 9 draw stats (out) | 10 work items
        // 11 cluster index per command (out, read by the debug views)
        std::vector<util::descriptor_item> layout_items;
        for (u32 binding = 0; binding < VIRTUAL_CULL_BINDINGS; ++binding)
            layout_items.emplace_back(binding, binding == VIRTUAL_CULL_CAMERA_BINDING ? VKDT::eUniformBuffer : VKDT::eStorageBuffer,
                VKSF::eCompute, 1, nullptr);
        m_vg_cull_set_layout = dev->create_descriptor_set_layout(layout_items);

        vk::PushConstantRange push_range{};
        push_range.stageFlags = VKSF::eCompute;
        push_range.offset = 0;
        push_range.size = sizeof(virtual_geometry_cull_push);

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_vg_cull_set_layout;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_range;
        m_vg_cull_pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

        vk::ShaderModule compute_shader = dev->create_shader_module(compute_spv);

        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = VKSF::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;     // bound via descriptor buffers (spec: mandatory)
        pipeline_info.layout = m_vg_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = device.createComputePipeline(nullptr, pipeline_info);
        device.destroyShaderModule(compute_shader);
        if (result.result != vk::Result::eSuccess) {

            LOG(error, "Failed to create the virtual geometry cull pipeline - virtual geometry is off, meshes use their LODs")
            device.destroyPipelineLayout(m_vg_cull_pipeline_layout);
            device.destroyDescriptorSetLayout(m_vg_cull_set_layout);
            m_vg_cull_pipeline_layout = nullptr;
            m_vg_cull_set_layout = nullptr;
            return;
        }
        m_vg_cull_pipeline = result.value;

        // Per-frame buffers. Commands / counts are graph-owned (barriers between the cull writes and the indirect reads)
        // and grown by ensure_virtual_geometry_buffers(); the work items grow in finish_virtual_geometry_frame()
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            using BU = vk::BufferUsageFlagBits;

            graph::buffer_desc cmd_desc{};
            cmd_desc.name = "virtual_draw_commands_" + std::to_string(frame_index);
            cmd_desc.size = static_cast<vk::DeviceSize>(INITIAL_VIRTUAL_DRAW_CAPACITY) * sizeof(vk::DrawIndexedIndirectCommand);
            cmd_desc.usage = BU::eStorageBuffer | BU::eIndirectBuffer;
            cmd_desc.persistent = true;
            m_vg_draw_commands_handles[frame_index] = m_graph->create_buffer(cmd_desc);
            m_vg_draw_capacity[frame_index] = INITIAL_VIRTUAL_DRAW_CAPACITY;

            graph::buffer_desc info_desc{};
            info_desc.name = "virtual_draw_clusters_" + std::to_string(frame_index);
            info_desc.size = static_cast<vk::DeviceSize>(INITIAL_VIRTUAL_DRAW_CAPACITY) * sizeof(u32);
            info_desc.usage = BU::eStorageBuffer;
            info_desc.persistent = true;
            m_vg_draw_cluster_handles[frame_index] = m_graph->create_buffer(info_desc);

            graph::buffer_desc count_desc{};
            count_desc.name = "virtual_draw_count_" + std::to_string(frame_index);
            count_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * sizeof(u32);
            count_desc.usage = BU::eStorageBuffer | BU::eIndirectBuffer | BU::eTransferDst;
            count_desc.persistent = true;
            m_vg_draw_count_handles[frame_index] = m_graph->create_buffer(count_desc);

            m_vg_material_range_buffers[frame_index] = dev->create_buffer(MAX_MATERIALS * sizeof(glm::uvec2), BU::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            std::memset(dev->map_buffer(m_vg_material_range_buffers[frame_index]), 0, MAX_MATERIALS * sizeof(glm::uvec2));
            dev->unmap_buffer(m_vg_material_range_buffers[frame_index]);

            std::vector<util::descriptor_item> descriptor_items;
            for (u32 binding = 0; binding < VIRTUAL_CULL_BINDINGS; ++binding)
                descriptor_items.emplace_back(binding, binding == VIRTUAL_CULL_CAMERA_BINDING ? VKDT::eUniformBuffer : VKDT::eStorageBuffer,
                    VKSF::eCompute, 1, nullptr);
            m_vg_cull_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_vg_cull_set_layout, descriptor_items,
                util::descriptor_buffer_type::resource, 1);
        }
    }


    void scene_renderer::destroy_virtual_geometry_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_virtual_geometry_resources");

        auto* dev = m_plugin.get_dev();
        auto device = m_plugin.get_vk_device();
        device.waitIdle();

        for (auto& descriptor_buffer : m_vg_cull_desc_buffers)
            if (descriptor_buffer.buffer.buffer) {
                dev->destroy_buffer(descriptor_buffer.buffer);
                descriptor_buffer = {};
            }
        for (auto& buffer : m_vg_work_item_buffers)
            if (buffer.buffer) {
                dev->destroy_buffer(buffer);
                buffer = {};
            }
        for (auto& buffer : m_vg_material_range_buffers)
            if (buffer.buffer) {
                dev->destroy_buffer(buffer);
                buffer = {};
            }

        // The cluster command / count / cluster index buffers are graph-owned (persistent) and will be destroyed by
        // ~render_graph

        if (m_vg_cull_pipeline)
            device.destroyPipeline(m_vg_cull_pipeline);
        if (m_vg_cull_pipeline_layout)
            device.destroyPipelineLayout(m_vg_cull_pipeline_layout);
        if (m_vg_cull_set_layout)
            device.destroyDescriptorSetLayout(m_vg_cull_set_layout);

        m_vg_cull_pipeline = nullptr;
        m_vg_cull_pipeline_layout = nullptr;
        m_vg_cull_set_layout = nullptr;
        m_vg_active = false;
    }

    // frame-time operations (update()) --------------------------------------------------------------------------------

    // Called by build_instance_buffer() before it walks the scene. The per-material counters of last frame's materials
    // are cleared there, together with the regular ones
    void scene_renderer::begin_virtual_geometry_frame() {

        m_vg_work_items.clear();
        m_vg_instance_count = 0;
        m_vg_cluster_count = 0;
        m_vg_total_draw_capacity = 0;
        m_vg_active = false;
    }


    // Puts one asset instance on the virtual geometry path: lists its clusters as work items and reserves its share of
    // the frame's cluster draw budget. Returns the gpu_instance_data flags to set (0 = the instance stays on the LOD
    // path). The caller adds slot.cluster_max_visible[submesh] to m_vg_material_draw_counts for every opaque submesh
    u32 scene_renderer::add_virtual_geometry_instance(u32 instance_index, const mesh_slot& slot, const glm::mat4& transform) {

        if (slot.cluster_count == 0 || slot.cluster_max_visible.empty())
            return 0u;

        u32 instance_draws = 0;
        for (u32 bound : slot.cluster_max_visible)
            instance_draws += bound;
        if (instance_draws == 0)
            return 0u;

        // m_vg_total_draw_capacity counts what was reserved so far this frame (finish_virtual_geometry_frame() replaces it
        // by the exact sum over the opaque materials, which can only be smaller)
        if (static_cast<u64>(m_vg_total_draw_capacity) + instance_draws > MAX_VIRTUAL_DRAW_COMMANDS)
            return 0u;
        m_vg_total_draw_capacity += instance_draws;

        for (u32 first = 0; first < slot.cluster_count; first += VIRTUAL_CLUSTERS_PER_ITEM)
            m_vg_work_items.emplace_back(instance_index, first);

        m_vg_instance_count++;
        m_vg_cluster_count += slot.cluster_count;
        return INSTANCE_FLAG_VIRTUAL | (transform_keeps_cones(transform) ? INSTANCE_FLAG_CONE_CULL : 0u);
    }


    // Called by build_instance_buffer() after the scene walk: exact cluster command ranges (prefix sum of the per-material
    // bounds, same scheme as m_material_ranges) and this frame's uploads
    void scene_renderer::finish_virtual_geometry_frame() {
        GLT_PROFILE_SCOPE("scene_renderer::finish_virtual_geometry_frame");

        u32 base = 0;
        for (u32 slot : m_scene_materials) {
            const u32 count = m_vg_material_draw_counts[slot];
            m_vg_material_ranges[slot] = glm::uvec2(base, count);
            base += count;
        }
        m_vg_total_draw_capacity = base;
        m_vg_active = m_vg_cull_pipeline && !m_vg_work_items.empty() && base > 0;
        if (!m_vg_active)
            return;

        auto* dev = m_plugin.get_dev();
        const u32 frame = m_current_frame;

        const vk::DeviceSize item_bytes = m_vg_work_items.size() * sizeof(glm::uvec2);
        ensure_host_buffer(m_vg_work_item_buffers[frame], item_bytes, vk::BufferUsageFlagBits::eStorageBuffer);
        dev->update_buffer(m_vg_work_item_buffers[frame], m_vg_work_items.data(), item_bytes);

        // Only entries of scene materials are ever read; the whole table is 8 KB, upload it in one go
        dev->update_buffer(m_vg_material_range_buffers[frame], m_vg_material_ranges.data(), MAX_MATERIALS * sizeof(glm::uvec2));
    }

    // per-frame GPU work (render()) -----------------------------------------------------------------------------------

    // Grow (never shrink) this frame's cluster command buffer (and the cluster index list parallel to it) so every
    // material range fits. The graph retires the old allocations, so this never stalls
    void scene_renderer::ensure_virtual_geometry_buffers() {
        GLT_PROFILE_SCOPE("scene_renderer::ensure_virtual_geometry_buffers");

        if (!m_vg_active)
            return;

        const u32 frame = m_current_frame;
        const u32 needed = std::bit_ceil(std::max(m_vg_total_draw_capacity, INITIAL_VIRTUAL_DRAW_CAPACITY));
        if (needed <= m_vg_draw_capacity[frame])
            return;

        m_vg_draw_capacity[frame] = needed;

        graph::buffer_desc cmd_desc{};
        cmd_desc.name = "virtual_draw_commands_" + std::to_string(frame);
        cmd_desc.size = static_cast<vk::DeviceSize>(needed) * sizeof(vk::DrawIndexedIndirectCommand);
        cmd_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
        cmd_desc.persistent = true;
        m_vg_draw_commands_handles[frame] = m_graph->create_buffer(cmd_desc);

        graph::buffer_desc info_desc{};
        info_desc.name = "virtual_draw_clusters_" + std::to_string(frame);
        info_desc.size = static_cast<vk::DeviceSize>(needed) * sizeof(u32);
        info_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        info_desc.persistent = true;
        m_vg_draw_cluster_handles[frame] = m_graph->create_buffer(info_desc);
    }


    // Rewritten each frame: instance / work item / command buffers and the shared cluster pool can be reallocated when
    // they grow. The frame slot's previous GPU work is complete (fence), so this is safe
    void scene_renderer::update_virtual_geometry_descriptors() {
        GLT_PROFILE_SCOPE("scene_renderer::update_virtual_geometry_descriptors");

        if (!m_vg_active)
            return;

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        auto* dev = m_plugin.get_dev();
        const u32 frame = m_current_frame;

        util::allocated_buffer* commands = m_graph->buffer_allocation(m_vg_draw_commands_handles[frame]);
        util::allocated_buffer* counts = m_graph->buffer_allocation(m_vg_draw_count_handles[frame]);
        util::allocated_buffer* stats = m_graph->buffer_allocation(m_draw_stats_handles[frame]);
        util::allocated_buffer* draw_clusters = m_graph->buffer_allocation(m_vg_draw_cluster_handles[frame]);
        if (!commands || !counts || !stats || !draw_clusters || !m_shared->get_cluster_data_buffer_alloc_ref().buffer) {

            LOG(error, "Virtual geometry buffers are missing - skipping the cluster pass this frame")
            m_vg_active = false;                // the instances are flagged virtual: they are not drawn this frame
            return;
        }

        std::vector<util::descriptor_item> items;
        items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame]);
        items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_mesh_data_buffer_alloc_ref());
        items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_mesh_cluster_buffer_alloc_ref());
        items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_cluster_data_buffer_alloc_ref());
        items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, commands);
        items.emplace_back(5, VKDT::eStorageBuffer, VKSF::eCompute, 1, counts);
        items.emplace_back(VIRTUAL_CULL_CAMERA_BINDING, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_camera_ubos[frame]);
        items.emplace_back(7, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_material_table_buffers[frame]);
        items.emplace_back(8, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_vg_material_range_buffers[frame]);
        items.emplace_back(9, VKDT::eStorageBuffer, VKSF::eCompute, 1, stats);
        items.emplace_back(10, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_vg_work_item_buffers[frame]);
        items.emplace_back(11, VKDT::eStorageBuffer, VKSF::eCompute, 1, draw_clusters);
        dev->compute_descriptor_binding_offsets(m_vg_cull_set_layout, items);
        dev->update_descriptor_buffer(m_vg_cull_desc_buffers[frame], items, util::descriptor_buffer_type::resource);
    }

    // passes ----------------------------------------------------------------------------------------------------------

    // Everything the cluster selection depends on besides the scene: used by the cull pass, and by the debug views to
    // show the same numbers
    virtual_geometry_cull_push scene_renderer::virtual_geometry_cull_constants() const {

        const settings::visual& visual = m_plugin.visual_settings();
        const f32 pixel_error = std::isfinite(visual.virtual_geometry_pixel_error)
            ? std::clamp(visual.virtual_geometry_pixel_error, 0.25f, 16.0f) : 1.0f;

        virtual_geometry_cull_push push{};
        push.item_count = static_cast<u32>(m_vg_work_items.size());
        push.dispatch_width = VIRTUAL_DISPATCH_WIDTH;
        push.error_threshold = pixel_error / static_cast<f32>(std::max(m_extent.y, 1u));
        push.projection_scale = 0.5f * std::abs(camera_projection()[1][1]);
        push.near_plane = std::max(m_active_camera.near_plane, 1e-4f);
        if (visual.virtual_geometry_cone_culling)
            push.flags |= VIRTUAL_CULL_FLAG_CONE_CULLING;
        return push;
    }


    // After reset_draw_count (which also clears the cluster counters and the statistics) and before the instance cull:
    // that pass ends with the barrier that makes the statistics of both visible to the host
    void scene_renderer::add_virtual_geometry_cull_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_virtual_geometry_cull_pass");

        if (!m_vg_active)
            return;

        m_graph->add_pass("virtual_geometry_cull",
            [this](graph::pass_builder& builder) {
                builder.write(m_vg_draw_commands_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_vg_draw_count_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_vg_draw_cluster_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_draw_stats_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
            },
            [this](graph::pass_context& ctx) {

                const u32 item_count = static_cast<u32>(m_vg_work_items.size());
                if (item_count == 0)
                    return;

                const virtual_geometry_cull_push push = virtual_geometry_cull_constants();

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_vg_cull_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ m_vg_cull_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_vg_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);
                ctx.cmd.pushConstants(m_vg_cull_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);

                // One workgroup per work item, VIRTUAL_DISPATCH_WIDTH items per row
                const u32 groups_x = std::min(item_count, VIRTUAL_DISPATCH_WIDTH);
                const u32 groups_y = (item_count + VIRTUAL_DISPATCH_WIDTH - 1) / VIRTUAL_DISPATCH_WIDTH;
                ctx.cmd.dispatch(groups_x, groups_y, 1);
            });
    }


    // Setup-lambda helper of every pass that draws the cluster commands (depth pre-pass, scene, triangle density)
    void scene_renderer::read_virtual_geometry_commands(graph::pass_builder& builder) {

        if (!m_vg_active)
            return;
        builder.read(m_vg_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
        builder.read(m_vg_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
    }


    // Cluster draws with the material pipelines: one drawIndexedIndirectCount per scene material that has a cluster
    // command range. Expects what the culled asset draws expect: shared vertex + index buffer, frame + material
    // descriptor buffers and set 0 bound
    void scene_renderer::draw_virtual_geometry(graph::pass_context& ctx, material_pipeline_kind kind) {

        if (!m_vg_active)
            return;

        const vk::Buffer cmd_buf = ctx.get_buffer(m_vg_draw_commands_handles[m_current_frame]);
        const vk::Buffer cnt_buf = ctx.get_buffer(m_vg_draw_count_handles[m_current_frame]);
        constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);

        for (u32 slot : m_scene_materials) {

            const glm::uvec2 range = m_vg_material_ranges[slot];
            if (range.y == 0)
                continue;

            const auto& entry = m_shared->get_material(slot);
            const vk::Pipeline pipeline = (kind == material_pipeline_kind::depth) ? entry.depth_pipeline : entry.pipeline;
            if (!entry.alive || !pipeline)
                continue;

            ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            const u32 offset = m_material_desc_buffers[m_current_frame].get_offset_to_set(slot);
            m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1, offset, ctx.cmd,
                vk::PipelineBindPoint::eGraphics);
            ctx.cmd.drawIndexedIndirectCount(
                cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32),
                range.y, stride);
        }
    }


    // The same draws for passes that bring their own pipeline (debug views): all material ranges, nothing bound here
    void scene_renderer::draw_virtual_geometry_unbound(graph::pass_context& ctx) {

        if (!m_vg_active)
            return;

        const vk::Buffer cmd_buf = ctx.get_buffer(m_vg_draw_commands_handles[m_current_frame]);
        const vk::Buffer cnt_buf = ctx.get_buffer(m_vg_draw_count_handles[m_current_frame]);
        constexpr u32 stride = sizeof(vk::DrawIndexedIndirectCommand);

        for (u32 slot : m_scene_materials) {

            const glm::uvec2 range = m_vg_material_ranges[slot];
            if (range.y == 0)
                continue;
            ctx.cmd.drawIndexedIndirectCount(
                cmd_buf, static_cast<vk::DeviceSize>(range.x) * stride,
                cnt_buf, static_cast<vk::DeviceSize>(slot) * sizeof(u32),
                range.y, stride);
        }
    }

}
