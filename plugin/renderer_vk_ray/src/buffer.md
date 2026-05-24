

    void renderer::create_acceleration_structures() {

        // vertex and index data for the triangle
        f32 vertices[] = {
            1.0f, -1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
            0.0f, 1.0f, 0.0f};
        u32 indices[] = {0, 1, 2};

        // create a buffer for the vertices and copy the data to it
        m_vertex_buffer = m_vr_dev->create_buffer(
            sizeof(f32) * 3 * 3,                                                  // 3 vertices, 3 floats per vertex
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR, // this buffer will be used as a source for the BLAS
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);              // we will be writing to this buffer on the CPU, so we need to set this flag, the buffer is also host visible so it is not fast GPU memory
        m_index_buffer = m_vr_dev->create_buffer(
            sizeof(u32) * 3, // 3 vertices, 3 floats per vertex
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT); // same as above

        // upload the vertex data to the buffer, UpdateBuffer(...) will use mapping the buffer and memcpy
        m_vr_dev->update_buffer(m_vertex_buffer, vertices, sizeof(f32) * 3 * 3);
        m_vr_dev->update_buffer(m_index_buffer, indices, sizeof(u32) * 3);

        // Create info struct for the BLAS
        vr::blas_create_info blas_create_info = {};
        blas_create_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        // [POI]
        // triangle geometry data, blas_create_info can have multiple geometries
        vr::geometry_data geom_data = {};
        geom_data.vertex_format = vk::Format::eR32G32B32Sfloat;
        geom_data.stride = sizeof(f32) * 3; // 3 floats per vertex: x, y, z
        geom_data.index_format = vk::IndexType::eUint32;
        geom_data.primitive_count = 1;
        geom_data.data_addresses.vertex_dev_address = m_vertex_buffer.dev_address;
        geom_data.data_addresses.index_dev_address = m_index_buffer.dev_address;

        // add triangle geometry to the BLAS create info
        // NOTE: blas_create_info can have multiple geometries and they are of type VkAccelerationStructureGeometryKHR
        blas_create_info.geometries.push_back(geom_data);

        // [POI]
        // this only creates the BLAS, it does not build it
        // it creates acceleration structure and allocates memory for it and scratch memory
        auto [blas_handle, blas_build_info] = m_vr_dev->create_blas(blas_create_info);

        // Create a scratch buffer for the BLAS build
        auto blas_scratch_buffer = m_vr_dev->create_scratch_buffer_from_build_info(blas_build_info);
        // To have avoid allocating scratch memory, every build you can create a big scratch buffer and reuse it for all BLAS builds
        // You can create a big buffer with minimum scratch alignment properties from VulrayDevice::GetAccelerationStructureProperties()
        // and divide it into smaller buffers for each BLAS build according to how much scratch memory each BLAS needs
        // Set the scratch buffer address for a BLAS by setting blas_build_info.BuildGeometryInfo.scratchData
        // or just call VulrayDevice::BindScratchBufferToBuildInfo() to do the same thing

        m_blas_handle = blas_handle;

        // [POI]
        // create a TLAS
        vr::tlas_create_info tlas_create_info = {};
        tlas_create_info.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        tlas_create_info.max_instance_count = 1; // Max number of instances in the TLAS, when building the TLAS num of instances may be lower

        auto [tlas_handle, tlas_build_info] = m_vr_dev->create_tlas(tlas_create_info);

        m_tlas_handle = tlas_handle;

        // Create the scratch buffer for TLAS build
        auto tlas_scratch_buffer = m_vr_dev->create_scratch_buffer_from_build_info(tlas_build_info);
        auto instance_buffer = m_vr_dev->create_instance_buffer(1);         // create a buffer for the instance data
        auto inst = vk::AccelerationStructureInstanceKHR()                  // Specify the instance data
            .setInstanceCustomIndex(0)
            .setAccelerationStructureReference(m_blas_handle.buffer.dev_address)
            .setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
            .setMask(0xFF)
            .setInstanceShaderBindingTableRecordOffset(0);

        // set the transform matrix to identity
        inst.transform = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f};

        // [POI]
        // upload the instance data to the buffer
        m_vr_dev->update_buffer(instance_buffer, &inst, sizeof(vk::AccelerationStructureInstanceKHR), 0);

        // create a command buffer to build the BLAS and TLAS, m_graphics_pool is a command pool that is created in the Base Application class
        auto build_cmd = m_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];

        build_cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        // [POI]

        // build the AS
        std::vector<vr::blas_build_info> buildInfos = {blas_build_info};    // We can have multiple BLAS builds at once, but we only have one for now
        m_vr_dev->build_blas(buildInfos, build_cmd);                        // Add build commands to command buffer and retrieve scratch buffer for the build
        m_vr_dev->add_acceleration_build_barrier(build_cmd);                // Add a barrier to the command buffer to make sure the BLAS build is finished before the TLAS build starts

        // Add build commands to command buffer and retrieve scratch buffer for the build
        // We can reuse the scratch buffer from here to update the TLAS, but for now we don't update

        m_vr_dev->build_tlas(tlas_build_info, instance_buffer, 1, build_cmd);

        build_cmd.end();

        // submit the command buffer and wait for it to finish
        auto submitInfo = vk::SubmitInfo()
            .setCommandBufferCount(1)
            .setPCommandBuffers(&build_cmd);

        m_queues.graphics_queue.submit(submitInfo, nullptr);

        m_device.waitIdle();

        // Destroy the scratch buffers, because the build is finished
        // NOTE: We know the build is finished because we waited for the device to be idle, but in a real application we would use a fence or something else
        m_vr_dev->destroy_buffer(blas_scratch_buffer);
        m_vr_dev->destroy_buffer(tlas_scratch_buffer);
        m_vr_dev->destroy_buffer(instance_buffer);                  // We don't need the instance buffer anymore, because the TLAS is built and we don't plan on updating it
        m_device.freeCommandBuffers(m_graphics_pool, build_cmd);    // free the command buffer

        m_deletion_queue.push_func([&]() {

            m_vr_dev->destroy_buffer(m_vertex_buffer);
            m_vr_dev->destroy_buffer(m_index_buffer);
            m_vr_dev->destroy_blas(m_blas_handle);
            m_vr_dev->destroy_tlas(m_tlas_handle);
        });

    }


    void renderer::create_rt_pipeline() {

        // [POI]
        // Now we create a descriptor layout for the ray tracing pipeline
        // last parameter is a pointer to the items vector, so we can use it later to create the descriptor set
        // for now we have only one item, so we just pass the address of the first element
        // if we want to update the descriptor set later with another item,
        // we can just reassign the vr::descriptor_item::pItems with new items and update the descriptor set
        m_resource_bindings = {
            vr::descriptor_item(0, vk::DescriptorType::eAccelerationStructureKHR, vk::ShaderStageFlagBits::eRaygenKHR, 1, &m_tlas_handle.buffer.dev_address),
            vr::descriptor_item(1, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eRaygenKHR, 1, &m_uniform_buffer),
            vr::descriptor_item(2, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eRaygenKHR, 10, &m_output_image, 1)
        };

        // create a descriptor set layout, for the ray tracing pipeline
        m_resource_descriptor_layout = m_vr_dev->create_descriptor_set_layout(m_resource_bindings);

        m_pipeline_layout = m_vr_dev->create_pipeline_layout(m_resource_descriptor_layout);

        // create shaders for the ray tracing pipeline
        // Spir-V bytecode is required

        // load the ray gen shader
        auto ray_gen_spv = m_shader_compiler.compile_glsl_to_spirv(GLT::util::get_executable_path() / "assets" / "shader" / "hello_triangle.rgen.glsl");
        ASSERT(!ray_gen_spv.empty(), "", "Failed to load shader")
        auto ray_gen_shader_module = m_vr_dev->create_shader_from_spv(ray_gen_spv);

        // load the miss shader
        auto ray_miss_spv = m_shader_compiler.compile_glsl_to_spirv(GLT::util::get_executable_path() / "assets" / "shader" / "hello_triangle.rmiss.glsl");
        ASSERT(!ray_miss_spv.empty(), "", "Failed to load shader")
        auto ray_miss_shader_module = m_vr_dev->create_shader_from_spv(ray_miss_spv);

        // load the closest hit shader
        auto closest_hit_spv = m_shader_compiler.compile_glsl_to_spirv(GLT::util::get_executable_path() / "assets" / "shader" / "hello_triangle.rchit.glsl");
        ASSERT(!closest_hit_spv.empty(), "", "Failed to load shader")
        auto closest_hit_shader_module = m_vr_dev->create_shader_from_spv(closest_hit_spv);

        // [POI]
        // Pipeline settings for the ray tracing pipeline
        // we can set the max recursion depth, max payload size and max hit attribute size
        // max payload size is the size of the data we that every ray can carry, in this case it is a vec3
        // Look at the shader code to see how the payload is used
        // max hit attribute size is the size of the that gets passed to the hit shaders if there is a hit
        // we get barycentric coordinates of the hit point in this case which is a vec2
        vr::pipeline_settings pipeline_settings = {};
        pipeline_settings.pipeline_layout = m_pipeline_layout;
        pipeline_settings.max_recursion_depth = 1;
        pipeline_settings.max_payload_size = sizeof(glm::vec3);
        pipeline_settings.max_hit_attribute_size = sizeof(glm::vec2);

        // Collection of shaders for the pipeline
        vr::ray_tracing_shader_collection shader_collection = {};

        // add the shader to the shader binding table which stores all the shaders for the pipeline
        shader_collection.ray_gen_shaders.push_back(ray_gen_shader_module);
        shader_collection.miss_shaders.push_back(ray_miss_shader_module);

        // [POI]
        // hit groups can contain multiple shaders, so there is another special struct for it
        vr::hit_group hit_group = {};
        hit_group.closest_hit_shader = closest_hit_shader_module;
        shader_collection.hit_groups.push_back(hit_group);

        // create the ray tracing pipeline

        // create the ray tracing pipeline, a vk::Pipeline object
        auto [pipeline, sbtInfo] = m_vr_dev->create_ray_tracing_pipeline(shader_collection, pipeline_settings);
        m_rt_pipeline = pipeline;

        // [POI]
        // Build the shader binding table, it is a buffer that contains the shaders for the pipeline and we can update hit record data if we want
        m_sbt_buffer = m_vr_dev->create_sbt(m_rt_pipeline, sbtInfo);

        // create a descriptor buffer for the ray tracing pipeline
        m_resource_desc_buffer = m_vr_dev->create_descriptor_buffer(m_resource_descriptor_layout, m_resource_bindings, vr::descriptor_buffer_type::resource);

        // cleanup
        m_device.destroyShaderModule(ray_gen_shader_module.module);
        m_device.destroyShaderModule(ray_miss_shader_module.module);
        m_device.destroyShaderModule(closest_hit_shader_module.module);
        m_deletion_queue.push_func([&]() {

            m_vr_dev->destroy_sbt_buffer(m_sbt_buffer);
            m_device.destroyPipeline(m_rt_pipeline);
            m_device.destroyPipelineLayout(m_pipeline_layout);
            m_device.destroyDescriptorSetLayout(m_resource_descriptor_layout);
            m_vr_dev->destroy_buffer(m_resource_desc_buffer.buffer);
        });
    }


    void renderer::update_descriptor_set() {

        // // Set the camera position
        // // movement, rotation and input is handled by the Application Base class and we can modify the camera values as we like
        // m_active_camera->m_position = glm::vec3(0.0f, 0.0f, 2.5f);

        // [POI] We already provided each descriptor item with the pointer to a resource back when we created the descriptor set layout
        // so we can just update the resource values here
        // if we want to update the descriptor set with a new item, we can just reassign the vr::descriptor_item::p*** with new items and update the descriptor set
        m_vr_dev->update_descriptor_buffer(m_resource_desc_buffer, m_resource_bindings, vr::descriptor_buffer_type::resource);
    }
