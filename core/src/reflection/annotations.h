
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::reflect {

    // @brief Text inside an annotation: [[=display_name{"Lit"}]]
    //
    // Annotation values are read back with std::meta::extract. A `const char*` member pointing to a string literal cannot
    // be (GCC 16: "reflect_constant failed", a pointer to a string literal is not a valid constant); a char array can.
    // Longer text is cut at N - 1 characters
    template <std::size_t N>
    struct annotation_text {

        char                    data[N]{};

        consteval annotation_text() = default;
        consteval annotation_text(const char* text) {
            std::size_t i = 0;
            for (; text && text[i] && i + 1 < N; ++i)
                data[i] = text[i];
        }

        [[nodiscard]] constexpr const char* c_str() const noexcept { return data; }
        [[nodiscard]] constexpr bool empty() const noexcept { return data[0] == 0; }
        constexpr operator std::string_view() const noexcept { return std::string_view(data); }
    };

    using short_text = annotation_text<128>;
    using long_text = annotation_text<1024>;

}

namespace GLT::reflect::annotations {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // serialization ---------------------------------------------------------------------------------------------------
    struct serialize {};                        // default-on for non-static data members


    struct skip {};                             // never serialize, never show in editor


    struct asset_ref {};                        // serialize as UUID, retain/release on load


    struct version {                            // overrides the descriptor's default version
        u16                     value;
    };

    // editor hints ----------------------------------------------------------------------------------------------------

    struct display_name {

        short_text              value;
    };


    struct tooltip {

        long_text               value;
    };


    struct category {

        short_text              value;
    };


    struct range {

        f64                     min;
        f64                     max; 
    };


    // An integer member that is a choice: one entry per value, '|' separated, optionally "value=label" for values that
    // are not 0, 1, 2, ... in order. Examples: options{"ACES|AgX|Reinhard|None"}, options{"1024=Low|2048=Medium|4096=High"}
    struct options {
        long_text               value;
    };
    struct color {};


    struct multiline {};


    struct readonly {};

    // kind annotations (exactly one per class) ------------------------------------------------------------------------

    struct entity_script {};


    struct global_script {};


    struct world_generator {};


    struct editor_action {

        short_text              label;
    };

    // field annotations -----------------------------------------------------------------------------------------------

    struct persist {};


    struct transient {};                        // runtime-only, drop on save


    struct editor {

        short_text              category{};
    };

    // class annotations -----------------------------------------------------------------------------------------------

    struct name {

        short_text              value;
    };


    struct priority {

        i32                     value;
    };

    // function annotations --------------------------------------------------------------------------------------------

    struct editor_callable {

        short_text              label{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
