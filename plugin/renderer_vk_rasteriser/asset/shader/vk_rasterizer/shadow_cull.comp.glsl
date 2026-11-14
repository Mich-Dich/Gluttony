
#version 450

// Shadow culling for the shadow atlases: one thread per instance, against the shadow views that are drawn this frame.
// Two dispatches use it, each with its own command / count buffers: the local lights' atlas and the sun's cascades.
//
// Shadow maps are cached (scene/local_shadow_cache.h, scene/sun_shadow_cascades.h): a frame only draws the views whose
// light or casters changed. Those views are flagged (ShadowView::cull.x) and sit in the pc.cull_view_count views from
// pc.view_first on, group after group: the first view of a group (a light, or one sun cascade) carries the group's
// number of views and its sphere, so a light that does not reach the instance is skipped as a whole.
//
// Output layout: per material slot one block of draw commands for ALL drawn views, back to back (atomic per material),
// so the atlas pass is one drawIndexedIndirectCount per material for every view at once. The command's firstInstance
// carries the view; the material vertex shader projects with that view and clips to its atlas tile
// (renderer_project_vertex, shading_strings.h). Block of material m: commands [range.x * N, (range.x + range.y) * N) with
// N = pc.render_view_count, counter counts[m]. range = the main view's material range (material_ranges): an
// instance-submesh lands at most once per drawn view, so range.y * N is an exact upper bound.

layout(local_size_x = 64) in;

struct InstanceData { mat4 model;
    mat4        normal_matrix;
    uint        mesh_index;
    uint        material_index;
    uint        flags;          // bits 0..3: forced LOD + 1 (0 = automatic)
    uint        _pad1;
};

struct MeshData { vec3 local_min;
    uint        first_submesh;
    vec3        local_max;
    uint        submesh_count;
    uint        vertex_offset;
    uint        index_offset;
    uint        lod_count;      // submesh table holds lod_count x submesh_count entries, [lod][submesh]
    uint        _pad1;
    float       lod_screen[8];  // [0] cull size (not used for shadows), [n] screen size below which LOD n takes over
};

struct SubmeshData {
    uint        first_index;
    uint        index_count;
    uint        material_index;
    uint        _pad0;
};

struct DrawIndexedIndirectCommand {
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    int         vertexOffset;
    uint        firstInstance;
};

struct ShadowView {             // gpu_shadow_view (scene/type.h)
    mat4        view_proj;
    vec4        atlas_rect;     // z <= 0: no depth map
    vec4        params;         // x = texel size at distance 1 (2 tan / tile size); parallel light: texel size in the world
    vec4        sphere;         // light position, radius (as the view was / is drawn)
    vec4        planes[6];
    vec4        cull;           // x != 0: drawn this frame. y: views of this group (on its first view only). z != 0: parallel light
    vec4        group_sphere;   // on the first view of a group: the light's sphere this frame
    vec4        _pad;
};

// Shared limits (MAX_MATERIALS, MAX_SHADOW_VIEWS, ...) are injected as #defines by the renderer's shader compiler from the
// C++ constants (util/shader_compiler.cpp). Do not redeclare them here.

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer { InstanceData instances[]; };
layout(std430, set = 0, binding = 1) readonly buffer MeshBuffer { MeshData meshes[]; };
layout(std430, set = 0, binding = 2) readonly buffer SubmeshBuffer { SubmeshData submeshes[]; };
layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer { DrawIndexedIndirectCommand commands[]; };
layout(std430, set = 0, binding = 4) buffer DrawCountBuffer { uint counts[]; };                 // [MAX_MATERIALS]
layout(std430, set = 0, binding = 5) readonly buffer ShadowViewBuffer { ShadowView views[]; };
layout(std430, set = 0, binding = 6) readonly buffer MaterialTable { uint material_table[]; };   // see cull.comp.glsl
layout(std430, set = 0, binding = 7) readonly buffer MaterialRanges { uvec2 material_ranges[]; };

layout(push_constant) uniform PushConstants {
    uint instance_count;    // number of valid instances this frame (the SSBO holds stale data past it)
    uint view_first;        // first view of this dispatch in the view buffer
    uint cull_view_count;   // views to walk from there: the groups that draw at least one view this frame
    uint render_view_count; // views drawn this frame (flagged), sizes the command blocks
} pc;

// firstInstance in the shadow atlas pass (scene/type.h, shading_strings.h): instance index | view << 20 | bit 31
const uint INSTANCE_SHADOW_PASS         = 1u << 31u;
const uint INSTANCE_SHADOW_VIEW_SHIFT   = 20u;

// LOD metric of the main view (cull.comp.glsl): bounding sphere diameter / viewport height, with the switch sizes fitted
// for 1 pixel at 1080 lines. A shadow tile texel is treated like a pixel of that reference: the same geometric error in
// texels as the camera allows in pixels
const float SHADOW_LOD_REFERENCE_HEIGHT = 1080.0;


