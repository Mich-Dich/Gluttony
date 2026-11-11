
#include "util/pch.h"
#include "util/util_pch.h"
#include "shared_resource_manager.h"
#include <debug/trace.h>

#include <asset/material.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    static constexpr vk::VertexInputBindingDescription vertex_binding = {
        0, sizeof(GLT::asset::mesh::vertex), vk::VertexInputRate::eVertex,
    };

    static constexpr std::array<vk::VertexInputAttributeDescription, 4> vertex_attributes = {{
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, position) },
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, normal) },
        { 2, 0, vk::Format::eR32G32B32A32Sfloat,offsetof(GLT::asset::mesh::vertex, tangent) },
        { 3, 0, vk::Format::eR32G32Sfloat, offsetof(GLT::asset::mesh::vertex, uv0) },
    }};

    // SPIR-V of asset/shader/vk_rasterizer/depth_only.frag.glsl (`void main() { }`), assembled with spirv-as and checked
    // with spirv-val. Inline because the material pipelines are created before any GLSL shader file is compiled:
    //                 OpCapability Shader
    //                 OpMemoryModel Logical GLSL450
    //                 OpEntryPoint Fragment %main "main"
    //                 OpExecutionMode %main OriginUpperLeft
    //         %void = OpTypeVoid
    //         %func = OpTypeFunction %void
    //         %main = OpFunction %void None %func
    //        %entry = OpLabel
    //                 OpReturn
    //                 OpFunctionEnd
    static constexpr std::array<u32, 32> depth_only_fragment_spirv = {{
        0x07230203, 0x00010000, 0x00070000, 0x00000005, 0x00000000,     // magic, SPIR-V 1.0, generator, bound, schema
        0x00020011, 0x00000001,                                         // OpCapability Shader
        0x0003000e, 0x00000000, 0x00000001,                             // OpMemoryModel Logical GLSL450
        0x0005000f, 0x00000004, 0x00000001, 0x6e69616d, 0x00000000,     // OpEntryPoint Fragment %1 "main"
        0x00030010, 0x00000001, 0x00000007,                             // OpExecutionMode %1 OriginUpperLeft
        0x00020013, 0x00000002,                                         // %2 = OpTypeVoid
        0x00030021, 0x00000003, 0x00000002,                             // %3 = OpTypeFunction %2
        0x00050036, 0x00000002, 0x00000001, 0x00000000, 0x00000003,     // %1 = OpFunction %2 None %3
        0x000200f8, 0x00000004,                                         // %4 = OpLabel
        0x000100fd,                                                     // OpReturn
        0x00010038,                                                     // OpFunctionEnd
    }};

    // MACROS ==========================================================================================================

    // The call ALWAYS runs; only the result check is debug-only (see VK_SR_CHECK in scene_renderer.cpp)
    #if defined(DEBUG)
        #define VK_SRM_CHECK(expr)  ASSERT_S((expr) == vk::Result::eSuccess)
    #else
        #define VK_SRM_CHECK(expr)  ((void)(expr))
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static u32 fallback_texture_slot_for(u32 texture_slot_index);


    static bool bytecode_has_alpha_test(const GLT::asset::material::material_asset& asset);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static u32 fallback_texture_slot_for(u32 texture_slot_index) {
        return texture_slot_index == static_cast<u32>(GLT::asset::material::texture_slot::normal)
            ? TEXTURE_SLOT_FLAT_NORMAL : TEXTURE_SLOT_WHITE;
    }


    // The reflection header describes the bytecode stored next to it. compile() only emits the alpha-testing depth shader
    // (shading_strings::depth_suffix) for a material with an alpha_test entry point; every other material gets the no-op
    // one. Unreadable header: assume alpha test, so the material's own depth shader stays in use
    static bool bytecode_has_alpha_test(const GLT::asset::material::material_asset& asset) {

        namespace AM = GLT::asset::material;
        if (asset.reflection.size() < sizeof(AM::reflection_header))
            return true;
        AM::reflection_header header{};
        std::memcpy(&header, asset.reflection.data(), sizeof(header));
        if (header.magic != AM::REFLECT_SCHEMA_MAGIC)
            return true;
        return (header.entry_flags & AM::alpha_test) != 0;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    shared_resource_manager::shared_resource_manager(vk::Device device, vk::PhysicalDevice physical_device, util::device* dev,
        vk::Queue graphics_queue, u32 graphics_queue_family)
        : m_device(device), m_physical_device(physical_device), m_dev(dev), m_graphics_queue(graphics_queue),
        m_graphics_family(graphics_queue_family) {}


    shared_resource_manager::~shared_resource_manager() {

        if (m_mesh_set_layout)
            LOG(error, "shared_resource_manager destroyed without destroy() — leaking Vulkan resources")
    }

    // CLASS PUBLIC ====================================================================================================

    void shared_resource_manager::create() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create");

        m_retire.init(m_device);


        create_immediate_submit();
        create_samplers();
        create_shader_compiler();
        create_layouts();
        create_depth_only_fragment_module();
        create_mesh_buffers();

        // texture table + fallbacks
        m_gpu_textures.assign(FIRST_FREE_TEXTURE_SLOT, gpu_texture{});
        m_texture_table.assign(MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS, util::accessible_image{});
        create_fallback_textures();
        for (u32 i = FIRST_FREE_TEXTURE_SLOT; i < MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS; ++i)
            m_texture_table[i] = m_texture_table[TEXTURE_SLOT_WHITE];

        // material table + fallback material
        material_slot default_slot{};
        default_slot.ref_count = 1;
        default_slot.alive = true;
        default_slot.asset = INVALID_HANDLE;
        default_slot.texture_assets.fill(INVALID_HANDLE);
        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
            default_slot.texture_slots[i] = fallback_texture_slot_for(i);
        m_material_slots.assign(1, default_slot);

        install_fallback_material();

        LOG(trace, "shared_resource_manager created (buffers={}, pipelines={}, sets={})",
            m_live_buffer_count, m_live_pipeline_count, m_live_descriptor_set_count);
    }


    void shared_resource_manager::destroy() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy");

        m_device.waitIdle();
        m_retire.flush();                       // device is idle: everything retired so far can go now

        for (auto& slot : m_material_slots) {
            if (slot.pipeline)
                m_device.destroyPipeline(slot.pipeline);
            if (slot.depth_pipeline)
                m_device.destroyPipeline(slot.depth_pipeline);
            if (slot.vertex_module)
                m_device.destroyShaderModule(slot.vertex_module);
            if (slot.fragment_module)
                m_device.destroyShaderModule(slot.fragment_module);
            if (slot.depth_module)
                m_device.destroyShaderModule(slot.depth_module);
            if (slot.params_ubo.buffer)
                m_dev->destroy_buffer(slot.params_ubo);
        }
        m_material_slots.clear();
        m_free_material_slots.clear();
        m_material_slot_by_asset.clear();

        for (auto& texture : m_gpu_textures)
            if (texture.alive) destroy_gpu_texture(texture);
        m_gpu_textures.clear();
        m_free_texture_slots.clear();
        m_texture_slot_by_asset.clear();
        m_texture_table.clear();

        for (auto& slot : m_mesh_slots)
            if (slot.alive)
                for (u32 material_slot : slot.material_slots)
                    release_material(material_slot);

        m_mesh_slots.clear();
        m_free_slots.clear();
        m_failed_mesh_loads.clear();
        m_mesh_id_by_asset.clear();

        if (m_vertex_buffer.buffer) {
            m_dev->destroy_buffer(m_vertex_buffer);
            m_vertex_buffer = {}; 
        }
        if (m_index_buffer.buffer) {
            m_dev->destroy_buffer(m_index_buffer);
            m_index_buffer = {}; 
        }
        if (m_mesh_data_buffer.buffer) {
            m_dev->destroy_buffer(m_mesh_data_buffer);
            m_mesh_data_buffer = {}; 
        }
        if (m_submesh_data_buffer.buffer) {
            m_dev->destroy_buffer(m_submesh_data_buffer);
            m_submesh_data_buffer = {}; 
        }

        destroy_depth_only_fragment_module();
        destroy_layouts();
        destroy_samplers();
        destroy_shader_compiler();
        destroy_immediate_submit();

        m_vertex_pool = {};
        m_index_pool = {};
        m_submesh_pool = {};
        m_live_buffer_count = 0;
        m_live_pipeline_count = 0;
        m_live_descriptor_set_count = 0;

        m_retire.shutdown();                    // runs anything the teardown above retired (device still idle)

        LOG(trace, "shared_resource_manager destroyed");
    }


    void shared_resource_manager::begin_frame(u64 /*frame_number*/) {
        GLT_PROFILE_SCOPE("shared_resource_manager::begin_frame");

        m_retire.collect();                     // destroy everything the GPU is done with

        m_last_gpu_stats = std::move(m_accum_gpu_stats);
        m_accum_gpu_stats = {};

        m_frames_since_compact++;

        if ((++m_material_poll_counter % MATERIAL_POLL_INTERVAL) == 0)
            poll_material_changes();

        // Opportunistic compaction: a growable pool is >2x its live size && at least 10 frames between hiccups
        if (should_opportunistic_compact() && can_compact()) {

            try_compact();
            shrink_pools();
        }
    }


    void shared_resource_manager::create_immediate_submit() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_immediate_submit");
        vk::CommandPoolCreateInfo cpi{};
        cpi.queueFamilyIndex = m_graphics_family;
        cpi.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_immediate_pool = m_device.createCommandPool(cpi);

        vk::CommandBufferAllocateInfo cai{};
        cai.commandPool = m_immediate_pool;
        cai.level = vk::CommandBufferLevel::ePrimary;
        cai.commandBufferCount = 1;
        m_immediate_cmd = m_device.allocateCommandBuffers(cai)[0];

        vk::FenceCreateInfo fci{};
        fci.flags = vk::FenceCreateFlagBits::eSignaled;
        m_immediate_fence = m_device.createFence(fci);
    }


    void shared_resource_manager::destroy_immediate_submit() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_immediate_submit");
        if (m_immediate_fence) { m_device.destroyFence(m_immediate_fence);       m_immediate_fence = nullptr; }
        if (m_immediate_pool)  { m_device.destroyCommandPool(m_immediate_pool);  m_immediate_pool = nullptr; m_immediate_cmd = nullptr; }
    }


    void shared_resource_manager::immediate_submit(std::function<void(vk::CommandBuffer)>&& fn) {
        GLT_PROFILE_SCOPE("Immediate submit (upload)");
        VK_SRM_CHECK(m_device.waitForFences(m_immediate_fence, VK_TRUE, UINT64_MAX));
        m_device.resetFences(m_immediate_fence);
        m_immediate_cmd.reset();
        m_immediate_cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        fn(m_immediate_cmd);
        m_immediate_cmd.end();

        vk::CommandBufferSubmitInfo cbsi{}; cbsi.commandBuffer = m_immediate_cmd;
        vk::SubmitInfo2 si{}; si.commandBufferInfoCount = 1; si.pCommandBufferInfos = &cbsi;
        m_graphics_queue.submit2(si, m_immediate_fence);
        GLT_PROFILE_WAIT("Wait for GPU (immediate submit)");
        VK_SRM_CHECK(m_device.waitForFences(m_immediate_fence, VK_TRUE, UINT64_MAX));
    }


    u32 shared_resource_manager::acquire_material(GLT::asset::handle handle) {
        GLT_PROFILE_SCOPE("shared_resource_manager::acquire_material");

        if (handle == INVALID_HANDLE)
            return DEFAULT_MATERIAL_SLOT;

        if (const auto it = m_material_slot_by_asset.find(handle); it != m_material_slot_by_asset.end()) {
            m_material_slots[it->second].ref_count++;
            return it->second;
        }

        u32 slot;
        if (!m_free_material_slots.empty()) {

            slot = m_free_material_slots.back();
            m_free_material_slots.pop_back();

        } else {

            slot = static_cast<u32>(m_material_slots.size());
            VALIDATE(slot < MAX_MATERIALS, return DEFAULT_MATERIAL_SLOT, "", "Material table full ({}), using default", (MAX_MATERIALS))
            m_material_slots.emplace_back();
        }

        material_slot& entry = m_material_slots[slot];
        entry = {};
        entry.asset = handle;
        entry.ref_count = 1;
        entry.alive = true;
        entry.texture_assets.fill(INVALID_HANDLE);
        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
            entry.texture_slots[i] = fallback_texture_slot_for(i);
        m_material_slot_by_asset[handle] = slot;

        const auto roll_back = [&]() -> u32 {
            m_material_slots[slot] = {};
            m_material_slot_by_asset.erase(handle);
            m_free_material_slots.push_back(slot);
            return DEFAULT_MATERIAL_SLOT;
        };

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, return roll_back(), "", "Material registry unavailable")
        const auto* asset = registry->data_as<GLT::asset::material::material_asset>(handle);
        VALIDATE(asset, return roll_back(), "", "Material [{}] not loaded", ((u64)handle))
        VALIDATE(!asset->bytecode.empty(), return roll_back(),
            "", "Material [{}] has no compiled bytecode", (registry->info(handle).name))

        entry.blend = asset->blend;
        entry.flags = asset->flags;
        pack_material(slot);

        if (!entry.pipeline) {
            LOG(warn, "Material [{}] failed to build a pipeline, using fallback", registry->info(handle).name);
            m_material_slot_by_asset.erase(handle);
            destroy_material_shaders(entry);
            for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
                if (entry.texture_assets[i] != INVALID_HANDLE)
                    release_texture_internal(entry.texture_slots[i]);
            if (entry.params_ubo.buffer) m_dev->destroy_buffer(entry.params_ubo);
            m_material_slots[slot] = {};
            m_free_material_slots.push_back(slot);
            return DEFAULT_MATERIAL_SLOT;
        }
        return slot;
    }


    void shared_resource_manager::release_material(u32 slot) {
        GLT_PROFILE_SCOPE("shared_resource_manager::release_material");
        if (slot == DEFAULT_MATERIAL_SLOT || slot >= m_material_slots.size() || !m_material_slots[slot].alive)
            return;
        material_slot& entry = m_material_slots[slot];
        if (--entry.ref_count > 0)
            return;
        destroy_material_shaders(entry);
        if (entry.params_ubo.buffer) { retire_buffer(entry.params_ubo); entry.params_size = 0; }
        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
            if (entry.texture_assets[i] != INVALID_HANDLE)
                release_texture_internal(entry.texture_slots[i]);
        m_material_slot_by_asset.erase(entry.asset);
        entry = {};
        m_free_material_slots.push_back(slot);
    }


    const material_slot& shared_resource_manager::get_material(u32 slot) const {
        ASSERT(slot < m_material_slots.size(), "", "material slot out of range")
        return m_material_slots[slot];
    }


    void shared_resource_manager::poll_material_changes() {
        GLT_PROFILE_SCOPE("shared_resource_manager::poll_material_changes");
        for (u32 slot = 1; slot < m_material_slots.size(); ++slot)
            if (m_material_slots[slot].alive)
                pack_material(slot);
    }


    void shared_resource_manager::write_material_descriptor(util::descriptor_buffer& target, u32 slot_index) const {
        GLT_PROFILE_SCOPE("shared_resource_manager::write_material_descriptor");

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        const material_slot& slot = m_material_slots[slot_index];

        std::array<util::accessible_image, 6> images{};
        for (u32 t = 0; t < 6; ++t) {
            images[t] = m_texture_table[slot.texture_slots[t]];
            images[t].sampler = nullptr;
        }

        std::vector<util::descriptor_item> items;
        items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, const_cast<util::allocated_buffer*>(&slot.params_ubo));
        for (u32 t = 0; t < 6; ++t)
            items.emplace_back(1 + t, vk::DescriptorType::eSampledImage, VKSF::eFragment, 1, &images[t]);

        util::accessible_image sampler_image{};
        sampler_image.sampler = m_material_sampler;
        items.emplace_back(7, vk::DescriptorType::eSampler, VKSF::eFragment, 1, &sampler_image);

        m_dev->compute_descriptor_binding_offsets(m_material_set_layout, items);
        m_dev->update_descriptor_buffer(target, items, util::descriptor_buffer_type::combined, slot_index);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::create_layouts() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_layouts");
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        std::vector<util::descriptor_item> mesh_items;
        mesh_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, nullptr);
        mesh_items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(7, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(8, VKDT::eSampler, VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(9, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);
        mesh_items.emplace_back(10, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);     // prefiltered specular cube
        mesh_items.emplace_back(11, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);    // SH9 irradiance
        mesh_items.emplace_back(12, VKDT::eSampler, VKSF::eFragment, 1, nullptr);          // env sampler
        m_mesh_set_layout = m_dev->create_descriptor_set_layout(mesh_items);
        m_live_descriptor_set_count++;

        std::vector<util::descriptor_item> mat_items;
        mat_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, nullptr);
        for (u32 t = 0; t < 6; ++t)
            mat_items.emplace_back(1 + t, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
        mat_items.emplace_back(7, VKDT::eSampler, VKSF::eFragment, 1, nullptr);
        m_material_set_layout = m_dev->create_descriptor_set_layout(mat_items);
        m_live_descriptor_set_count++;

        const std::array<vk::DescriptorSetLayout, 2> sets = { m_mesh_set_layout, m_material_set_layout };
        vk::PipelineLayoutCreateInfo pli{};
        pli.setLayoutCount = static_cast<u32>(sets.size());
        pli.pSetLayouts = sets.data();
        m_mesh_pipeline_layout = m_device.createPipelineLayout(pli);
    }


    void shared_resource_manager::destroy_layouts() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_layouts");
        if (m_mesh_pipeline_layout) { m_device.destroyPipelineLayout(m_mesh_pipeline_layout); m_mesh_pipeline_layout = nullptr; }
        if (m_mesh_set_layout)      { m_device.destroyDescriptorSetLayout(m_mesh_set_layout); m_mesh_set_layout = nullptr; m_live_descriptor_set_count--; }
        if (m_material_set_layout)  { m_device.destroyDescriptorSetLayout(m_material_set_layout); m_material_set_layout = nullptr; m_live_descriptor_set_count--; }
    }


    // The fragment shader of every depth-only material pipeline without alpha test (see create_material_pipeline)
    void shared_resource_manager::create_depth_only_fragment_module() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_depth_only_fragment_module");

        m_depth_only_fragment_module = m_dev->create_shader_module(std::span<const u32>(depth_only_fragment_spirv));
        if (!m_depth_only_fragment_module)
            LOG(error, "Failed to create the depth-only fragment shader: depth pipelines keep the per-material no-op shader")
    }


    void shared_resource_manager::destroy_depth_only_fragment_module() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_depth_only_fragment_module");

        if (m_depth_only_fragment_module) {
            m_device.destroyShaderModule(m_depth_only_fragment_module);
            m_depth_only_fragment_module = nullptr;
        }
    }


    void shared_resource_manager::create_samplers() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_samplers");
        const f32 max_aniso = m_physical_device.getProperties().limits.maxSamplerAnisotropy;

        vk::SamplerCreateInfo mat{};
        mat.magFilter = vk::Filter::eLinear;
        mat.minFilter = vk::Filter::eLinear;
        mat.mipmapMode = vk::SamplerMipmapMode::eLinear;
        mat.addressModeU = vk::SamplerAddressMode::eRepeat;
        mat.addressModeV = vk::SamplerAddressMode::eRepeat;
        mat.addressModeW = vk::SamplerAddressMode::eRepeat;
        mat.anisotropyEnable = VK_TRUE;
        mat.maxAnisotropy = std::min(8.0f, max_aniso);
        mat.maxLod = 1000.0f;
        m_material_sampler = m_device.createSampler(mat);

        vk::SamplerCreateInfo shadow{};
        shadow.magFilter = vk::Filter::eLinear;
        shadow.minFilter = vk::Filter::eLinear;
        shadow.mipmapMode = vk::SamplerMipmapMode::eNearest;
        shadow.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        shadow.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        shadow.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        shadow.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        shadow.compareEnable = VK_TRUE;
        shadow.compareOp = vk::CompareOp::eLessOrEqual;
        shadow.maxLod = 0.0f;
        m_shadow_sampler = m_device.createSampler(shadow);

        vk::SamplerCreateInfo depth{};
        depth.magFilter = vk::Filter::eNearest;
        depth.minFilter = vk::Filter::eNearest;
        depth.mipmapMode = vk::SamplerMipmapMode::eNearest;
        depth.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        depth.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        depth.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        depth.maxLod = 0.0f;
        m_depth_sampler = m_device.createSampler(depth);
    }


    void shared_resource_manager::destroy_samplers() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_samplers");

        if (m_material_sampler) {
            m_device.destroySampler(m_material_sampler);
            m_material_sampler = nullptr;
        }
        if (m_shadow_sampler)   {
            m_device.destroySampler(m_shadow_sampler);
            m_shadow_sampler = nullptr;
        }
        if (m_depth_sampler)    {
            m_device.destroySampler(m_depth_sampler);
            m_depth_sampler = nullptr;
        }
    }


    void shared_resource_manager::install_fallback_material() {
        GLT_PROFILE_SCOPE("shared_resource_manager::install_fallback_material");

        auto compiled = compile(GLT::asset::material::DEFAULT_SLANG_SOURCE, GLT::asset::material::blend_mode::opaque);
        if (!compiled) {
            LOG(fatal, "Failed to compile default material: {}", compiled.error().message);
            throw std::runtime_error("default material compilation failed");
        }

        material_slot& fb = m_material_slots[DEFAULT_MATERIAL_SLOT];

        std::vector<std::byte> vs, fs, ds;
        {
            ASSERT(compiled->bytecode.size() >= 3 * sizeof(u64), "", "Fallback bytecode too small")
            const u64 vs_s = GLT::util::get_bytes<u64>(compiled->bytecode, 0);
            const u64 fs_s = GLT::util::get_bytes<u64>(compiled->bytecode, sizeof(u64));
            const u64 ds_s = GLT::util::get_bytes<u64>(compiled->bytecode, 2 * sizeof(u64));
            ASSERT(3 * sizeof(u64) + vs_s + fs_s + ds_s <= compiled->bytecode.size(), "", "Fallback bytecode inconsistent")
            const std::byte* base = compiled->bytecode.data() + 3 * sizeof(u64);
            vs.assign(base, base + vs_s);
            fs.assign(base + vs_s, base + vs_s + fs_s);
            if (ds_s > 0) ds.assign(base + vs_s + fs_s, base + vs_s + fs_s + ds_s);
        }

        VALIDATE(!vs.empty(), return, "", "Fallback vertex shader empty")
        VALIDATE(!fs.empty(), return, "", "Fallback fragment shader empty")
        fb.vertex_module = m_dev->create_shader_module(std::span<const std::byte>(vs));
        fb.fragment_module = m_dev->create_shader_module(std::span<const std::byte>(fs));
        if (!ds.empty()) fb.depth_module = m_dev->create_shader_module(std::span<const std::byte>(ds));

        const u32 params_bytes = static_cast<u32>(compiled->default_params.size());
        const u32 alloc_size = std::max<u32>(params_bytes, 256);
        fb.params_ubo = m_dev->create_buffer(alloc_size, vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        fb.params_size = alloc_size;
        m_live_buffer_count++;
        if (params_bytes > 0)
            m_dev->update_buffer(fb.params_ubo, compiled->default_params.data(), params_bytes);

        fb.blend = GLT::asset::material::blend_mode::opaque;
        fb.depth_alpha_test = false;                    // compiled as opaque above: no alpha_test entry point
        fb.pipeline = create_material_pipeline(fb, material_pipeline_kind::main);
        fb.depth_pipeline = create_material_pipeline(fb, material_pipeline_kind::depth);
        if (fb.pipeline)
            m_live_pipeline_count++;
        if (fb.depth_pipeline)
            m_live_pipeline_count++;
    }


    void shared_resource_manager::pack_material(u32 slot_index) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pack_material");

        material_slot& slot = m_material_slots[slot_index];
        if (slot.asset == INVALID_HANDLE || !slot.alive)
            return;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;
        const auto* asset = registry->data_as<GLT::asset::material::material_asset>(slot.asset);
        if (!asset)
            return;

        slot.blend = asset->blend;
        slot.flags = asset->flags;

        if (asset->bytecode_hash != slot.bytecode_hash) {
            destroy_material_shaders(slot);
            bool ok = false;
            if (asset->bytecode_hash != 0)
                ok = load_material_shaders(slot, *asset);
            if (ok)
                slot.bytecode_hash = asset->bytecode_hash;

        } else if (slot.pipeline && (slot.pipeline_blend != slot.blend || slot.pipeline_flags != slot.flags)) {

            // Blend mode / double-sided changed without new bytecode (e.g. toggled in the material editor): same shader
            // modules, new fixed-function state
            rebuild_material_pipelines(slot);
        }

        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i) {
            const auto wanted = asset->textures[i];
            if (wanted == slot.texture_assets[i])
                continue;
            if (slot.texture_assets[i] != INVALID_HANDLE)
                release_texture_internal(slot.texture_slots[i]);
            slot.texture_assets[i] = wanted;
            slot.texture_slots[i] = (wanted == INVALID_HANDLE) ? fallback_texture_slot_for(i)
                : acquire_texture_internal(wanted);
        }

        // Params: only touch the GPU when they actually changed, and never write into a UBO that in-flight frames may still
        // read (this ran every MATERIAL_POLL_INTERVAL frames and rewrote it in place). A change gets a fresh UBO; the old
        // one is retired. Material descriptors are rewritten every frame, so they pick up the new buffer
        const u32 needed = static_cast<u32>(asset->packed_params.size());
        const u64 params_hash = needed > 0
            ? GLT::util::fnv1a_64(std::string_view(reinterpret_cast<const char*>(asset->packed_params.data()), needed)) | 1ull
            : 1ull;                                                         // | 1: never 0, so "no upload yet" stays distinct
        if (!slot.params_ubo.buffer || params_hash != slot.params_hash) {
            const bool had_buffer = slot.params_ubo.buffer != nullptr;
            if (had_buffer)
                retire_buffer(slot.params_ubo);                             // previous frames may still read it
            const u32 alloc_size = std::max<u32>(needed, 256);
            slot.params_ubo = m_dev->create_buffer(alloc_size, vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            slot.params_size = alloc_size;
            if (!had_buffer)
                m_live_buffer_count++;
            if (needed > 0)
                m_dev->update_buffer(slot.params_ubo, asset->packed_params.data(), needed);
            slot.params_hash = params_hash;
        }
    }


    // Pipelines may still be bound by in-flight command buffers (material released or hot-reloaded this frame), so the
    // handles are moved into the retire queue instead of being destroyed immediately
    void shared_resource_manager::destroy_material_shaders(material_slot& slot) {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_material_shaders");

        const vk::Pipeline pipeline = std::exchange(slot.pipeline, nullptr);
        const vk::Pipeline depth_pipeline = std::exchange(slot.depth_pipeline, nullptr);
        const vk::ShaderModule vertex_module = std::exchange(slot.vertex_module, nullptr);
        const vk::ShaderModule fragment_module = std::exchange(slot.fragment_module, nullptr);
        const vk::ShaderModule depth_module = std::exchange(slot.depth_module, nullptr);

        if (pipeline)       m_live_pipeline_count--;
        if (depth_pipeline) m_live_pipeline_count--;

        if (!pipeline && !depth_pipeline && !vertex_module && !fragment_module && !depth_module)
            return;

        m_retire.retire([device = m_device, pipeline, depth_pipeline, vertex_module, fragment_module, depth_module]() {
            if (pipeline)        device.destroyPipeline(pipeline);
            if (depth_pipeline)  device.destroyPipeline(depth_pipeline);
            if (vertex_module)   device.destroyShaderModule(vertex_module);
            if (fragment_module) device.destroyShaderModule(fragment_module);
            if (depth_module)    device.destroyShaderModule(depth_module);
        });
    }


    void shared_resource_manager::retire_buffer(util::allocated_buffer& buffer) {
        GLT_PROFILE_SCOPE("shared_resource_manager::retire_buffer");

        if (!buffer.buffer)
            return;

        m_retire.retire([dev = m_dev, dead = buffer]() mutable { dev->destroy_buffer(dead); });
        buffer = {};
    }


    void shared_resource_manager::report_gpu_frame(const gpu_frame_report& report) {
        GLT_PROFILE_SCOPE("shared_resource_manager::report_gpu_frame");

        m_accum_gpu_stats.gpu_time_ms += report.gpu_time_ms;
        m_accum_gpu_stats.render_passes += report.render_passes;
        m_accum_gpu_stats.draw_calls += report.draw_calls;
        m_accum_gpu_stats.triangles += report.triangles;

        // Merge by pass name so several scene renderers (editor viewports) sum up instead of producing duplicate rows
        for (const auto& [name, ms] : report.pass_times_ms) {
            auto it = std::find_if(m_accum_gpu_stats.pass_times_ms.begin(), m_accum_gpu_stats.pass_times_ms.end(),
                [&](const auto& entry) { return entry.first == name; });
            if (it != m_accum_gpu_stats.pass_times_ms.end())
                it->second += ms;
            else
                m_accum_gpu_stats.pass_times_ms.emplace_back(name, ms);
        }
    }


    bool shared_resource_manager::load_material_shaders(material_slot& slot, const GLT::asset::material::material_asset& asset) {
        GLT_PROFILE_SCOPE("shared_resource_manager::load_material_shaders");

        if (asset.bytecode.size() < 3 * sizeof(u64)) return false;

        std::vector<std::byte> vs, fs, ds;
        {
            const auto* blob = reinterpret_cast<const u64*>(asset.bytecode.data());
            const u64 vs_s = blob[0], fs_s = blob[1], ds_s = blob[2];
            const std::byte* base = asset.bytecode.data() + 3 * sizeof(u64);
            if (3 * sizeof(u64) + vs_s + fs_s + ds_s > asset.bytecode.size()) return false;
            vs.assign(base, base + vs_s);
            fs.assign(base + vs_s, base + vs_s + fs_s);
            if (ds_s > 0) ds.assign(base + vs_s + fs_s, base + vs_s + fs_s + ds_s);
        }

        if (vs.empty() || fs.empty()) return false;

        slot.vertex_module = m_dev->create_shader_module(std::span<const std::byte>(vs));
        slot.fragment_module = m_dev->create_shader_module(std::span<const std::byte>(fs));
        if (!slot.vertex_module || !slot.fragment_module) { destroy_material_shaders(slot); return false; }

        if (!ds.empty()) {
            slot.depth_module = m_dev->create_shader_module(std::span<const std::byte>(ds));
            if (!slot.depth_module) { destroy_material_shaders(slot); return false; }
        }

        slot.depth_alpha_test = bytecode_has_alpha_test(asset);
        slot.pipeline = create_material_pipeline(slot, material_pipeline_kind::main);
        slot.depth_pipeline = create_material_pipeline(slot, material_pipeline_kind::depth);
        if (!slot.pipeline || !slot.depth_pipeline) { destroy_material_shaders(slot); return false; }
        m_live_pipeline_count += 2;
        slot.pipeline_blend = slot.blend;
        slot.pipeline_flags = slot.flags;
        return true;
    }


    void shared_resource_manager::rebuild_material_pipelines(material_slot& slot) {
        GLT_PROFILE_SCOPE("shared_resource_manager::rebuild_material_pipelines");

        const vk::Pipeline main = create_material_pipeline(slot, material_pipeline_kind::main);
        const vk::Pipeline depth = create_material_pipeline(slot, material_pipeline_kind::depth);
        if (!main || !depth) {
            if (main)  m_device.destroyPipeline(main);
            if (depth) m_device.destroyPipeline(depth);
            LOG(warn, "Failed to rebuild pipelines after a blend/flag change, keeping the previous state")
            slot.pipeline_blend = slot.blend;           // don't retry every poll
            slot.pipeline_flags = slot.flags;
            return;
        }

        // Old pipelines may still be bound by in-flight frames
        m_retire.retire([device = m_device, old_main = slot.pipeline, old_depth = slot.depth_pipeline]() {
            if (old_main)  device.destroyPipeline(old_main);
            if (old_depth) device.destroyPipeline(old_depth);
        });
        slot.pipeline = main;
        slot.depth_pipeline = depth;
        slot.pipeline_blend = slot.blend;
        slot.pipeline_flags = slot.flags;
    }


    // Blend state per translucent mode, targeting the linear HDR scene colour. Alpha accumulates coverage
    // (scene_hdr is cleared to alpha 0), which the tonemap pass uses to composite translucents over the background
    static vk::PipelineColorBlendAttachmentState translucent_blend_state(GLT::asset::material::blend_mode blend) {

        using bm = GLT::asset::material::blend_mode;
        using BF = vk::BlendFactor;

        vk::PipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_TRUE;
        cb.colorBlendOp = vk::BlendOp::eAdd;
        cb.alphaBlendOp = vk::BlendOp::eAdd;
        cb.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        auto set = [&](BF src_c, BF dst_c, BF src_a, BF dst_a) {
            cb.srcColorBlendFactor = src_c; cb.dstColorBlendFactor = dst_c;
            cb.srcAlphaBlendFactor = src_a; cb.dstAlphaBlendFactor = dst_a;
        };

        switch (blend) {
            case bm::additive:          set(BF::eSrcAlpha, BF::eOne,                    BF::eZero, BF::eOne);                  break;  // rgb * opacity added
            case bm::modulate:          set(BF::eDstColor, BF::eZero,                   BF::eZero, BF::eOne);                  break;  // dst * rgb
            case bm::alpha_composite:   set(BF::eOne,      BF::eOneMinusSrcAlpha,       BF::eOne,  BF::eOneMinusSrcAlpha);     break;  // premultiplied
            case bm::alpha_holdout:     set(BF::eZero,     BF::eOneMinusSrcAlpha,       BF::eZero, BF::eOneMinusSrcAlpha);     break;  // cut out
            case bm::translucent:
            default:                    set(BF::eSrcAlpha, BF::eOneMinusSrcAlpha,       BF::eOne,  BF::eOneMinusSrcAlpha);     break;  // classic "over"
        }
        return cb;
    }


    vk::Pipeline shared_resource_manager::create_material_pipeline(const material_slot& slot, material_pipeline_kind kind) {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_material_pipeline");

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = slot.vertex_module;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = (kind == material_pipeline_kind::main) ? slot.fragment_module : slot.depth_module;
        stages[1].pName = "main";

        // Depth-only without alpha test (pre-pass, shadow maps): the per-material no-op shader contains a `discard` that
        // never fires (it only keeps Slang from emitting an empty entry point, see shading_strings::depth_noop_suffix). A
        // shader that may discard has to run for every fragment before depth can be written. The empty shader cannot
        // discard, so the GPU writes depth straight from the rasteriser. Same vertex shader, same positions, same depth
        if (kind == material_pipeline_kind::depth && !slot.depth_alpha_test && m_depth_only_fragment_module)
            stages[1].module = m_depth_only_fragment_module;

        vk::PipelineVertexInputStateCreateInfo vi{};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vertex_binding;
        vi.vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size());
        vi.pVertexAttributeDescriptions = vertex_attributes.data();

        vk::PipelineInputAssemblyStateCreateInfo ia{};
        ia.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo vp{};
        vp.viewportCount = 1; vp.scissorCount = 1;

        const bool translucent = slot.is_translucent();

        vk::PipelineRasterizationStateCreateInfo rs{};
        rs.polygonMode = vk::PolygonMode::eFill;
        rs.cullMode = slot.is_double_sided() ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
        rs.frontFace = vk::FrontFace::eCounterClockwise;
        rs.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo ms{};
        ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Opaque / masked main pass: depth equal against the pre-pass. Translucent: not in the pre-pass, so test against
        // the opaque depth (LESS_OR_EQUAL) without writing it
        vk::PipelineDepthStencilStateCreateInfo ds{};
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = (kind == material_pipeline_kind::depth);
        if (kind == material_pipeline_kind::main)
            ds.depthCompareOp = translucent ? vk::CompareOp::eLessOrEqual : vk::CompareOp::eEqual;
        else
            ds.depthCompareOp = vk::CompareOp::eLess;

        vk::PipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        if (kind == material_pipeline_kind::main && translucent)
            cb = translucent_blend_state(slot.blend);
        vk::PipelineColorBlendStateCreateInfo cbs{};
        if (kind == material_pipeline_kind::main) { cbs.attachmentCount = 1; cbs.pAttachments = &cb; }

        vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dyn_state{};
        dyn_state.dynamicStateCount = 2; dyn_state.pDynamicStates = dyn;

        vk::PipelineRenderingCreateInfo ri{};
        const vk::Format color_format = HDR_COLOR_FORMAT;          // linear HDR; the tonemap pass converts to display
        const vk::Format depth_format = vk::Format::eD32Sfloat;
        if (kind == material_pipeline_kind::main) {
            ri.colorAttachmentCount = 1;
            ri.pColorAttachmentFormats = &color_format;
        }
        ri.depthAttachmentFormat = depth_format;

        vk::GraphicsPipelineCreateInfo info{};
        info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;      // bound via descriptor buffers (spec: mandatory)
        info.pNext = &ri;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vi;
        info.pInputAssemblyState = &ia;
        info.pViewportState = &vp;
        info.pRasterizationState = &rs;
        info.pMultisampleState = &ms;
        info.pDepthStencilState = &ds;
        info.pColorBlendState = &cbs;
        info.pDynamicState = &dyn_state;
        info.layout = m_mesh_pipeline_layout;

        auto [result, pipeline] = m_device.createGraphicsPipeline(nullptr, info);
        VALIDATE(result == vk::Result::eSuccess, return nullptr, "", "material pipeline creation failed")
        return pipeline;
    }

}
