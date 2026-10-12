#include "shader_glsl.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>
#include <mutex>
#include <cctype>
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
void apply_texture_remaps(std::string& source) {
    size_t offset=0;
    bool sampled=false;
    while((offset=source.find('(',offset))!=std::string::npos) {
        size_t start=offset;
        while(start && (std::isalnum(static_cast<unsigned char>(source[start-1])) || source[start-1]=='_')) --start;
        const auto function=source.substr(start,offset-start);
        if(function!="texture" && function!="textureLod" && function!="textureGrad" &&
           function!="textureProj" && function!="texelFetch") {++offset;continue;}
        const auto comma=source.find(',',offset+1);
        if(comma==std::string::npos)break;
        auto sampler=source.substr(offset+1,comma-offset-1);
        sampler.erase(0,sampler.find_first_not_of(" \t\n"));
        sampler.erase(sampler.find_last_not_of(" \t\n")+1);
        const bool vertex=sampler.rfind("rsx_vtex",0)==0;
        const std::string prefix=vertex?"rsx_vtex":"rsx_tex";
        if(sampler.rfind(prefix,0)!=0 || sampler.size()==prefix.size()) {++offset;continue;}
        const auto suffix=sampler.substr(prefix.size());
        if(suffix.find_first_not_of("0123456789")!=std::string::npos) {++offset;continue;}
        const unsigned unit=std::stoul(suffix)+(vertex?16:0);
        size_t end=offset+1;int nesting=1;
        while(end<source.size() && nesting) {if(source[end]=='(')++nesting;else if(source[end]==')')--nesting;++end;}
        const auto replacement="haze_swizzle("+source.substr(start,end-start)+", haze_remap["+std::to_string(unit)+"])";
        source.replace(start,end-start,replacement);offset=start+replacement.size();sampled=true;
    }
    if(sampled) source.insert(source.find('\n')+1,R"(
precision highp float;
precision highp int;
uniform ivec4 haze_remap[20];
float haze_channel(vec4 value, int lane) { return lane<4 ? value[lane] : (lane==5 ? 1.0 : 0.0); }
vec4 haze_swizzle(vec4 value, ivec4 lanes) {
    return vec4(haze_channel(value,lanes.x),haze_channel(value,lanes.y),
                haze_channel(value,lanes.z),haze_channel(value,lanes.w));
}
)");
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
    // Scalarize the decompiler's register arrays and remove unused lanes
    // before handing the shader to the browser's driver compiler.
    // Keep constant-buffer members: removing an unused leading member would
    // require explicit member offsets, which GLSL ES 3.0 cannot represent.
    options.optimizePerformance = true;
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
        apply_texture_remaps(source);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
