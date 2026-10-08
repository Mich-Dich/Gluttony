#pragma once

#include <glm/gtc/matrix_transform.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Unit UV sphere at origin. Position == normal since radius = 1.
    void generate_preview_sphere(std::vector<GLT::asset::mesh::vertex>& verts, std::vector<u32>& indices, const u32 segments,
        const u32 rings);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    void generate_preview_sphere(std::vector<GLT::asset::mesh::vertex>& verts, std::vector<u32>& indices, const u32 segments,
        const u32 rings) {

        verts.clear();
        indices.clear();
        verts.reserve((segments + 1) * (rings + 1));

        const f32 pi = glm::pi<f32>();
        const f32 two_pi = glm::two_pi<f32>();

        for (u32 y = 0; y <= rings; ++y) {

            const f32 v = static_cast<f32>(y) / static_cast<f32>(rings);
            const f32 phi = v * pi;
            const f32 sin_phi = std::sin(phi);
            const f32 cos_phi = std::cos(phi);

            for (u32 x = 0; x <= segments; ++x) {

                const f32 u = static_cast<f32>(x) / static_cast<f32>(segments);
                const f32 theta = u * two_pi;
                const f32 sin_theta = std::sin(theta);
                const f32 cos_theta = std::cos(theta);

                GLT::asset::mesh::vertex vtx{};
                vtx.position = { sin_phi * cos_theta, cos_phi, sin_phi * sin_theta };
                vtx.normal = vtx.position;
                vtx.tangent = { -sin_theta, 0.0f, cos_theta, 1.0f };
                vtx.uv0 = { u, v };
                verts.push_back(vtx);
            }
        }

        indices.reserve(segments * rings * 6);
        for (u32 y = 0; y < rings; ++y) {
            for (u32 x = 0; x < segments; ++x) {

                const u32 i0 = y * (segments + 1) + x;
                const u32 i1 = i0 + 1;
                const u32 i2 = i0 + segments + 1;
                const u32 i3 = i2 + 1;

                // Skip degenerate triangles at the poles.
                if (y != 0) {
                    indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                }
                if (y != rings - 1) {
                    indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
                }
            }
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void* renderer::render_material_preview(GLT::asset::handle material, const GLT::asset::material::material_params& params,
        std::span<const GLT::asset::handle> textures, const glm::vec3& camera_pos) {

        if (!m_preview_ready)
            return nullptr;

        m_preview_material = material;
        m_preview_params = params;
        m_preview_camera_pos = camera_pos;

        m_preview_textures.fill(INVALID_HANDLE);
        const std::size_t count = std::min(textures.size(), m_preview_textures.size());
        for (std::size_t index = 0; index < count; ++index)
            m_preview_textures[index] = textures[index];

        m_preview_queued = true;
        return m_preview_image->get_descriptor_set();
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // Called once from create(), after create_rt_pipeline() and imgui_init()
    // Allocates every preview resource and writes the initial descriptor set
    void renderer::create_material_preview_resources() {

        // ---- preview image ----------------------------------------------------------

        m_preview_image = GLT::create_ref<image>();
        m_preview_image->resize({ PREVIEW_IMAGE_SIZE, PREVIEW_IMAGE_SIZE, 1 });
        m_preview_image->get_descriptor_set();
        m_preview_dummy_gbuffer = GLT::create_ref<image>();
        m_preview_dummy_gbuffer->resize({1, 1, 1}, GLT::render::image_format::RGBA32F);
        
        // The preview shares the pipeline layout, so it also has to provide binding 8. Its sample count is pinned to 0
        // (see upload_preview_data), so the accumulation branch is never taken and this image is only ever written, never read back
        m_preview_accum_image = GLT::create_ref<image>();
        m_preview_accum_image->resize({ PREVIEW_IMAGE_SIZE, PREVIEW_IMAGE_SIZE, 1 }, GLT::render::image_format::RGBA32F);
            
        // ---- sphere geometry --------------------------------------------------------

        std::vector<GLT::asset::mesh::vertex> sphere_verts;
        std::vector<u32> sphere_indices{};
        generate_preview_sphere(sphere_verts, sphere_indices, PREVIEW_SPHERE_SEGMENTS, PREVIEW_SPHERE_RINGS);

        const vk::BufferUsageFlags geom_usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
          | vk::BufferUsageFlagBits::eStorageBuffer;

        m_preview_vertex_buffer = m_vr_dev->create_buffer(sphere_verts.size() * sizeof(GLT::asset::mesh::vertex), geom_usage,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_preview_index_buffer  = m_vr_dev->create_buffer(sphere_indices.size() * sizeof(u32), geom_usage,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memcpy(m_vr_dev->map_buffer(m_preview_vertex_buffer), sphere_verts.data(), sphere_verts.size() * sizeof(GLT::asset::mesh::vertex));
        m_vr_dev->unmap_buffer(m_preview_vertex_buffer);

        std::memcpy(m_vr_dev->map_buffer(m_preview_index_buffer), sphere_indices.data(), sphere_indices.size() * sizeof(u32));
        m_vr_dev->unmap_buffer(m_preview_index_buffer);

        // ---- sphere BLAS ------------------------------------------------------------
        {
            vr::blas_create_info bci{};
            bci.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

            vr::geometry_data gd{};
            gd.vertex_format = vk::Format::eR32G32B32Sfloat;
            gd.stride = sizeof(GLT::asset::mesh::vertex);
            gd.index_format = vk::IndexType::eUint32;
            gd.primitive_count = static_cast<u32>(sphere_indices.size()) / 3;
            gd.data_addresses.vertex_dev_address = m_preview_vertex_buffer.dev_address;
            gd.data_addresses.index_dev_address = m_preview_index_buffer.dev_address;
            bci.geometries.push_back(gd);

            auto [handle, build_info] = m_vr_dev->create_blas(bci);
            m_preview_sphere_blas = handle;

            std::vector<vr::blas_build_info> infos = { build_info };
            auto scratch = m_vr_dev->create_scratch_buffer_from_build_infos(infos);
            auto cmd = m_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];

            cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
            m_vr_dev->build_blas(infos, cmd);
            m_vr_dev->add_acceleration_build_barrier(cmd);
            cmd.end();

            auto si = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&cmd);
            m_queues.graphics_queue.submit(si, nullptr);
            m_device.waitIdle();

            m_vr_dev->destroy_buffer(scratch);
            m_device.freeCommandBuffers(m_graphics_pool, cmd);
        }

        // ---- preview TLAS -----------------------------------------------------------
        {
            vr::tlas_create_info tci{};
            tci.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            tci.max_instance_count = 1;

            std::tie(m_preview_tlas, m_preview_tlas_build_info) = m_vr_dev->create_tlas(tci);
            m_preview_instance_buffer = m_vr_dev->create_instance_buffer(1);
        }

        // ---- single-slot buffers ----------------------------------------------------
        m_preview_material_buffer = m_vr_dev->create_buffer(sizeof(gpu_material), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        m_preview_geometry_buffer = m_vr_dev->create_buffer(sizeof(gpu_geometry), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        m_preview_camera_ubo = m_vr_dev->create_buffer(sizeof(camera_ubo), vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        // Single geometry entry: sphere's vertex/index offsets are zero
        gpu_geometry geometry{};
        geometry.vertex_base = 0;
        geometry.index_base = 0;
        geometry.material_index = 0;
        std::memcpy(m_vr_dev->map_buffer(m_preview_geometry_buffer), &geometry, sizeof(geometry));
        m_vr_dev->unmap_buffer(m_preview_geometry_buffer);

        // Initialise the preview's own descriptor mirror
        // Everything starts at the checkerboard fallback, exactly like m_texture_descriptors did at init
        m_preview_texture_descriptors = m_texture_descriptors;

        // Don't lie [using] but it make the binding more readable
        using VKDI = vr::descriptor_item;
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // ---- preview descriptor buffer ---------------------------------------------
        // Same layout as the main one - only the contents differ. Bindings 4 and 5 are NOT the shared main vertex/index buffers
        // the sphere lives in its own buffers so a resize of the main geometry doesn't invalidate this set
        m_preview_bindings = {
            VKDI(0, VKDT::eAccelerationStructureKHR, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 1, &m_preview_tlas.buffer.dev_address),
            VKDI(1, VKDT::eUniformBuffer, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 1, &m_preview_camera_ubo),
            VKDI(2, VKDT::eStorageImage, VKSF::eRaygenKHR, 10, &m_preview_image->get_accessible_image_ref(), 1),
            VKDI(3, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_preview_material_buffer),
            VKDI(4, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_preview_vertex_buffer),
            VKDI(5, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_preview_index_buffer),
            VKDI(6, VKDT::eCombinedImageSampler, VKSF::eClosestHitKHR, BINDLESS_TEXTURE_MAX, m_preview_texture_descriptors.data()),
            VKDI(7, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_preview_geometry_buffer),
            VKDI(8,  VKDT::eStorageImage, VKSF::eRaygenKHR, 10, &m_preview_accum_image->get_accessible_image_ref(), 1),
            VKDI(9,  VKDT::eStorageImage, VKSF::eRaygenKHR, 10, &m_preview_accum_image->get_accessible_image_ref(), 1),
            VKDI(10, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 10, &m_preview_dummy_gbuffer->get_accessible_image_ref(), 1),
            VKDI(11, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 10, &m_preview_dummy_gbuffer->get_accessible_image_ref(), 1),
            VKDI(12, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 10, &m_preview_dummy_gbuffer->get_accessible_image_ref(), 1),
            VKDI(13, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 10, &m_preview_dummy_gbuffer->get_accessible_image_ref(), 1),

            // Reuse the existing dummy gbuffer (it's a 1x1 RGBA float image — the shader never reads it in the preview path since the
            // compute pass isn't run for previews; the preview's own dispatch stops after the RT pass)
            VKDI(14, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eCompute, 10, &m_preview_dummy_gbuffer->get_accessible_image_ref(), 1),
        };

        m_preview_desc_buffer = m_vr_dev->create_descriptor_buffer(m_resource_descriptor_layout, m_preview_bindings, vr::descriptor_buffer_type::combined);
        m_vr_dev->update_descriptor_buffer(m_preview_desc_buffer, m_preview_bindings, vr::descriptor_buffer_type::combined);
        m_preview_ready = true;

        // ---- cleanup -----------------------------------------------------------------
        m_deletion_queue.push_func([this]() {

            if (m_preview_desc_buffer.buffer.buffer)
                m_vr_dev->destroy_buffer(m_preview_desc_buffer.buffer);

            if (m_preview_camera_ubo.buffer)
                m_vr_dev->destroy_buffer(m_preview_camera_ubo);

            if (m_preview_geometry_buffer.buffer)
                m_vr_dev->destroy_buffer(m_preview_geometry_buffer);

            if (m_preview_material_buffer.buffer)
                m_vr_dev->destroy_buffer(m_preview_material_buffer);

            if (m_preview_instance_buffer.buffer)
                m_vr_dev->destroy_buffer(m_preview_instance_buffer);

            if (m_preview_index_buffer.buffer)
                m_vr_dev->destroy_buffer(m_preview_index_buffer);

            if (m_preview_vertex_buffer.buffer)
                m_vr_dev->destroy_buffer(m_preview_vertex_buffer);

            if (m_preview_sphere_blas.buffer.buffer)
                m_vr_dev->destroy_blas(m_preview_sphere_blas);

            if (m_preview_tlas.buffer.buffer)
                m_vr_dev->destroy_tlas(m_preview_tlas);
        });
    }


    // Called at the top of begin_frame() when a preview is queued
    // Fills the per-frame buffers - everything here is host-side memcpy, no GPU work yet
    void renderer::upload_preview_data() {

        // ---- material ---------------------------------------------------------------
        auto registry = GLT::asset::registry::get_ref();

        gpu_material gpu{};
        gpu.base_color = m_preview_params.base_color;
        gpu.emissive = m_preview_params.emissive;
        gpu.roughness = m_preview_params.roughness;
        gpu.metallic = m_preview_params.metallic;
        gpu.reflectance = m_preview_params.reflectance;
        gpu.normal_scale = m_preview_params.normal_scale;
        gpu.occlusion_strength = m_preview_params.occlusion_strength;
        gpu.flags = m_preview_params.flags;
        gpu.textures.fill(0);                       // checkerboard fallback

        if (m_preview_material != INVALID_HANDLE) {
            if (registry->data_as<GLT::asset::material::material_asset>(m_preview_material)) {
                for (size_t index = 0; index < TEXTURE_SLOT_COUNT; index++) {

                    const GLT::asset::handle tex = m_preview_textures[index];
                    if (tex == INVALID_HANDLE)
                        continue;

                    u32 idx = find_texture_index(tex);
                    if (idx == UINT32_MAX)
                        idx = load_texture(tex);        // registers ref_count = 1, one shot

                    gpu.textures[index] = idx;
                }
            }
        }

        std::memcpy(m_vr_dev->map_buffer(m_preview_material_buffer), &gpu, sizeof(gpu));
        m_vr_dev->unmap_buffer(m_preview_material_buffer);

        // ---- camera -----------------------------------------------------------------
        const f32 aspect = 1.0f;
        const glm::mat4 view = glm::lookAt(m_preview_camera_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.01f, 100.0f);
        proj[1][1] *= -1.0f;                        // Vulkan Y flip

        camera_ubo ubo{};
        ubo.view_inv = glm::inverse(view);
        ubo.proj_inv = glm::inverse(proj);
        ubo.sun_direction = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)), 0.0f);
        ubo.sun_color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);
        ubo.temporal = glm::uvec4{ 1u, 1u, 0u, 0u };   // reset, write index 0

        std::memcpy(m_vr_dev->map_buffer(m_preview_camera_ubo), &ubo, sizeof(ubo));
        m_vr_dev->unmap_buffer(m_preview_camera_ubo);

        // ---- TLAS instance ----------------------------------------------------------
        VkTransformMatrixKHR xform{};
        xform.matrix[0][0] = 1.0f;
        xform.matrix[1][1] = 1.0f;
        xform.matrix[2][2] = 1.0f;

        vk::AccelerationStructureInstanceKHR inst{};
        inst.setTransform(xform)
            .setInstanceCustomIndex(0)
            .setAccelerationStructureReference(m_preview_sphere_blas.buffer.dev_address)
            .setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
            .setMask(0xFF)
            .setInstanceShaderBindingTableRecordOffset(0);

        m_vr_dev->update_buffer(m_preview_instance_buffer, &inst, sizeof(inst));

        // ---- rebuild preview TLAS ---------------------------------------------------
        // One instance, one BLAS. The build itself is submitted outside this function because the caller already has a command buffer open
    }


    // Emits the preview pass into the current command buffer
    // Must be called after upload_preview_data() and after a TLAS rebuild has run for the preview's instance buffer
    void renderer::dispatch_material_preview(vk::CommandBuffer cmd) {

        // Rebuild TLAS ---------------------------------------------------------------
        // Keep it simple: immediate submit for the rebuild, since previews are cheap and this happens once per frame while the viewer is open
        {
            auto scratch = m_vr_dev->create_scratch_buffer_from_build_info(m_preview_tlas_build_info);
            auto tcmd = m_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];

            tcmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
            m_vr_dev->build_tlas(m_preview_tlas_build_info, m_preview_instance_buffer, 1, tcmd);
            tcmd.end();

            auto si = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&tcmd);
            m_queues.graphics_queue.submit(si, nullptr);
            m_device.waitIdle();

            m_vr_dev->destroy_buffer(scratch);
            m_device.freeCommandBuffers(m_graphics_pool, tcmd);
        }

        // Image barrier: undefined-ish -> GENERAL for storage write
        vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        m_vr_dev->transition_image_layout(cmd,
            m_preview_image->get_allocated_image_ref().image,
            m_preview_image->get_accessible_image_ref().layout,
            vk::ImageLayout::eGeneral, range,
            vk::PipelineStageFlagBits::eAllCommands,
            vk::PipelineStageFlagBits::eAllCommands);
        m_preview_image->get_accessible_image_ref().layout = vk::ImageLayout::eGeneral;

        // Bind preview descriptors
        m_vr_dev->bind_descriptor_buffer({ m_preview_desc_buffer }, cmd);
        m_vr_dev->bind_descriptor_set(m_pipeline_layout, 0, 0, 0, cmd);

        // Trace
        cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline);
        m_vr_dev->dispatch_rays(m_rt_pipeline, m_sbt_buffer, PREVIEW_IMAGE_SIZE, PREVIEW_IMAGE_SIZE, 1, cmd);

        m_frame_draw_calls += 1;

        // Barrier back: GENERAL -> SHADER_READ for ImGui
        m_vr_dev->transition_image_layout(cmd,
            m_preview_image->get_allocated_image_ref().image,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eShaderReadOnlyOptimal, range,
            vk::PipelineStageFlagBits::eAllCommands,
            vk::PipelineStageFlagBits::eAllCommands);
        m_preview_image->get_accessible_image_ref().layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

}
