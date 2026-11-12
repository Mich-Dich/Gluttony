
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
        [[=tooltip{"0 = ACES (fitted)   1 = AgX   2 = Reinhard   3 = None (clamp)"}]]
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
