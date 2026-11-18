#include "util/pch.h"

#include "procedural/construct/engine/service.h"
#include "procedural/construct/core_pack.h"
#include "procedural/construct/demo_pack.h"
#include "util/data_structures/thread_pool.inl"            // submit() is a template defined there (not part of the pch)



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        // Per-vertex tangents from positions and UVs (accumulated per triangle, Gram-Schmidt against the normal)
        void compute_tangents(std::span<GLT::asset::mesh::vertex> vertices, std::span<const u32> indices) {

            std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f)), bitan(vertices.size(), glm::vec3(0.0f));
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const u32 a = indices[i], b = indices[i + 1], c = indices[i + 2];
                const glm::vec3 e1 = vertices[b].position - vertices[a].position;
                const glm::vec3 e2 = vertices[c].position - vertices[a].position;
                const glm::vec2 d1 = vertices[b].uv0 - vertices[a].uv0;
                const glm::vec2 d2 = vertices[c].uv0 - vertices[a].uv0;
                const f32 det = d1.x * d2.y - d2.x * d1.y;
                if (std::abs(det) < 1e-12f)
                    continue;
                const f32 r = 1.0f / det;
                const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
                const glm::vec3 s = (e2 * d1.x - e1 * d2.x) * r;
                for (const u32 v : { a, b, c }) {
                    tan[v] += t;
                    bitan[v] += s;
                }
            }
            for (size_t v = 0; v < vertices.size(); ++v) {
                const glm::vec3 n = vertices[v].normal;
                glm::vec3 t = tan[v] - n * glm::dot(n, tan[v]);
                if (glm::length(t) < 1e-8f) {                   // no usable UVs: any perpendicular
                    const glm::vec3 axis = std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    t = glm::cross(axis, n);
                }
                t = glm::normalize(t);
                const f32 sign = glm::dot(glm::cross(n, t), bitan[v]) < 0.0f ? -1.0f : 1.0f;
                vertices[v].tangent = glm::vec4(t, sign);
            }
        }

    }


    registry& packs() {

        static registry instance{};
        static std::once_flag core_registered;
        std::call_once(core_registered, [] {
            register_core_pack(instance, CORE_OWNER);
            register_demo_pack(instance, CORE_OWNER);
        });
        return instance;
    }


    evaluation_result evaluate(const evaluation_request& request) {

        const auto snapshot = packs().snapshot();
        return evaluate(*snapshot, request);
    }


    std::future<evaluation_result> evaluate_async(evaluation_request request) {

        auto snapshot = packs().snapshot();                    // the code version the request was made against
        return GLT::thread_pool::submit([snapshot = std::move(snapshot), request = std::move(request)]() {
            return evaluate(*snapshot, request);
        });
    }


    GLT::render::procedural_mesh_data to_procedural_mesh_data(const output_part& part,
        const std::function<GLT::asset::handle(name_id)>& material_for, std::vector<std::string>& warnings) {

        GLT::render::procedural_mesh_data data{};
        const size_t max_submeshes = GLT::render::PROCEDURAL_MAX_SUBMESHES;
        if (part.groups.size() > max_submeshes)
            warnings.push_back(std::format("{} material groups, only {} submeshes possible: the rest use the last material",
                                           part.groups.size(), max_submeshes));

        for (size_t g = 0; g < part.groups.size(); ++g) {
            const output_group& group = part.groups[g];
            const u32 base_vertex = static_cast<u32>(data.vertices.size());
            const u32 first_index = static_cast<u32>(data.indices.size());
            for (size_t i = 0; i < group.mesh.positions.size(); ++i) {
                GLT::asset::mesh::vertex v{};
                v.position = group.mesh.positions[i];
                v.normal = group.mesh.normals[i];
                v.uv0 = group.mesh.uvs[i];
                data.vertices.push_back(v);
            }
            for (const u32 index : group.mesh.indices)
                data.indices.push_back(base_vertex + index);
            const u32 count = static_cast<u32>(group.mesh.indices.size());

            if (g < max_submeshes) {
                GLT::asset::mesh::submesh sm{};
                sm.first_index = first_index;
                sm.index_count = count;
                sm.material = material_for ? material_for(group.material) : INVALID_HANDLE;
                data.submeshes.push_back(sm);
            } else {
                data.submeshes.back().index_count += count;    // contiguous: groups are appended in order
            }
        }
        compute_tangents(data.vertices, data.indices);
        return data;
    }


    std::vector<published_mesh> publish(const evaluation_result& result, const publish_settings& settings,
                                        std::span<const published_mesh> reuse) {

        auto& store = GLT::render::procedural_meshes();
        std::vector<published_mesh> out;
        std::vector<bool> reused(reuse.size(), false);

        for (const output_result& o : result.outputs) {
            if (std::ranges::find(settings.purposes, o.desc.purpose) == settings.purposes.end())
                continue;
            std::map<element_path, u32> occurrences;
            for (const output_part& part : o.parts) {
                const u32 occurrence = occurrences[part.element]++;
                if (part.groups.empty())
                    continue;
                std::vector<std::string> warnings;
                GLT::render::procedural_mesh_data data = to_procedural_mesh_data(part, settings.material_for, warnings);
                for (const std::string& w : warnings)
                    LOG(warn, "construct: output [{}] part [{}]: {}", o.desc.name, part.element.to_string(), w)

                published_mesh pm{ o.desc.name, part.element, occurrence, GLT::render::INVALID_PROCEDURAL_MESH };
                for (size_t i = 0; i < reuse.size(); ++i)
                    if (!reused[i] && reuse[i].output == pm.output && reuse[i].element == pm.element && reuse[i].occurrence == occurrence
                        && store.is_valid(reuse[i].mesh)) {
                        pm.mesh = reuse[i].mesh;
                        reused[i] = true;
                        break;
                    }
                if (pm.mesh == GLT::render::INVALID_PROCEDURAL_MESH)
                    pm.mesh = store.create({ .update_class = settings.update_class, .detail = settings.detail,
                                             .debug_name = std::format("{} {}", settings.debug_name, o.desc.name) });
                if (auto updated = store.update(pm.mesh, std::move(data)); !updated) {
                    LOG(warn, "construct: publishing output [{}] failed: {}", o.desc.name, updated.error().message)
                    store.destroy(pm.mesh);
                    continue;
                }
                out.push_back(std::move(pm));
            }
        }
        for (size_t i = 0; i < reuse.size(); ++i)
            if (!reused[i])
                store.destroy(reuse[i].mesh);
        return out;
    }


    void unpublish(std::span<const published_mesh> meshes) {

        auto& store = GLT::render::procedural_meshes();
        for (const published_mesh& pm : meshes)
            store.destroy(pm.mesh);
    }

}
