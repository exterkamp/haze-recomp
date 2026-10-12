#include "shader_glsl.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>
#include <mutex>
#include <vector>

namespace {
void replace_all(std::string& source, const std::string& before, const std::string& after) {
    size_t offset = 0;
    while ((offset = source.find(before, offset)) != std::string::npos) {
        source.replace(offset, before.size(), after);
        offset += after.size();
    }
}
std::string separate_samplers(const char* hlsl) {
    std::string source = hlsl;
    // RSX texture unit indices are instruction immediates. Convert the
    // decompiler's legacy resource arrays to individual bindings so GLSL ES
    // can combine each image with its corresponding sampler.
    for (const auto& type : {std::string("Texture2D    "), std::string("SamplerState ")}) {
        const std::string name = type[0] == 'T' ? "rsx_tex" : "rsx_samp";
        const std::string reg = type[0] == 'T' ? "t" : "s";
        const std::string declaration = type + name + "[16] : register(" + reg + "0);";
        std::string individual;
        for (int unit = 0; unit < 16; ++unit) {
            individual += type + name + std::to_string(unit) + " : register(" + reg + std::to_string(unit) + ");\n";
        }
        if (source.find(declaration) != std::string::npos) {
            replace_all(source, declaration, individual);
            for (int unit = 0; unit < 16; ++unit)
                replace_all(source, name + "[" + std::to_string(unit) + "]", name + std::to_string(unit));
        }
    }
    return source;
}
}

bool haze_hlsl_to_glsl(const char* hlsl, bool vertex, std::string& source, std::string& error)
{
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
    const EShLanguage stage = vertex ? EShLangVertex : EShLangFragment;
    const auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
    glslang::TShader shader(stage);
    const auto input = separate_samplers(hlsl);
    const char* text = input.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    shader.setEntryPoint("main");
    shader.setAutoMapBindings(true);
    shader.setAutoMapLocations(true);
    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
        error = shader.getInfoLog();
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages) || !program.mapIO()) {
        error = program.getInfoLog();
        return false;
    }
    std::vector<unsigned> words;
    glslang::SpvOptions options;
    options.disableOptimizer = false; // Legalize HLSL for SPIRV-Cross.
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &options);
    try {
        spirv_cross::CompilerGLSL compiler(std::move(words));
        spirv_cross::CompilerGLSL::Options glsl;
        glsl.version = 300;
        glsl.es = true;
        glsl.enable_420pack_extension = false;
        glsl.vertex.fixup_clipspace = true;
        glsl.vertex.flip_vert_y = true;
        compiler.set_common_options(glsl);
        // glslang preserves HLSL semantics on SPIR-V inputs and outputs. Give
        // both stages the same names so WebGL links matching COLOR/TEXCOORDs.
        const auto resources = compiler.get_shader_resources();
        for (const auto& resource : vertex ? resources.stage_outputs : resources.stage_inputs) {
            auto name = resource.name.substr(resource.name.find_last_of('.') + 1);
            if (name.size() == 2 && name[0] == 't') name = "tc" + name.substr(1);
            compiler.set_name(resource.id, "rsx_" + name);
            compiler.unset_decoration(resource.id, spv::DecorationLocation);
        }
        if (vertex) for (const auto& resource : resources.stage_inputs) {
            auto name = resource.name.substr(resource.name.find_last_of('.') + 1);
            if (name.size() > 1 && name[0] == 'a') {
                compiler.set_decoration(resource.id, spv::DecorationLocation, std::stoul(name.substr(1)));
            }
        }
        compiler.build_combined_image_samplers();
        for (const auto& sampler : compiler.get_combined_image_samplers()) {
            compiler.set_name(sampler.combined_id, compiler.get_name(sampler.image_id));
        }
        source = compiler.compile();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
