
#pragma once

// Using PIMP to not include/declare the SLANG types in the header



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class raw_slang_error_code : u32 {

        none = 0,
        session_creation_failed,
        source_load_failed,
        entry_point_missing,
        link_failed,
        spirv_emission_failed,
        already_compiled,
    };


    struct slang_error {

        raw_slang_error_code                    code = raw_slang_error_code::none;
        std::string                             message{};
    };


    // @brief Opaque handle to a Slang linked program
    //
    // Keeps the slang::IComponentType alive so the caller can walk its reflection (build_reflection_schema) after the SPIR-V
    // has been emitted. Defined in the .cpp so <slang.h> stays out of this header
    class linked_program {
    public:

        linked_program();
        ~linked_program();

        linked_program(linked_program&&) noexcept;
        linked_program& operator=(linked_program&&) noexcept;

        DELETE_COPY_CONSTRUCTOR(linked_program)


        [[nodiscard]] bool valid() const noexcept;


        // Raw slang::IComponentType*; only usable inside translation units that include <slang.h>. Lifetime is tied to this handle
        [[nodiscard]] void* raw_component() const noexcept;

    private:

        struct impl;
        std::unique_ptr<impl> m_impl;

        friend class slang_compiler;
    };


    struct raw_slang_result {

        std::vector<std::byte>  spv;                        // SPIR-V bytecode
        std::string             slang_module_name;
        std::string             diagnostics;                // populated on failure paths
        linked_program          linked;                     // reflection accessor
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    [[nodiscard]] std::string_view slang_toolchain_version();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class slang_compiler {
    public:

        static std::expected<slang_compiler, slang_error> create(const std::filesystem::path& contract_dir);

        slang_compiler();
        ~slang_compiler();

        slang_compiler(slang_compiler&&) noexcept;
        slang_compiler& operator=(slang_compiler&&) noexcept;
        DELETE_COPY_CONSTRUCTOR(slang_compiler)

        // @brief Compiles one source string and emits SPIR-V for one entry point.
        //
        // The source is the full concatenated translation unit (prefix + material + suffix). `source_virtual_name` doubles as
        // the module cache key: pass a distinct value per entry point so Slang compiles the three variants independently
        [[nodiscard]] std::expected<raw_slang_result, slang_error> compile(std::string_view source, std::string_view entry_point_name,
            const std::filesystem::path& source_virtual_name);

    private:

        void manage_session();


        struct impl;
        impl*                               m_impl = nullptr;
        u32                                 m_compile_count = 0;
        std::filesystem::path               m_contract_dir{};
    };

}
