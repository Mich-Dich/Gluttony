
#include "util/pch.h"

#include "asset_ref.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    asset_ref::asset_ref() = default;


    asset_ref::asset_ref(handle h) : m_handle(h) {

        if (m_handle != INVALID_HANDLE)
            registry::get_ref()->retain(m_handle);
    }


    asset_ref::asset_ref(const asset_ref& other) : m_handle(other.m_handle) {

        if (m_handle != INVALID_HANDLE)
            registry::get_ref()->retain(m_handle);
    }


    asset_ref::asset_ref(asset_ref&& other) noexcept : m_handle(std::exchange(other.m_handle, INVALID_HANDLE)) {}


    asset_ref::~asset_ref() { reset(); }

    // CLASS PUBLIC ====================================================================================================

    void asset_ref::reset() {
        if (m_handle != INVALID_HANDLE) {
            registry::get_ref()->release(m_handle);
            m_handle = INVALID_HANDLE;
        }
    }


    [[nodiscard]] handle asset_ref::get() const noexcept { return m_handle; }


    [[nodiscard]] bool asset_ref::valid() const noexcept { return m_handle != INVALID_HANDLE; }


    asset_ref& asset_ref::operator=(const asset_ref& other) {

        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            if (m_handle != INVALID_HANDLE)
                registry::get_ref()->retain(m_handle);
        }
        return *this;
    }


    asset_ref& asset_ref::operator=(asset_ref&& other) noexcept {

        if (this != &other) {
            reset();
            m_handle = std::exchange(other.m_handle, INVALID_HANDLE);
        }
        return *this;
    }


    asset_ref::operator handle() const noexcept { return m_handle; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
