
#include "util/pch.h"
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <util/io/vfs.h>

#include "shader_compiler.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray::utils {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    bool shader_compiler::initialized = false;

    // FUNCTION IMPLEMENTATION =========================================================================================

    template<typename T>
    bool file_read(const std::string& file_path, std::vector<T>& out_vector) {

        //open the file. With cursor at the end
        std::ifstream file(file_path, std::ios::ate | std::ios::binary);
        VALIDATE(file.is_open(), return false, "", "Failed to open file: {}", file_path.c_str())

        // if the file size is not a multiple of the size of the type, we need to add one to the size of the vector,
        // so in the case of a vector of u32, if the file size is 5 bytes we need a vector of size 2 of u32, which is 8 bytes

        size_t file_size = static_cast<size_t>(file.tellg());
        out_vector.resize(file_size / sizeof(T));

        file.seekg(0);                                          //put file cursor at beginning
        file.read((char*)out_vector.data(), file_size);           //load the entire file into the buffer
        file.close();                                           //now that the file is loaded into the buffer, we can close it
        return true;
    }

    bool file_read(const std::filesystem::path& file_path, std::string& source_code) {

        std::ifstream file(file_path);
        VALIDATE(file.is_open(), return false, "", "Failed to open GLSL file: {}", file_path.string().c_str())

        std::stringstream buffer;
        buffer << file.rdbuf();
        source_code = buffer.str();
        file.close();
        return true;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    shader_compiler::shader_compiler() {

        if (!initialized) {

            glslang::InitializeProcess();
            initialized = true;
        }
    }


    shader_compiler::~shader_compiler() {

        if (initialized) {

            glslang::FinalizeProcess();
            initialized = false;
        }
    }

    // CLASS PUBLIC ====================================================================================================

    std::vector<u32> shader_compiler::compile_glsl_to_spirv(const std::filesystem::path& source_path) {

        // Read the GLSL source file
        std::string source_code;
        std::error_code error{};
        GLT::vfs::file_handle opend_file = GLT::vfs::open_file(source_path, GLT::vfs::file_open_mode::read, error);
        VALIDATE(!error && opend_file != 0, return {}, "", "Failed to open file [{}]", source_path.generic_string())

        error.clear();
        const u64 file_size = GLT::vfs::file_size(source_path, error);
        
        source_code.resize(file_size);
        const u64 read_size = GLT::vfs::read_file(opend_file, source_code.data(), file_size, 0);
        VALIDATE(read_size > 0, return {}, "", "Failed to read file content")

        // Determine shader stage based on file extension or content
        EShLanguage shader_stage = EShLangVertex;
        std::string filename = source_path.filename().string();
        size_t first_dot = filename.find_first_of('.');             // Parse multiple extensions (e.g., "raygen.rgen.glsl")
        size_t last_dot = filename.find_last_of('.');

        std::string extention;
        if (first_dot != last_dot && first_dot != std::string::npos) {

            std::string middle_part = filename.substr(first_dot + 1, last_dot - first_dot - 1);     // Has multiple extensions, get the middle one
            extention = middle_part;
            size_t middle_dot = middle_part.find_last_of('.');                                      // The middle part might also have dots, so let's get the actual extension
            if (middle_dot != std::string::npos)
                extention = middle_part.substr(middle_dot + 1);

        } else {

            extention = source_path.extension().string();                                           // Single extension
            if (!extention.empty() && extention[0] == '.')
                extention = extention.substr(1);                                                    // Remove leading dot
        }

        // Map file extensions to shader stages
        if (extention == "vert") shader_stage = EShLangVertex;
        else if (extention == "frag") shader_stage = EShLangFragment;
        else if (extention == "comp") shader_stage = EShLangCompute;
        else if (extention == "geom") shader_stage = EShLangGeometry;
        else if (extention == "tesc") shader_stage = EShLangTessControl;
        else if (extention == "tese") shader_stage = EShLangTessEvaluation;
        else if (extention == "rgen") shader_stage = EShLangRayGen;
        else if (extention == "rahit") shader_stage = EShLangAnyHit;
        else if (extention == "rchit") shader_stage = EShLangClosestHit;
        else if (extention == "rmiss") shader_stage = EShLangMiss;
        else if (extention == "rint") shader_stage = EShLangIntersect;
        else if (extention == "rcall") shader_stage = EShLangCallable;
        else {
            // Try to detect from source content
            if (source_code.find("layout(rgba32f") != std::string::npos) shader_stage = EShLangCompute;
            else if (source_code.find("RayDesc") != std::string::npos) shader_stage = EShLangRayGen;
        }

        // Create shader object
        glslang::TShader shader(shader_stage);
        const char* shader_strings[] = { source_code.c_str() };
        shader.setStrings(shader_strings, 1);

        // Set up Vulkan/SPIR-V environment
        shader.setEnvInput(glslang::EShSourceGlsl, shader_stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

        // Set up default resources
        TBuiltInResource resources = {};
        resources.maxLights = 32;
        resources.maxClipPlanes = 6;
        resources.maxTextureUnits = 32;
        resources.maxTextureCoords = 32;
        resources.maxVertexAttribs = 64;
        resources.maxVertexUniformComponents = 4096;
        resources.maxVaryingFloats = 64;
        resources.maxVertexTextureImageUnits = 32;
        resources.maxCombinedTextureImageUnits = 80;
        resources.maxTextureImageUnits = 32;
        resources.maxFragmentUniformComponents = 4096;
        resources.maxDrawBuffers = 32;
        resources.maxVertexUniformVectors = 128;
        resources.maxVaryingVectors = 8;
        resources.maxFragmentUniformVectors = 16;
        resources.maxVertexOutputVectors = 16;
        resources.maxFragmentInputVectors = 15;
        resources.minProgramTexelOffset = -8;
        resources.maxProgramTexelOffset = 7;
        resources.maxClipDistances = 8;
        resources.maxComputeWorkGroupCountX = 65535;
        resources.maxComputeWorkGroupCountY = 65535;
        resources.maxComputeWorkGroupCountZ = 65535;
        resources.maxComputeWorkGroupSizeX = 1024;
        resources.maxComputeWorkGroupSizeY = 1024;
        resources.maxComputeWorkGroupSizeZ = 64;
        resources.maxComputeUniformComponents = 1024;
        resources.maxComputeTextureImageUnits = 16;
        resources.maxComputeImageUniforms = 8;
        resources.maxComputeAtomicCounters = 8;
        resources.maxComputeAtomicCounterBuffers = 1;
        resources.maxVaryingComponents = 60;
        resources.maxVertexOutputComponents = 64;
        resources.maxGeometryInputComponents = 64;
        resources.maxGeometryOutputComponents = 128;
        resources.maxFragmentInputComponents = 128;
        resources.maxImageUnits = 8;
        resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
        resources.maxCombinedShaderOutputResources = 8;
        resources.maxImageSamples = 0;
        resources.maxVertexImageUniforms = 0;
        resources.maxTessControlImageUniforms = 0;
        resources.maxTessEvaluationImageUniforms = 0;
        resources.maxGeometryImageUniforms = 0;
        resources.maxFragmentImageUniforms = 8;
        resources.maxCombinedImageUniforms = 8;
        resources.maxGeometryTextureImageUnits = 16;
        resources.maxGeometryOutputVertices = 256;
        resources.maxGeometryTotalOutputComponents = 1024;
        resources.maxGeometryUniformComponents = 1024;
        resources.maxGeometryVaryingComponents = 64;
        resources.maxTessControlInputComponents = 128;
        resources.maxTessControlOutputComponents = 128;
        resources.maxTessControlTextureImageUnits = 16;
        resources.maxTessControlUniformComponents = 1024;
        resources.maxTessControlTotalOutputComponents = 4096;
        resources.maxTessEvaluationInputComponents = 128;
        resources.maxTessEvaluationOutputComponents = 128;
        resources.maxTessEvaluationTextureImageUnits = 16;
        resources.maxTessEvaluationUniformComponents = 1024;
        resources.maxTessPatchComponents = 120;
        resources.maxPatchVertices = 32;
        resources.maxTessGenLevel = 64;
        resources.maxViewports = 16;
        resources.maxVertexAtomicCounters = 0;
        resources.maxTessControlAtomicCounters = 0;
        resources.maxTessEvaluationAtomicCounters = 0;
        resources.maxGeometryAtomicCounters = 0;
        resources.maxFragmentAtomicCounters = 8;
        resources.maxCombinedAtomicCounters = 8;
        resources.maxAtomicCounterBindings = 1;
        resources.maxVertexAtomicCounterBuffers = 0;
        resources.maxTessControlAtomicCounterBuffers = 0;
        resources.maxTessEvaluationAtomicCounterBuffers = 0;
        resources.maxGeometryAtomicCounterBuffers = 0;
        resources.maxFragmentAtomicCounterBuffers = 1;
        resources.maxCombinedAtomicCounterBuffers = 1;
        resources.maxAtomicCounterBufferSize = 16384;
        resources.maxTransformFeedbackBuffers = 4;
        resources.maxTransformFeedbackInterleavedComponents = 64;
        resources.maxCullDistances = 8;
        resources.maxCombinedClipAndCullDistances = 8;
        resources.maxSamples = 4;
        resources.limits.nonInductiveForLoops = 1;
        resources.limits.whileLoops = 1;
        resources.limits.doWhileLoops = 1;
        resources.limits.generalUniformIndexing = 1;
        resources.limits.generalAttributeMatrixVectorIndexing = 1;
        resources.limits.generalVaryingIndexing = 1;
        resources.limits.generalSamplerIndexing = 1;
        resources.limits.generalVariableIndexing = 1;
        resources.limits.generalConstantMatrixVectorIndexing = 1;

        // Preprocess and parse the shader
        EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

        if (!shader.parse(&resources, 100, false, messages)) {
            LOG(error, "GLSL Parsing Failed [{}]", shader.getInfoLog());
            LOG(error, "GLSL Info Debug Log [{}]", shader.getInfoDebugLog());
            return {};
        }

        // Link shader (even single shader needs linking for SPIR-V generation)
        glslang::TProgram program;
        program.addShader(&shader);

        if (!program.link(messages)) {
            LOG(error, "GLSL Linking Failed [{}]", program.getInfoLog());
            return {};
        }

        // Generate SPIR-V
        std::vector<unsigned int> spirv;
        glslang::GlslangToSpv(*program.getIntermediate(shader_stage), spirv);

        std::vector<u32> result;
        result.reserve(spirv.size());
        for (unsigned int val : spirv) {
            result.push_back(static_cast<u32>(val));
        }
        return result;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
