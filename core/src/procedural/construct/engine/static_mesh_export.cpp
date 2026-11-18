#include "util/pch.h"

#include "procedural/construct/engine/static_mesh_export.h"
#include "procedural/construct/engine/service.h"

#include "asset/i_asset_registry.h"
#include "config/project.h"
#include "application.h"



namespace GLT::construct {

    // FUNCTION IMPLEMENTATION =========================================================================================

    namespace {

        void append(output_part& into, const output_group& group) {

            output_group* target = nullptr;
            for (output_group& g : into.groups)
                if (g.material == group.material)
                    target = &g;
            if (!target) {
                into.groups.push_back({ group.material, {} });
                target = &into.groups.back();
            }
            geometry::triangle_mesh& m = target->mesh;
            const u32 base = static_cast<u32>(m.positions.size());
            m.positions.insert(m.positions.end(), group.mesh.positions.begin(), group.mesh.positions.end());
            m.normals.insert(m.normals.end(), group.mesh.normals.begin(), group.mesh.normals.end());
            m.uvs.insert(m.uvs.end(), group.mesh.uvs.begin(), group.mesh.uvs.end());
            for (const u32 i : group.mesh.indices)
                m.indices.push_back(base + i);
            m.triangle_material.insert(m.triangle_material.end(), group.mesh.triangle_material.begin(), group.mesh.triangle_material.end());
        }


        GLT::AABB bounds_of(std::span<const GLT::asset::mesh::vertex> vertices) {

            GLT::AABB box{};
            if (vertices.empty())
                return box;
            box.min = box.max = vertices.front().position;
            for (const auto& v : vertices) {
                box.min = glm::min(box.min, v.position);
                box.max = glm::max(box.max, v.position);
            }
            return box;
        }


        // Replaces the geometry of [target] with [source]'s. Materials stay when the submesh count is unchanged
        void replace_geometry(GLT::asset::mesh::mesh_asset& target, GLT::asset::mesh::mesh_asset& source) {

            if (target.submeshes.size() == source.submeshes.size())
                for (size_t i = 0; i < source.submeshes.size(); ++i)
                    source.submeshes[i].material = target.submeshes[i].material;
            target.vertices = std::move(source.vertices);
            target.indices = std::move(source.indices);
            target.submeshes = std::move(source.submeshes);
            target.bounds = source.bounds;
            target.lods.clear();
            target.lod_ranges.clear();
            target.lod_indices.clear();
            target.lod_vertices.clear();
            target.clusters.clear();
            target.cluster_indices.clear();
            target.cluster_submeshes.clear();
            target.cluster_vertices.clear();
            ++target.revision;
        }

    }


    GLT::unique_ref<GLT::asset::mesh::mesh_asset> to_static_mesh(const evaluation_result& result, const static_mesh_options& options,
                                                                 std::vector<std::string>& warnings) {

        output_part merged{};
        for (const output_result& o : result.outputs) {
            if (std::ranges::find(options.purposes, o.desc.purpose) == options.purposes.end())
                continue;
            if (!options.outputs.empty() && std::ranges::find(options.outputs, o.desc.name) == options.outputs.end())
                continue;
            for (const output_part& part : o.parts)
                for (const output_group& group : part.groups)
                    append(merged, group);
        }
        if (merged.groups.empty())
            return nullptr;

        GLT::render::procedural_mesh_data data = to_procedural_mesh_data(merged, {}, warnings);
        auto mesh = GLT::create_unique_ref<GLT::asset::mesh::mesh_asset>();
        mesh->asset_type = GLT::asset::core_types::static_mesh;
        mesh->vertices = std::move(data.vertices);
        mesh->indices = std::move(data.indices);
        mesh->submeshes = std::move(data.submeshes);
        for (auto& sm : mesh->submeshes)
            sm.material = INVALID_HANDLE;                               // the default material until one is assigned
        mesh->bounds = bounds_of(mesh->vertices);
        return mesh;
    }


    std::expected<GLT::asset::asset_ref, std::string> save_static_mesh(GLT::unique_ref<GLT::asset::mesh::mesh_asset> mesh,
                                                                       const std::filesystem::path& content_path) {

        if (!mesh)
            return std::unexpected(std::string("nothing to export (no geometry)"));
        auto* registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return std::unexpected(std::string("no asset registry"));

        // Already there: update in place (keeps the asset id, so placed meshes and references follow)
        GLT::asset::asset_ref existing{};
        if (const GLT::asset::handle h = registry->find(content_path); h != INVALID_HANDLE && registry->is_loaded(h))
            existing = GLT::asset::asset_ref(h);
        else if (std::filesystem::exists(PROJECT_CONTENT_DIR / content_path))
            if (auto loaded = registry->load(content_path))
                existing = std::move(*loaded);
        if (existing.valid()) {
            auto* target = dynamic_cast<GLT::asset::mesh::mesh_asset*>(registry->data(existing.get()));
            if (!target || target->type() != GLT::asset::core_types::static_mesh)
                return std::unexpected(std::format("[{}] exists and is not a static mesh", content_path.generic_string()));
            replace_geometry(*target, *mesh);
            if (auto saved = registry->save(existing.get()); !saved)
                return std::unexpected(std::format("could not write the file (error {})", static_cast<int>(saved.error())));
            return existing;
        }

        auto registered = registry->register_runtime(std::move(mesh), content_path, content_path.stem().string());
        if (!registered)
            return std::unexpected(std::format("could not register the mesh (error {})", static_cast<int>(registered.error())));
        if (auto saved = registry->save(registered->get()); !saved)
            return std::unexpected(std::format("could not write the file (error {})", static_cast<int>(saved.error())));
        return std::move(*registered);
    }

}
