
#include <util/pch.h>

#include <event/event_bus.h>
#include <event/application_event.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_window_plugin.h>
#include <plugin_system/i_renderer_plugin.h>
#include <config/imgui_config.h>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    #if defined(DEBUG)
        constexpr bool                  USE_VULKAN_VALIDATION = true;
    #else
        constexpr bool                  USE_VULKAN_VALIDATION = false;
    #endif

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        #define VALIDATE_INIT                               if (!m_imgui_initialized) { return; }
        #define VK_CHECK_S(expr)		                    ASSERT_S(expr == vk::Result::eSuccess)
        #define VK_CHECK(expr, successMsg, failureMsg)		ASSERT(expr == vk::Result::eSuccess)
    #else
        #define VALIDATE_INIT
        #define VK_CHECK_S(expr)
        #define VK_CHECK(expr, successMsg, failureMsg)
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================
    
    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    bool renderer::create() {

        mp_window = GLT::plugin_manager::get_plugin_ref<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);
        ASSERT(mp_window, "", "Failed to get window plugin")

        // TODO: remove
        m_active_camera = create_ref<GLT::world::camera>();
        m_active_camera->set_position({0.0f, 0.0f, 2.5f});

        init_vulkan();
        create_base_resources();
        create_acceleration_structures();
        create_rt_pipeline();
        update_descriptor_set();
        imgui_init();

        m_framebuffer_resize_sub = GLT::event_bus::subscribe<GLT::window_framebuffer_resize_event>(
            [this](const GLT::window_framebuffer_resize_event& event) { 
                m_target_framebuffer_size = glm::ivec2{event.get_width(), event.get_height()};
            }
        );

        m_state = system_state::idle;
        LOG_INIT
        return true;
    }


    void renderer::destroy() {

        m_state = system_state::destroyed;
        m_device.waitIdle();

        GLT::event_bus::unsubscribe(m_framebuffer_resize_sub);

        // Wait for all frames to complete
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
            if (m_in_flight_fences[i])
                VK_CHECK_S(m_device.waitForFences(m_in_flight_fences[i], VK_TRUE, UINT64_MAX));
        }

        imgui_shutdown();

        // Destroy swapchain resources
        if (m_swapchain.swapchain_handle) {
            for (auto& view : m_swapchain.swapchain_image_views)
                m_device.destroyImageView(view);
            m_device.destroySwapchainKHR(m_swapchain.swapchain_handle);
        }
        if (m_old_swapchain)
            m_device.destroySwapchainKHR(m_old_swapchain);

        m_deletion_queue.shutdown();
        m_old_swapchain = nullptr;

        if (m_surface) {
            m_instance.instance_handle.destroySurfaceKHR(m_surface);
            m_surface = nullptr;
        }

        mp_window.reset();
        LOG_SHUTDOWN
    }
    
    // CLASS PUBLIC ====================================================================================================

    void renderer::begin_frame() {

        m_state = system_state::running;

        // Wait for the in-flight fence for this frame
        VK_CHECK_S(m_device.waitForFences(m_in_flight_fences[m_current_frame], VK_TRUE, UINT64_MAX));
        m_device.resetFences(m_in_flight_fences[m_current_frame]);

        try {

            auto acquire_result = m_device.acquireNextImageKHR(m_swapchain.swapchain_handle, UINT64_MAX, m_present_semaphores[m_current_frame], nullptr);
            m_current_swapchain_image = acquire_result.value;

        } catch (const vk::OutOfDateKHRError&) {

            resize_swapchain(m_target_framebuffer_size);
            m_state = system_state::idle;
            return;                                                         // skip the rest of begin_frame this frame
        }

        // Reset command buffer and begin recording
        vk::CommandBuffer& current_cmd = m_rt_render_cmd[m_current_frame];  // Record ImGui rendering into the command buffer
        current_cmd.reset();
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        current_cmd.begin(begin_info);

        // Update camera matrices
        {
            m_active_camera->set_aspect_ratio((f32)m_render_size.x / (f32)m_render_size.y);
            glm::mat4 proj = m_active_camera->get_projection_matrix();
            glm::mat4 view = m_active_camera->get_view_matrix();
            glm::mat4 mats[2] = {glm::inverse(view), glm::inverse(proj)};

            void* data = m_vr_dev->map_buffer(m_uniform_buffer);
            memcpy(data, mats, sizeof(mats));
            m_vr_dev->unmap_buffer(m_uniform_buffer);
        }

        // Bind descriptor buffer
        m_vr_dev->bind_descriptor_buffer({m_resource_desc_buffer}, current_cmd);
        m_vr_dev->bind_descriptor_set(m_pipeline_layout, 0, 0, 0, current_cmd);

        transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eTransferDstOptimal);
        clear_output_image(current_cmd, m_clear_color);
        transition_image_layout(current_cmd, image_type::render, vk::ImageLayout::eGeneral);                     // Transition output to GENERAL

        // Ray tracing
        current_cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline);
        m_vr_dev->dispatch_rays(m_rt_pipeline, m_sbt_buffer, m_render_size.x, m_render_size.y, 1, current_cmd);

        // Swapchain image transitions:
        transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eTransferDstOptimal);       // to TRANSFER_DST_OPTIMAL
        transition_image_layout(current_cmd, image_type::render, vk::ImageLayout::eTransferSrcOptimal);          // to TRANSFER_SRC_OPTIMAL

        // Blit from output image to swapchain image
        current_cmd.blitImage(
            m_output_image_buffer.image, 
            vk::ImageLayout::eTransferSrcOptimal,
            m_swapchain.swapchain_images[m_current_swapchain_image], 
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageBlit(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                {vk::Offset3D(0, 0, 0), vk::Offset3D(m_render_size.x, m_render_size.y, 1)},
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                {vk::Offset3D(0, 0, 0), vk::Offset3D(m_render_size.x, m_render_size.y, 1)}),
            vk::Filter::eLinear);

        begin_imgui_frame(current_cmd);
    }


    void renderer::draw_frame() {

        VALIDATE_INIT
        
        vk::CommandBuffer& current_cmd = m_rt_render_cmd[m_current_frame];              // Record ImGui rendering into the command buffer
        end_imgui_frame(current_cmd);
        current_cmd.end();                                                              // End command buffer

        vk::SubmitInfo submit_info{};                                                   // Submit to graphics queue
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &m_present_semaphores[m_current_frame];
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &current_cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &m_render_semaphores[m_current_frame];

        try {
            m_queues.graphics_queue.submit(submit_info, m_in_flight_fences[m_current_frame]);
        } catch (...) {
            LOG(error, "Failed to submit");
        }

        vk::PresentInfoKHR present_info{};                                              // Present
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &m_render_semaphores[m_current_frame];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &m_swapchain.swapchain_handle;
        present_info.pImageIndices = &m_current_swapchain_image;

        try {
            IGNORE_UNUSED_VARIABLE_START
            const vk::Result result = m_queues.present_queue.presentKHR(present_info);
            IGNORE_UNUSED_VARIABLE_STOP
        } catch (const vk::OutOfDateKHRError&) {
            resize_swapchain(m_target_framebuffer_size);
        }

        // Advance to next frame
        m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;
        m_state = system_state::idle;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void renderer::init_vulkan() {

        LOG(trace, "Renderer configuration:");
        LOG(trace, "  - Vulkan validation: [{}]", (USE_VULKAN_VALIDATION ? "ENABLED" : "DISABLED"));

        u32 count;
        const char** extensions = mp_window->get_required_render_extensions(&count);

        vr::vulkan_builder builder;
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = utils::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;

        for (u32 i = 0; i < count; i++)                                      // Add the extensions to the builder
            builder.instance_extensions.push_back(extensions[i]);

        m_instance = builder.create_instance();                             // Create the instance
        m_surface = mp_window->create_vulkan_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);        // Pick the physical device to use
        m_device = builder.create_device();                                 // Create the logical device
        m_queues = builder.get_queues();                                    // Get the queues for the logical device
        ASSERT(m_queues.graphics_queue, "", "Failed to select the graphics queue")

        m_deletion_queue.setup(m_device);

        // Create semaphores and fences
        vk::SemaphoreCreateInfo semaphore_info = {};
        vk::FenceCreateInfo fence_info = {};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        glm::ivec2 framebuffer_size = mp_window->get_framebuffer_size();
        create_swapchain(framebuffer_size);

        // Resize vectors to match number of swapchain images
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
        m_render_semaphores.resize(m_image_count);
        m_present_semaphores.resize(m_image_count);
        m_in_flight_fences.resize(m_image_count);
        m_swapchain_images_layout.resize(m_image_count);
        for (u32 x = 0; x < m_image_count; x++) {
            m_render_semaphores[x] = m_device.createSemaphore(semaphore_info);
            m_present_semaphores[x] = m_device.createSemaphore(semaphore_info);
            m_in_flight_fences[x] = m_device.createFence(fence_info);
            m_swapchain_images_layout[x] = vk::ImageLayout::eUndefined;
        }

        // Create command pools
        vk::CommandPoolCreateInfo pool_info = {};
        pool_info.queueFamilyIndex = m_queues.graphics_index;
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_graphics_pool = m_device.createCommandPool(pool_info);

        // create command buffers
        vk::CommandBufferAllocateInfo alloc_info = {};
        alloc_info.commandPool = m_graphics_pool;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;
        alloc_info.commandBufferCount = m_image_count;

        auto allocated_cmd_buffers = m_device.allocateCommandBuffers(alloc_info);
        for (u32 x = 0; x < m_image_count; x++)
            m_rt_render_cmd[x] = allocated_cmd_buffers[x];

        m_vr_dev = new vr::device(m_instance.instance_handle, m_device, m_physical_device);


        // vk::SamplerCreateInfo sampler_info{};
        // sampler_info.magFilter = vk::Filter::eNearest;
        // sampler_info.minFilter = vk::Filter::eNearest;
        // sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
        // sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        // sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        // sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        // sampler_info.anisotropyEnable = VK_FALSE;
        // sampler_info.maxAnisotropy = 1.0f;
        // sampler_info.borderColor = vk::BorderColor::eFloatOpaqueBlack;
        // sampler_info.unnormalizedCoordinates = VK_FALSE;
        // sampler_info.compareEnable = VK_FALSE;
        // sampler_info.compareOp = vk::CompareOp::eAlways;
        // sampler_info.mipLodBias = 0.0f;
        // sampler_info.minLod = 0.0f;
        // sampler_info.maxLod = 0.0f;
        // m_default_sampler_nearest = m_device.createSampler(sampler_info);

        // sampler_info.magFilter = vk::Filter::eLinear;
        // sampler_info.minFilter = vk::Filter::eLinear;
        // m_default_sampler_linear = m_device.createSampler(sampler_info);


        m_deletion_queue.push_func([&]() {

            // Destroy semaphores
            for (auto& sem : m_render_semaphores)
                if (sem) m_device.destroySemaphore(sem);

            for (auto& sem : m_present_semaphores)
                if (sem) m_device.destroySemaphore(sem);

            m_render_semaphores.clear();
            m_present_semaphores.clear();

            // Destroy fences
            for (u32 i = 0; i < m_image_count; i++)
                if (m_in_flight_fences[i]) m_device.destroyFence(m_in_flight_fences[i]);

            if (m_graphics_pool)
                m_device.destroyCommandPool(m_graphics_pool);

            if (m_vr_dev)
                delete m_vr_dev;
        });
    }


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


    void renderer::clear_output_image(vk::CommandBuffer cmd, const glm::vec4& color) {

        // Clear the image
        vk::ClearColorValue clear_color;
        clear_color.setFloat32({color.r, color.g, color.b, color.a});
        cmd.clearColorImage(
            m_swapchain.swapchain_images[m_current_swapchain_image],
            vk::ImageLayout::eTransferDstOptimal,
            clear_color,
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
        );
    }


    ImTextureID renderer::create_imgui_texture(vr::accessible_image& img) {

        vk::ImageLayout layout = img.layout;                        // Ensure the image layout is correct for sampling
        if (layout == vk::ImageLayout::eUndefined)
            layout = vk::ImageLayout::eShaderReadOnlyOptimal;       // OR shader read only optimal

        // If sampler is null, create a default sampler (see below)
        vk::Sampler& sampler = img.sampler;
        if (!sampler) {
            
            vk::SamplerCreateInfo sampler_info{};
            sampler_info.magFilter = vk::Filter::eLinear;
            sampler_info.minFilter = vk::Filter::eLinear;
            sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
            sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            sampler_info.anisotropyEnable = VK_FALSE;
            sampler_info.maxAnisotropy = 1.0f;
            sampler_info.borderColor = vk::BorderColor::eFloatOpaqueBlack;
            sampler_info.unnormalizedCoordinates = VK_FALSE;
            sampler_info.compareEnable = VK_FALSE;
            sampler_info.compareOp = vk::CompareOp::eAlways;
            sampler_info.mipLodBias = 0.0f;
            sampler_info.minLod = 0.0f;
            sampler_info.maxLod = 0.0f;
            sampler = m_device.createSampler(sampler_info);
        }

        return reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
            static_cast<VkImageView>(img.view), 
            static_cast<VkImageLayout>(layout))
        );
    }

    // ----- SWAPCHAIN -------------------------------------------------------------------------------------------------

	void renderer::create_swapchain(const glm::ivec2 size) {

        // create a swapchain
        m_swapchain_builder = vr::swapchain_builder(m_device, m_physical_device, m_surface, m_queues.graphics_index, m_queues.present_index);
        m_swapchain_builder.width = static_cast<u32>(size.x);
        m_swapchain_builder.height = static_cast<u32>(size.y);
        m_swapchain_builder.back_buffer_count = 2;
        m_swapchain_builder.image_usage = vk::ImageUsageFlagBits::eTransferDst;
        m_swapchain_builder.desired_format = vk::Format::eB8G8R8A8Unorm;
        m_swapchain_builder.present_mode = mp_window->get_vsync() ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eMailbox;
        LOG(trace, "Creating swapchain with dimensions: {}x{}", m_swapchain_builder.width, m_swapchain_builder.height);

        try {

            m_swapchain = m_swapchain_builder.build_swapchain();

        } catch (const std::exception &exception) {

            LOG(fatal, "Failed to create initial swapchain: [{}]", exception.what());
            throw;
        }
	}


	void renderer::destroy_swapchain() {

        m_swapchain_builder.destroy_swapchain(m_device, m_swapchain);
	}


	void renderer::resize_swapchain(const glm::ivec2 size) {

        VALIDATE(m_target_framebuffer_size.x > 0 && m_target_framebuffer_size.y > 0,
            return , "", "Cant resize if one dimension is to small");

		vkDeviceWaitIdle(m_device);
		destroy_swapchain();
		create_swapchain(size);

        // re‑initialise layout tracking
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);
        
        // override imgui framebuffers
        m_imgui_framebuffers.resize(m_swapchain.swapchain_images.size());
        for (size_t x = 0; x < m_swapchain.swapchain_images.size(); x++) {

            vk::ImageView attachments[] = { m_swapchain.swapchain_image_views[x] };
            vk::FramebufferCreateInfo fb_info = {};
            fb_info.renderPass = m_imgui_render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = attachments;
            fb_info.width = m_swapchain.swapchain_extent.width;
            fb_info.height = m_swapchain.swapchain_extent.height;
            fb_info.layers = 1;

            try {
                m_imgui_framebuffers[x] = m_device.createFramebuffer(fb_info);
            } catch (const vk::SystemError& e) {
                LOG(error, "Failed to create ImGui framebuffer: [{}]", e.what());
                throw;
            }
        }
	}


    void renderer::create_base_resources() {

        // Create an image to render to
        auto image_create_info = vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setExtent(vk::Extent3D(m_swapchain.swapchain_extent, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eStorage 
                | vk::ImageUsageFlagBits::eTransferSrc
                | vk::ImageUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        // create the image with dedicated memory
        m_output_image_buffer = m_vr_dev->create_image(image_create_info, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        // create a view for the image
        auto view_create_info = vk::ImageViewCreateInfo()
            .setImage(m_output_image_buffer.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

        m_output_image.view = m_device.createImageView(view_create_info);

        // create a uniform buffer
        u32 uniform_buffer_size = sizeof(f32) * 4 * 4 * 2; // two 4x4 matrix
        uniform_buffer_size += sizeof(f32) * 4;            // pass time, and 3 floats for padding or whatever else in the future

        // {       // create descriptor set
        //     // Image info for the combined image sampler
        //     vk::DescriptorImageInfo descriptor_image_info{};
        //     descriptor_image_info.sampler       = m_default_sampler_linear;
        //     descriptor_image_info.imageView     = m_output_image.view;
        //     descriptor_image_info.imageLayout   = vk::ImageLayout::eShaderReadOnlyOptimal;

        //     // Write descriptor set
        //     vk::WriteDescriptorSet write{};
        //     write.dstSet                        = m_output_image.descriptor;   // must be a valid descriptor set
        //     write.dstBinding                    = 0;
        //     write.dstArrayElement               = 0;
        //     write.descriptorCount               = 1;
        //     write.descriptorType                = vk::DescriptorType::eCombinedImageSampler;
        //     write.pImageInfo                    = &descriptor_image_info;      // pointer to image info
        //     m_device.updateDescriptorSets(write, nullptr); 
        // }


        // we will be writing to this buffer on the CPU
        m_uniform_buffer = m_vr_dev->create_buffer(uniform_buffer_size, vk::BufferUsageFlagBits::eUniformBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        m_deletion_queue.push_func([&]() {

            if (m_output_image.view)                                    // Destroy image view
                m_device.destroyImageView(m_output_image.view);

            if (m_output_image_buffer.image)                            // destroy the image
                m_vr_dev->destroy_image(m_output_image_buffer);

            m_output_image.view = nullptr;
            m_output_image_buffer.image = nullptr;

            if (m_uniform_buffer.buffer)
                m_vr_dev->destroy_buffer(m_uniform_buffer);
        });
    }


    void renderer::transition_image_layout(vk::CommandBuffer command_buffer, const image_type type, 
        const vk::ImageLayout new_layout) {

        switch (type) {

            case image_type::swapchain: {

                // Define the image subresource range for swapchain images
                vk::ImageSubresourceRange swapchain_range(
                    vk::ImageAspectFlagBits::eColor,            // Color aspect
                    0,                                          // Base mip level
                    1,                                          // Level count
                    0,                                          // Base array layer
                    1                                           // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_swapchain.swapchain_images[m_current_swapchain_image],
                    m_swapchain_images_layout[m_current_swapchain_image],
                    new_layout,
                    swapchain_range,                            // Added: Image subresource range
                    vk::PipelineStageFlagBits::eAllGraphics,    // Added: Source stage (using default)
                    vk::PipelineStageFlagBits::eAllCommands     // Added: Destination stage (using default)
                );
                m_swapchain_images_layout[m_current_swapchain_image] = new_layout;

            } break;

            case image_type::render: {

                // Define the image subresource range for render images
                vk::ImageSubresourceRange render_range(
                    vk::ImageAspectFlagBits::eColor,            // Color aspect
                    0,                                          // Base mip level
                    1,                                          // Level count
                    0,                                          // Base array layer
                    1                                           // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_output_image_buffer.image,
                    m_output_image_layout,
                    new_layout,
                    render_range,                                   // Added: Image subresource range
                    vk::PipelineStageFlagBits::eAllGraphics,        // Added: Source stage
                    vk::PipelineStageFlagBits::eAllCommands         // Added: Destination stage
                );
                m_output_image_layout = new_layout;
                m_output_image.layout = new_layout;

            } break;
        }
    }

    // ----- IMGUI -----------------------------------------------------------------------------------------------------

    void renderer::imgui_init() {

        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

        ImGui::StyleColorsDark();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        mp_window->imgui_init(GLT::render::backend_api::vulkan);

        ASSERT(ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4,
            [](const char* function_name, void* user_data) -> PFN_vkVoidFunction {

                vk::Instance* instance = static_cast<vk::Instance*>(user_data);
                return vkGetInstanceProcAddr(static_cast<VkInstance>(*instance), function_name);
            },
            &m_instance.instance_handle
        ), "", "Failed to load Vulkan functions for ImGui");

        create_imgui_resources();
        LOG(trace, "ImGui initialized");
    }


    void renderer::imgui_shutdown() {

        mp_window->imgui_shutdown();
        destroy_imgui_resources();
        LOG(trace, "ImGui shutdown");
    }


    void renderer::begin_imgui_frame(vk::CommandBuffer& current_cmd) {

        VALIDATE_INIT

        // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL for ImGui
        transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eColorAttachmentOptimal);

        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        ImGui_ImplVulkan_NewFrame();                                                    // Start ImGui frame (no command buffer needed)
        mp_window->begin_imgui_frame();
        ImGui::NewFrame();
    }


    void renderer::end_imgui_frame(vk::CommandBuffer& current_cmd) {

        VALIDATE_INIT

        ImGui::EndFrame();
        ImGui::Render();                                                                // Finalize ImGui draw data
        
        vk::RenderPassBeginInfo rp_info{};                                              // Begin render pass (clears background to dark blue)
        rp_info.renderPass = m_imgui_render_pass;
        rp_info.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
        rp_info.renderArea.offset = vk::Offset2D{};
        rp_info.renderArea.extent = m_swapchain.swapchain_extent;

        std::array<vk::ClearValue, 1> clear_values{};                                   // Clear colour (dark blue)
        clear_values[0].color = {m_clear_color.x, m_clear_color.y, m_clear_color.z, m_clear_color.w};
        rp_info.clearValueCount = static_cast<u32>(clear_values.size());
        rp_info.pClearValues = clear_values.data();

        current_cmd.beginRenderPass(rp_info, vk::SubpassContents::eInline);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), current_cmd);
        current_cmd.endRenderPass();

        // After the render pass, the image layout is PRESENT_SRC_KHR (set in render pass)
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;
        
        // Update and Render additional Platform Windows
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

}
