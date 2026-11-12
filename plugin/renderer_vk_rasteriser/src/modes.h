
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::modes {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using GLT::reflect::annotations::display_name;
    using GLT::reflect::annotations::tooltip;
    using GLT::reflect::annotations::category;


    [[=display_name{"Lit"}]]
    [[=tooltip{"Full shading with lighting"}]]
    [[=category{"Shading"}]]
    inline constexpr GLT::render::render_mode Lit{ .id = 0, .is_default = true };


    // [[=display_name{"Unlit"}]]
    // [[=tooltip{"Flat surface color, no lighting"}]]
    // [[=category{"Shading"}]]
    // inline constexpr GLT::render::render_mode Unlit{ .id = 1 };


    [[=display_name{"Light Culling"}]]
    [[=tooltip{"Heatmap of lights per cluster (debug)"}]]
    [[=category{"Debug"}]]
    inline constexpr GLT::render::render_mode Light_cull{ .id = 2 };

    [[=display_name{"Level of Detail"}]]
    [[=tooltip{"Meshes coloured by the LOD they are drawn with: green 0, yellow-green 1, yellow 2, orange 3, red 4, magenta 5, purple 6, blue 7. Dark grey = no LODs generated, light grey = procedural. Virtual geometry: colour = hierarchy level of each cluster (see the Virtual Geometry views for more)"}]]
    [[=category{"Debug"}]]
    inline constexpr GLT::render::render_mode Lod{ .id = 3 };

    [[=display_name{"Triangle Density"}]]
    [[=tooltip{"Heatmap of triangles per pixel over a flat, unlit world (blue = sparse, red = one or more triangles per pixel). Blur radius and range: renderer settings, Debug"}]]
    [[=category{"Debug"}]]
    inline constexpr GLT::render::render_mode Tri_density{ .id = 4 };

    // ---- virtual geometry debug views (scene_renderer_virtual_geometry_debug.inl) ---------------------------------
    // Drawn by one dedicated pass from the commands the lit path draws, so they show exactly what is on screen.
    // Meshes that are not on the virtual geometry path are grey in all of them (except "Triangles")

    [[=display_name{"Geometry Clusters"}]]
    [[=tooltip{"Virtual geometry: every drawn cluster in its own colour. Shows the clusters and how their size on the surface changes with distance. Grey = mesh drawn with discrete LODs"}]]
    [[=category{"Virtual Geometry"}]]
    inline constexpr GLT::render::render_mode Vg_clusters{ .id = 5 };

    [[=display_name{"Cluster Levels"}]]
    [[=tooltip{"Virtual geometry: clusters coloured by their level in the hierarchy. Green = source triangles, then yellow, orange, red, magenta, purple, blue, cyan as they get coarser (about half the triangles per level). Grey = mesh drawn with discrete LODs"}]]
    [[=category{"Virtual Geometry"}]]
    inline constexpr GLT::render::render_mode Vg_levels{ .id = 6 };

    [[=display_name{"Triangles"}]]
    [[=tooltip{"Every triangle that is drawn in its own colour (all meshes). Shows the real triangle size on screen: virtual geometry should stay at a few pixels per triangle at any distance"}]]
    [[=category{"Virtual Geometry"}]]
    inline constexpr GLT::render::render_mode Vg_triangles{ .id = 7 };

    [[=display_name{"Screen Error"}]]
    [[=tooltip{"Virtual geometry: simplification error of each drawn cluster on screen, relative to the allowed pixel error. Dark blue = source triangles (no error), blue to green to yellow to red = 0 to 100 % of the limit. Grey = mesh drawn with discrete LODs"}]]
    [[=category{"Virtual Geometry"}]]
    inline constexpr GLT::render::render_mode Vg_error{ .id = 8 };

    // STATIC VARIABLES ================================================================================================

    inline constexpr auto mode_table = GLT::render::make_render_mode_entries<^^modes>();

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
