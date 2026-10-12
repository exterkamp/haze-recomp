#include "rsx_draw_engine.h"
#include "../port/haze_wasm_vm.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <GLES3/gl3.h>

static const rsx_draw_backend* backend;
extern "C" void rsx_draw_engine_set_backend(const rsx_draw_backend* value) { backend = value; }
extern "C" void rsx_draw_engine_set_default(int) {}
extern "C" int rsx_draw_engine_init(u32 w,u32 h) { return backend->init(nullptr,w,h); }
extern "C" u32 cellGcmGetCurrentDisplayBufferId(void) { return 0; }
extern "C" void rsx_draw_engine_present_buffer(u32) {}
extern "C" int haze_webgl_backend_init(u32,u32,const char*);

int main()
{
    // Exercise GL bindings with the same >8 GiB shared heap as the game.
    assert(haze_wasm_reserve_guest_vm());
    assert(haze_webgl_backend_init(1280,720,"smoke")==0);
    const char* vs=R"(
        struct VSInput { float4 a0:ATTR0; float4 a1:ATTR1; };
        struct VSOutput { float4 pos:SV_POSITION; float4 t0:TEXCOORD0; };
        cbuffer VPConst:register(b0) { float4 c[1]; };
        VSOutput main(VSInput input) { VSOutput output;
            output.pos=input.a0*c[0]; output.t0=input.a1; return output; }
    )";
    const char* ps=R"(
        struct PSInput { float4 position:SV_POSITION; float4 tc0:TEXCOORD0; };
        Texture2D    rsx_tex[16] : register(t0);
        SamplerState rsx_samp[16] : register(s0);
        cbuffer PSConstants:register(b1) { float4 color; };
        float4 main(PSInput input):SV_TARGET {
            return rsx_tex[0].Sample(rsx_samp[0],input.tc0.xy)*color;
        }
    )";
    rsx_be_render_state state{};state.color_mask=0x01010101;state.front_face=0x901;
    state.depth_test=1;state.depth_write=1;state.depth_func=GL_LESS;
    rsx_vertex_layout_plan layout{};layout.mask=3;layout.stride=32;layout.count=2;
    memset(layout.slot_by_attr,-1,sizeof layout.slot_by_attr);
    layout.slot_by_attr[0]=0;layout.slot_by_attr[1]=1;
    const auto program=backend->pipeline_create(nullptr,vs,ps,&state,&layout,32,RSX_BE_FMT_R8G8B8A8,1);
    assert(program);
    const auto target=backend->color_target_create(nullptr,RSX_BE_FMT_R8G8B8A8,32,32,nullptr,0);
    const auto texture=backend->texture_create(nullptr,RSX_BE_FMT_R8G8B8A8,1,1,1,1,0,0);
    const unsigned char pixel[]={51,102,153,255};
    backend->texture_upload(nullptr,texture,0,0,1,1,pixel,4,1);
    const float black[]={0,0,0,1};backend->clear_color(nullptr,target,black);
    unsigned char cleared[4]{};
    backend->readback(nullptr,target,16,16,1,1,cleared,4);
    const unsigned char opaque_black[]={0,0,0,255};
    assert(glGetError()==GL_NO_ERROR && memcmp(cleared,opaque_black,4)==0);
    const auto depth=backend->depth_target_create(nullptr,64,64);
    backend->clear_depth_stencil(nullptr,depth,RSX_BE_CLEAR_DEPTH|RSX_BE_CLEAR_STENCIL,1,0);
    backend->bind_targets(nullptr,&target,1,depth);backend->bind_pipeline(nullptr,program);
    const float one[]={1,1,1,1};
    backend->bind_vs_constants(nullptr,one,sizeof one);backend->bind_ps_constants(nullptr,one,sizeof one);
    u32 textures[16]={texture};rsx_be_sampler_desc samplers[16]{};
    samplers[0].wrap_s=samplers[0].wrap_t=3;
    backend->bind_textures(nullptr,textures,samplers,1);
    backend->set_viewport(nullptr,0,0,32,32);backend->set_scissor(nullptr,0,0,32,32);
    const float vertices[][8]={
        {-1,-1,0.5,1,0,0,0,1},{1,-1,0.5,1,1,0,0,1},{-1,1,0.5,1,0,1,0,1},
        {1,-1,0.5,1,1,0,0,1},{1,1,0.5,1,1,1,0,1},{-1,1,0.5,1,0,1,0,1}};
    backend->draw(nullptr,RSX_TOPOLOGY_TRIANGLES,vertices,6,32,nullptr,0);
    printf("[webgl-smoke] draw error=0x%X framebuffer=0x%X\n",glGetError(),glCheckFramebufferStatus(GL_FRAMEBUFFER));
    unsigned char result[4]{};
    backend->readback(nullptr,target,16,16,1,1,result,4);
    printf("[webgl-smoke] read error=0x%X framebuffer=0x%X\n",glGetError(),glCheckFramebufferStatus(GL_READ_FRAMEBUFFER));
    printf("[webgl-smoke] pixel %u,%u,%u,%u\n",result[0],result[1],result[2],result[3]);
    assert(memcmp(result,pixel,4)==0);
    // Font atlases route a single stored channel into alpha through the RSX
    // crossbar. WebGL has no texture-view swizzles, so test the shader route.
    const auto atlas=backend->texture_create(nullptr,RSX_BE_FMT_R8,1,1,1,1,0,0x81);
    backend->texture_upload(nullptr,atlas,0,0,1,1,pixel,1,1);
    backend->clear_depth_stencil(nullptr,depth,RSX_BE_CLEAR_DEPTH,1,0);
    backend->bind_targets(nullptr,&target,1,depth);backend->bind_pipeline(nullptr,program);
    textures[0]=atlas;backend->bind_textures(nullptr,textures,samplers,1);
    backend->draw(nullptr,RSX_TOPOLOGY_TRIANGLES,vertices,6,32,nullptr,0);
    backend->readback(nullptr,target,16,16,1,1,result,4);
    const unsigned char mapped[]={51,51,51,51};assert(memcmp(result,mapped,4)==0);
    backend->clear_depth_stencil(nullptr,depth,RSX_BE_CLEAR_DEPTH,1,0);
    backend->bind_targets(nullptr,&target,1,depth);backend->bind_pipeline(nullptr,program);
    textures[0]=texture;backend->bind_textures(nullptr,textures,samplers,1);
    backend->draw(nullptr,RSX_TOPOLOGY_TRIANGLES,vertices,6,32,nullptr,0);
    backend->present(nullptr,target);
    assert(glGetError()==GL_NO_ERROR);
    backend->present(nullptr,target);
    assert(glGetError()==GL_NO_ERROR);
    puts("[webgl-smoke] PASS: HLSL translation, varying linkage, vertex layout, UBOs, sampler arrays, textured draw and GPU readback");
    return 0;
}
