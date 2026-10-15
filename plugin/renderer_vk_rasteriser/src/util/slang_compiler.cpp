
#include "util/pch.h"
#include "slang_compiler.h"

#include <slang.h>
#include <slang-com-ptr.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    inline constexpr u32                        RECREATE_SESSION_THRESHOLD = 200;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct linked_program::impl {

        Slang::ComPtr<slang::IComponentType>    component;
    };


    struct slang_compiler::impl {

        slang::IGlobalSession*                  global_session = nullptr;       // NOT owned after create()
        slang::ISession**                       session = nullptr;              // Slang::ComPtr would be cleaner
    };

    // STATIC VARIABLES ================================================================================================

    // One global session, process-wide. Created lazily on first slang_compiler::create().
    static slang::IGlobalSession*               g_global_session = nullptr;

    static slang::ISession*                     g_current_session = nullptr;

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

        if (m_impl->session && *m_impl->session) {
            (*m_impl->session)->release();
            *m_impl->session = nullptr;
        }

        delete m_impl;
        m_impl = nullptr;
    }


    slang_compiler::slang_compiler(slang_compiler&& other) noexcept
        : m_impl(other.m_impl)
        , m_compile_count(other.m_compile_count)
        , m_contract_dir(std::move(other.m_contract_dir)) {

        other.m_impl = nullptr;
        other.m_compile_count = 0;
    }


    slang_compiler& slang_compiler::operator=(slang_compiler&& other) noexcept {

        if (this != &other) {

            // Tear down our current state
            if (m_impl) {
                if (m_impl->session && *m_impl->session) {
                    (*m_impl->session)->release();
                    *m_impl->session = nullptr;
                }
                delete m_impl;
                m_impl = nullptr;
            }

            m_impl            = other.m_impl;
            m_compile_count   = other.m_compile_count;
            m_contract_dir    = std::move(other.m_contract_dir);

            other.m_impl = nullptr;
            other.m_compile_count = 0;
        }
        return *this;
    }

    // CLASS PUBLIC ====================================================================================================

    // linked_program --------------------------------------------------------------------------------------------------

    bool linked_program::valid() const noexcept { return m_impl != nullptr && m_impl->component != nullptr; }


    void* linked_program::raw_component() const noexcept { return m_impl ? static_cast<void*>(m_impl->component.get()) : nullptr; }

    // slang_compiler --------------------------------------------------------------------------------------------------

    std::expected<slang_compiler, slang_error> slang_compiler::create(const std::filesystem::path& contract_dir) {

        slang::IGlobalSession* global = ensure_global_session();
        if (!global)
            return std::unexpected{ slang_error{ raw_slang_error_code::session_creation_failed, ""} };

        slang::TargetDesc target = {};
        target.format = SLANG_SPIRV;
        target.profile = global->findProfile("spirv_1_6");

        const std::string contract_dir_str = contract_dir.string();
        const char* search_paths[] = { contract_dir_str.c_str() };

        slang::SessionDesc session_desc = {};
        session_desc.targets = &target;
        session_desc.targetCount = 1;
        session_desc.searchPaths = search_paths;
        session_desc.searchPathCount = 1;
        session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        const SlangResult res = global->createSession(session_desc, &g_current_session);
        if (SLANG_FAILED(res) || !g_current_session)
            return std::unexpected{ slang_error{ raw_slang_error_code::session_creation_failed, ""} };

        slang_compiler out{};
        out.m_impl = new impl{};
        out.m_impl->global_session = global;
        out.m_impl->session = &g_current_session;
        out.m_contract_dir  = contract_dir;
        return out;
    }


    std::expected<raw_slang_result, slang_error> slang_compiler::compile(std::string_view source, std::string_view entry_point_name,
        const std::filesystem::path& source_virtual_name) {


        if (!m_impl || !m_impl->session)
            return std::unexpected{ slang_error{ raw_slang_error_code::session_creation_failed, ""} };

        manage_session();

        // manage_session() may have swapped the session; re-check
        if (!*m_impl->session)
            return std::unexpected{ slang_error{ raw_slang_error_code::session_creation_failed, ""} };

        // Slang caches loaded modules by name and path for the lifetime of the session. A stable name (e.g. "material.vert") combined 
        // with an edited source trips E38202 ("module already loaded with different source"). Folding a hash of the source into the
        // name means:
        //   - identical sources hit the cache (no recompile)
        //   - edited sources resolve to a fresh slot
        // The virtual path stays stable so relative imports keep working
        const u64 source_hash = GLT::util::fnv1a_64(source);
        const std::string module_name = std::format("{}_{:016X}", source_virtual_name.stem().string(), source_hash);
        const std::string unique_virtual_path = std::format("{}_{:016X}{}", source_virtual_name.stem().string(), source_hash,
            source_virtual_name.extension().string());
        const std::string source_copy{ source };

        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* module = (*m_impl->session)->loadModuleFromSourceString(module_name.c_str(), unique_virtual_path.c_str(), 
            source_copy.c_str(), diagnostics.writeRef());

        if (!module) {

            slang_error error{};
            error.code = raw_slang_error_code::source_load_failed;

            if (diagnostics) {
                error.message.assign(static_cast<const char*>(diagnostics->getBufferPointer()),
                    diagnostics->getBufferSize());

                // The pinned Slang version doesn't expose a structured "already cached" status, so we key off the diagnostic text
                // E38202 is the documented error code for "module already loaded with different source"; anything else is a 
                // genuine load failure
                if (error.message.find("E38202") != std::string::npos || error.message.find("already loaded") != std::string::npos)
                    error.code = raw_slang_error_code::already_compiled;
            }

            LOG(warn, "Slang error {}", error.message)
            return std::unexpected{ error };
        }

        Slang::ComPtr<slang::IEntryPoint> entry_point;
        const std::string entry_name{ entry_point_name };
        if (SLANG_FAILED(module->findEntryPointByName(entry_name.c_str(), entry_point.writeRef())))
            return std::unexpected{ slang_error{raw_slang_error_code::entry_point_missing, ""} };

        slang::IComponentType* components[] = { module, entry_point.get() };
        Slang::ComPtr<slang::IComponentType> program;
        if (SLANG_FAILED((*m_impl->session)->createCompositeComponentType(components, 2, program.writeRef(), diagnostics.writeRef())))
            return std::unexpected{ slang_error{ raw_slang_error_code::link_failed, ""} };

        Slang::ComPtr<slang::IComponentType> linked;
        if (SLANG_FAILED(program->link(linked.writeRef(), diagnostics.writeRef()))) {
            slang_error error{};
            error.code = raw_slang_error_code::link_failed;
            if (diagnostics)
                error.message.assign(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
            return std::unexpected{ error };
        }

        Slang::ComPtr<slang::IBlob> spv_blob;
        if (SLANG_FAILED(linked->getEntryPointCode(0, 0, spv_blob.writeRef(), diagnostics.writeRef())))
            return std::unexpected{ slang_error{ raw_slang_error_code::spirv_emission_failed, ""} };

        raw_slang_result result{};
        result.slang_module_name = module_name;
        result.spv.resize(spv_blob->getBufferSize());
        std::memcpy(result.spv.data(), spv_blob->getBufferPointer(), spv_blob->getBufferSize());

        result.linked.m_impl = std::make_unique<linked_program::impl>();
        result.linked.m_impl->component = linked;

        return result;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void slang_compiler::manage_session() {

        if (++m_compile_count <= RECREATE_SESSION_THRESHOLD)
            return;

        ASSERT(!m_contract_dir.empty(), "", "Called compile() before the slang_compiler was created")

        // Release the global session. m_impl->session still points at &g_current_session,
        // so after we assign g_current_session below the compiler will see the new one
        if (g_current_session) {
            g_current_session->release();
            g_current_session = nullptr;
        }

        slang::TargetDesc target = {};
        target.format = SLANG_SPIRV;
        target.profile = m_impl->global_session->findProfile("spirv_1_6");

        const std::string contract_dir_str = m_contract_dir.string();
        const char* search_paths[] = { contract_dir_str.c_str() };

        slang::SessionDesc session_desc = {};
        session_desc.targets = &target;
        session_desc.targetCount = 1;
        session_desc.searchPaths = search_paths;
        session_desc.searchPathCount = 1;
        session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        slang::ISession* new_session = nullptr;
        const SlangResult res = m_impl->global_session->createSession(session_desc, &new_session);
        if (SLANG_SUCCEEDED(res) && new_session) {
            g_current_session = new_session;
            m_compile_count = 0;
        }
    }

}
