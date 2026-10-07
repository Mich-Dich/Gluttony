
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

    // @brief RAII smart-handle that owns one registry reference to an asset
    //
    // Constructing, copying, or copy-assigning an asset_ref calls registry::retain() on the underlying handle;
    // destroying, resetting, or move-assigning away from it calls registry::release(). 
    // The registry is therefore free to unload the asset the moment the last asset_ref to it goes away
    //
    // Ownership rules:
    // @brief   - Copyable     -> each copy is an additional reference
    // @brief   - Movable      -> the source is left INVALID_HANDLE, no ref-count churn
    // @brief   - Default-ctor -> creates an invalid, non-owning ref
    //
    // Implicitly converts to the raw handle, so an asset_ref can be passed anywhere a [handle] is expected without spelling out [.get()]
    class asset_ref {
    public:

        // @brief Constructs an invalid, non-owning reference
        asset_ref();


        // @brief Wraps `h`, taking ownership of one reference
        // @param h Asset handle to retain. INVALID_HANDLE is accepted and stored as-is (no retain call)
        asset_ref(handle h);


        // @brief Copy-constructs, retaining a second reference to the same asset
        // @param other Reference to copy the handle from
        asset_ref(const asset_ref& other);


        // @brief Move-constructs, transferring ownership from `other`
        // @param other Reference to move from. Left in an invalid state
        asset_ref(asset_ref&& other) noexcept;


        // @brief Releases this reference (if any) back to the registry
        ~asset_ref();


        // @brief Copy-assigns. Releases the current handle, then retains `other`'s
        // @param other Reference to copy from
        // @return *this
        asset_ref& operator=(const asset_ref& other);


        // @brief Move-assigns. Releases the current handle, then steals `other`'s
        // @param other Reference to move from. Left in an invalid state
        // @return *this
        asset_ref& operator=(asset_ref&& other) noexcept;


        // @brief Releases the held reference and makes this ref invalid
        //
        // Safe to call on an already-invalid ref. Called automatically by the
        // destructor, copy-assignment, and move-assignment
        void reset();


        // @brief Returns the raw asset handle without changing ownership
        // @return The wrapped handle, or INVALID_HANDLE when this ref is empty
        [[nodiscard]] handle get() const noexcept;


        // @brief Reports whether this ref currently holds a valid asset
        // @return true if the wrapped handle is not INVALID_HANDLE, false otherwise
        [[nodiscard]] bool valid() const noexcept;


        // @brief Implicit conversion to the raw handle
        // @return The wrapped handle, or INVALID_HANDLE when this ref is empty
        operator handle() const noexcept;

    private:

        handle                              m_handle{ INVALID_HANDLE };

    };

}