void main() {

    const uint instance_index = gl_GlobalInvocationID.x;
    if (instance_index >= pc.instance_count || instance_index >= uint(instances.length()))
        return;

    const InstanceData instance = instances[instance_index];
    const MeshData mesh = meshes[instance.mesh_index];
    if (mesh.submesh_count == 0u)
        return;

    // ---- bounds, once per instance -------------------------------------------------------------------------------
    vec3 world_min = vec3( 1e30);
    vec3 world_max = vec3(-1e30);
    for (uint corner_index = 0u; corner_index < 8u; ++corner_index) {
        const vec3 corner = mix(mesh.local_min, mesh.local_max, vec3(
            (corner_index & 1u) != 0u ? 1.0 : 0.0,
            (corner_index & 2u) != 0u ? 1.0 : 0.0,
            (corner_index & 4u) != 0u ? 1.0 : 0.0));
        const vec3 world = (instance.model * vec4(corner, 1.0)).xyz;
        world_min = min(world_min, world);
        world_max = max(world_max, world);
    }
    const vec3  center = (world_min + world_max) * 0.5;
    const float radius = length(world_max - world_min) * 0.5;          // sphere around the world AABB

    const uint lod_count = max(mesh.lod_count, 1u);
    const uint forced = instance.flags & 0xFu;
    const uint cull_views = min(pc.view_first + pc.cull_view_count, uint(views.length()));      // one past the last view to walk
    const uint render_views = max(pc.render_view_count, 1u);

    for (uint first = pc.view_first; first < cull_views; ) {

        const uint group = clamp(uint(views[first].cull.y), 1u, 6u);       // views of this light

        // 1. Light sphere vs AABB: the whole light at once
        const vec4 light = views[first].group_sphere;
        const vec3 closest = clamp(light.xyz, world_min, world_max);
        const vec3 delta = closest - light.xyz;
        if (dot(delta, delta) > light.w * light.w) {
            first += group;
            continue;
        }

        for (uint v = first; v < first + group && v < cull_views; ++v) {

            if (views[v].cull.x == 0.0 || views[v].atlas_rect.z <= 0.0)
                continue;                               // this view keeps its depth map

            // 2. View frustum
            bool outside = false;
            for (uint p = 0u; p < 6u; ++p) {
                const vec4 plane = views[v].planes[p];
                const vec3 positive_vertex = mix(world_min, world_max, step(vec3(0.0), plane.xyz));
                if (dot(plane.xyz, positive_vertex) + plane.w < 0.0) { outside = true; break; }
            }
            if (outside)
                continue;

            // 3. Small-caster rejection: smaller than ~1 shadow texel at its distance -> invisible in the map anyway.
            //    Parallel light (sun cascade): a texel has the same size everywhere in the map
            const bool  parallel = views[v].cull.z != 0.0;
            const float dist = parallel ? 1.0 : distance(center, views[v].sphere.xyz);
            const float texel = views[v].params.x * dist;       // world size of a texel at the caster
            if (radius < texel)
                continue;

            // 4. LOD: forced level (mesh editor preview) wins, otherwise by size in shadow texels (see above). Depends only
            //    on the light and the tile, never on the camera
            uint lod = 0u;
            if (forced != 0u) {
                lod = min(forced - 1u, lod_count - 1u);
            } else if (lod_count > 1u && (parallel || dist > radius)) {
                const float screen = 2.0 * radius / (texel * SHADOW_LOD_REFERENCE_HEIGHT);
                for (uint level = 1u; level < lod_count; ++level)
                    if (screen < mesh.lod_screen[level])
                        lod = level;
            }

            const uint first_submesh = mesh.first_submesh + lod * mesh.submesh_count;
            const uint instance_info = instance_index | (v << INSTANCE_SHADOW_VIEW_SHIFT) | INSTANCE_SHADOW_PASS;

            for (uint submesh_offset = 0u; submesh_offset < mesh.submesh_count; ++submesh_offset) {

                const SubmeshData submesh = submeshes[first_submesh + submesh_offset];
                if (submesh.index_count < 3u)
                    continue;

                const uint mat_slot = material_table[instance.material_index + submesh_offset];
                if (mat_slot >= MAX_MATERIALS)
                    continue;

                const uvec2 range = material_ranges[mat_slot];
                const uint slot = atomicAdd(counts[mat_slot], 1u);
                if (slot >= range.y * render_views)
                    continue;                           // cannot happen (exact bound), but never write outside the block

                const uint cmd = range.x * render_views + slot;
                commands[cmd].indexCount = submesh.index_count;
                commands[cmd].instanceCount = 1u;
                commands[cmd].firstIndex = mesh.index_offset + submesh.first_index;
                commands[cmd].vertexOffset = int(mesh.vertex_offset);
                commands[cmd].firstInstance = instance_info;
            }
        }
        first += group;
    }
}
