
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // World bounding sphere of an asset instance (the mesh's local box under the instance transform). xyz = centre, w = radius
    static glm::vec4 shadow_caster_sphere(const gpu_instance_data& instance, const mesh_slot* mesh);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static glm::vec4 shadow_caster_sphere(const gpu_instance_data& instance, const mesh_slot* mesh) {

        if (!mesh)
            return glm::vec4(glm::vec3(instance.model[3]), 0.f);

        const glm::vec3 center_local = 0.5f * (mesh->local_min + mesh->local_max);
        const f32 radius_local = 0.5f * glm::length(mesh->local_max - mesh->local_min);
        const f32 scale = std::max(glm::length(glm::vec3(instance.model[0])),
            std::max(glm::length(glm::vec3(instance.model[1])), glm::length(glm::vec3(instance.model[2]))));
        return glm::vec4(glm::vec3(instance.model * glm::vec4(center_local, 1.f)), radius_local * scale);
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::create_shadow_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_shadow_resources");

        // Sun shadow pass: draw commands + one counter per material, like the local lights' pass (shadow_cull.comp.glsl).
        // Its cull descriptors use the shadow cull layout: create_local_shadow_resources()
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            graph::buffer_desc commands_desc{};
            commands_desc.name = "shadow_draw_commands_" + std::to_string(frame_index);
            // ensure_draw_buffers() grows it to (draws in the scene x cascades drawn in a frame) on demand
            commands_desc.size = static_cast<vk::DeviceSize>(INITIAL_DRAW_CAPACITY) * sizeof(vk::DrawIndexedIndirectCommand);
            commands_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            commands_desc.persistent = true;
            m_shadow_draw_commands_handles[frame_index] = m_graph->create_buffer(commands_desc);
            m_sun_command_capacity[frame_index] = INITIAL_DRAW_CAPACITY;

            graph::buffer_desc count_desc{};
            count_desc.name = "shadow_draw_count_" + std::to_string(frame_index);
            count_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * sizeof(u32);
            count_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer
                | vk::BufferUsageFlagBits::eIndirectBuffer
                | vk::BufferUsageFlagBits::eTransferDst;
            count_desc.persistent = true;
            m_shadow_draw_count_handles[frame_index] = m_graph->create_buffer(count_desc);
        }
    }


    void scene_renderer::destroy_shadow_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_shadow_resources");

        m_plugin.get_vk_device().waitIdle();

        for (auto& desc : m_shadow_cull_desc_buffers) {
            if (desc.buffer.buffer) m_plugin.get_dev()->destroy_buffer(desc.buffer);
            desc = {};
        }
    }


    // The shadow values a user can change (settings::visual, engine settings) into m_shadow_settings. Sizes are brought
    // to what the textures can be; a changed size replaces the texture (import_frame_textures), which the shadow caches
    // notice by the new image and draw their maps again
    void scene_renderer::apply_visual_settings() {

        const settings::visual& visual = m_plugin.visual_settings();
        m_shadow_settings.resolution = std::clamp(std::bit_floor(std::max(visual.sun_shadow_resolution, 1u)), 1024u, 4096u);
        m_shadow_settings.atlas_size = std::clamp(std::bit_floor(std::max(visual.local_shadow_atlas, 1u)), 2048u, 8192u);
        m_shadow_settings.local_distance = std::clamp(visual.local_shadow_distance, 1.f, 1000.f);
        m_shadow_settings.local_far_distance = std::clamp(visual.local_shadow_far_distance, 0.f, 4000.f);
    }


    // Sun shadows of this frame: the cascades decide which of their maps are out of date and where they sit now
    // (scene/sun_shadow_cascades.h). Uploads one view per cascade behind the local lights' views; the material shaders
    // find them through camera_ubo_data::shadow_params
    void scene_renderer::update_shadow() {
        GLT_PROFILE_SCOPE("scene_renderer::update_shadow");

        m_shadow.cascades = 0;
        m_sun_shadow_tiles.clear();

        // The cascades planned in the previous update() were never drawn (render() returned early): plan them again
        if (m_sun_shadow_plan_pending) {
            m_sun_shadows.discard_frame();
            m_sun_shadow_plan_pending = false;
        }

        if (!m_shadow.active) {
            m_sun_shadows.clear();
            return;
        }

        const shadow_settings& settings = m_shadow_settings;
        const glm::vec3 direction = m_shadow.direction;

        // Extent of everything that casts shadows: along the light (the depth range of the maps) and as a sphere (how far
        // from the camera shadows are needed at all). Only recomputed when a caster or the sun moved
        if (m_caster_depth_revision != m_caster_revision || m_caster_depth_direction != direction) {
            m_caster_depth_revision = m_caster_revision;
            m_caster_depth_direction = direction;
            f32 depth_near = std::numeric_limits<f32>::infinity();
            f32 depth_far = -std::numeric_limits<f32>::infinity();
            glm::vec3 box_min(std::numeric_limits<f32>::infinity());
            glm::vec3 box_max(-std::numeric_limits<f32>::infinity());
            for (const glm::vec4& sphere : m_caster_spheres) {
                const f32 depth = glm::dot(glm::vec3(sphere), direction);
                if (!std::isfinite(depth) || !std::isfinite(sphere.w))
                    continue;
                depth_near = std::min(depth_near, depth - sphere.w);
                depth_far = std::max(depth_far, depth + sphere.w);
                box_min = glm::min(box_min, glm::vec3(sphere) - sphere.w);
                box_max = glm::max(box_max, glm::vec3(sphere) + sphere.w);
            }
            m_caster_depth_near = depth_near;
            m_caster_depth_far = depth_far;
            m_caster_bounds = depth_near <= depth_far
                ? glm::vec4(0.5f * (box_min + box_max), 0.5f * glm::length(box_max - box_min))
                : glm::vec4(0.f, 0.f, 0.f, -1.f);
        }

        // Shadow distance: what is set, but no further than the scene goes (a small scene gets finer maps instead)
        const f32 distance_limit = std::min(m_plugin.visual_settings().sun_shadow_distance, m_active_camera.far_plane);
        const f32 reach = m_caster_bounds.w >= 0.f
            ? glm::distance(m_active_camera.position, glm::vec3(m_caster_bounds)) + m_caster_bounds.w
            : std::numeric_limits<f32>::infinity();
        m_sun_shadow_distance = sun_shadow_cascades::fit_distance(m_sun_shadow_distance, reach, distance_limit);

        if (m_casters_all_dirty)
            m_sun_shadows.invalidate_all();
        else
            m_sun_shadows.invalidate(m_caster_dirty_spheres);

        sun_shadow_cascades::settings config{};
        config.cascades = settings.cascades;
        config.resolution = settings.resolution;
        config.first_range = settings.first_cascade;
        config.distance = m_sun_shadow_distance;
        config.normal_bias = settings.normal_bias;
        config.bias = settings.bias;
        m_sun_shadows.plan(direction, m_active_camera.position, m_caster_depth_near, m_caster_depth_far, config, SUN_CASCADES_PER_FRAME);

        m_shadow.cascades = m_sun_shadows.cascade_count();
        m_sun_shadow_tiles = m_sun_shadows.render_tiles();
        m_sun_shadow_plan_pending = !m_sun_shadow_tiles.empty();

        const auto& views = m_sun_shadows.views();
        m_plugin.get_dev()->update_buffer(m_shadow_view_buffers[m_current_frame], views.data(), views.size() * sizeof(gpu_shadow_view),
            SUN_SHADOW_VIEW_FIRST * SHADOW_VIEW_STRIDE);
    }

    // local shadows ---------------------------------------------------------------------------------------------------

    void scene_renderer::create_local_shadow_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_local_shadow_resources");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // Cull pipeline ------------------------------------------------------------------------------------------------
        std::vector<util::descriptor_item> layout_items;
        for (u32 binding = 0; binding < 5; binding++)
            layout_items.emplace_back(binding, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(5, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        layout_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);     // material table
        layout_items.emplace_back(7, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);     // material ranges

        m_local_shadow_cull_set_layout = m_plugin.get_dev()->create_descriptor_set_layout(layout_items);

        // Push constants: { valid instance count, first view, views to walk, views drawn } (see shadow_cull.comp.glsl)
        vk::PushConstantRange instance_count_range{};
        instance_count_range.stageFlags = VKSF::eCompute;
        instance_count_range.offset = 0;
        instance_count_range.size = 4 * sizeof(u32);

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_local_shadow_cull_set_layout;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &instance_count_range;
        m_local_shadow_cull_pipeline_layout = m_plugin.get_vk_device().createPipelineLayout(pipeline_layout_info);

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_plugin.get_shader_compiler_ref().compile_glsl_to_spirv(shader_dir / "shadow_cull.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile shadow cull compute shader");

        vk::ShaderModule compute_shader = m_plugin.get_dev()->create_shader_module(compute_spv);
        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = VKSF::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;     // bound via descriptor buffers (spec: mandatory)
        pipeline_info.layout = m_local_shadow_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = m_plugin.get_vk_device().createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create shadow cull pipeline");
        m_local_shadow_cull_pipeline = result.value;
        m_plugin.get_vk_device().destroyShaderModule(compute_shader);

        // Per-frame resources ------------------------------------------------------------------------------------------
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; frame_index++) {

            // Views buffer: read by the cull shader, the material vertex shader (atlas passes) and the fragment shader
            // (sampling), all through set 0 binding 6 / the cull set binding 5. The local lights' views, then the sun's
            // cascades (SUN_SHADOW_VIEW_FIRST)
            m_shadow_view_buffers[frame_index] = m_plugin.get_dev()->create_buffer(SHADOW_VIEW_CAPACITY * SHADOW_VIEW_STRIDE,
                vk::BufferUsageFlagBits::eStorageBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            std::memset(m_plugin.get_dev()->map_buffer(m_shadow_view_buffers[frame_index]), 0, SHADOW_VIEW_CAPACITY * SHADOW_VIEW_STRIDE);
            m_plugin.get_dev()->unmap_buffer(m_shadow_view_buffers[frame_index]);

            graph::buffer_desc commands_desc{};
            commands_desc.name = "local_shadow_commands_" + std::to_string(frame_index);
            // ensure_draw_buffers() grows it to (draws in the scene x views drawn in a frame) on demand
            commands_desc.size = static_cast<vk::DeviceSize>(INITIAL_DRAW_CAPACITY) * sizeof(vk::DrawIndexedIndirectCommand);
            commands_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
            commands_desc.persistent = true;
            m_local_shadow_commands_handles[frame_index] = m_graph->create_buffer(commands_desc);

            // One counter per material: every view of a material shares one command block (shadow_cull.comp.glsl)
            graph::buffer_desc counts_desc{};
            counts_desc.name = "local_shadow_counts_" + std::to_string(frame_index);
            counts_desc.size = static_cast<vk::DeviceSize>(MAX_MATERIALS) * sizeof(u32);
            counts_desc.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst;
            counts_desc.persistent = true;
            m_local_shadow_counts_handles[frame_index] = m_graph->create_buffer(counts_desc);
            m_local_command_capacity[frame_index] = INITIAL_DRAW_CAPACITY;

            // Cull descriptors: written every frame by update_cull_descriptors()
            std::vector<util::descriptor_item> cull_items;
            for (u32 binding = 0; binding <= 7; ++binding)
                cull_items.emplace_back(binding, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
            m_local_shadow_cull_desc_buffers[frame_index] = m_plugin.get_dev()->create_descriptor_buffer(m_local_shadow_cull_set_layout,
                cull_items, util::descriptor_buffer_type::resource, 1);
            m_shadow_cull_desc_buffers[frame_index] = m_plugin.get_dev()->create_descriptor_buffer(m_local_shadow_cull_set_layout,
                cull_items, util::descriptor_buffer_type::resource, 1);         // the sun pass: same layout, its own outputs
        }
    }


    void scene_renderer::destroy_local_shadow_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_local_shadow_resources");

        m_plugin.get_vk_device().waitIdle();

        for (auto& desc : m_local_shadow_cull_desc_buffers) {
            if (desc.buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(desc.buffer);
            desc = {};
        }
        for (auto& buffer : m_shadow_view_buffers) {
            if (buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(buffer);
            buffer = {};
        }

        if (m_local_shadow_cull_pipeline)
            m_plugin.get_vk_device().destroyPipeline(m_local_shadow_cull_pipeline);

        if (m_local_shadow_cull_pipeline_layout)
            m_plugin.get_vk_device().destroyPipelineLayout(m_local_shadow_cull_pipeline_layout);

        if (m_local_shadow_cull_set_layout)
            m_plugin.get_vk_device().destroyDescriptorSetLayout(m_local_shadow_cull_set_layout);

        m_local_shadow_cull_pipeline = nullptr;
        m_local_shadow_cull_pipeline_layout = nullptr;
        m_local_shadow_cull_set_layout = nullptr;
    }


    // Which shadow casters changed since the last frame. Shadow maps are kept until something in them moves, so this is
    // what decides how much shadow work a frame has: nothing moved -> none at all, however the camera moves.
    // Result: m_caster_dirty_spheres (world bounds of what moved / appeared / disappeared, old and new place) or
    // m_casters_all_dirty, and m_caster_revision (bumped on any change)
    void scene_renderer::update_shadow_casters() {
        GLT_PROFILE_SCOPE("scene_renderer::update_shadow_casters");

        m_caster_dirty_spheres.clear();
        m_casters_all_dirty = false;

        // Beyond this many moved casters in one frame, every map is treated as out of date (drawn again over the next
        // frames, most important lights first)
        constexpr size_t MAX_DIRTY_SPHERES = 4096;

        // Only the asset-backed prefix casts shadows (procedural instances are not in the shadow passes)
        const size_t count = m_asset_instance_count;
        const gpu_instance_data* current = m_packed_instances.data();
        bool changed = false;

        if (m_caster_mesh_generation != m_mesh_generation) {       // a mesh was re-uploaded: same instances, other geometry
            m_caster_mesh_generation = m_mesh_generation;
            m_casters_all_dirty = true;
            changed = true;
        }

        // What makes two packed instances the same caster: transform, mesh and forced LOD. The normal matrix follows the
        // transform; material table offset and virtual geometry flags do not change what a shadow map shows
        const auto same_caster = [](const gpu_instance_data& a, const gpu_instance_data& b) {
            return a.mesh_index == b.mesh_index
                && ((a.flags ^ b.flags) & GLT::asset::mesh::INSTANCE_FLAG_FORCE_LOD_MASK) == 0u
                && std::memcmp(&a.model, &b.model, sizeof(glm::mat4)) == 0;
        };
        const auto sphere_of = [this](const gpu_instance_data& instance) {
            return shadow_caster_sphere(instance, m_shared->get_mesh(instance.mesh_index));
        };
        const auto note = [&](const glm::vec4& sphere) {
            if (m_casters_all_dirty)
                return;
            if (m_caster_dirty_spheres.size() >= MAX_DIRTY_SPHERES)
                m_casters_all_dirty = true;
            else
                m_caster_dirty_spheres.push_back(sphere);
        };

        if (count == m_caster_instances.size()) {

            // Same instances in the same order (the usual frame): one memcmp says "nothing moved"
            if (count > 0 && std::memcmp(current, m_caster_instances.data(), count * sizeof(gpu_instance_data)) != 0) {
                for (size_t index = 0; index < count; ++index) {

                    if (std::memcmp(&current[index], &m_caster_instances[index], sizeof(gpu_instance_data)) == 0)
                        continue;
                    if (!same_caster(current[index], m_caster_instances[index])) {
                        const glm::vec4 sphere = sphere_of(current[index]);
                        note(m_caster_spheres[index]);              // where it was
                        note(sphere);                               // where it is
                        m_caster_spheres[index] = sphere;
                        changed = true;
                    }
                    m_caster_instances[index] = current[index];
                }
            }

        } else {

            // Instances were added or removed (the order may have changed with it): match both sets by content
            struct keyed { u64 key; u32 index; };
            const auto key_of = [](const gpu_instance_data& instance) {
                u64 hash = shadow_hash_bytes(&instance.model, sizeof(glm::mat4), 0xcbf29ce484222325ull);
                const u32 rest[2] = { instance.mesh_index, instance.flags & GLT::asset::mesh::INSTANCE_FLAG_FORCE_LOD_MASK };
                return shadow_hash_bytes(rest, sizeof(rest), hash);
            };

            std::vector<glm::vec4> spheres(count);
            for (size_t index = 0; index < count; ++index)
                spheres[index] = sphere_of(current[index]);

            constexpr size_t MAX_MATCHED = 262144;                  // larger scenes: not worth sorting, everything is redrawn
            if (count + m_caster_instances.size() > MAX_MATCHED) {
                m_casters_all_dirty = true;
            } else {

                std::vector<keyed> before(m_caster_instances.size());
                std::vector<keyed> after(count);
                for (size_t index = 0; index < before.size(); ++index)
                    before[index] = { key_of(m_caster_instances[index]), static_cast<u32>(index) };
                for (size_t index = 0; index < count; ++index)
                    after[index] = { key_of(current[index]), static_cast<u32>(index) };
                const auto by_key = [](const keyed& a, const keyed& b) { return a.key < b.key; };
                std::sort(before.begin(), before.end(), by_key);
                std::sort(after.begin(), after.end(), by_key);

                size_t b = 0;
                size_t a = 0;
                while (b < before.size() || a < after.size()) {
                    if (a == after.size() || (b < before.size() && before[b].key < after[a].key))
                        note(m_caster_spheres[before[b++].index]);  // gone
                    else if (b == before.size() || after[a].key < before[b].key)
                        note(spheres[after[a++].index]);            // new
                    else {
                        ++b;                                        // still there
                        ++a;
                    }
                }
            }

            m_caster_instances.assign(current, current + count);
            m_caster_spheres = std::move(spheres);
            changed = true;
        }

        if (changed)
            ++m_caster_revision;
    }


    // Local light shadows of this frame: the cache decides which lights have shadow maps, where they are in the atlas and
    // which views have to be drawn now (scene/local_shadow_cache.h). Called from update_lights(), before the lights are
    // uploaded: it writes gpu_light::params.z (first shadow view + 1)
    void scene_renderer::update_local_shadows() {
        GLT_PROFILE_SCOPE("scene_renderer::update_local_shadows");

        m_local_shadow_view_count = 0;
        m_local_shadow_cull_views = 0;
        m_local_shadow_tiles.clear();

        // The tiles planned in the previous update() were never drawn (render() returned early): plan them again
        if (m_local_shadow_plan_pending) {
            m_local_shadows.discard_frame();
            m_local_shadow_plan_pending = false;
        }

        const shadow_settings& settings = m_shadow_settings;
        const u32 max_lights = std::min(m_plugin.visual_settings().local_shadow_lights, MAX_SHADOW_LIGHTS);

        // No shaderClipDistance: the atlas pass cannot keep views in their tiles
        if (!settings.enabled || max_lights == 0 || m_light_count == 0 || !m_shared->local_shadow_atlas_supported()) {
            if (!m_local_shadows.empty())
                m_local_shadows.clear();
            return;
        }

        if (m_casters_all_dirty)
            m_local_shadows.invalidate_all();
        else
            m_local_shadows.invalidate(m_caster_dirty_spheres);

        glm::vec4 camera_planes[6];
        extract_frustum_planes(camera_projection() * m_active_camera.view, camera_planes);

        // Draw commands are reserved for every (draw, view drawn this frame) pair, see ensure_draw_buffers()
        const u32 view_budget = std::clamp(SHADOW_COMMAND_BUDGET / std::max(m_total_draw_capacity, 1u), 6u, SHADOW_VIEWS_PER_FRAME);

        local_shadow_cache::settings config{};
        config.atlas_size = settings.atlas_size;
        config.max_lights = max_lights;
        config.distance = settings.local_distance;
        config.far_distance = settings.local_far_distance;
        config.normal_bias = settings.local_normal_bias;
        config.bias = settings.local_bias;
        m_local_shadows.plan(m_packed_lights, m_packed_light_keys, m_active_camera.position, camera_planes, config, view_budget);

        const std::vector<gpu_shadow_view>& views = m_local_shadows.views();
        if (views.empty())
            return;

        m_local_shadow_view_count = static_cast<u32>(views.size());
        m_local_shadow_cull_views = m_local_shadows.cull_view_count();
        m_local_shadow_tiles = m_local_shadows.render_tiles();
        m_local_shadow_plan_pending = !m_local_shadow_tiles.empty();
        m_plugin.get_dev()->update_buffer(m_shadow_view_buffers[m_current_frame], views.data(), views.size() * sizeof(gpu_shadow_view));
    }


    // Draws the views of one shadow atlas that are out of date this frame (pass.tiles); every other tile keeps its depth
    // map. Three passes: reset the per-material counters, cull every instance against the views to draw (one dispatch,
    // shadow_cull.comp.glsl), then draw them in one render pass. The material vertex shader takes the view from
    // firstInstance and clips to the view's tile (renderer_project_vertex), so the pass binds ONE descriptor set (the
    // frame's set 0) and issues ONE indirect draw per material for all views
    void scene_renderer::add_shadow_atlas_passes(const shadow_atlas_pass& pass) {
        GLT_PROFILE_SCOPE("scene_renderer::add_shadow_atlas_passes");

        const bool draws = pass.tiles && !pass.tiles->empty();
        if (!draws && !pass.clear_atlas)
            return;

        if (draws) {

            // ---- Reset counts ---------------------------------------------------------------------------------------
            m_graph->add_pass(pass.reset_name,
                [pass](graph::pass_builder& builder) {
                    builder.write(pass.counts, graph::buffer_usage::transfer_dst);
                },
                [pass](graph::pass_context& ctx) {
                    ctx.cmd.fillBuffer(ctx.get_buffer(pass.counts), 0, MAX_MATERIALS * sizeof(u32), 0);
                });

            // ---- Cull -----------------------------------------------------------------------------------------------
            m_graph->add_pass(pass.cull_name,
                [pass](graph::pass_builder& builder) {
                    builder.write(pass.commands, graph::buffer_usage::storage_readwrite);
                    builder.write(pass.counts,   graph::buffer_usage::storage_readwrite);
                },
                [this, pass](graph::pass_context& ctx) {

                    // Only the asset-backed prefix: procedural instances have no mesh-table entry (mesh_index 0 would alias a real mesh)
                    if (m_asset_instance_count == 0)
                        return;

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_local_shadow_cull_pipeline);
                    m_plugin.get_dev()->bind_descriptor_buffer({ *pass.cull_descriptors }, ctx.cmd);
                    m_plugin.get_dev()->bind_descriptor_set(m_local_shadow_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);
                    const std::array<u32, 4> push = { m_asset_instance_count, pass.view_first, pass.view_count,
                        static_cast<u32>(pass.tiles->size()) };
                    ctx.cmd.pushConstants(m_local_shadow_cull_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                        static_cast<u32>(sizeof(push)), push.data());
                    ctx.cmd.dispatch((m_asset_instance_count + 63) / 64, 1, 1);     // one thread per instance, all views
                });
        }

        // ---- Draw (atlas) -------------------------------------------------------------------------------------------
        m_graph->add_pass(pass.draw_name,
            [pass, draws](graph::pass_builder& builder) {
                builder.depth_attachment(pass.atlas, pass.clear_atlas ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
                    vk::AttachmentStoreOp::eStore, 1.0f, 0);
                if (draws) {
                    builder.read(pass.commands, graph::buffer_usage::indirect);
                    builder.read(pass.counts,   graph::buffer_usage::indirect);
                }
            },
            [this, pass, draws](graph::pass_context& ctx) {

                ctx.begin_rendering();                  // viewport + scissor = the whole atlas
                if (!draws) {
                    ctx.end_rendering();
                    return;
                }

                // Only the tiles that are drawn again lose their old depth map
                if (!pass.clear_atlas) {
                    std::vector<vk::ClearRect> rects;
                    rects.reserve(pass.tiles->size());
                    for (const auto& tile : *pass.tiles)
                        rects.emplace_back(vk::Rect2D({ static_cast<i32>(tile.x), static_cast<i32>(tile.y) }, { tile.size, tile.size }), 0u, 1u);
                    const vk::ClearAttachment depth(vk::ImageAspectFlagBits::eDepth, 0u, vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0u)));
                    ctx.cmd.clearAttachments(depth, rects);
                }

                if (m_instance_count == 0 || m_asset_instance_count == 0) {
                    ctx.end_rendering();
                    return;
                }

                const vk::Buffer commands = ctx.get_buffer(pass.commands);
                const vk::Buffer counts = ctx.get_buffer(pass.counts);
                const vk::DeviceSize views = pass.tiles->size();                    // views drawn this frame
                constexpr vk::DeviceSize stride = sizeof(vk::DrawIndexedIndirectCommand);

                ctx.cmd.bindVertexBuffers(0, { m_shared->get_vertex_buffer() }, { 0 });
                ctx.cmd.bindIndexBuffer(m_shared->get_index_buffer(), 0, vk::IndexType::eUint32);

                // set 0 = the frame's set: instances (1), shadow views (6) and globals (9) are all the vertex shader reads here
                m_plugin.get_dev()->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame], m_material_desc_buffers[m_current_frame] },
                    ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                for (u32 mat_slot : m_scene_materials) {

                    const glm::uvec2 range = m_material_ranges[mat_slot];
                    if (range.y == 0)
                        continue;                               // no culled draws use this material (e.g. procedural only)

                    const material_slot& entry = m_shared->get_material(mat_slot);
                    if (!entry.alive || !entry.depth_pipeline)
                        continue;

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.depth_pipeline);
                    m_plugin.get_dev()->bind_descriptor_set(m_shared->get_mesh_pipeline_layout(), 1, 1,
                        m_material_desc_buffers[m_current_frame].get_offset_to_set(mat_slot), ctx.cmd, vk::PipelineBindPoint::eGraphics);

                    // every drawn view of this material: block [range.x * views, (range.x + range.y) * views), counter per material
                    ctx.cmd.drawIndexedIndirectCount(
                        commands, static_cast<vk::DeviceSize>(range.x) * views * stride,
                        counts, static_cast<vk::DeviceSize>(mat_slot) * sizeof(u32),
                        static_cast<u32>(range.y * views),
                        static_cast<u32>(stride));
                }

                ctx.end_rendering();
            });
    }


    // The sun's cascades that are out of date this frame (m_sun_shadow_tiles, planned by update_shadow())
    void scene_renderer::add_sun_shadow_passes(const bool clear_atlas) {
        GLT_PROFILE_SCOPE("scene_renderer::add_sun_shadow_passes");

        shadow_atlas_pass pass{};
        pass.reset_name = "reset_shadow_draw_count";
        pass.cull_name = "shadow_cull";
        pass.draw_name = "shadow_pass";
        pass.atlas = m_shadow_map;
        pass.commands = m_shadow_draw_commands_handles[m_current_frame];
        pass.counts = m_shadow_draw_count_handles[m_current_frame];
        pass.cull_descriptors = &m_shadow_cull_desc_buffers[m_current_frame];
        pass.tiles = &m_sun_shadow_tiles;
        pass.view_first = SUN_SHADOW_VIEW_FIRST;
        pass.view_count = MAX_SUN_CASCADES;
        pass.clear_atlas = clear_atlas;
        add_shadow_atlas_passes(pass);
    }


    // The local lights' atlas tiles that are out of date this frame (m_local_shadow_tiles, planned by update_local_shadows())
    void scene_renderer::add_local_shadow_passes(const bool clear_atlas) {
        GLT_PROFILE_SCOPE("scene_renderer::add_local_shadow_passes");

        shadow_atlas_pass pass{};
        pass.reset_name = "reset_local_shadow_counts";
        pass.cull_name = "local_shadow_cull";
        pass.draw_name = "local_shadow_pass";
        pass.atlas = m_shadow_atlas;
        pass.commands = m_local_shadow_commands_handles[m_current_frame];
        pass.counts = m_local_shadow_counts_handles[m_current_frame];
        pass.cull_descriptors = &m_local_shadow_cull_desc_buffers[m_current_frame];
        pass.tiles = &m_local_shadow_tiles;
        pass.view_first = 0;
        pass.view_count = m_local_shadow_cull_views;
        pass.clear_atlas = clear_atlas;
        add_shadow_atlas_passes(pass);
    }

}
