#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::undo_system {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template <typename T>
    step make_value_step(std::string name, T* target, T before, T after, std::function<void()> on_change) {

        static_assert(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>, "make_value_step needs a copyable value");

        step result;
        result.name = std::move(name);
        result.undo = [target, value = std::move(before), on_change]() {
            *target = value;
            if (on_change)
                on_change();
        };
        result.redo = [target, value = std::move(after), on_change]() {
            *target = value;
            if (on_change)
                on_change();
        };
        return result;
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}
