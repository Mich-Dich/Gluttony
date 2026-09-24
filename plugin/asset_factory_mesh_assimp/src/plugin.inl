#pragma once

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/gtc/type_ptr.hpp>

#include <asset/mesh.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::factory::mesh_assimp {

    // CONSTANTS =======================================================================================================

    // Assimp post-process flags that are ALWAYS on:
    //   - Triangulate:                 our vertex/index layout has no polygon concept
    //   - GenUVCoords:                 fills missing UV sets from the file's texture refs
    //   - ValidateDataStructure:       cheap safety net; catches broken exporters early
    constexpr unsigned int ASSIMP_FLAGS_BASE =
        aiProcess_Triangulate
        | aiProcess_GenUVCoords
        | aiProcess_ValidateDataStructure;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Tri-state for how normals + tangents are sourced.
    enum class normals_tangents_mode : u8 {

        import_from_file = 0,       // trust whatever the file has
        calculate = 1,       // regenerate from geometry (GenSmoothNormals + CalcTangentSpace)
        ignore = 2,       // drop them; shader must derive its own
    };


    // Mirror of the values carried in opts.type_specific. Populated by
    // parse_type_specific() using the SAME order as option_schema().
    struct parsed_options {

        f32                             scale = 1.0f;                                       // uniform post-import scale
        bool                            use_file_units = false;                             // aiProcess_GlobalScale
        bool                            weld_vertices = true;                               // aiProcess_JoinIdenticalVertices
        bool                            optimize_mesh = true;                               // aiProcess_ImproveCacheLocality
        normals_tangents_mode           normals_tangents = normals_tangents_mode::calculate;
        f32                             smoothing_angle = 60.0f;                            // degrees, only used when calculating
        bool                            swap_uvs = false;
        bool                            generate_lightmap_uvs = false;                      // stub for now
        u8                              compression_quality = 85;                           // 0..100, future vertex codec hint
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Read whole file through the VFS. Import-only - factories run in the
    // editor / build tools, so a synchronous read here is fine.
    [[nodiscard]] std::expected<std::vector<std::byte>, GLT::asset::import_error> read_source(const std::filesystem::path& path);


    // FNV-1a 64. Placeholder for xxh3 - swap when you have a wrapper.
    [[nodiscard]] constexpr u64 hash_bytes(std::span<const std::byte> data) noexcept;


    // Bake the accumulated node transform into a position.
    FORCE_INLINE_R glm::vec3 to_glm(const aiVector3D& v) noexcept;


    FORCE_INLINE_R glm::mat4 to_glm(const aiMatrix4x4& m) noexcept;


    // Assemble the post-process flag mask from the parsed options.
    [[nodiscard]] constexpr unsigned int build_assimp_flags(const parsed_options& opt) noexcept;


    // Unpack opts.type_specific into parsed_options. Empty / truncated blob is
    // NOT an error - headless import passes {} and gets the schema defaults.
    [[nodiscard]] parsed_options parse_type_specific(std::span<const std::byte> blob) noexcept;


    // In-place U<->V swap on every vertex of the mesh.
    void swap_uvs_inplace(std::span<GLT::asset::mesh::vertex> verts) noexcept;


    // Recursively append all meshes under `node`, transformed by `parent`.
    void append_node(const aiScene* scene, const aiNode* node, const glm::mat4& parent, const parsed_options& opt,
        std::vector<GLT::asset::mesh::vertex>& vertices, std::vector<u32>& indices,
        std::vector<GLT::asset::mesh::submesh>& submeshes, std::vector<std::string>& material_paths,
        const std::filesystem::path& source_dir);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    std::expected<std::vector<std::byte>, GLT::asset::import_error> read_source(const std::filesystem::path& path) {

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


    constexpr unsigned int build_assimp_flags(const parsed_options& opt) noexcept {

        unsigned int flags = ASSIMP_FLAGS_BASE;

        if (opt.weld_vertices)
            flags |= aiProcess_JoinIdenticalVertices;

        if (opt.optimize_mesh)
            flags |= aiProcess_ImproveCacheLocality;

        if (opt.normals_tangents == normals_tangents_mode::calculate)
            flags |= aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;

        if (opt.use_file_units)
            flags |= aiProcess_GlobalScale;

        return flags;
    }


    parsed_options parse_type_specific(std::span<const std::byte> blob) noexcept {

        // Defaults mirror option_schema() exactly.
        parsed_options out{};

        size_t cursor = 0;

        auto take = [&](void* dst, size_t n) -> bool {

            if (cursor + n > blob.size())
                return false;
            std::memcpy(dst, blob.data() + cursor, n);
            cursor += n;
            return true;
        };

        auto take_string = [&](std::string& s) -> bool {

            u32 len = 0;
            if (!take(&len, sizeof(len)))
                return false;
            if (cursor + len > blob.size())
                return false;
            s.assign(reinterpret_cast<const char*>(blob.data() + cursor), len);
            cursor += len;
            return true;
        };

        u8  b = 0;
        f32 f = 0.0f;

        if (take(&f, sizeof(f)))                    // field 1: scale (real -> f32)
            out.scale = f;

        if (take(&b, 1))                            // field 2: use_file_units (boolean -> u8)
            out.use_file_units = (b != 0);

        if (take(&b, 1))                            // field 3: weld_vertices (boolean -> u8)
            out.weld_vertices = (b != 0);

        if (take(&b, 1))                            // field 4: optimize_mesh (boolean -> u8)
            out.optimize_mesh = (b != 0);

        std::string nt;                             // field 5: normals_tangents (enumeration -> u32-len + bytes)
        if (take_string(nt)) {

            if      (nt == "import")    out.normals_tangents = normals_tangents_mode::import_from_file;
            else if (nt == "calculate") out.normals_tangents = normals_tangents_mode::calculate;
            else if (nt == "ignore")    out.normals_tangents = normals_tangents_mode::ignore;
            else                        out.normals_tangents = normals_tangents_mode::calculate;
        }

        if (take(&f, sizeof(f)))                    // field 6: smoothing_angle (real -> f32)
            out.smoothing_angle = f;

        if (take(&b, 1))                            // field 7: swap_uvs (boolean -> u8)
            out.swap_uvs = (b != 0);

        if (take(&b, 1))                            // field 8: generate_lightmap_uvs (boolean -> u8)
            out.generate_lightmap_uvs = (b != 0);

        i32 q = 85;                                 // field 9: compression_quality (integer -> i32)
        if (take(&q, sizeof(q)))
            out.compression_quality = static_cast<u8>(std::clamp(q, 0, 100));

        return out;
    }


    void swap_uvs_inplace(std::span<GLT::asset::mesh::vertex> verts) noexcept {

        for (auto& v : verts)
            std::swap(v.uv0.x, v.uv0.y);
    }


    void append_node(const aiScene* scene, const aiNode* node, const glm::mat4& parent, const parsed_options& opt,
        std::vector<GLT::asset::mesh::vertex>& vertices, std::vector<u32>& indices,
        std::vector<GLT::asset::mesh::submesh>& submeshes, std::vector<std::string>& material_paths,
        const std::filesystem::path& source_dir) {


        using GLT::asset::mesh::vertex;
        using GLT::asset::mesh::submesh;
        const glm::mat4 world = parent * to_glm(node->mTransformation);
        const glm::mat3 nrm_m = glm::mat3(glm::transpose(glm::inverse(world)));

        const bool want_normals  = (opt.normals_tangents != normals_tangents_mode::ignore);
        const bool want_tangents = (opt.normals_tangents != normals_tangents_mode::ignore);

        for (unsigned int mi = 0; mi < node->mNumMeshes; ++mi) {

            const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
            const u32 base_vertex = static_cast<u32>(vertices.size());
            const u32 base_index  = static_cast<u32>(indices.size());

            // --- vertices ---
            vertices.reserve(vertices.size() + mesh->mNumVertices);
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {

                vertex out{};

                const glm::vec4 local_pos{ to_glm(mesh->mVertices[v]), 1.0f };
                out.position = glm::vec3(world * local_pos);

                if (want_normals && mesh->HasNormals())
                    out.normal = glm::normalize(nrm_m * to_glm(mesh->mNormals[v]));

                if (want_tangents && mesh->HasTangentsAndBitangents()) {

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
            for (unsigned int fi = 0; fi < mesh->mNumFaces; ++fi) {

                const aiFace& face = mesh->mFaces[fi];
                // aiProcess_Triangulate guarantees 3 verts/face.
                indices.push_back(base_vertex + face.mIndices[0]);
                indices.push_back(base_vertex + face.mIndices[1]);
                indices.push_back(base_vertex + face.mIndices[2]);
            }

            // --- submesh (one per Assimp mesh node) ---
            submesh sm{
                .first_index   = base_index,
                .index_count   = static_cast<u32>(indices.size()) - base_index,
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
            append_node(scene, node->mChildren[c], world, opt, vertices, indices, submeshes, material_paths, source_dir);
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


    // Schema order here MUST stay in lockstep with parse_type_specific().
    // Changing a field's type or position invalidates every editor session
    // that already has a pending import open.
    [[nodiscard]] std::span<const GLT::asset::factory::i_asset_factory_plugin::option_descriptor>
        plugin::option_schema(const GLT::asset::type& target_type) const noexcept {

        if (target_type != GLT::asset::core_types::static_mesh)
            return {};

        using desc = GLT::asset::factory::i_asset_factory_plugin::option_descriptor;

        static constexpr desc schema[] = {
            {   // 1
                .key                = "scale",
                .label              = "Scale factor",
                .type               = desc::kind::real,
                .default_value      = "1.0",
                .enumeration_values = "",
                .tooltip            = "Uniform scale applied to all vertex positions after import. Use 0.01 for centimeters → meters, 0.0254 for inches → meters.",
            },
            {   // 2
                .key                = "use_file_units",
                .label              = "Use file units",
                .type               = desc::kind::boolean,
                .default_value      = "false",
                .enumeration_values = "",
                .tooltip            = "Honor the source file's declared unit (FBX / glTF / Collada). Applied BEFORE the scale factor above.",
            },
            {   // 3
                .key                = "weld_vertices",
                .label              = "Weld vertices",
                .type               = desc::kind::boolean,
                .default_value      = "true",
                .enumeration_values = "",
                .tooltip            = "Merge vertices with identical position / normal / UV. Shrinks the index buffer without changing the silhouette.",
            },
            {   // 4
                .key                = "optimize_mesh",
                .label              = "Optimize mesh",
                .type               = desc::kind::boolean,
                .default_value      = "true",
                .enumeration_values = "",
                .tooltip            = "Reorder triangles for better vertex-cache locality. Costs a moment on import, saves GPU time every frame.",
            },
            {   // 5
                .key                = "normals_tangents",
                .label              = "Normals / tangents",
                .type               = desc::kind::enumeration,
                .default_value      = "calculate",
                .enumeration_values = "import|calculate|ignore",
                .tooltip            = "'import' trusts the source. 'calculate' regenerates from geometry (recommended for procedural / non-DCC meshes). 'ignore' drops them; the shader must derive its own.",
            },
            {   // 6
                .key                = "smoothing_angle",
                .label              = "Smoothing angle (°)",
                .type               = desc::kind::real,
                .default_value      = "60.0",
                .enumeration_values = "",
                .tooltip            = "Only used when normals are calculated. Edges sharper than this stay faceted; smoother ones get averaged.",
            },
            {   // 7
                .key                = "swap_uvs",
                .label              = "Swap UVs",
                .type               = desc::kind::boolean,
                .default_value      = "false",
                .enumeration_values = "",
                .tooltip            = "Swap U and V of UV0. Fixes textures that come in rotated 90° from certain exporters (Maya → glTF, some FBX presets).",
            },
            {   // 8
                .key                = "generate_lightmap_uvs",
                .label              = "Generate lightmap UVs",
                .type               = desc::kind::boolean,
                .default_value      = "false",
                .enumeration_values = "",
                .tooltip            = "Author a non-overlapping UV1 for baked lighting. Reserved - a lightmap UV unwrapper is not wired up yet; the option is accepted but currently logged and ignored.",
            },
            {   // 9
                .key                = "compression_quality",
                .label              = "Compression quality",
                .type               = desc::kind::integer,
                .default_value      = "85",
                .enumeration_values = "",
                .tooltip            = "0 = smallest (most lossy), 100 = best fidelity. Reserved - consumed once vertex-data quantization is implemented.",
            },
        };
        return schema;
    }


    [[nodiscard]] std::expected<GLT::asset::factory::import_result, GLT::asset::import_error> plugin::import(
        const std::filesystem::path& source, GLT::asset::type target_type, const GLT::asset::import_options& opts,
        GLT::asset::asset_writer& out) {

        // ========================== skeletal isn't wired up yet in this factory ==========================
        if (target_type == GLT::asset::core_types::skeletal_mesh)
            return std::unexpected{ GLT::asset::import_error::not_supported };

        if (target_type != GLT::asset::core_types::static_mesh)
            return std::unexpected{ GLT::asset::import_error::not_supported };

        const parsed_options option = parse_type_specific(opts.type_specific);

        // read source through the VFS ----
        auto bytes_res = read_source(source);
        if (!bytes_res)
            return std::unexpected{ bytes_res.error() };
        const std::vector<std::byte>& bytes = *bytes_res;

        // ---- configure Assimp from the options ----------------------------------------------------
        Assimp::Importer importer;

        // Smoothing angle is only consulted when GenSmoothNormals is enabled.
        // Set it unconditionally - the importer ignores it otherwise.
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, option.smoothing_angle);

        const unsigned int flags = build_assimp_flags(option);

        // Hint = extension without the dot. String must outlive the call.
        const std::string ext = source.extension().string();
        const char* hint = (ext.size() > 1) ? (ext.c_str() + 1) : nullptr;
        const aiScene* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, hint);
        if (!scene) {

            LOG(error, "mesh_assimp: Assimp failed on [{}]: {}", source.generic_string(), importer.GetErrorString());
            return std::unexpected{ GLT::asset::import_error::unsupported_format };
        }
        if (!scene->HasMeshes())
            return std::unexpected{ GLT::asset::import_error::invalid_source };

        // ---- flatten the scene graph into one vertex/index blob ------------------------------------
        std::vector<GLT::asset::mesh::vertex> vertices;
        std::vector<u32> indices;
        std::vector<GLT::asset::mesh::submesh> submeshes;
        std::vector<std::string> material_paths;
        const glm::mat4 root = glm::mat4(1.0f);
        const std::filesystem::path source_dir = source.parent_path();
        append_node(scene, scene->mRootNode, root, option, vertices, indices, submeshes, material_paths, source_dir);
        if (vertices.empty() || indices.empty())
            return std::unexpected{ GLT::asset::import_error::invalid_source };

        // ---- post-pass: uniform scale --------------------------------------------------------------
        // Runs after flattening so it also scales the bake-in transforms.
        // Applied BEFORE bounds so the bounds reflect the final geometry.
        if (option.scale != 1.0f) {

            for (auto& v : vertices)
                v.position *= option.scale;
        }

        // ---- post-pass: U/V swap -------------------------------------------------------------------
        if (option.swap_uvs)
            swap_uvs_inplace(vertices);

        // ---- post-pass: lightmap UVs (stub) --------------------------------------------------------
        if (option.generate_lightmap_uvs) {

            // NOTE: reserved. A real implementation would run an unwrap pass
            // (chart-based, like xatlas) and store the result in a UV1 set.
            // The on-disk vertex layout currently has only UV0, so there is
            // nowhere to put the output yet. Log and move on so the option
            // round-trips through the schema without silently vanishing.
            LOG(warn, "mesh_assimp: generate_lightmap_uvs is not implemented yet [{}] - ignored", source.generic_string());
        }

        // ---- bounds --------------------------------------------------------------------------------
        GLT::AABB bounds{};
        bounds.min = bounds.max = vertices.front().position;
        for (const GLT::asset::mesh::vertex& v : vertices) {

            bounds.min = glm::min(bounds.min, v.position);
            bounds.max = glm::max(bounds.max, v.position);
        }

        // ---- write the chunks. Registry owns header / string table / deps --------------------------
        out.set_name(source.stem().string());
        out.write_chunk(GLT::asset::mesh::CHUNK_VERTICES, std::as_bytes(std::span{ vertices }));
        out.write_chunk(GLT::asset::mesh::CHUNK_INDICES, std::as_bytes(std::span{ indices }));
        out.write_chunk(GLT::asset::mesh::CHUNK_SUBMESHES, std::as_bytes(std::span{ submeshes }));
        out.write_chunk(GLT::asset::mesh::CHUNK_BOUNDS, std::as_bytes(std::span{ &bounds, 1 }));

        // Dependencies: one per submesh, in submesh order so material_slot
        // lines up with info.dependencies[N] on load.
        for (const std::string& mat_path : material_paths) {
            if (mat_path.empty()) {
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

            .output_path  = {},                             // Let the registry pick the output path.
            .id           = UUID(),
            .source_hash  = hash_bytes(bytes),
            .payload_hash = hash,
        };

        LOG(info, "mesh_assimp: [{}] → {} verts, {} tris, {} submeshes [scale={}, unit={}, weld={}, opt={}, nt={}, swap_uv={}, lmuv={}, q={}]",
            source.generic_string(), vertices.size(), indices.size() / 3, submeshes.size(),
            option.scale,
            option.use_file_units    ? "y" : "n",
            option.weld_vertices     ? "y" : "n",
            option.optimize_mesh     ? "y" : "n",
            static_cast<int>(option.normals_tangents),
            option.swap_uvs          ? "y" : "n",
            option.generate_lightmap_uvs ? "y" : "n",
            option.compression_quality);

        return result;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
