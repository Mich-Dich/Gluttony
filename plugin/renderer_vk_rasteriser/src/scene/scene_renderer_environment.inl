#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Push constants of sky.comp.glsl
    struct sky_push_constants {

        glm::vec4                                       sun_dir{0.f, 1.f, 0.f, 0.f};    // xyz towards the sun
        glm::vec4                                       sun_radiance{1.f};              // rgb scale of the unit-irradiance model
        glm::vec4                                       params{0.f};                    // x mie scale, y ground albedo, z face size
    };
    static_assert(sizeof(sky_push_constants) == 48, "sky_push_constants layout mismatch");


    // Push constants of env_prefilter.comp.glsl
    struct env_prefilter_push_constants {

        f32                                             roughness = 0.f;
        u32                                             size = 0;
        u32                                             sample_count = 0;
        u32                                             _pad = 0;
    };
    static_assert(sizeof(env_prefilter_push_constants) == 16, "env_prefilter_push_constants layout mismatch");


    // Push constants of sky_background.frag.glsl
    struct sky_background_push_constants {

        glm::mat4                                       inv_view_proj{1.f};
        glm::vec4                                       sun_dir{0.f};       // xyz towards the sun, w = cos(angular radius)
        glm::vec4                                       sun_disk{0.f};      // rgb = disk radiance, w = viewport width
        glm::vec4                                       params{0.f};        // x = viewport height
    };
    static_assert(sizeof(sky_background_push_constants) == 112, "sky_background_push_constants layout mismatch");

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    namespace env_detail {

        // Same atmosphere as sky.comp.glsl. Transmittance from the viewer towards the sun, used to tint the sun DISK (the
        // directional light itself is whatever the scene says)
        inline glm::vec3 sun_transmittance(const glm::vec3& sun, f32 mie_scale) {

            constexpr f32 earth_radius = 6360e3f;
            constexpr f32 atmosphere_radius = 6420e3f;
            constexpr f32 rayleigh_h = 8000.f;
            constexpr f32 mie_h = 1200.f;
            const glm::vec3 rayleigh_beta(5.802e-6f, 13.558e-6f, 33.1e-6f);
            constexpr f32 mie_extinction = 3.996e-6f * 1.11f;

            const glm::vec3 origin(0.f, earth_radius + 50.f, 0.f);
            const f32 b = glm::dot(origin, sun);
            const f32 c_ground = glm::dot(origin, origin) - earth_radius * earth_radius;
            const f32 d_ground = b * b - c_ground;
            if (d_ground >= 0.f && -b - std::sqrt(d_ground) > 0.f)
                return glm::vec3(0.f);                                  // below the horizon

            const f32 c = glm::dot(origin, origin) - atmosphere_radius * atmosphere_radius;
            const f32 t_max = -b + std::sqrt(std::max(b * b - c, 0.f));
            constexpr u32 steps = 16;
            const f32 ds = t_max / static_cast<f32>(steps);
            f32 depth_r = 0.f, depth_m = 0.f;
            for (u32 i = 0; i < steps; ++i) {
                const glm::vec3 p = origin + sun * (ds * (static_cast<f32>(i) + 0.5f));
                const f32 h = glm::length(p) - earth_radius;
                depth_r += std::exp(-h / rayleigh_h) * ds;
                depth_m += std::exp(-h / mie_h) * ds;
            }
            return glm::exp(-(rayleigh_beta * depth_r + glm::vec3(mie_extinction * mie_scale * depth_m)));
        }


        inline u64 hash_floats(std::initializer_list<f32> values) {

            u64 h = 1469598103934665603ull;
            for (f32 v : values) {
                const i32 q = static_cast<i32>(std::lround(v * 4096.f));     // ignore sub-quantum jitter
                h ^= static_cast<u64>(static_cast<u32>(q)) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            }
            return h;
        }


        inline vk::Pipeline create_compute(vk::Device device, util::device* dev, vk::PipelineLayout layout,
            const std::vector<u32>& spv, const char* what) {

            ASSERT(!spv.empty(), "", "Failed to compile {} shader", (what))
            vk::ShaderModule module = dev->create_shader_module(spv);
            vk::ComputePipelineCreateInfo ci{};
            ci.layout = layout;
            ci.stage.stage = vk::ShaderStageFlagBits::eCompute;
            ci.stage.module = module;
            ci.stage.pName = "main";
            auto result = device.createComputePipeline(nullptr, ci);
            ASSERT(result.result == vk::Result::eSuccess, "", "Failed to create {} pipeline", (what))
            device.destroyShaderModule(module);
            return result.value;
        }


        inline vk::PipelineLayout create_layout(vk::Device device, vk::DescriptorSetLayout set_layout,
            vk::ShaderStageFlags stages, u32 push_size) {

            vk::PushConstantRange range{};
            range.stageFlags = stages;
            range.offset = 0;
            range.size = push_size;
            vk::PipelineLayoutCreateInfo pli{};
            pli.setLayoutCount = 1;
            pli.pSetLayouts = &set_layout;
            pli.pushConstantRangeCount = push_size > 0 ? 1 : 0;
            pli.pPushConstantRanges = push_size > 0 ? &range : nullptr;
            return device.createPipelineLayout(pli);
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // pipelines + resources -------------------------------------------------------------------------------------------

    void scene_renderer::create_environment_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::create_environment_resources");

        auto* dev = m_plugin.get_dev();
        const vk::Device device = m_plugin.get_vk_device();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        constexpr auto bt = util::descriptor_buffer_type::combined;

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader" / "vk_rasterizer";
        auto& compiler = m_plugin.get_shader_compiler_ref();

        // sky.comp: 0 = storage cube (as 2D array) ------------------------------------------------------------------
        std::vector<util::descriptor_item> sky_items;
        sky_items.emplace_back(0, VKDT::eStorageImage, VKSF::eCompute, 1, nullptr);
        m_sky_set_layout = dev->create_descriptor_set_layout(sky_items);
        m_sky_pipeline_layout = env_detail::create_layout(device, m_sky_set_layout, VKSF::eCompute, sizeof(sky_push_constants));
        m_sky_pipeline = env_detail::create_compute(device, dev, m_sky_pipeline_layout,
            compiler.compile_glsl_to_spirv(shader_dir / "sky.comp.glsl"), "sky");

        // env_prefilter.comp: 0 = sky cube (sampled), 1 = output mip (storage 2D array) -------------------------------
        std::vector<util::descriptor_item> prefilter_items;
        prefilter_items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eCompute, 1, nullptr);
        prefilter_items.emplace_back(1, VKDT::eStorageImage, VKSF::eCompute, 1, nullptr);
        m_env_prefilter_set_layout = dev->create_descriptor_set_layout(prefilter_items);
        m_env_prefilter_pipeline_layout = env_detail::create_layout(device, m_env_prefilter_set_layout, VKSF::eCompute,
            sizeof(env_prefilter_push_constants));
        m_env_prefilter_pipeline = env_detail::create_compute(device, dev, m_env_prefilter_pipeline_layout,
            compiler.compile_glsl_to_spirv(shader_dir / "env_prefilter.comp.glsl"), "env_prefilter");

        // env_sh.comp: 0 = sky cube (sampled), 1 = SH buffer ---------------------------------------------------------
        std::vector<util::descriptor_item> sh_items;
        sh_items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eCompute, 1, nullptr);
        sh_items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, nullptr);
        m_env_sh_set_layout = dev->create_descriptor_set_layout(sh_items);
        m_env_sh_pipeline_layout = env_detail::create_layout(device, m_env_sh_set_layout, VKSF::eCompute, 0);
        m_env_sh_pipeline = env_detail::create_compute(device, dev, m_env_sh_pipeline_layout,
            compiler.compile_glsl_to_spirv(shader_dir / "env_sh.comp.glsl"), "env_sh");

        // sky background: fullscreen triangle at the far plane --------------------------------------------------------
        std::vector<util::descriptor_item> bg_items;
        bg_items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eFragment, 1, nullptr);
        m_sky_bg_set_layout = dev->create_descriptor_set_layout(bg_items);
        m_sky_bg_pipeline_layout = env_detail::create_layout(device, m_sky_bg_set_layout, VKSF::eFragment,
            sizeof(sky_background_push_constants));

        {
            auto vs_spv = compiler.compile_glsl_to_spirv(shader_dir / "sky_background.vert.glsl");
            auto fs_spv = compiler.compile_glsl_to_spirv(shader_dir / "sky_background.frag.glsl");
            ASSERT(!vs_spv.empty() && !fs_spv.empty(), "", "Failed to compile sky background shaders")
            vk::ShaderModule vs = dev->create_shader_module(vs_spv);
            vk::ShaderModule fs = dev->create_shader_module(fs_spv);

            vk::PipelineShaderStageCreateInfo stages[2]{};
            stages[0].stage = VKSF::eVertex;   stages[0].module = vs; stages[0].pName = "main";
            stages[1].stage = VKSF::eFragment; stages[1].module = fs; stages[1].pName = "main";

            vk::PipelineVertexInputStateCreateInfo vi{};
            vk::PipelineInputAssemblyStateCreateInfo ia{};
            ia.topology = vk::PrimitiveTopology::eTriangleList;
            vk::PipelineViewportStateCreateInfo vp{};
            vp.viewportCount = 1; vp.scissorCount = 1;
            vk::PipelineRasterizationStateCreateInfo rs{};
            rs.polygonMode = vk::PolygonMode::eFill;
            rs.cullMode = vk::CullModeFlagBits::eNone;
            rs.frontFace = vk::FrontFace::eCounterClockwise;
            rs.lineWidth = 1.0f;
            vk::PipelineMultisampleStateCreateInfo ms{};
            ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

            // Only where the depth buffer still holds the clear value (1.0): no geometry there
            vk::PipelineDepthStencilStateCreateInfo ds{};
            ds.depthTestEnable = VK_TRUE;
            ds.depthWriteEnable = VK_FALSE;
            ds.depthCompareOp = vk::CompareOp::eLessOrEqual;

            vk::PipelineColorBlendAttachmentState cb{};
            cb.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo cbs{};
            cbs.attachmentCount = 1;
            cbs.pAttachments = &cb;

            vk::DynamicState dyn[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dyn_state{};
            dyn_state.dynamicStateCount = 2;
            dyn_state.pDynamicStates = dyn;

            const vk::Format color_format = HDR_COLOR_FORMAT;
            vk::PipelineRenderingCreateInfo ri{};
            ri.colorAttachmentCount = 1;
            ri.pColorAttachmentFormats = &color_format;
            ri.depthAttachmentFormat = vk::Format::eD32Sfloat;

            vk::GraphicsPipelineCreateInfo pi{};
            pi.pNext = &ri;
            pi.stageCount = 2;
            pi.pStages = stages;
            pi.pVertexInputState = &vi;
            pi.pInputAssemblyState = &ia;
            pi.pViewportState = &vp;
            pi.pRasterizationState = &rs;
            pi.pMultisampleState = &ms;
            pi.pDepthStencilState = &ds;
            pi.pColorBlendState = &cbs;
            pi.pDynamicState = &dyn_state;
            pi.layout = m_sky_bg_pipeline_layout;

            auto [result, pipeline] = device.createGraphicsPipeline(nullptr, pi);
            ASSERT(result == vk::Result::eSuccess, "", "sky background pipeline creation failed")
            m_sky_bg_pipeline = pipeline;
            device.destroyShaderModule(vs);
            device.destroyShaderModule(fs);
        }

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {
            m_sky_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_sky_set_layout, sky_items, bt, 1);
            m_env_prefilter_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_env_prefilter_set_layout, prefilter_items,
                bt, ENV_SPECULAR_MIPS);
            m_env_sh_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_env_sh_set_layout, sh_items, bt, 1);
            m_sky_bg_desc_buffers[frame_index] = dev->create_descriptor_buffer(m_sky_bg_set_layout, bg_items, bt, 1);
        }

        m_env = {};
    }


    void scene_renderer::destroy_environment_resources() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_environment_resources");

        auto* dev = m_plugin.get_dev();
        const vk::Device device = m_plugin.get_vk_device();

        auto destroy_descs = [&](auto& buffers) {
            for (auto& d : buffers)
                if (d.buffer.buffer) {
                    dev->destroy_buffer(d.buffer);
                    d = {};
                }
        };
        destroy_descs(m_sky_desc_buffers);
        destroy_descs(m_env_prefilter_desc_buffers);
        destroy_descs(m_env_sh_desc_buffers);
        destroy_descs(m_sky_bg_desc_buffers);

        auto destroy_pipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l, vk::DescriptorSetLayout& s) {
            if (p) { device.destroyPipeline(p); p = nullptr; }
            if (l) { device.destroyPipelineLayout(l); l = nullptr; }
            if (s) { device.destroyDescriptorSetLayout(s); s = nullptr; }
        };
        destroy_pipeline(m_sky_pipeline, m_sky_pipeline_layout, m_sky_set_layout);
        destroy_pipeline(m_env_prefilter_pipeline, m_env_prefilter_pipeline_layout, m_env_prefilter_set_layout);
        destroy_pipeline(m_env_sh_pipeline, m_env_sh_pipeline_layout, m_env_sh_set_layout);
        destroy_pipeline(m_sky_bg_pipeline, m_sky_bg_pipeline_layout, m_sky_bg_set_layout);

        // m_env_sky / m_env_specular / m_env_sh are graph-owned (persistent) and destroyed with the graph
    }


    // Graph-owned, fixed size. Called every frame (persistent lookup is cheap); the targets only get (re)built on the first
    // frame, which update_environment() detects through the image handles
    void scene_renderer::ensure_environment_textures() {
        GLT_PROFILE_SCOPE("scene_renderer::ensure_environment_textures");

        using VKIU = vk::ImageUsageFlagBits;

        graph::texture_desc sky{};
        sky.name = "env_sky";
        sky.format = HDR_COLOR_FORMAT;
        sky.width = ENV_SKY_SIZE;
        sky.height = ENV_SKY_SIZE;
        sky.array_layers = 6;
        sky.cube = true;
        sky.usage = VKIU::eStorage | VKIU::eSampled;
        sky.persistent = true;
        m_env_sky = m_graph->create_texture(sky);

        graph::texture_desc spec{};
        spec.name = "env_specular";
        spec.format = HDR_COLOR_FORMAT;
        spec.width = ENV_SPECULAR_SIZE;
        spec.height = ENV_SPECULAR_SIZE;
        spec.mip_levels = ENV_SPECULAR_MIPS;
        spec.array_layers = 6;
        spec.cube = true;
        spec.usage = VKIU::eStorage | VKIU::eSampled;
        spec.persistent = true;
        m_env_specular = m_graph->create_texture(spec);

        graph::buffer_desc sh{};
        sh.name = "env_sh";
        sh.size = 9 * sizeof(glm::vec4);
        sh.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        sh.persistent = true;
        m_env_sh = m_graph->create_buffer(sh);
    }


    // Sun + sky parameters come from the world entity (environment_snapshot). Without a sun the sky uses a fixed default
    // one. Rebuilds the cubemaps only when something that affects them changed (sun moved, colour, haze, ...) or the
    // targets were recreated
    void scene_renderer::update_environment() {
        GLT_PROFILE_SCOPE("scene_renderer::update_environment");

        const auto& env = m_environment;
        if (env.has_sun) {
            const f32 len2 = glm::dot(env.sun_direction, env.sun_direction);
            m_env.sun_dir = len2 > 1e-6f ? -glm::normalize(env.sun_direction) : glm::vec3(0.f, 1.f, 0.f);
            m_env.sun_color = env.sun_color * std::max(env.sun_intensity, 0.f);
        } else {
            const f32 elevation = glm::radians(35.f);
            const f32 azimuth = glm::radians(30.f);
            m_env.sun_dir = glm::vec3(std::cos(elevation) * std::sin(azimuth), std::sin(elevation), std::cos(elevation) * std::cos(azimuth));
            m_env.sun_color = glm::vec3(3.f);
        }

        m_env.enabled = env.has_sky;

        const u64 hash = env_detail::hash_floats({
            m_env.sun_dir.x, m_env.sun_dir.y, m_env.sun_dir.z,
            m_env.sun_color.r, m_env.sun_color.g, m_env.sun_color.b,
            env.sky_intensity, env.sky_haze, env.sky_ground_albedo });

        const vk::Image sky_image = m_graph->texture_image(m_env_sky);
        const vk::Image specular_image = m_graph->texture_image(m_env_specular);
        m_env.dirty = m_env.dirty || hash != m_env.hash || sky_image != m_env.sky_image || specular_image != m_env.specular_image;
        m_env.hash = hash;
        m_env.sky_image = sky_image;
        m_env.specular_image = specular_image;
    }

    // passes ----------------------------------------------------------------------------------------------------------

    void scene_renderer::add_environment_passes() {
        GLT_PROFILE_SCOPE("scene_renderer::add_environment_passes");

        if (!m_env.dirty)
            return;
        m_env.dirty = false;

        auto* dev = m_plugin.get_dev();
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;
        constexpr auto bt = util::descriptor_buffer_type::combined;
        const u32 frame = m_current_frame;

        // Descriptors (written while recording the graph: the views are stable, this frame's buffers are not in flight)
        util::accessible_image sky_storage{};
        sky_storage.view = m_graph->texture_mip_array_view(m_env_sky, 0);
        sky_storage.layout = vk::ImageLayout::eGeneral;

        util::accessible_image sky_sampled{};
        sky_sampled.view = m_graph->texture_default_view(m_env_sky);
        sky_sampled.sampler = m_shared->get_material_sampler();
        sky_sampled.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        {
            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eStorageImage, VKSF::eCompute, 1, &sky_storage);
            dev->compute_descriptor_binding_offsets(m_sky_set_layout, items);
            dev->update_descriptor_buffer(m_sky_desc_buffers[frame], items, bt);
        }

        std::array<util::accessible_image, ENV_SPECULAR_MIPS> mip_storage{};
        for (u32 mip = 0; mip < ENV_SPECULAR_MIPS; ++mip) {
            mip_storage[mip].view = m_graph->texture_mip_array_view(m_env_specular, mip);
            mip_storage[mip].layout = vk::ImageLayout::eGeneral;
            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eCompute, 1, &sky_sampled);
            items.emplace_back(1, VKDT::eStorageImage, VKSF::eCompute, 1, &mip_storage[mip]);
            dev->compute_descriptor_binding_offsets(m_env_prefilter_set_layout, items);
            dev->update_descriptor_buffer(m_env_prefilter_desc_buffers[frame], items, bt, mip);
        }

        {
            auto* sh_buffer = m_graph->buffer_allocation(m_env_sh);
            ASSERT(sh_buffer, "", "Graph failed to allocate the SH buffer")
            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eCombinedImageSampler, VKSF::eCompute, 1, &sky_sampled);
            items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eCompute, 1, sh_buffer);
            dev->compute_descriptor_binding_offsets(m_env_sh_set_layout, items);
            dev->update_descriptor_buffer(m_env_sh_desc_buffers[frame], items, bt);
        }

        // engine light units are PI x physical radiance (Lambert without the 1/PI), see sky.comp.glsl
        sky_push_constants sky_push{};
        sky_push.sun_dir = glm::vec4(m_env.sun_dir, 0.f);
        sky_push.sun_radiance = glm::vec4(m_env.sun_color * glm::pi<f32>() * std::max(m_environment.sky_intensity, 0.f), 0.f);
        sky_push.params = glm::vec4(std::max(m_environment.sky_haze, 0.f), std::clamp(m_environment.sky_ground_albedo, 0.f, 1.f),
            static_cast<f32>(ENV_SKY_SIZE), 0.f);

        m_graph->add_pass("env_sky",
            [&](graph::pass_builder& builder) {
                builder.write(m_env_sky, graph::texture_usage::storage_readwrite);
                builder.set_side_effects(true);
            },
            [this, sky_push](graph::pass_context& ctx) {
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_sky_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ m_sky_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_sky_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);
                ctx.cmd.pushConstants(m_sky_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(sky_push), &sky_push);
                const u32 groups = (ENV_SKY_SIZE + 7) / 8;
                ctx.cmd.dispatch(groups, groups, 6);
            });

        // All mips in one pass: each dispatch writes its own subresource, no hazards between them
        m_graph->add_pass("env_prefilter",
            [&](graph::pass_builder& builder) {
                builder.read(m_env_sky, graph::texture_usage::sampled);
                builder.write(m_env_specular, graph::texture_usage::storage_readwrite);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_env_prefilter_pipeline);
                const auto& desc = m_env_prefilter_desc_buffers[m_current_frame];
                m_plugin.get_dev()->bind_descriptor_buffer({ desc }, ctx.cmd);
                for (u32 mip = 0; mip < ENV_SPECULAR_MIPS; ++mip) {
                    m_plugin.get_dev()->bind_descriptor_set(m_env_prefilter_pipeline_layout, 0, 0, desc.get_offset_to_set(mip),
                        ctx.cmd, vk::PipelineBindPoint::eCompute);
                    env_prefilter_push_constants push{};
                    push.size = std::max(ENV_SPECULAR_SIZE >> mip, 1u);
                    push.roughness = static_cast<f32>(mip) / static_cast<f32>(ENV_SPECULAR_MIPS - 1);
                    push.sample_count = ENV_PREFILTER_SAMPLES;
                    ctx.cmd.pushConstants(m_env_prefilter_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
                    const u32 groups = (push.size + 7) / 8;
                    ctx.cmd.dispatch(groups, groups, 6);
                }
            });

        m_graph->add_pass("env_sh",
            [&](graph::pass_builder& builder) {
                builder.read(m_env_sky, graph::texture_usage::sampled);
                builder.write(m_env_sh, graph::buffer_usage::storage_readwrite);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_env_sh_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ m_env_sh_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_env_sh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);
                ctx.cmd.dispatch(1, 1, 1);
            });
    }


    // After the opaque scene pass (depth complete), before translucents (which then blend over the sky in HDR)
    void scene_renderer::add_sky_background_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_sky_background_pass");

        if (!m_env.enabled)
            return;


        util::accessible_image sky{};
        sky.view = m_graph->texture_default_view(m_env_sky);
        sky.sampler = m_shared->get_material_sampler();
        sky.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        std::vector<util::descriptor_item> items;
        items.emplace_back(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &sky);
        auto& desc = m_sky_bg_desc_buffers[m_current_frame];
        m_plugin.get_dev()->compute_descriptor_binding_offsets(m_sky_bg_set_layout, items);
        m_plugin.get_dev()->update_descriptor_buffer(desc, items, util::descriptor_buffer_type::combined);

        // Sun disk: physical radiance of a disk of that size delivering the sun's irradiance (engine units), attenuated by
        // the atmosphere along the view ray to the sun. Clamped to the fp16 range by the shader
        const f32 radius = glm::radians(std::clamp(m_environment.sun_disk_size, 0.f, 5.f) * 0.5f);
        const f32 cos_radius = std::cos(radius);
        const f32 solid_angle = 2.f * glm::pi<f32>() * (1.f - cos_radius);
        glm::vec3 disk(0.f);
        if (radius > 0.f && solid_angle > 1e-9f)
            disk = m_env.sun_color * env_detail::sun_transmittance(m_env.sun_dir, std::max(m_environment.sky_haze, 0.f))
                * (glm::pi<f32>() / solid_angle);

        sky_background_push_constants push{};
        push.inv_view_proj = glm::inverse(camera_projection() * m_active_camera.view);
        push.sun_dir = glm::vec4(m_env.sun_dir, radius > 0.f ? cos_radius : 2.f);     // > 1: never inside the disk
        push.sun_disk = glm::vec4(glm::min(disk, glm::vec3(60000.f)), static_cast<f32>(m_extent.x));
        push.params = glm::vec4(static_cast<f32>(m_extent.y), 0.f, 0.f, 0.f);

        m_graph->add_pass("sky_background",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_hdr, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_env_sky, graph::texture_usage::sampled);
                builder.set_side_effects(true);
            },
            [this, push](graph::pass_context& ctx) {
                const vk::Extent2D extent{ m_extent.x, m_extent.y };
                ctx.begin_rendering();
                ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.f, 1.f));
                ctx.cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_sky_bg_pipeline);
                m_plugin.get_dev()->bind_descriptor_buffer({ m_sky_bg_desc_buffers[m_current_frame] }, ctx.cmd);
                m_plugin.get_dev()->bind_descriptor_set(m_sky_bg_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                ctx.cmd.pushConstants(m_sky_bg_pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(push), &push);
                ctx.cmd.draw(3, 1, 0, 0);
                ctx.end_rendering();
            });
    }

}
