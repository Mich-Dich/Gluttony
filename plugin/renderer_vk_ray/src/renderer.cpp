
#include <util/pch.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <vk_ray/vk_ray.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_window_plugin.h>
#include <plugin_system/i_renderer_plugin.h>

#include "util/utils.h"

#include "renderer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    #if defined(DEBUG)

        constexpr bool                  USE_VULKAN_VALIDATION = true;

    #else

        constexpr bool                  USE_VULKAN_VALIDATION = false;

    #endif

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    renderer::renderer() {

        init_vulkan();
        create_base_resources();
        create_acceleration_structures();
        create_rt_pipeline();
        update_descriptor_set();
        imgui_init();

        m_state = system_state::active; // renderer is now ready
        LOG_INIT
    }


    renderer::~renderer() {

    }
    
    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void renderer::init_vulkan() {

        LOG(trace, "Renderer configuration:");
        LOG(trace, "  - Vulkan validation: [{}]", (USE_VULKAN_VALIDATION ? "ENABLED" : "DISABLED"));

        auto p_window = GLT::plugin_manager::get_plugin<GLT::platform::i_window_plugin>(GLT::plugin_manager::targeted_interface::window);
        ASSERT(!p_window.expired(), "", "Failed to get window plugin")
        u32 count;
        auto p_window_strong = p_window.lock();
        const char** extensions = p_window_strong->get_required_render_extensions(&count);

        vr::vulkan_builder builder;
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = utils::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;

        for (u32 i = 0; i < count; i++)                                      // Add the extensions to the builder
            builder.instance_extensions.push_back(extensions[i]);

        m_instance = builder.create_instance();                             // Create the instance
        m_surface = p_window_strong->create_vulkan_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);        // Pick the physical device to use
        m_device = builder.create_device();                                 // Create the logical device
        m_queues = builder.get_queues();                                    // Get the queues for the logical device
        ASSERT(m_queues.graphics_queue, "", "Failed to select the graphics queue")

        m_deletion_queue.setup(m_device);

        // create a swapchain
        m_swapchain_builder = vr::swapchain_builder(m_device, m_physical_device, m_surface, m_queues.graphics_index, m_queues.present_index);
        m_swapchain_builder.height = static_cast<u32>(400);
        m_swapchain_builder.width = static_cast<u32>(600);
        m_swapchain_builder.back_buffer_count = 2;
        m_swapchain_builder.image_usage = vk::ImageUsageFlagBits::eTransferDst;
        m_swapchain_builder.desired_format = vk::Format::eB8G8R8A8Unorm;
        LOG(trace, "Creating swapchain with dimensions: {}x{}", m_swapchain_builder.width, m_swapchain_builder.height);

        try {

            m_swapchain_resources = m_swapchain_builder.build_swapchain();

        } catch (const std::exception &e) {

            LOG(fatal, "Failed to create initial swapchain: {}", e.what());
            throw;
        }

        // Create semaphores and fences
        vk::SemaphoreCreateInfo semaphore_info = {};
        vk::FenceCreateInfo fence_info = {};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        // Resize vectors to match number of swapchain images
        m_image_count = static_cast<u32>(m_swapchain_resources.swapchain_images.size());
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


    void renderer::create_base_resources() {

        // Create an image to render to
        auto image_create_info = vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setExtent(vk::Extent3D(m_swapchain_resources.swapchain_extent, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)
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


    void renderer::imgui_init() {

        // Setup ImGui context
        IMGUI_CHECKVERSION();
        m_imgui_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_imgui_context);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

        // Setup ImGui style
        ImGui::StyleColorsDark();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        auto p_window = GLT::plugin_manager::get_plugin<GLT::platform::i_window_plugin>(GLT::plugin_manager::targeted_interface::window);
        ASSERT(!p_window.expired(), "", "Failed to get window plugin")
        p_window.lock()->imgui_init(GLT::render::backend_api::vulkan);

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

        // TODO: shutdown imgui on window
        // auto p_window = GLT::plugin_manager::get_plugin<GLT::platform::i_window_plugin>(GLT::plugin_manager::targeted_interface::window);
        // ASSERT(!p_window.expired(), "", "Failed to get window plugin")
        // p_window.lock()->imgui_init(GLT::render::backend_api::vulkan);
        
        LOG(trace, "ImGui shutdown");
        destroy_imgui_resources();
        ImGui::DestroyContext();
    }


    void renderer::create_imgui_resources() {

        // Create descriptor pool for ImGui (optional in newer versions, but still recommended)
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            { vk::DescriptorType::eSampler, 1000 },
            { vk::DescriptorType::eCombinedImageSampler, 1000 },
            { vk::DescriptorType::eSampledImage, 1000 },
            { vk::DescriptorType::eStorageImage, 1000 },
            { vk::DescriptorType::eUniformTexelBuffer, 1000 },
            { vk::DescriptorType::eStorageTexelBuffer, 1000 },
            { vk::DescriptorType::eUniformBuffer, 1000 },
            { vk::DescriptorType::eStorageBuffer, 1000 },
            { vk::DescriptorType::eUniformBufferDynamic, 1000 },
            { vk::DescriptorType::eStorageBufferDynamic, 1000 },
            { vk::DescriptorType::eInputAttachment, 1000 }
        };

        vk::DescriptorPoolCreateInfo pool_info = {};
        pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
        pool_info.poolSizeCount = static_cast<u32>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        try {
            m_imgui_descriptor_pool = m_device.createDescriptorPool(pool_info);
        } catch (const vk::SystemError& e) {
            LOG(error, "Failed to create ImGui descriptor pool: {}", e.what());
            throw;
        }

        // Create render pass for ImGui FIRST (needed for init_info)
        vk::AttachmentDescription attachment = {};
        attachment.format = m_swapchain_resources.swapchain_format;
        attachment.samples = vk::SampleCountFlagBits::e1;
        attachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Load existing content (our rendered image)
        attachment.storeOp = vk::AttachmentStoreOp::eStore;
        attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass = {};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        vk::SubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo render_pass_info = {};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        try {
            m_imgui_render_pass = m_device.createRenderPass(render_pass_info);
        } catch (const vk::SystemError& e) {
            LOG(error, "Failed to create ImGui render pass: {}", e.what());
            throw;
        }

        // Create ImGui Vulkan backend initialization info for NEW API
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_instance.instance_handle;
        init_info.PhysicalDevice = m_physical_device;
        init_info.Device = m_device;
        init_info.QueueFamily = m_queues.graphics_index;
        init_info.Queue = m_queues.graphics_queue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = m_imgui_descriptor_pool;
        init_info.MinImageCount = static_cast<u32>(m_swapchain_resources.swapchain_images.size());
        init_info.ImageCount = static_cast<u32>(m_swapchain_resources.swapchain_images.size());
        init_info.Allocator = nullptr;

        // Required: Set the API version
        init_info.ApiVersion = VK_API_VERSION_1_3; // Use 1.2 or 1.3 based on what you initialized

        // Set up pipeline rendering info for render pass
        init_info.PipelineInfoMain.RenderPass = m_imgui_render_pass;
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        // For dynamic rendering (if you were using it), you'd need to set PipelineRenderingCreateInfo
        // But since we're using a traditional render pass, we don't need that

        // Min allocation size (optional but recommended to avoid validation warnings)
        init_info.MinAllocationSize = 256; // 256 bytes minimum allocation

        init_info.CheckVkResultFn = [](VkResult err) {
            ASSERT(err == VK_SUCCESS, "", "ImGui Vulkan error: {}", vk::to_string(static_cast<vk::Result>(err)))
        };

        // Initialize ImGui Vulkan backend (single argument in new API)
        ASSERT(ImGui_ImplVulkan_Init(&init_info), "", "Failed to initialize ImGui Vulkan backend");

        // Create framebuffers for each swapchain image
        m_imgui_framebuffers.resize(m_swapchain_resources.swapchain_images.size());
        for (size_t x = 0; x < m_swapchain_resources.swapchain_images.size(); x++) {
            vk::ImageView attachments[] = { m_swapchain_resources.swapchain_image_views[x] };

            vk::FramebufferCreateInfo fb_info = {};
            fb_info.renderPass = m_imgui_render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = attachments;
            fb_info.width = m_swapchain_resources.swapchain_extent.width;
            fb_info.height = m_swapchain_resources.swapchain_extent.height;
            fb_info.layers = 1;

            try {
                m_imgui_framebuffers[x] = m_device.createFramebuffer(fb_info);
            } catch (const vk::SystemError& e) {
                LOG(error, "Failed to create ImGui framebuffer: [{}]", e.what());
                throw;
            }
        }

        // Note: In newer ImGui versions, font texture creation is automatic!
        // The first call to ImGui::NewFrame() will create the font texture if needed.
        // No manual font upload required anymore.

        m_imgui_initialized = true;
    }


    void renderer::destroy_imgui_resources() {

        if (!m_imgui_initialized)
            return;

        m_device.waitIdle();

        for (auto& framebuffer : m_imgui_framebuffers) {            // Destroy framebuffers
            if (framebuffer)
                m_device.destroyFramebuffer(framebuffer);
        }
        m_imgui_framebuffers.clear();

        if (m_imgui_render_pass) {                                  // Destroy render pass
            m_device.destroyRenderPass(m_imgui_render_pass);
            m_imgui_render_pass = nullptr;
        }

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();

        if (m_imgui_descriptor_pool) {                              // Destroy descriptor pool
            m_device.destroyDescriptorPool(m_imgui_descriptor_pool);
            m_imgui_descriptor_pool = nullptr;
        }

        m_imgui_initialized = false;
    }

}
