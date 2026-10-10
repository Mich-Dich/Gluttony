
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

    void renderer::create_light_culling_pipeline() {

        // Descriptor set layout -----------------------------------------------------------------------------------
        //   0  Camera UBO           (per frame)
        //   1  Lights SSBO          (per frame, Step 1)
        //   2  Light grid SSBO      (per frame, Step 2 — persistent, {offset, count} per cluster)
        //   3  Light index list     (per frame, Step 2 — persistent, packed indices)
        //   4  Light counter        (per frame, Step 2 — persistent, single u32)
        //   5  Light meta UBO       (per frame — {light_count, list_capacity, per_cluster_cap})
        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(1, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(2, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(3, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(4, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(5, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eCompute, 1, nullptr);
        layout_items.emplace_back(6, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute, 1, nullptr);

        m_light_cull_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_light_cull_set_layout;
        m_light_cull_pipeline_layout = m_device.createPipelineLayout(pipeline_layout_info);

        // Compute pipeline ----------------------------------------------------------------------------------------
        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto compute_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "light_culling.comp.glsl");
        ASSERT(!compute_spv.empty(), "", "Failed to compile light culling compute shader");

        vk::ShaderModule compute_shader = m_dev->create_shader_module(compute_spv);

        vk::PipelineShaderStageCreateInfo shader_stage{};
        shader_stage.stage = vk::ShaderStageFlagBits::eCompute;
        shader_stage.module = compute_shader;
        shader_stage.pName = "main";

        vk::ComputePipelineCreateInfo pipeline_info{};
        pipeline_info.layout = m_light_cull_pipeline_layout;
        pipeline_info.stage = shader_stage;

        auto result = m_device.createComputePipeline(nullptr, pipeline_info);
        VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create light culling pipeline");
        m_light_cull_pipeline = result.value;
        m_live_pipeline_count++;

        m_device.destroyShaderModule(compute_shader);

        // Nearest filtering: we want exact texel values for min/max reduction, not interpolated ones
        vk::SamplerCreateInfo sampler_info{};
        sampler_info.magFilter = vk::Filter::eNearest;
        sampler_info.minFilter = vk::Filter::eNearest;
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
        sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        sampler_info.maxLod = 0.0f;
        m_depth_sampler = m_device.createSampler(sampler_info);

        // Per-frame meta UBO. Allocated empty; content is written each frame by update_light_meta_ubo()
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {
            m_light_meta_ubos[frame_index] = m_dev->create_buffer(sizeof(glm::uvec4),
                vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;
        }

        // Descriptor buffers are not created here — they depend on the cluster buffers, which only
        // exist once begin_frame() has computed the grid dimensions. update_light_cull_descriptors()
        // creates them lazily on the first frame and updates them in place thereafter
    }


    void renderer::destroy_light_culling_pipeline() {

        m_device.waitIdle();

        for (auto& descriptor_buffer : m_light_cull_desc_buffers) {
            if (descriptor_buffer.buffer.buffer)
                m_dev->destroy_buffer(descriptor_buffer.buffer);
            descriptor_buffer = {};
        }

        for (auto& meta_ubo : m_light_meta_ubos) {
            if (meta_ubo.buffer)
                m_dev->destroy_buffer(meta_ubo);
            meta_ubo = {};
        }

        if (m_depth_sampler) {
            m_device.destroySampler(m_depth_sampler);
            m_depth_sampler = nullptr;
        }

        if (m_light_cull_pipeline)
            m_device.destroyPipeline(m_light_cull_pipeline);

        if (m_light_cull_pipeline_layout)
            m_device.destroyPipelineLayout(m_light_cull_pipeline_layout);

        if (m_light_cull_set_layout)
            m_device.destroyDescriptorSetLayout(m_light_cull_set_layout);

        m_light_cull_pipeline = nullptr;
        m_light_cull_pipeline_layout = nullptr;
        m_light_cull_set_layout = nullptr;
    }


    void renderer::update_light_meta_ubo() {

        glm::uvec4 meta{};
        meta.x = m_light_count;
        meta.y = m_cluster_index_capacity;
        meta.z = MAX_LIGHTS_PER_CLUSTER;
        meta.w = 0u;

        void* mapped = m_dev->map_buffer(m_light_meta_ubos[m_current_frame]);
        std::memcpy(mapped, &meta, sizeof(meta));
        m_dev->unmap_buffer(m_light_meta_ubos[m_current_frame]);
    }


    void renderer::update_light_cull_descriptors() {

        if (m_cluster_params.cluster_count == 0)
            return;

        // Only refresh the CURRENT frame's descriptor buffer. The fence wait at the top of
        // begin_frame() guarantees the GPU has finished the submission that last used this slot,
        // so it is safe to rewrite. The other two slots may still be executing on the GPU and
        // must not be touched.
        const u32 frame_index = m_current_frame;

        util::allocated_buffer* grid_allocation    = m_graph->buffer_allocation(m_light_grid_handles[frame_index]);
        util::allocated_buffer* list_allocation    = m_graph->buffer_allocation(m_light_index_list_handles[frame_index]);
        util::allocated_buffer* counter_allocation = m_graph->buffer_allocation(m_light_counter_handles[frame_index]);

        ASSERT(grid_allocation && list_allocation && counter_allocation, "",
            "Graph failed to allocate light culling buffers");

        const vk::ImageView depth_view = m_graph->texture_default_view(m_depth_target);
        ASSERT(depth_view && m_depth_sampler, "", "Depth view/sampler not ready for light culling descriptors");

        // descriptor_item's union reads this as a util::accessible_image*, so the field layout must
        // match exactly — no local substitute structs.
        util::accessible_image depth_image{};
        depth_image.view = depth_view;
        depth_image.sampler = m_depth_sampler;
        depth_image.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> descriptor_items;
        descriptor_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_camera_ubos[frame_index]);
        descriptor_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_light_buffers[frame_index]);
        descriptor_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, grid_allocation);
        descriptor_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, list_allocation);
        descriptor_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, counter_allocation);
        descriptor_items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_light_meta_ubos[frame_index]);
        descriptor_items.emplace_back(6, VKDT::eCombinedImageSampler, VKSF::eCompute, 1, &depth_image);

        if (m_light_cull_desc_buffers[frame_index].buffer.buffer == nullptr) {

            // First time this slot is used: allocate with combined usage so the sampler binding is legal.
            m_light_cull_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_light_cull_set_layout, descriptor_items,
                util::descriptor_buffer_type::combined, 1);

        } else
            m_dev->update_descriptor_buffer(m_light_cull_desc_buffers[frame_index], descriptor_items, util::descriptor_buffer_type::combined);

    }

}
