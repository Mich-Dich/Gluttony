
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

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; frame_index++) {

            util::allocated_buffer* grid_allocation = m_graph->buffer_allocation(m_light_grid_handles[frame_index]);
            util::allocated_buffer* list_allocation = m_graph->buffer_allocation(m_light_index_list_handles[frame_index]);
            util::allocated_buffer* counter_allocation = m_graph->buffer_allocation(m_light_counter_handles[frame_index]);

            ASSERT(grid_allocation && list_allocation && counter_allocation, "",
                "Graph failed to allocate light culling buffers");

            using VKDT = vk::DescriptorType;
            using VKSF = vk::ShaderStageFlagBits;

            std::vector<util::descriptor_item> descriptor_items;
            descriptor_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_camera_ubos[frame_index]);
            descriptor_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, &m_light_buffers[frame_index]);
            descriptor_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eCompute, 1, grid_allocation);
            descriptor_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eCompute, 1, list_allocation);
            descriptor_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eCompute, 1, counter_allocation);
            descriptor_items.emplace_back(5, VKDT::eUniformBuffer, VKSF::eCompute, 1, &m_light_meta_ubos[frame_index]);

            if (m_light_cull_desc_buffers[frame_index].buffer.buffer == nullptr) {

                m_light_cull_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_light_cull_set_layout, descriptor_items,
                    util::descriptor_buffer_type::resource, 1);

            } else 
                m_dev->update_descriptor_buffer(m_light_cull_desc_buffers[frame_index], descriptor_items, util::descriptor_buffer_type::resource);
        }
    }

}
