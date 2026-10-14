
#include "util/pch.h"
#include "slang_compiler.h"

#include <slang.h>
#include <slang-com-ptr.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct linked_program::impl {

        Slang::ComPtr<slang::IComponentType> component;
    };


    struct slang_compiler::impl {

        slang::IGlobalSession*  global_session = nullptr;   // NOT owned after create()
        slang::ISession*        session = nullptr;   // Slang::ComPtr would be cleaner
    };

    // STATIC VARIABLES ================================================================================================

    // One global session, process-wide. Created lazily on first slang_compiler::create().
    static slang::IGlobalSession* g_global_session = nullptr;

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    static slang::IGlobalSession* ensure_global_session();

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static slang::IGlobalSession* ensure_global_session() {

        if (g_global_session)
            return g_global_session;

        const SlangResult res = slang::createGlobalSession(&g_global_session);
        return SLANG_SUCCEEDED(res) ? g_global_session : nullptr;
    }

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // linked_program --------------------------------------------------------------------------------------------------

    linked_program::linked_program() = default;


    linked_program::~linked_program() = default;


    linked_program::linked_program(linked_program&&) noexcept = default;


    linked_program& linked_program::operator=(linked_program&&) noexcept = default;

    // slang_compiler --------------------------------------------------------------------------------------------------

    slang_compiler::slang_compiler() : m_impl(nullptr) {}


    slang_compiler::~slang_compiler() {

        if (!m_impl)
            return;

        if (m_impl->session)
            m_impl->session->release();

        delete m_impl;
        m_impl = nullptr;
    }


    slang_compiler::slang_compiler(slang_compiler&& other) noexcept
        : m_impl(other.m_impl) {

        other.m_impl = nullptr;
    }


    slang_compiler& slang_compiler::operator=(slang_compiler&& other) noexcept {

        if (this != &other) {
            this->~slang_compiler();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // linked_program --------------------------------------------------------------------------------------------------

    bool linked_program::valid() const noexcept { return m_impl != nullptr && m_impl->component != nullptr; }

    void* linked_program::raw_component() const noexcept { return m_impl ? static_cast<void*>(m_impl->component.get()) : nullptr; }

    // slang_compiler --------------------------------------------------------------------------------------------------

    std::string_view slang_toolchain_version() {

        static const std::string s_version = []() -> std::string {
            if (!g_global_session)
                return "slang-unknown";

            // IGlobalSession provides the build tag string
            const char* tag = g_global_session->getBuildTagString();
            return tag ? std::string(tag) : std::string("slang-unknown");
        }();
        return s_version;
    }


    std::expected<slang_compiler, raw_slang_error> slang_compiler::create(const std::filesystem::path& contract_dir) {

        slang::IGlobalSession* global = ensure_global_session();
        if (!global)
            return std::unexpected{ raw_slang_error::session_creation_failed };

        // ---- Target: SPIR-V 1.5 / Vulkan 1.2, matching the glslang path -----------------

        slang::TargetDesc target = {};
        target.format = SLANG_SPIRV;
        target.profile = global->findProfile("spirv_1_5");
        target.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

        // ---- Include search path: the material contract package ------------------------

        const std::string contract_dir_str = contract_dir.string();
        const char* search_paths[] = { contract_dir_str.c_str() };

        slang::SessionDesc session_desc = {};
        session_desc.targets = &target;
        session_desc.targetCount = 1;
        session_desc.searchPaths = search_paths;
        session_desc.searchPathCount = 1;

        slang::ISession* session = nullptr;
        const SlangResult res = global->createSession(session_desc, &session);
        if (SLANG_FAILED(res) || !session)
            return std::unexpected{ raw_slang_error::session_creation_failed };

        auto out = slang_compiler{};
        out.m_impl = new impl{};
        out.m_impl->global_session = global;
        out.m_impl->session = session;
        return out;
    }


    std::expected<raw_slang_result, raw_slang_error> slang_compiler::compile(std::string_view source, std::string_view entry_point_name,
        const std::filesystem::path& source_virtual_name) {


        if (!m_impl || !m_impl->session)
            return std::unexpected{ raw_slang_error::session_creation_failed };

        Slang::ComPtr<slang::IBlob> diagnostics;

        // Distinct module name per virtual path so Slang doesn't hand back a cached module.
        const std::string module_name = source_virtual_name.stem().string();
        const std::string virtual_name = source_virtual_name.string();
        const std::string source_copy{ source };

        slang::IModule* module = m_impl->session->loadModuleFromSourceString(module_name.c_str(), virtual_name.c_str(), 
            source_copy.c_str(), diagnostics.writeRef());

        if (!module) {
            raw_slang_result err{};
            if (diagnostics)
                err.diagnostics.assign(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
            return std::unexpected{ raw_slang_error::source_load_failed };
        }

        Slang::ComPtr<slang::IEntryPoint> entry_point;
        const std::string entry_name{ entry_point_name };
        if (SLANG_FAILED(module->findEntryPointByName(entry_name.c_str(), entry_point.writeRef())))
            return std::unexpected{ raw_slang_error::entry_point_missing };

        slang::IComponentType* components[] = { module, entry_point.get() };
        Slang::ComPtr<slang::IComponentType> program;
        if (SLANG_FAILED(m_impl->session->createCompositeComponentType(components, 2, program.writeRef(), diagnostics.writeRef())))
            return std::unexpected{ raw_slang_error::link_failed };

        Slang::ComPtr<slang::IComponentType> linked;
        if (SLANG_FAILED(program->link(linked.writeRef(), diagnostics.writeRef()))) {
            raw_slang_result err{};
            if (diagnostics)
                err.diagnostics.assign(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
            return std::unexpected{ raw_slang_error::link_failed };
        }

        Slang::ComPtr<slang::IBlob> spv_blob;
        if (SLANG_FAILED(linked->getEntryPointCode(0, 0, spv_blob.writeRef(), diagnostics.writeRef())))
            return std::unexpected{ raw_slang_error::spirv_emission_failed };

        raw_slang_result result{};
        result.slang_module_name = module_name;
        result.spv.resize(spv_blob->getBufferSize());
        std::memcpy(result.spv.data(), spv_blob->getBufferPointer(), spv_blob->getBufferSize());

        result.linked.m_impl = std::make_unique<linked_program::impl>();
        result.linked.m_impl->component = linked;

        return result;
    }

}
