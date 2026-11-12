
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

    // cull pipeline ---------------------------------------------------------------------------------------------------

    void scene_renderer::create_cull_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::create_cull_pipeline");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // 0 instances | 1 mesh table | 2 submesh table | 3 commands (out) | 4 counts (out) | 5 camera | 6 material table
        // 7 material ranges | 8 draw stats (out, main view only)
        std::vector<util::descriptor_item> layout_items;
        for (u32 binding = 0; binding <= 8; ++binding)
            layout_items.emplace_back(binding, binding == 5 ? VKDT::eUniformBuffer : VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);

        m_cull_set_layout = m_plugin.get_dev()->create_descriptor_set_layout(layout_items);

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_cull_set_layout;
        m_cull_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pipeline_layout_info);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "cull.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile cull compute shader")

        vk::ShaderModule compute_shader = m_plugin.get_dev()->create_shader_module(compute_spv);

        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = vk::ShaderStageFlagBits::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;     // bound via descriptor buffers (spec: mandatory)
        pipeline_info.layout = m_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = m_plugin.get_vk_device().createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create cull pipeline")
        m_cull_pipeline = result.value;

        m_plugin.get_vk_device().destroyShaderModule(compute_shader);

        // Per-frame buffers owned by the graph (barriers between cull writes and indirect reads). One set per in-flight frame.
        // Command buffers start at INITIAL_DRAW_CAPACITY and are grown by ensure_draw_buffers(); descriptors are rewritten
        // every frame by update_cull_descriptors(), so here we only create the descriptor buffers themselves
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            using BU = vk::BufferUsageFlagBits;

            graph::buffer_desc cmd_desc{};
            cmd_desc.name = "draw_commands_" + std::to_string(frame_index);
            cmd_desc.size = static_cast<vk::DeviceSize>(INITIAL_DRAW_CAPACITY) * sizeof(vk::DrawIndexedIndirectCommand);
            cmd_desc.usage = BU::eStorageBuffer | BU::eIndirectBuffer;
            cmd_desc.persistent = true;
            m_draw_commands_handles[frame_index] = m_graph->create_buffer(cmd_desc);
            m_draw_capacity[frame_index] = INITIAL_DRAW_CAPACITY;

            graph::buffer_desc count_desc{};
            count_desc.name = "draw_count_" + std::to_string(frame_index);
            count_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * sizeof(u32);
            count_desc.usage = BU::eStorageBuffer | BU::eIndirectBuffer | BU::eTransferDst;
            count_desc.persistent = true;
            m_draw_count_handles[frame_index] = m_graph->create_buffer(count_desc);

            // Host-readable so the CPU can read the visible draw / triangle counts after the frame's fence
            graph::buffer_desc stats_desc{};
            stats_desc.name = "draw_stats_" + std::to_string(frame_index);
            stats_desc.size = sizeof(gpu_draw_stats);
            stats_desc.usage = BU::eStorageBuffer | BU::eTransferDst;
            stats_desc.vma_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            stats_desc.persistent = true;
            m_draw_stats_handles[frame_index] = m_graph->create_buffer(stats_desc);
            m_draw_stats_written[frame_index] = false;

            std::vector<util::descriptor_item> descriptor_items;
            for (u32 binding = 0; binding <= 8; ++binding)
                descriptor_items.emplace_back(binding, binding == 5 ? VKDT::eUniformBuffer : VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
            m_cull_desc_buffers[frame_index] = m_plugin.get_dev()->create_descriptor_buffer(m_cull_set_layout, descriptor_items,
                util::descriptor_buffer_type::resource, 1);
        }
    }


    void scene_renderer::destroy_cull_pipeline() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_cull_pipeline");

        m_plugin.get_vk_device().waitIdle();

        for (auto& descriptor_buffer : m_cull_desc_buffers)
            if (descriptor_buffer.buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(descriptor_buffer.buffer);

        // The draw command / count / stats buffers are graph-owned (persistent) and will be destroyed by ~render_graph

        if (m_cull_pipeline)
            m_plugin.get_vk_device().destroyPipeline(m_cull_pipeline);

        if (m_cull_pipeline_layout)
            m_plugin.get_vk_device().destroyPipelineLayout(m_cull_pipeline_layout);

        if (m_cull_set_layout)
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_cull_set_layout);

        m_cull_pipeline = nullptr;
        m_cull_pipeline_layout = nullptr;
        m_cull_set_layout = nullptr;
    }

    // frame-time operations -------------------------------------------------------------------------------------------

    // Pack every scene instance into the SSBO. Asset-backed instances come first ([0, m_asset_instance_count)); procedural
    // instances append after that. Cull only dispatches over the asset-backed prefix, so procedural draws are handled by a
    // manual loop in add_depth_prepass_pass / add_scene_pass
    //
    // Cost is O(instances x submeshes) with O(1) work per entry: materials are resolved once per unique override handle
    // (one ref per scene, not per submesh), the mesh's own default materials are used directly (the mesh holds those refs)
    void scene_renderer::build_instance_buffer() {
        GLT_PROFILE_SCOPE("scene_renderer::build_instance_buffer");

        // Reset the per-material counters of the previous frame (only the touched entries)
        for (u32 slot : m_scene_materials) {
            m_material_draw_counts[slot] = 0;
            m_vg_material_draw_counts[slot] = 0;
            m_vg_material_ranges[slot] = glm::uvec2(0u);
        }
        begin_virtual_geometry_frame();
        const bool virtual_geometry = virtual_geometry_enabled();

        m_packed_instances.clear();
        m_material_table.clear();
        m_scene_materials.clear();
        m_translucent_candidates.clear();
        m_procedural_draw_slots.clear();
        m_asset_instance_count = 0;

        // Dedup of scene materials without a set: a material is "seen" when its counter entry is non-zero, or it was
        // registered by a procedural draw (which does not count). Use a small marker vector for the latter
        std::vector<bool> seen(MAX_MATERIALS, false);
        auto note_material = [&](u32 slot) {
            if (slot >= MAX_MATERIALS)
                return;
            if (!seen[slot]) {
                seen[slot] = true;
                m_scene_materials.push_back(slot);
            }
        };

        // Override materials: acquire once per unique handle this frame, release last frame's set afterwards. Acquiring
        // first keeps materials that stay in use from being unloaded and reloaded
        std::unordered_map<GLT::asset::handle, u32> resolved;
        std::vector<u32> held;
        held.reserve(m_held_materials.size());
        auto resolve_override = [&](GLT::asset::handle handle) -> u32 {
            auto [it, inserted] = resolved.try_emplace(handle, DEFAULT_MATERIAL_SLOT);
            if (inserted) {
                it->second = m_shared->acquire_material(handle);
                held.push_back(it->second);
            }
            return it->second;
        };

        m_packed_instances.reserve(m_scene_instances.size());

        // ---- Pass 1: asset-backed instances -------------------------------------------------------
        for (const auto& scene_instance : m_scene_instances) {

            if (scene_instance.procedural_mesh != 0)
                continue;
            if (scene_instance.mesh == INVALID_HANDLE)
                continue;

            auto rit = m_resident_meshes.find(scene_instance.mesh);
            if (rit == m_resident_meshes.end())
                continue;
            const mesh_slot* slot = m_shared->get_mesh(rit->second);
            if (!slot || !slot->alive)
                continue;

            VALIDATE(m_packed_instances.size() < MAX_INSTANCES, break,
                "", "Instance count exceeds MAX_INSTANCES ({}), truncating scene", (MAX_INSTANCES))

            const glm::mat3 ul(scene_instance.transform);

            gpu_instance_data inst{};
            inst.model = scene_instance.transform;
            inst.normal_matrix = glm::mat4(glm::transpose(glm::inverse(ul)));
            inst.mesh_index = rit->second;
            inst.material_index = static_cast<u32>(m_material_table.size());     // first entry of this instance's material list
            const u32 force_bits = scene_instance.flags & GLT::asset::mesh::INSTANCE_FLAG_FORCE_LOD_MASK;   // forced LOD (mesh editor preview)

            // Virtual geometry: meshes with clusters are drawn per cluster in the main view, unless a level is forced or
            // the frame's cluster budget is used up (then the instance simply stays on the LOD path)
            u32 virtual_flags = 0u;
            if (virtual_geometry && force_bits == 0u && slot->cluster_count > 0)
                virtual_flags = add_virtual_geometry_instance(static_cast<u32>(m_packed_instances.size()), *slot, scene_instance.transform);
            const bool virtual_instance = (virtual_flags & INSTANCE_FLAG_VIRTUAL) != 0u;

            inst.flags = force_bits | virtual_flags;
            m_packed_instances.push_back(inst);

            // Same rule as cull.comp: forced level clamped to the uploaded levels, otherwise LOD 0
            const u32 forced_lod = force_bits != 0u ? std::min(force_bits - 1u, std::max(slot->lod_count, 1u) - 1u) : 0u;

            // Exactly `submesh_count` entries: cull.comp loops the GPU mesh table's submesh_count, which is the same value
            const u32 submesh_count = std::min(slot->submesh_count, MAX_SUBMESHES_PER_MESH);
            for (u32 sm = 0; sm < submesh_count; ++sm) {

                u32 material = DEFAULT_MATERIAL_SLOT;
                if (sm < scene_instance.material_overrides.size() && scene_instance.material_overrides[sm] != INVALID_HANDLE)
                    material = resolve_override(scene_instance.material_overrides[sm]);
                else if (sm < slot->material_slots.size())
                    material = slot->material_slots[sm];

                m_material_table.push_back(material);
                if (material < MAX_MATERIALS) {
                    note_material(material);            // translucents need their material descriptor written too

                    // Translucent submeshes are not counted: their range stays empty, so cull.comp skips them (and they
                    // stay out of the depth pre-pass and shadows). They are drawn sorted in the translucent pass instead
                    const material_slot& material_entry = m_shared->get_material(material);
                    if (material_entry.is_translucent())
                        m_translucent_candidates.push_back({ 0.f, static_cast<u32>(m_packed_instances.size() - 1), material,
                            rit->second, sm, false, forced_lod, force_bits != 0u });
                    else {
                        m_material_draw_counts[material]++;     // shadow views (and the main view without virtual geometry)

                        // Main view: this submesh is drawn as clusters. Reserve the most it can ever emit at once
                        if (virtual_instance && sm < slot->cluster_max_visible.size()) {
                            m_vg_material_draw_counts[material] += slot->cluster_max_visible[sm];
                            if (material_entry.is_double_sided())
                                m_packed_instances.back().flags &= ~INSTANCE_FLAG_CONE_CULL;    // back faces are visible
                        }
                    }
                }
            }
        }

        m_asset_instance_count = static_cast<u32>(m_packed_instances.size());

        // ---- Pass 2: procedural instances ---------------------------------------------------------
        for (const auto& scene_instance : m_scene_instances) {

            if (scene_instance.procedural_mesh == 0)
                continue;
            const u32 id = scene_instance.procedural_mesh;
            if (id > m_procedural_meshes.size())
                continue;

            const auto& pm = m_procedural_meshes[id - 1];
            if (!pm.alive || pm.vertex_count == 0 || pm.index_count == 0)
                continue;

            VALIDATE(m_packed_instances.size() < MAX_INSTANCES, break, "", "Instance count exceeds MAX_INSTANCES (procedural), truncating", ())

            const glm::mat3 ul(scene_instance.transform);
            gpu_instance_data inst{};
            inst.model = scene_instance.transform;
            inst.normal_matrix = glm::mat4(glm::transpose(glm::inverse(ul)));
            inst.mesh_index = 0;
            inst.material_index = 0;                    // unused: procedural draws are issued directly, not via cull
            m_packed_instances.push_back(inst);
            m_procedural_draw_slots.push_back(id - 1);

            note_material(pm.material_slot);
            if (pm.material_slot < MAX_MATERIALS && m_shared->get_material(pm.material_slot).is_translucent())
                m_translucent_candidates.push_back({ 0.f, static_cast<u32>(m_packed_instances.size() - 1), pm.material_slot,
                    id - 1, 0u, true, 0u, false });
        }

        // ---- Material refs: drop last frame's set now that this frame's set is acquired ------------
        for (u32 slot : m_held_materials)
            m_shared->release_material(slot);
        m_held_materials = std::move(held);

        // ---- Exact command ranges: prefix sum of per-material draw counts --------------------------
        u32 base = 0;
        for (u32 slot : m_scene_materials) {
            const u32 count = m_material_draw_counts[slot];
            m_material_ranges[slot] = glm::uvec2(base, count);
            base += count;
        }
        m_total_draw_capacity = base;
        m_instance_count = static_cast<u32>(m_packed_instances.size());

        finish_virtual_geometry_frame();        // cluster command ranges + work items of the virtual geometry instances
        upload_instance_data();
    }


    void scene_renderer::upload_instance_data() {
        GLT_PROFILE_SCOPE("scene_renderer::upload_instance_data");

        auto* dev = m_plugin.get_dev();
        const u32 frame = m_current_frame;

        if (m_instance_count > 0) {
            const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(m_instance_count) * sizeof(gpu_instance_data);
            if (ensure_host_buffer(m_instance_buffers[frame], bytes, vk::BufferUsageFlagBits::eStorageBuffer))
                m_instance_buffer_generation[frame]++;
            dev->update_buffer(m_instance_buffers[frame], m_packed_instances.data(), bytes);
        }

        if (!m_material_table.empty()) {
            const vk::DeviceSize bytes = m_material_table.size() * sizeof(u32);
            ensure_host_buffer(m_material_table_buffers[frame], bytes, vk::BufferUsageFlagBits::eStorageBuffer);
            dev->update_buffer(m_material_table_buffers[frame], m_material_table.data(), bytes);
        }

        // Only entries of scene materials are ever read; the whole table is 8 KB, upload it in one go
        dev->update_buffer(m_material_range_buffers[frame], m_material_ranges.data(), MAX_MATERIALS * sizeof(glm::uvec2));
    }


    // Grow (never shrink) this frame's indirect buffers so every material range fits. Called from render() before the
    // descriptors are written. The graph retires the old allocation, so this never stalls
    void scene_renderer::ensure_draw_buffers() {
        GLT_PROFILE_SCOPE("scene_renderer::ensure_draw_buffers");

        const u32 frame = m_current_frame;
        const u32 needed = std::bit_ceil(std::max(m_total_draw_capacity, INITIAL_DRAW_CAPACITY));
        constexpr vk::DeviceSize stride = sizeof(vk::DrawIndexedIndirectCommand);

        if (needed > m_draw_capacity[frame]) {

            m_draw_capacity[frame] = needed;

            graph::buffer_desc cmd_desc{};
            cmd_desc.name = "draw_commands_" + std::to_string(frame);
            cmd_desc.size = needed * stride;
            cmd_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            cmd_desc.persistent = true;
            m_draw_commands_handles[frame] = m_graph->create_buffer(cmd_desc);

            cmd_desc.name = "shadow_draw_commands_" + std::to_string(frame);
            m_shadow_draw_commands_handles[frame] = m_graph->create_buffer(cmd_desc);

            m_local_draw_views[frame] = 0;              // stride changed -> local buffer must be re-laid out
        }

        // Local shadows: one full set of material ranges per view, `m_draw_capacity` apart
        const u32 views = std::max(m_local_shadow_view_count, 1u);
        if (m_local_shadow_view_count > 0 && views > m_local_draw_views[frame]) {

            const u32 view_capacity = std::bit_ceil(views);
            m_local_draw_views[frame] = std::min(view_capacity, MAX_SHADOW_VIEWS);

            graph::buffer_desc commands_desc{};
            commands_desc.name = "local_shadow_commands_" + std::to_string(frame);
            commands_desc.size = static_cast<vk::DeviceSize>(m_local_draw_views[frame]) * m_draw_capacity[frame] * stride;
            commands_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            commands_desc.persistent = true;
            m_local_shadow_commands_handles[frame] = m_graph->create_buffer(commands_desc);
        }
    }


    // Descriptors of every cull dispatch for the current frame. Rewritten each frame because instance / table / command
    // buffers can be reallocated when they grow. The frame slot's previous GPU work is complete (fence), so this is safe
    void scene_renderer::update_cull_descriptors() {
        GLT_PROFILE_SCOPE("scene_renderer::update_cull_descriptors");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        auto* dev = m_plugin.get_dev();
        const u32 frame = m_current_frame;

        util::allocated_buffer* commands = m_graph->buffer_allocation(m_draw_commands_handles[frame]);
        util::allocated_buffer* counts = m_graph->buffer_allocation(m_draw_count_handles[frame]);
        util::allocated_buffer* stats = m_graph->buffer_allocation(m_draw_stats_handles[frame]);
        util::allocated_buffer* shadow_commands = m_graph->buffer_allocation(m_shadow_draw_commands_handles[frame]);
        util::allocated_buffer* shadow_counts = m_graph->buffer_allocation(m_shadow_draw_count_handles[frame]);
        ASSERT(commands && counts && stats && shadow_commands && shadow_counts, "", "Graph failed to allocate cull buffers")

        auto cull_items = [&](util::allocated_buffer* out_commands, util::allocated_buffer* out_counts, util::allocated_buffer* camera) {
            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame]);
            items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_mesh_data_buffer_alloc_ref());
            items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_submesh_data_buffer_alloc_ref());
            items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, out_commands);
            items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, out_counts);
            items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, camera);
            items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_material_table_buffers[frame]);
            items.emplace_back(7, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_material_range_buffers[frame]);
            items.emplace_back(8, VKDT::eStorageBuffer, VKSF::eCompute, 1, stats);  // shadow cull: bound, never written (flags.y = 0)
            dev->compute_descriptor_binding_offsets(m_cull_set_layout, items);
            return items;
        };

        // Main view
        dev->update_descriptor_buffer(m_cull_desc_buffers[frame], cull_items(commands, counts, &m_camera_ubos[frame]),
            util::descriptor_buffer_type::resource);

        // Directional shadow: same pipeline + layout, different outputs and "camera"
        dev->update_descriptor_buffer(m_shadow_cull_desc_buffers[frame],
            cull_items(shadow_commands, shadow_counts, &m_shadow_camera_ubos[frame]), util::descriptor_buffer_type::resource);

        // Local shadows
        util::allocated_buffer* local_commands = m_graph->buffer_allocation(m_local_shadow_commands_handles[frame]);
        util::allocated_buffer* local_counts = m_graph->buffer_allocation(m_local_shadow_counts_handles[frame]);
        if (local_commands && local_counts) {

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_instance_buffers[frame]);
            items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_mesh_data_buffer_alloc_ref());
            items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shared->get_submesh_data_buffer_alloc_ref());
            items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, local_commands);
            items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, local_counts);
            items.emplace_back(5, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_shadow_view_buffers[frame]);
            items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_material_table_buffers[frame]);
            items.emplace_back(7, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_material_range_buffers[frame]);
            dev->compute_descriptor_binding_offsets(m_local_shadow_cull_set_layout, items);
            dev->update_descriptor_buffer(m_local_shadow_cull_desc_buffers[frame], items, util::descriptor_buffer_type::resource);
        }

        // Local shadow draw sets (one per view) reference the instance buffer: only rewrite when it was reallocated
        if (m_local_shadow_desc_generation[frame] != m_instance_buffer_generation[frame]) {
            m_local_shadow_desc_generation[frame] = m_instance_buffer_generation[frame];
            write_local_shadow_draw_descriptors(frame);
        }
    }


    // Called in update() after the frame slot's fence: the stats of the frame that last used this slot are final
    void scene_renderer::read_back_draw_stats(u32 frame_slot, gpu_frame_report& report) {
        GLT_PROFILE_SCOPE("scene_renderer::read_back_draw_stats");

        if (!m_draw_stats_written[frame_slot] || !m_draw_stats_handles[frame_slot].is_valid())
            return;
        m_draw_stats_written[frame_slot] = false;

        util::allocated_buffer* stats = m_graph->buffer_allocation(m_draw_stats_handles[frame_slot]);
        if (!stats || !stats->allocation)
            return;

        auto* dev = m_plugin.get_dev();
        vmaInvalidateAllocation(dev->get_allocator(), stats->allocation, 0, VK_WHOLE_SIZE);    // no-op on coherent memory
        gpu_draw_stats values{};
        std::memcpy(&values, dev->map_buffer(*stats), sizeof(values));
        dev->unmap_buffer(*stats);

        report.draw_calls = values.visible_draws;
        report.triangles = values.triangles;
        report.virtual_clusters = values.virtual_clusters;      // subset of the two above (virtual_geometry_cull.comp)
        report.virtual_triangles = values.virtual_triangles;
    }


    void scene_renderer::add_reset_draw_count_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_reset_draw_count_pass");

        m_graph->add_pass("reset_draw_count",
            [this](graph::pass_builder& builder) {
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::transfer_dst);
                builder.write(m_draw_stats_handles[m_current_frame], graph::buffer_usage::transfer_dst);
                if (m_vg_active)
                    builder.write(m_vg_draw_count_handles[m_current_frame], graph::buffer_usage::transfer_dst);
            },
            [this](graph::pass_context& ctx) {
                ctx.cmd.fillBuffer(ctx.get_buffer(m_draw_count_handles[m_current_frame]), 0, MAX_MATERIALS * sizeof(u32), 0);
                ctx.cmd.fillBuffer(ctx.get_buffer(m_draw_stats_handles[m_current_frame]), 0, sizeof(gpu_draw_stats), 0);
                if (m_vg_active)                // cluster draw counters of the virtual geometry pass
                    ctx.cmd.fillBuffer(ctx.get_buffer(m_vg_draw_count_handles[m_current_frame]), 0, MAX_MATERIALS * sizeof(u32), 0);
            });
    }


    void scene_renderer::add_cull_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_cull_pass");

        m_graph->add_pass("cull",
            [this](graph::pass_builder& builder) {
                builder.write(m_draw_commands_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_draw_stats_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
            },
            [this](graph::pass_context& ctx) {

                if (m_asset_instance_count == 0)
                    return;

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cull_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ m_cull_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);

                // Only the asset-backed prefix
                const u32 dispatch_count = (m_asset_instance_count + 63) / 64;
                ctx.cmd.dispatch(dispatch_count, 1, 1);

                // Make the statistics visible to the host once the frame's fence signals (the graph has no host usage)
                vk::BufferMemoryBarrier2 host_barrier{};
                host_barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
                host_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
                host_barrier.dstStageMask = vk::PipelineStageFlagBits2::eHost;
                host_barrier.dstAccessMask = vk::AccessFlagBits2::eHostRead;
                host_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                host_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                host_barrier.buffer = ctx.get_buffer(m_draw_stats_handles[m_current_frame]);
                host_barrier.offset = 0;
                host_barrier.size = VK_WHOLE_SIZE;
                vk::DependencyInfo dependency{};
                dependency.bufferMemoryBarrierCount = 1;
                dependency.pBufferMemoryBarriers = &host_barrier;
                ctx.cmd.pipelineBarrier2(dependency);

                m_draw_stats_written[m_current_frame] = true;
            });
    }

}
