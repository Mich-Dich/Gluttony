#pragma once

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/gtc/type_ptr.hpp>

#include <asset/mesh.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory::mesh_assimp {

    // CONSTANTS =======================================================================================================

    // Assimp post-process flags tuned for RT-ready static geometry.
    // We deliberately do NOT use aiProcess_PreTransformVertices here —
    // it flattens the scene graph and destroys instancing. We walk nodes
    // ourselves and bake transforms into vertices below.
    constexpr unsigned int ASSIMP_FLAGS =
        aiProcess_Triangulate
        | aiProcess_GenSmoothNormals
        | aiProcess_GenUVCoords
        | aiProcess_CalcTangentSpace
        | aiProcess_JoinIdenticalVertices
        | aiProcess_ImproveCacheLocality
        | aiProcess_ValidateDataStructure;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Read whole file through the VFS. Import-only — factories run in the
    // editor / build tools, so a synchronous read here is fine.
    [[nodiscard]] std::expected<std::vector<std::byte>, GLT::asset::import_error> read_source(const std::filesystem::path& path);


    // FNV-1a 64. Placeholder for xxh3 — swap when you have a wrapper.
    [[nodiscard]] constexpr u64 hash_bytes(std::span<const std::byte> data) noexcept;


    // Bake the accumulated node transform into a position.
    FORCE_INLINE_R glm::vec3 to_glm(const aiVector3D& v) noexcept;


    FORCE_INLINE_R glm::mat4 to_glm(const aiMatrix4x4& m) noexcept;


    // Recursively append all meshes under `node`, transformed by `parent`.
    void append_node(const aiScene* scene,const aiNode* node, const glm::mat4& parent, std::vector<GLT::asset::mesh::vertex>& vertices,
        std::vector<u32>& indices, std::vector<GLT::asset::mesh::submesh>& submeshes, std::vector<std::string>& material_paths, 
        const std::filesystem::path& source_dir);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::expected<std::vector<std::byte>, GLT::asset::import_error>read_source(const std::filesystem::path& path) {

        std::error_code ec;
        const u64 size = GLT::vfs::file_size(path, ec);
        if (ec || size == 0)
            return std::unexpected{ GLT::asset::import_error::io_failure };

        const auto handle = GLT::vfs::open_file(path, GLT::vfs::file_open_mode::read, ec);
        if (handle == ::INVALID_HANDLE || ec)
            return std::unexpected{ GLT::asset::import_error::io_failure };

        std::vector<std::byte> bytes(size);
        const size_t got = GLT::vfs::read_file(handle, bytes.data(), size);
        GLT::vfs::close_file(handle);

        if (got != size)
            return std::unexpected{ GLT::asset::import_error::io_failure };

        return bytes;
    }


    constexpr u64 hash_bytes(std::span<const std::byte> data) noexcept {

        u64 h = 0xcbf29ce484222325ull;                 // FNV offset basis
        for (auto b : data) {

            h ^= static_cast<u64>(std::to_integer<u8>(b));
            h *= 0x100000001b3ull;                     // FNV prime
        }
        return h;
    }


    FORCE_INLINE_R glm::vec3 to_glm(const aiVector3D& v) noexcept { return { v.x, v.y, v.z }; }


    FORCE_INLINE_R glm::mat4 to_glm(const aiMatrix4x4& m) noexcept {

        // Assimp stores row-major; GLM is column-major. Transpose on copy.
        return glm::mat4(
            m.a1, m.b1, m.c1, m.d1,
            m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3,
            m.a4, m.b4, m.c4, m.d4);
    }


    void append_node(const aiScene* scene, const aiNode* node, const glm::mat4& parent, std::vector<GLT::asset::mesh::vertex>& vertices,
        std::vector<u32>& indices, std::vector<GLT::asset::mesh::submesh>& submeshes, std::vector<std::string>& material_paths, 
        const std::filesystem::path& source_dir) {


        using GLT::asset::mesh::vertex;
        using GLT::asset::mesh::submesh;
        const glm::mat4 world = parent * to_glm(node->mTransformation);
        const glm::mat3 nrm_m = glm::mat3(glm::transpose(glm::inverse(world)));

        for (unsigned int mi = 0; mi < node->mNumMeshes; ++mi) {
            
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
            const u32 base_vertex  = static_cast<u32>(vertices.size());
            const u32 base_index   = static_cast<u32>(indices.size());

            // --- vertices ---
            vertices.reserve(vertices.size() + mesh->mNumVertices);
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                vertex out{};

                const glm::vec4 local_pos{ to_glm(mesh->mVertices[v]), 1.0f };
                out.position = glm::vec3(world * local_pos);

                if (mesh->HasNormals())
                    out.normal = glm::normalize(nrm_m * to_glm(mesh->mNormals[v]));

                if (mesh->HasTangentsAndBitangents()) {
                    const glm::vec3 t = glm::normalize(glm::mat3(world) * to_glm(mesh->mTangents[v]));
                    const glm::vec3 b = glm::normalize(glm::mat3(world) * to_glm(mesh->mBitangents[v]));
                    const glm::vec3 n = out.normal;
                    // Sign: positive if (n × t) agrees with b.
                    const float sign = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
                    out.tangent = glm::vec4(t, sign);
                } else {
                    out.tangent = { 1, 0, 0, 1 };
                }

                if (mesh->HasTextureCoords(0)) {
                    const aiVector3D& uv = mesh->mTextureCoords[0][v];
                    out.uv0 = { uv.x, uv.y };
                }

                vertices.push_back(out);
            }

            // --- indices ---
            indices.reserve(indices.size() + mesh->mNumFaces * 3);
            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {

                const aiFace& face = mesh->mFaces[f];
                // aiProcess_Triangulate guarantees 3 verts/face.
                indices.push_back(base_vertex + face.mIndices[0]);
                indices.push_back(base_vertex + face.mIndices[1]);
                indices.push_back(base_vertex + face.mIndices[2]);
            }

            // --- submesh (one per Assimp mesh node) ---
            submesh sm{
                .first_index = base_index,
                .index_count = static_cast<u32>(indices.size()) - base_index,
                .material_slot = static_cast<u32>(material_paths.size()),
            };
            submeshes.push_back(sm);

            // --- material path (best-effort) ---
            std::string mat_path;
            if (mesh->mMaterialIndex < scene->mNumMaterials) {

                const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
                aiString name;
                if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) {

                    // Heuristic: <source_dir>/<material_name>.glt_material
                    std::filesystem::path p = source_dir / name.C_Str();
                    p.replace_extension(".glt_material");
                    mat_path = p.generic_string();
                }
            }
            material_paths.push_back(std::move(mat_path));
        }

        for (unsigned int c = 0; c < node->mNumChildren; ++c)
            append_node(scene, node->mChildren[c], world, vertices, indices, submeshes, material_paths, source_dir);
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void plugin::on_load() {

        m_registry = GLT::asset::registry::get_ref();
        VALIDATE(m_registry, return, "", "Asset registry not available")
        m_registry->register_factory(this);
        LOG_LOADED
    }


    void plugin::on_unload() {

        if (m_registry) 
            m_registry->unregister_factory(this);
        m_registry.reset();
        LOG_UNLOADED
    }


    [[nodiscard]] std::span<const GLT::asset::factory::binding> plugin::bindings() const noexcept {

        static constexpr GLT::asset::factory::binding b[] = {
            { "fbx",    GLT::asset::core_types::static_mesh },
            { "fbx",    GLT::asset::core_types::skeletal_mesh },
            { "obj",    GLT::asset::core_types::static_mesh },
            { "gltf",   GLT::asset::core_types::static_mesh },
            { "glb",    GLT::asset::core_types::static_mesh },
            { "dae",    GLT::asset::core_types::static_mesh },
        };
        return b;
    }


    [[nodiscard]] std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> plugin::import(
        const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& opts, 
        GLT::asset::asset_writer& out) {

        // ========================== skeletal isn't wired up yet in this factory ==========================
        if (target_type == GLT::asset::core_types::skeletal_mesh)
            return std::unexpected{ GLT::asset::import_error::not_supported };

        // read source through the VFS ----
        auto bytes_res = read_source(source);
        if (!bytes_res) 
            return std::unexpected{ bytes_res.error() };
        const std::vector<std::byte>& bytes = *bytes_res;

        Assimp::Importer importer;

        // Hint = extension without the dot. String must outlive the call.
        const std::string ext = source.extension().string();
        const char* hint = (ext.size() > 1) ? (ext.c_str() + 1) : nullptr;
        const aiScene* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), ASSIMP_FLAGS, hint);
        if (!scene) {

            LOG(error, "mesh_assimp: Assimp failed on '{}': {}", source.generic_string(), importer.GetErrorString());
            return std::unexpected{ GLT::asset::import_error::unsupported_format };
        }
        if (!scene->HasMeshes())
            return std::unexpected{ GLT::asset::import_error::invalid_source };

        // flatten the scene graph into one vertex/index blob
        std::vector<GLT::asset::mesh::vertex> vertices;
        std::vector<u32> indices;
        std::vector<GLT::asset::mesh::submesh> submeshes;
        std::vector<std::string> material_paths;
        const glm::mat4 root = glm::mat4(1.0f);
        const std::filesystem::path source_dir = source.parent_path();
        append_node(scene, scene->mRootNode, root, vertices, indices, submeshes, material_paths, source_dir);
        if (vertices.empty() || indices.empty())
            return std::unexpected{ GLT::asset::import_error::invalid_source };

        // bounds
        GLT::asset::mesh::bounds b{};
        b.min = b.max = vertices.front().position;
        for (const GLT::asset::mesh::vertex& v : vertices) {

            b.min = glm::min(b.min, v.position);
            b.max = glm::max(b.max, v.position);
        }

        // write the chunks. Registry owns header / string table / deps
        out.set_name(source.stem().string());
        out.write_chunk(GLT::asset::mesh::CHUNK_VERTICES, std::as_bytes(std::span{ vertices }));
        out.write_chunk(GLT::asset::mesh::CHUNK_INDICES, std::as_bytes(std::span{ indices }));
        out.write_chunk(GLT::asset::mesh::CHUNK_SUBMESHES, std::as_bytes(std::span{ submeshes }));
        out.write_chunk(GLT::asset::mesh::CHUNK_BOUNDS, std::as_bytes(std::span{ &b, 1 }));

        // Dependencies: one per submesh, in submesh order so material_slot
        // lines up with info.dependencies[N] on load.
        for (const std::string& mat_path : material_paths) {
            if (mat_path.empty()) {
                // Keep the slot count aligned even when a submesh has no material.
                // The registry treats an empty path as an "unresolved" slot.
                out.declare_dependency("", GLT::asset::core_types::material);
            } else {
                out.declare_dependency(mat_path, GLT::asset::core_types::material);
            }
        }

        // Cheap payload hash: mix vertex bytes, index bytes and submesh bytes.
        u64 hash = hash_bytes(std::as_bytes(std::span{ vertices }));
        hash ^= hash_bytes(std::as_bytes(std::span{ indices }))   * 0x9E3779B97F4A7C15ull;
        hash ^= hash_bytes(std::as_bytes(std::span{ submeshes })) * 0xC2B2AE3D27D4EB4Full;

        GLT::asset::factory::import_result result{          // result metadata 

            .output_path = {},                              // Let the registry pick the output path (respects import root config).
            .id = UUID(),
            .source_hash = hash_bytes(bytes),
            .payload_hash = hash,
        };

        LOG(info, "mesh_assimp: '{}' → {} verts, {} tris, {} submeshes", 
            source.generic_string(), vertices.size(), indices.size() / 3, submeshes.size());

        return result;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
