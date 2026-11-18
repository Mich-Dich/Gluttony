#pragma once

#include "procedural/construct/evaluate.h"

// Procedural construction: a small example pack ("demo") and a construct that uses it. It is the reference for writing a pack and
// the content of the editor's "Construct > Spawn demo" action.
//
//   A curve guide with role "demo.railing" becomes a railing: the production demo.posts places a post every
//   demo.spacing metres along it (children "post[i]"), the realizer demo.post builds each post, the realizer
//   demo.railing sweeps a top rail and a middle rail along the guide.



namespace GLT::construct {

    inline constexpr std::string_view                   DEMO_PACK = "demo";

    void register_demo_pack(i_registry& registry, std::string_view owner);

    // A deck (box guide, loop-cut, with a material region), a railing along its edge, a mirrored buoy and a lamp marker
    [[nodiscard]] std::shared_ptr<construct_data> make_demo_construct();

}
