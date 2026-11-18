#pragma once

#include <procedural/construct/registry.h>
#include <procedural/construct/evaluate.h>

// The spaceship pack: an internal frame from the hull's shape. Engine-independent (tested by ship_self_test).
//
// Guides
//   volume, role ship.hull        the outer shell (any primitive + loop cuts / edits). Ribs follow its sections
//   volume, role ship.component   a blocking volume (reactor, cargo, backbone): ribs are cut away around it
//
// Passes
//   primary    ship.frames     a frame station every ship.frame_spacing metres along ship.axis -> ship.rib elements
//   secondary  ship.stringers  ship.stringers longitudinal members through all stations
//
// Realizers: ship.rib (hull section, inset by the rib depth, extruded by its thickness, cut around components),
// ship.stringer (square tube), ship.hull (skin, optional), ship.component (block-out, optional)
// Outputs: "frame", "hull", "components"



namespace GLT::construct::ship {

    // CONSTANTS =======================================================================================================

    inline constexpr std::string_view                   PACK_NAME = "spaceship";

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] role_id role_hull();
    [[nodiscard]] role_id role_component();
    [[nodiscard]] kind_id kind_rib();
    [[nodiscard]] kind_id kind_stringer();

    namespace tags {
        [[nodiscard]] attribute_id frame_spacing();
        [[nodiscard]] attribute_id rib_depth();
        [[nodiscard]] attribute_id rib_thickness();
        [[nodiscard]] attribute_id axis();
        [[nodiscard]] attribute_id stringers();
        [[nodiscard]] attribute_id stringer_size();
        [[nodiscard]] attribute_id show_skin();
        [[nodiscard]] attribute_id clearance();
        [[nodiscard]] attribute_id show_component();
    }

    void register_pack(i_registry& registry, std::string_view owner);

    // A capsule hull along z with a reactor, a backbone and a cargo bay
    [[nodiscard]] construct_data make_default_ship();

}
