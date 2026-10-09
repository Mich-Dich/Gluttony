#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    template<typename Setup, typename Execute>
    void render_graph::add_pass(std::string name, Setup&& setup, Execute&& execute) {

        m_passes.emplace_back();
        pass& pass = m_passes.back();
        pass.name = std::move(name);

        pass_builder builder;
        builder.set_name(pass.name);
        builder.graph = this;
        setup(builder);

        pass.builder = std::move(builder);
        pass.execute = [fn = std::forward<Execute>(execute)] (pass_context& context) mutable { fn(context); };
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
