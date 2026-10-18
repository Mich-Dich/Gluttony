#pragma once

#include <asset/material.h>
#include <asset/mesh.h>
#include <asset/texture.h>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static glm::mat4 preview_view(const GLT::render::preview_camera& cam);

    static void write_preview_camera_ubo(camera_ubo_data& ubo, const GLT::render::preview_camera& cam, const glm::uvec2& size);

    static void write_preview_globals(gpu_globals& g, const GLT::render::preview_camera& cam, const glm::uvec2& size);

    static void generate_preview_primitive(GLT::render::preview_primitive shape, std::vector<GLT::asset::mesh::vertex>& out_verts, 
        std::vector<u32>& out_indices);

    static glm::vec3 preview_eye_from_camera(const GLT::render::preview_camera& cam);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static glm::mat4 preview_view(const GLT::render::preview_camera& cam) {

        const glm::vec3 eye = preview_eye_from_camera(cam);
        return glm::lookAt(eye, cam.target, glm::vec3(0.f, 1.f, 0.f));
    }


    static void write_preview_camera_ubo(camera_ubo_data& ubo, const GLT::render::preview_camera& cam, const glm::uvec2& size) {

        const glm::vec3 eye = preview_eye_from_camera(cam);
        const glm::mat4 view = glm::lookAt(eye, cam.target, glm::vec3(0.f, 1.f, 0.f));

        const f32 aspect = static_cast<f32>(size.x) / static_cast<f32>(size.y);
        glm::mat4 proj = glm::perspective(glm::radians(cam.fov_deg), aspect, 0.01f, 1000.f);
        proj[1][1] *= -1.0f;

        ubo.view_proj = proj * view;
        ubo.view = view;
        ubo.camera_pos = glm::vec4(eye, 1.f);
        ubo.projection = proj;
        ubo.inv_projection = glm::inverse(proj);
        ubo.cluster_dims = glm::uvec4(0u);
        ubo.cluster_depth = glm::vec4(0.01f, 1000.f, static_cast<f32>(size.x), static_cast<f32>(size.y));
        ubo.light_view_proj = glm::mat4(1.f);
        ubo.shadow_params = glm::vec4(0.f);

        extract_frustum_planes(ubo.view_proj, ubo.frustum_planes);
    }


    static void write_preview_globals(gpu_globals& g, const GLT::render::preview_camera& cam, const glm::uvec2& size) {

        g.time = GLT::application::get().get_total_time();
        g.delta_time = GLT::application::get().get_delta_time();
        g.frame_index = 0u;
        g.viewport_size = glm::vec2(size);
        g.render_size = glm::vec2(size);

        const glm::vec3 eye = preview_eye_from_camera(cam);
        const glm::mat4 view = glm::lookAt(eye, cam.target, glm::vec3(0.f, 1.f, 0.f));
        const f32 aspect = static_cast<f32>(size.x) / static_cast<f32>(size.y);
        glm::mat4 proj = glm::perspective(glm::radians(cam.fov_deg), aspect, 0.01f, 1000.f);
        proj[1][1] *= -1.0f;

        g.camera_position = eye;
        g.camera_forward = glm::normalize(cam.target - eye);
        g.camera_right = glm::normalize(glm::cross(g.camera_forward, glm::vec3(0.f, 1.f, 0.f)));
        g.camera_up = glm::cross(g.camera_right, g.camera_forward);
        g.near_plane = 0.01f;
        g.far_plane = 1000.f;

        g.view = view;
        g.projection = proj;
        g.view_projection = proj * view;
        g.inv_view = glm::inverse(view);
        g.inv_projection = glm::inverse(proj);
        g.inv_view_projection = glm::inverse(g.view_projection);
    }


    static void generate_preview_primitive(GLT::render::preview_primitive shape, std::vector<GLT::asset::mesh::vertex>& out_verts,
        std::vector<u32>& out_indices) {

        out_verts.clear();
        out_indices.clear();

        auto push_vertex = [&](const glm::vec3& pos, const glm::vec3& nrm, const glm::vec4& tan, const glm::vec2& uv) {
            GLT::asset::mesh::vertex v;
            v.position = pos;
            v.normal = nrm;
            v.tangent = tan;
            v.uv0 = uv;
            out_verts.push_back(v);
        };

        switch (shape) {

        case GLT::render::preview_primitive::sphere: {
            constexpr u32 segs = 32, rings = 16;
            for (u32 y = 0; y <= rings; ++y) {
                const f32 v = static_cast<f32>(y) / rings;
                const f32 phi = v * glm::pi<f32>();
                for (u32 x = 0; x <= segs; ++x) {
                    const f32 u = static_cast<f32>(x) / segs;
                    const f32 theta = u * glm::two_pi<f32>();
                    const glm::vec3 p{ std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta) };
                    const glm::vec3 t{ -std::sin(theta), 0.f, std::cos(theta) };
                    push_vertex(p, p, glm::vec4(t, 1.f), glm::vec2(u, 1.f - v));
                }
            }
            for (u32 y = 0; y < rings; ++y) for (u32 x = 0; x < segs; ++x) {
                const u32 a = y * (segs + 1) + x, b = a + 1;
                const u32 c = a + (segs + 1), d = c + 1;
                out_indices.insert(out_indices.end(), { a, c, b, b, c, d });
            }
            break;
        }

        case GLT::render::preview_primitive::cube: {
            struct face { glm::vec3 n, u, v; };
            static constexpr face faces[6] = {
                {{ 1,0,0}, {0,0,-1}, {0,1,0}},
                {{-1,0,0}, {0,0, 1}, {0,1,0}},
                {{ 0,1,0}, {1,0, 0}, {0,0,-1}},
                {{ 0,-1,0},{1,0, 0}, {0,0, 1}},
                {{ 0,0,1}, {1,0, 0}, {0,1, 0}},
                {{ 0,0,-1},{-1,0,0}, {0,1, 0}},
            };
            for (const auto& f : faces) {
                const u32 base = static_cast<u32>(out_verts.size());
                for (u32 i = 0; i < 4; ++i) {
                    const f32 x = (i & 1) ? 1.f : -1.f;
                    const f32 y = (i & 2) ? 1.f : -1.f;
                    const glm::vec3 p = f.n * 0.5f + f.u * (x * 0.5f) + f.v * (y * 0.5f);
                    push_vertex(p, f.n, glm::vec4(f.u, 1.f), glm::vec2((x+1.f)*0.5f, (y+1.f)*0.5f));
                }
                out_indices.insert(out_indices.end(), { base, base+1, base+2, base, base+2, base+3 });
            }
            break;
        }

        case GLT::render::preview_primitive::plane: {
            push_vertex({-1,0,-1}, {0,1,0}, {1,0,0,1}, {0,0});
            push_vertex({ 1,0,-1}, {0,1,0}, {1,0,0,1}, {1,0});
            push_vertex({-1,0, 1}, {0,1,0}, {1,0,0,1}, {0,1});
            push_vertex({ 1,0, 1}, {0,1,0}, {1,0,0,1}, {1,1});
            out_indices = { 0,2,1, 1,2,3 };
            break;
        }

        case GLT::render::preview_primitive::cylinder: {
            constexpr u32 segs = 32;
            constexpr f32 half_h = 0.75f;
            constexpr f32 r = 0.5f;
            for (u32 i = 0; i < segs; ++i) {
                const f32 t0 = (f32)i / segs * glm::two_pi<f32>();
                const f32 t1 = (f32)(i+1) / segs * glm::two_pi<f32>();
                const glm::vec3 p00{r*std::cos(t0), -half_h, r*std::sin(t0)};
                const glm::vec3 p01{r*std::cos(t0),  half_h, r*std::sin(t0)};
                const glm::vec3 p10{r*std::cos(t1), -half_h, r*std::sin(t1)};
                const glm::vec3 p11{r*std::cos(t1),  half_h, r*std::sin(t1)};
                const glm::vec3 n0{std::cos(t0), 0, std::sin(t0)};
                const glm::vec3 n1{std::cos(t1), 0, std::sin(t1)};
                const u32 b = static_cast<u32>(out_verts.size());
                push_vertex(p00, n0, {std::sin(t0),0,-std::cos(t0),1}, {(f32)i/segs, 1});
                push_vertex(p01, n0, {std::sin(t0),0,-std::cos(t0),1}, {(f32)i/segs, 0});
                push_vertex(p10, n1, {std::sin(t1),0,-std::cos(t1),1}, {(f32)(i+1)/segs, 1});
                push_vertex(p11, n1, {std::sin(t1),0,-std::cos(t1),1}, {(f32)(i+1)/segs, 0});
                out_indices.insert(out_indices.end(), { b, b+1, b+2, b+2, b+1, b+3 });
            }
            // caps (simple fan around centers)
            const u32 top_center = static_cast<u32>(out_verts.size());
            push_vertex({0, half_h, 0}, {0,1,0}, {1,0,0,1}, {0.5f, 0.5f});
            const u32 top_first = static_cast<u32>(out_verts.size());
            for (u32 i = 0; i < segs; ++i) {
                const f32 t = (f32)i / segs * glm::two_pi<f32>();
                push_vertex({r*std::cos(t), half_h, r*std::sin(t)}, {0,1,0}, {1,0,0,1}, {std::cos(t)*0.5f+0.5f, std::sin(t)*0.5f+0.5f});
            }
            for (u32 i = 0; i < segs; ++i) {
                out_indices.insert(out_indices.end(), { top_center, top_first + i, top_first + ((i+1) % segs) });
            }
            const u32 bot_center = static_cast<u32>(out_verts.size());
            push_vertex({0, -half_h, 0}, {0,-1,0}, {1,0,0,1}, {0.5f, 0.5f});
            const u32 bot_first = static_cast<u32>(out_verts.size());
            for (u32 i = 0; i < segs; ++i) {
                const f32 t = (f32)i / segs * glm::two_pi<f32>();
                push_vertex({r*std::cos(t), -half_h, r*std::sin(t)}, {0,-1,0}, {1,0,0,1}, {std::cos(t)*0.5f+0.5f, std::sin(t)*0.5f+0.5f});
            }
            for (u32 i = 0; i < segs; ++i) {
                out_indices.insert(out_indices.end(), { bot_center, bot_first + ((i+1) % segs), bot_first + i });
            }
            break;
        }

        case GLT::render::preview_primitive::torus: {
            constexpr u32 major = 32, minor = 16;
            constexpr f32 R = 0.5f, r = 0.2f;
            for (u32 i = 0; i <= major; ++i) {
                const f32 u = (f32)i / major;
                const f32 tu = u * glm::two_pi<f32>();
                for (u32 j = 0; j <= minor; ++j) {
                    const f32 v = (f32)j / minor;
                    const f32 tv = v * glm::two_pi<f32>();
                    const glm::vec3 p{(R + r*std::cos(tv))*std::cos(tu), r*std::sin(tv), (R + r*std::cos(tv))*std::sin(tu)};
                    const glm::vec3 n{std::cos(tv)*std::cos(tu), std::sin(tv), std::cos(tv)*std::sin(tu)};
                    push_vertex(p, n, {-std::sin(tu), 0, std::cos(tu), 1}, {u, v});
                }
            }
            for (u32 i = 0; i < major; ++i) for (u32 j = 0; j < minor; ++j) {
                const u32 a = i * (minor + 1) + j, b = a + 1;
                const u32 c = a + (minor + 1), d = c + 1;
                out_indices.insert(out_indices.end(), { a, c, b, b, c, d });
            }
            break;
        }

        default: break;
        }
    }


    static glm::vec3 preview_eye_from_camera(const GLT::render::preview_camera& cam) {
        const f32 cp = std::cos(cam.pitch_rad);
        const glm::vec3 dir{
            cp * std::sin(cam.yaw_rad),
            std::sin(cam.pitch_rad),
            cp * std::cos(cam.yaw_rad)
        };
        return cam.target + dir * cam.distance;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    GLT::render::preview_id renderer::create(const GLT::render::preview_desc& desc, const glm::uvec2& size) {

        if (size.x == 0 || size.y == 0)
            return GLT::render::INVALID_PREVIEW_ID;

        const GLT::render::preview_id id = m_next_preview_id++;
        auto& state = m_previews[id];
        state.desc = desc;
        state.settings = {};
        state.camera = {};
        state.overrides = {};
        state.active = true;
        state.auto_rotate = false;
        state.auto_rotate_speed = 0.5f;
        state.rotation_phase = 0.f;
        state.geometry_dirty = true;
        state.material_dirty = true;
        state.target = GLT::create_ref<image>();
        state.target->resize({ size.x, size.y, 1 });

        // Per-frame staging buffers
        for (u32 index = 0; index < MAX_CONCURRENT_FRAMES; ++index) {
            state.camera_ubos[index] = m_dev->create_buffer(sizeof(camera_ubo_data), vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            state.globals_ubos[index] = m_dev->create_buffer(sizeof(gpu_globals),     vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            state.instance_buffers[index] = m_dev->create_buffer(sizeof(gpu_instance_data), vk::BufferUsageFlagBits::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count += 3;
        }

        // Copy the override span into the preview's own storage
        if (!state.overrides.packed_params.empty()) {
            state.owned_packed_params.assign(state.overrides.packed_params.begin(), state.overrides.packed_params.end());
            state.overrides.packed_params = state.owned_packed_params;
        }

        // Geometry
        rebuild_preview_geometry(state);

        // Material
        rebuild_preview_material(state);

        return id;
    }


    void renderer::destroy(GLT::render::preview_id id) {

        auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        // The interface promises the renderer defers GPU free. We're conservative here.
        m_device.waitIdle();

        preview_state& state = it->second;

        if (state.material_slot != 0)
            release_material(state.material_slot);

        for (u32 index = 0; index < MAX_CONCURRENT_FRAMES; ++index) {
            if (state.camera_ubos[index].buffer)
                m_dev->destroy_buffer(state.camera_ubos[index]);
            if (state.globals_ubos[index].buffer)
                m_dev->destroy_buffer(state.globals_ubos[index]);
            if (state.instance_buffers[index].buffer)
                m_dev->destroy_buffer(state.instance_buffers[index]);
        }
        if (state.vertex_buffer.buffer)
            m_dev->destroy_buffer(state.vertex_buffer);
        if (state.index_buffer.buffer)
            m_dev->destroy_buffer(state.index_buffer);
        if (state.mesh_data_buffer.buffer)
            m_dev->destroy_buffer(state.mesh_data_buffer);
        if (state.submesh_data_buffer.buffer)
            m_dev->destroy_buffer(state.submesh_data_buffer);

        m_previews.erase(it);
    }


    void renderer::set_active(GLT::render::preview_id id, bool active) {

        if (auto it = m_previews.find(id); it != m_previews.end())
            it->second.active = active;
    }


    void renderer::set_desc(GLT::render::preview_id id, const GLT::render::preview_desc& desc) {

        auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        preview_state& state = it->second;
        const bool geometry_changed = state.desc.source  != desc.source
                                   || state.desc.shape != desc.shape
                                   || state.desc.mesh != desc.mesh;
        const bool material_changed = state.desc.material != desc.material
                                   || state.desc.material_instance != desc.material_instance;

        state.desc = desc;
        if (geometry_changed)
            state.geometry_dirty = true;
        if (material_changed)
            state.material_dirty = true;
    }


    void renderer::set_settings(GLT::render::preview_id id, const GLT::render::preview_settings& settings) {

        if (auto it = m_previews.find(id); it != m_previews.end())
            it->second.settings = settings;
    }


    void renderer::set_camera(GLT::render::preview_id id, const GLT::render::preview_camera& camera) {

        if (auto it = m_previews.find(id); it != m_previews.end())
            it->second.camera = camera;
    }


    void renderer::set_material_overrides(GLT::render::preview_id id, const GLT::render::preview_material_overrides& overrides) {

        auto it = m_previews.find(id);
        if (it == m_previews.end())
            return;

        preview_state& state = it->second;

        // Take ownership of the packed_params
        state.owned_packed_params.assign(overrides.packed_params.begin(), overrides.packed_params.end());
        state.overrides.packed_params = state.owned_packed_params;
        state.overrides.textures = overrides.textures;
        state.material_dirty = true;

        // If this is called before create() ever ran with data, rebuild immediately so the first frame is correct
        if (state.material_slot == 0 || true)
            rebuild_preview_material(state);
    }


    void renderer::set_auto_rotate(GLT::render::preview_id id, bool enabled, f32 speed_rad_per_sec) {

        if (auto it = m_previews.find(id); it != m_previews.end()) {
            it->second.auto_rotate = enabled;
            it->second.auto_rotate_speed = speed_rad_per_sec;
        }
    }


    void renderer::set_highlight_submeshes(GLT::render::preview_id id, std::span<const u32> submesh_indices) {

        if (auto it = m_previews.find(id); it != m_previews.end()) {
            it->second.highlighted_submeshes.assign(submesh_indices.begin(), submesh_indices.end());
        }
        // NOTE: No shader support yet — highlights currently have no effect. Extend the material contract
        //       with a highlight uniform or a wireframe-overlay pass to enable this.
    }


    void* renderer::get_render_target(GLT::render::preview_id id) const {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return nullptr;
        return static_cast<void*>(it->second.target->get_descriptor_set());
    }


    glm::uvec2 renderer::get_size(GLT::render::preview_id id) const {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return {0, 0};
        return it->second.target->get_size();
    }


    void renderer::set_size(GLT::render::preview_id id, const glm::uvec2& size) {

        auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target || size.x == 0 || size.y == 0)
            return;

        if (it->second.target->get_size() == size)
            return;

        m_device.waitIdle();
        it->second.target->resize({ size.x, size.y, 1 });
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::create_preview_resources() {

        // Nothing global to create — every resource is per-preview. Left as a hook for future global
        // preview infrastructure (e.g. a shared reflection cubemap, a ground grid mesh, ...)
    }


    void renderer::destroy_preview_resources() {

        m_device.waitIdle();
        for (auto& [id, _] : m_previews)
            destroy(id);
        m_previews.clear();
    }


    void renderer::rebuild_preview_geometry(preview_state& state) {

        std::vector<GLT::asset::mesh::vertex> verts;
        std::vector<u32> indices;
        std::vector<GLT::asset::mesh::submesh> submeshes;

        if (state.desc.source == GLT::render::preview_source::primitive) {
            generate_preview_primitive(state.desc.shape, verts, indices);
            GLT::asset::mesh::submesh sm{};
            sm.first_index = 0;
            sm.index_count = static_cast<u32>(indices.size());
            sm.material = state.desc.material;   // may be INVALID -> replaced by bound_material later
            submeshes.push_back(sm);
        } else if (state.desc.source == GLT::render::preview_source::mesh_asset) {
            auto registry = GLT::asset::registry::get_ref();
            if (registry) {
                if (auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(state.desc.mesh)) {
                    verts = mesh->vertices;
                    indices = mesh->indices;
                    submeshes = mesh->submeshes;
                }
            }
        }

        // (Re)allocate the buffers
        if (state.vertex_buffer.buffer)
            m_dev->destroy_buffer(state.vertex_buffer);
        if (state.index_buffer.buffer)
            m_dev->destroy_buffer(state.index_buffer);
        if (state.mesh_data_buffer.buffer)
            m_dev->destroy_buffer(state.mesh_data_buffer);
        if (state.submesh_data_buffer.buffer)
            m_dev->destroy_buffer(state.submesh_data_buffer);

        const auto v_usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer;
        const auto i_usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer;

        state.vertex_buffer = m_dev->create_buffer(std::max<size_t>(verts.size() * sizeof(GLT::asset::mesh::vertex), 64),
            v_usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        state.index_buffer = m_dev->create_buffer(std::max<size_t>(indices.size() * sizeof(u32), 64),
            i_usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count += 2;

        if (!verts.empty()) {
            std::memcpy(m_dev->map_buffer(state.vertex_buffer), verts.data(), verts.size() * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(state.vertex_buffer);
        }
        if (!indices.empty()) {
            std::memcpy(m_dev->map_buffer(state.index_buffer), indices.data(), indices.size() * sizeof(u32));
            m_dev->unmap_buffer(state.index_buffer);
        }

        // Compute bounds for the mesh_data buffer
        glm::vec3 mn{0.f}, mx{0.f};
        if (!verts.empty()) {
            mn = mx = verts[0].position;
            for (const auto& v : verts) { mn = glm::min(mn, v.position); mx = glm::max(mx, v.position); }
        }

        // Mesh data buffer: 1 entry
        gpu_mesh_data md{};
        md.local_min = mn;
        md.local_max = mx;
        md.first_submesh = 0;
        md.submesh_count = static_cast<u32>(submeshes.size());
        md.vertex_offset = 0;
        md.index_offset = 0;

        state.mesh_data_buffer = m_dev->create_buffer(sizeof(gpu_mesh_data), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;
        std::memcpy(m_dev->map_buffer(state.mesh_data_buffer), &md, sizeof(md));
        m_dev->unmap_buffer(state.mesh_data_buffer);

        // Submesh data buffer: N entries
        std::vector<gpu_submesh_data> sd(submeshes.size());
        for (size_t i = 0; i < submeshes.size(); ++i) {
            sd[i].first_index = submeshes[i].first_index;
            sd[i].index_count = submeshes[i].index_count;
            sd[i].material_index = 0;
        }
        state.submesh_data_buffer = m_dev->create_buffer(
            std::max<size_t>(sd.size() * sizeof(gpu_submesh_data), 16),
            vk::BufferUsageFlagBits::eStorageBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;
        if (!sd.empty()) {
            std::memcpy(m_dev->map_buffer(state.submesh_data_buffer), sd.data(), sd.size() * sizeof(gpu_submesh_data));
            m_dev->unmap_buffer(state.submesh_data_buffer);
        }

        state.submeshes = std::move(submeshes);
        state.vertex_count = static_cast<u32>(verts.size());
        state.index_count = static_cast<u32>(indices.size());
        state.submesh_count = static_cast<u32>(state.submeshes.size());
        state.geometry_dirty = false;
    }


    void renderer::rebuild_preview_material(preview_state& state) {

        if (state.material_slot != 0) {
            release_material(state.material_slot);
            state.material_slot = 0;
        }

        // Resolve the base material handle
        GLT::asset::handle base_material = state.desc.material;

        if (state.desc.material_instance != INVALID_HANDLE) {
            auto registry = GLT::asset::registry::get_ref();
            if (registry) {
                if (auto* inst = registry->data_as<GLT::asset::material::material_instance_asset>(state.desc.material_instance)) {
                    base_material = inst->base_material;
                }
            }
        }

        if (base_material == INVALID_HANDLE)
            base_material = DEFAULT_MATERIAL_SLOT;   // fall back to the default slot inside acquire_material

        // Acquire the base slot: this makes sure it's loaded
        const u32 base_slot = acquire_material(base_material);

        const bool has_overrides = !state.overrides.packed_params.empty()
            || std::any_of(state.overrides.textures.begin(), state.overrides.textures.end(),
                [](const auto& opt) { return opt.has_value(); });

        if (!has_overrides) {
            state.material_slot = base_slot;
            state.bound_material = base_material;
            state.material_dirty = false;
            return;
        }

        // Allocate a virtual slot to hold the overridden material
        u32 slot_idx;
        if (!m_free_material_slots.empty()) {
            slot_idx = m_free_material_slots.back();
            m_free_material_slots.pop_back();
        } else {
            slot_idx = static_cast<u32>(m_material_slots.size());
            VALIDATE(slot_idx < MAX_MATERIALS, state.material_slot = base_slot; return, "",
                "Material table full while allocating preview override slot")
            m_material_slots.emplace_back();
        }

        material_slot& slot = m_material_slots[slot_idx];
        material_slot& base = m_material_slots[base_slot];

        slot = {};
        slot.asset = base_material;
        slot.ref_count = 1;
        slot.alive = true;
        slot.pipeline = base.pipeline;
        slot.depth_pipeline = base.depth_pipeline;
        slot.vertex_module = base.vertex_module;
        slot.fragment_module = base.fragment_module;
        slot.depth_module = base.depth_module;
        slot.blend = base.blend;

        // Textures: override where provided, otherwise reuse base
        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i) {
            const auto& opt = state.overrides.textures[i];
            if (opt.has_value()) {
                const auto handle = *opt;
                if (handle == INVALID_HANDLE) {
                    slot.texture_slots[i] = fallback_texture_slot_for(i);
                    slot.texture_assets[i] = INVALID_HANDLE;
                } else {
                    slot.texture_slots[i] = acquire_texture(handle);
                    slot.texture_assets[i] = handle;
                }
            } else {
                slot.texture_slots[i] = base.texture_slots[i];
                slot.texture_assets[i] = INVALID_HANDLE;   // marker: we don't own this reference
            }
        }

        // Params
        std::span<const std::byte> params_src;
        if (!state.overrides.packed_params.empty()) {
            params_src = state.overrides.packed_params;
        } else {
            auto registry = GLT::asset::registry::get_ref();
            if (registry) {
                if (auto* asset = registry->data_as<GLT::asset::material::material_asset>(base_material))
                    params_src = asset->packed_params;
            }
        }
        const u32 params_bytes = static_cast<u32>(params_src.size());
        const u32 alloc_size = std::max<u32>(params_bytes, 256);

        slot.params_ubo = m_dev->create_buffer(alloc_size,
            vk::BufferUsageFlagBits::eUniformBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        slot.params_size = alloc_size;
        m_live_buffer_count++;

        if (params_bytes > 0) {
            std::memcpy(m_dev->map_buffer(slot.params_ubo), params_src.data(), params_bytes);
            m_dev->unmap_buffer(slot.params_ubo);
        }

        write_material_descriptors(slot_idx);

        state.material_slot = slot_idx;
        state.bound_material = base_material;
        state.material_dirty = false;
    }


    void renderer::import_preview_textures() {

        for (auto& [id, state] : m_previews) {

            if (!state.active || !state.target)
                continue;

            // Colour (imported from the preview's external image)
            graph::texture_desc color_desc{};
            color_desc.name = "preview_color_" + std::to_string(id);
            color_desc.format = vk::Format::eR8G8B8A8Unorm;
            color_desc.width = state.target->get_size().x;
            color_desc.height = state.target->get_size().y;
            color_desc.usage = vk::ImageUsageFlagBits::eSampled
                | vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eTransferSrc
                | vk::ImageUsageFlagBits::eTransferDst;
            color_desc.persistent = true;

            state.color_handle = m_graph->create_texture(color_desc);
            m_graph->import_texture(state.color_handle,
                state.target->get_allocated_image_ref().image,
                state.target->get_accessible_image_ref().view,
                { state.target->get_accessible_image_ref().layout, {}, vk::PipelineStageFlagBits2::eNone });

            // Depth (graph-managed)
            graph::texture_desc depth_desc{};
            depth_desc.name = "preview_depth_" + std::to_string(id);
            depth_desc.format = vk::Format::eD32Sfloat;
            depth_desc.width = state.target->get_size().x;
            depth_desc.height = state.target->get_size().y;
            depth_desc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
            depth_desc.persistent = true;
            state.depth_handle = m_graph->create_texture(depth_desc);
        }
    }


    void renderer::add_preview_passes() {

        if (m_previews.empty())
            return;

        import_preview_textures();

        for (auto& [id, state] : m_previews) {

            if (!state.active || !state.target)
                continue;
            if (state.submesh_count == 0 || state.vertex_count == 0)
                continue;

            // Advance auto-rotation
            if (state.auto_rotate) {
                state.rotation_phase += state.auto_rotate_speed * static_cast<f32>(GLT::application::get().get_delta_time());
            }
            GLT::render::preview_camera cam = state.camera;
            cam.yaw_rad += state.rotation_phase;

            // Refresh per-frame CPU data
            write_preview_camera_ubo(*(camera_ubo_data*)m_dev->map_buffer(state.camera_ubos[m_current_frame]), cam, state.target->get_size());
            m_dev->unmap_buffer(state.camera_ubos[m_current_frame]);

            gpu_globals g{};
            write_preview_globals(g, cam, state.target->get_size());
            std::memcpy(m_dev->map_buffer(state.globals_ubos[m_current_frame]), &g, sizeof(g));
            m_dev->unmap_buffer(state.globals_ubos[m_current_frame]);

            // Instance buffer: 1 instance
            gpu_instance_data inst{};
            inst.model = glm::mat4(1.f);
            inst.normal_matrix = glm::mat4(1.f);
            inst.mesh_index = 0;
            inst.material_index = 0;
            std::memcpy(m_dev->map_buffer(state.instance_buffers[m_current_frame]), &inst, sizeof(inst));
            m_dev->unmap_buffer(state.instance_buffers[m_current_frame]);

            // Build the descriptor set for this frame
            {
                const u32 frame_index = m_current_frame;

                // Fall back to shared scene resources for lights/shadows when unavailable
                util::accessible_image shadow_image{};
                shadow_image.view = m_graph->texture_default_view(m_shadow_map);
                shadow_image.sampler = m_shadow_sampler;
                shadow_image.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

                util::accessible_image atlas_image{};
                atlas_image.view = m_graph->texture_default_view(m_shadow_atlas);
                atlas_image.sampler = m_shadow_sampler;
                atlas_image.layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

                util::accessible_image shadow_sampler_image{};
                shadow_sampler_image.sampler = m_shadow_sampler;

                // Reuse the main scene's light buffers for cluster lookups. With a 1-instance preview,
                // the shader will simply see no matching lights.
                util::allocated_buffer* grid_alloc = m_graph->buffer_allocation(m_light_grid_handles[frame_index]);
                util::allocated_buffer* list_alloc = m_graph->buffer_allocation(m_light_index_list_handles[frame_index]);

                using VKDT = vk::DescriptorType;
                using VKSF = vk::ShaderStageFlagBits;

                std::vector<util::descriptor_item> items;
                items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &state.camera_ubos[frame_index]);
                items.emplace_back(1, VKDT::eStorageBuffer, VKSF::eVertex, 1, &state.instance_buffers[frame_index]);
                items.emplace_back(2, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_light_buffers[frame_index]);
                items.emplace_back(3, VKDT::eStorageBuffer, VKSF::eFragment, 1, grid_alloc);
                items.emplace_back(4, VKDT::eStorageBuffer, VKSF::eFragment, 1, list_alloc);
                items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, &shadow_image);
                items.emplace_back(6, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_shadow_view_buffers[frame_index]);
                items.emplace_back(7, VKDT::eSampledImage,  VKSF::eFragment, 1, &atlas_image);
                items.emplace_back(8, VKDT::eSampler, VKSF::eFragment, 1, &shadow_sampler_image);
                items.emplace_back(9, VKDT::eUniformBuffer, VKSF::eVertex | VKSF::eFragment, 1, &state.globals_ubos[frame_index]);

                if (state.frame_desc_buffers[frame_index].buffer.buffer == nullptr)
                    state.frame_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(
                        m_mesh_set_layout, items, util::descriptor_buffer_type::combined, 1);

                m_dev->compute_descriptor_binding_offsets(m_mesh_set_layout, items);
                m_dev->update_descriptor_buffer(state.frame_desc_buffers[frame_index], items,
                    util::descriptor_buffer_type::combined);
            }

            // Add the pass
            m_graph->add_pass("preview_" + std::to_string(id),
                [this, &state](graph::pass_builder& builder) {
                    builder.color_attachment(state.color_handle, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                        vk::ClearColorValue(std::array<f32, 4>{
                            state.settings.background.r, state.settings.background.g,
                            state.settings.background.b, state.settings.background.a }));
                    builder.depth_attachment(state.depth_handle, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                    builder.read(state.color_handle, graph::texture_usage::color_attachment);
                    builder.set_side_effects(true);
                },
                [this, &state](graph::pass_context& ctx) {

                    if (state.submesh_count == 0 || state.vertex_count == 0)
                        return;

                    const u32 slot = state.material_slot;
                    const material_slot& entry = m_material_slots[slot];
                    if (!entry.alive || !entry.pipeline)
                        return;

                    ctx.begin_rendering();

                    const auto size = state.target->get_size();
                    ctx.cmd.setViewport(0, vk::Viewport(0.f, 0.f, static_cast<f32>(size.x), static_cast<f32>(size.y), 0.f, 1.f));
                    ctx.cmd.setScissor(0, vk::Rect2D({0, 0}, {size.x, size.y}));

                    ctx.cmd.bindVertexBuffers(0, { state.vertex_buffer.buffer }, { 0 });
                    ctx.cmd.bindIndexBuffer(state.index_buffer.buffer, 0, vk::IndexType::eUint32);

                    m_dev->bind_descriptor_buffer({
                        state.frame_desc_buffers[m_current_frame],
                        m_material_desc_buffers[m_current_frame] }, ctx.cmd);
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                    m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 1, 1,
                        m_material_desc_buffers[m_current_frame].get_offset_to_set(slot),
                        ctx.cmd, vk::PipelineBindPoint::eGraphics);

                    ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, entry.pipeline);

                    for (const auto& sm : state.submeshes) {
                        if (sm.index_count == 0)
                            continue;
                        ctx.cmd.drawIndexed(sm.index_count, 1, sm.first_index, 0, 0);
                        m_frame_draw_calls++;
                    }

                    ctx.end_rendering();
                    m_frame_render_passes++;
                });
        }
    }

}
