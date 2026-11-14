
#pragma once

#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::settings {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using GLT::reflect::annotations::display_name;
    using GLT::reflect::annotations::tooltip;
    using GLT::reflect::annotations::category;
    using GLT::reflect::annotations::range;
    using GLT::reflect::annotations::options;
    using GLT::reflect::annotations::name;


    // Stable name required - two plugins can't both be "renderer.vk_rasterizer.visual"
    struct [[=name{"renderer.vk_rasterizer.visual"}]] visual {

        // ---- Post Process -------------------------------------------------------

        [[=category{"Post Process"}]]
        [[=display_name{"Exposure (EV)"}]]
        [[=tooltip{"Exposure compensation in stops. Each +1 doubles the brightness before tonemapping"}]]
        [[=range{-10.0, 10.0}]]
        f32 exposure_ev = 0.0f;


        [[=category{"Post Process"}]]
        [[=display_name{"Tonemapper"}]]
        [[=tooltip{"How the bright range of the image is brought to what a screen can show"}]]
        [[=options{"ACES (fitted)|AgX|Reinhard|None (clamp)"}]]
        [[=range{0.0, 3.0}]]
        u32 tonemapper = 0u;

        // ---- Virtual geometry ---------------------------------------------------
        // Meshes in virtual geometry mode (mesh editor: Geometry Detail) are drawn cluster by cluster, with the detail
        // chosen per cluster and frame. Everything else, and all shadow maps, use the discrete LOD levels. Debug views:
        // render modes of the category "Virtual Geometry"

        [[=category{"Virtual Geometry"}]]
        [[=display_name{"Virtual geometry"}]]
        [[=tooltip{"Draw meshes whose detail mode is Virtual Geometry (mesh editor: Geometry Detail) per cluster. Off: every mesh uses its discrete LOD levels"}]]
        bool virtual_geometry = true;

        [[=category{"Virtual Geometry"}]]
        [[=display_name{"Pixel error"}]]
        [[=tooltip{"How many pixels a cluster may deviate from the full-detail surface. 1 = not visible; higher values draw fewer triangles"}]]
        [[=range{0.25, 16.0}]]
        f32 virtual_geometry_pixel_error = 1.0f;

        [[=category{"Virtual Geometry"}]]
        [[=display_name{"Backface cluster culling"}]]
        [[=tooltip{"Skip clusters whose triangles all face away from the camera (not applied to double-sided materials or non-uniformly scaled objects)"}]]
        bool virtual_geometry_cone_culling = true;

        // ---- Shadows --------------------------------------------------------------
        // The sun's shadows are nested maps around the camera that reach up to the distance below. Each map is only
        // drawn again when the camera travelled out of it or something in it moved, so a long distance costs little
        // per frame; it makes the maps in between coarser (they share the range evenly on a logarithmic scale)

        [[=category{"Shadows|Sun"}]]
        [[=display_name{"Sun shadow distance (m)"}]]
        [[=tooltip{"How far from the camera the sun casts shadows at most (it stops earlier where the scene ends). Shorter distances give sharper shadows in the middle range"}]]
        [[=range{50.0, 5000.0}]]
        f32 sun_shadow_distance = 1500.0f;

        [[=category{"Shadows|Sun"}]]
        [[=display_name{"Sun shadow resolution"}]]
        [[=tooltip{"Size of each of the sun's shadow maps. Higher is sharper and uses four times the memory per step (Medium: 64 MB per viewport)"}]]
        [[=options{"1024=Low|2048=Medium|4096=High"}]]
        [[=range{1024.0, 4096.0}]]
        u32 sun_shadow_resolution = 2048u;

        // Point and spot lights keep their shadow maps until the light or something in its range moves, so what the
        // values below cost is mostly memory (the atlas) and the lit passes (every shadowed light a fragment is lit by
        // adds shadow map lookups), not drawing the maps.
        // All shadowed lights share one atlas texture. Lights within the shadow distance divide it among themselves
        // by their size on screen; lights between that and the far distance get small maps from what is left over

        [[=category{"Shadows|Point and spot lights"}]]
        [[=display_name{"Shadow-casting local lights"}]]
        [[=tooltip{"How many point / spot lights cast shadows at once (the ones that are largest on screen first). A point light needs six shadow maps, a spot light one; 2040 maps fit at most. 0 = only the sun casts shadows"}]]
        [[=range{0.0, 1024.0}]]
        u32 local_shadow_lights = 1024u;

        [[=category{"Shadows|Point and spot lights"}]]
        [[=display_name{"Local light shadow distance (m)"}]]
        [[=tooltip{"Point / spot lights closer to the camera than this cast shadows with full detail. More lights in range means smaller shadow maps for each, unless the atlas is larger"}]]
        [[=range{10.0, 1000.0}]]
        f32 local_shadow_distance = 80.0f;

        [[=category{"Shadows|Point and spot lights"}]]
        [[=display_name{"Local light far shadow distance (m)"}]]
        [[=tooltip{"Lights beyond the shadow distance keep casting coarse shadows up to here, as far as the atlas has room. Lights that only cover a few pixels are skipped. Below the shadow distance = off"}]]
        [[=range{0.0, 4000.0}]]
        f32 local_shadow_far_distance = 320.0f;

        [[=category{"Shadows|Point and spot lights"}]]
        [[=display_name{"Local light shadow atlas"}]]
        [[=tooltip{"Size of the texture that holds all point / spot light shadow maps. Larger: more lights with shadows and sharper ones. Memory per viewport: 16 MB / 64 MB / 256 MB"}]]
        [[=options{"2048=Small|4096=Medium|8192=Large"}]]
        [[=range{2048.0, 8192.0}]]
        u32 local_shadow_atlas = 4096u;

        // ---- Debug views --------------------------------------------------------
        [[=category{"Debug"}]]
        [[=display_name{"Triangle density blur (px)"}]]
        [[=tooltip{"Render mode 'Triangle Density': gaussian blur radius of the heatmap in pixels (0 = raw per-pixel counts)"}]]
        [[=range{0.0, 32.0}]]
        u32 tri_density_radius = 4u;

        [[=category{"Debug"}]]
        [[=display_name{"Triangle density max (tris / px)"}]]
        [[=tooltip{"Render mode 'Triangle Density': triangles per pixel shown as full red. The scale is logarithmic over 10 stops below it"}]]
        [[=range{0.01, 16.0}]]
        f32 tri_density_max = 1.0f;

        // Sky, sun and ambient light are world components now ("sky" / "ambient_light" / directional "light" on the
        // world entity), see GLT::world::environment_snapshot
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
