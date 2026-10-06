
#pragma once

#include "asset/type.h"
#include "asset/i_asset_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class asset_ref {
    public:

        asset_ref();

        asset_ref(handle h);

        asset_ref(const asset_ref& other);

        asset_ref(asset_ref&& other) noexcept;

        ~asset_ref();


        asset_ref& operator=(const asset_ref& other);


        asset_ref& operator=(asset_ref&& other) noexcept;


        void reset();


        [[nodiscard]] handle get() const noexcept;


        [[nodiscard]] bool valid() const noexcept;


        operator handle() const noexcept;

    private:

        handle                              m_handle{ INVALID_HANDLE };

    };

}
