#include "rsx_draw_engine.h"
#include "shader_glsl.h"
#include "rsx_texture_layout.h"
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#include <emscripten/emscripten.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

EM_JS(void, haze_publish_canvas, (), {
    Module.hazeFrameCount=(Module.hazeFrameCount||0)+1;
    if ((Module.hazeFrameCount&127)===1)
        err('[canvas] '+JSON.stringify({width:GLctx.canvas.width,height:GLctx.canvas.height,
            drawingWidth:GLctx.drawingBufferWidth,drawingHeight:GLctx.drawingBufferHeight,
            framebuffer:GLctx.getParameter(GLctx.DRAW_FRAMEBUFFER_BINDING)?.name ?? 0,
            lost:GLctx.isContextLost()}));
    const frame=GLctx.canvas.transferToImageBitmap();
    postMessage({type:'haze-frame', frame},[frame]);
    // Explicitly renew the default framebuffer after handing its storage to
    // ImageBitmap. Chromium otherwise retains a detached drawing buffer in
    // worker contexts that also render into user framebuffers.
    GLctx.canvas.width=GLctx.canvas.width;
});
EM_JS(void, haze_publish_pixels, (int w,int h,int out_w,int out_h), {
    let capture=Module.hazePixelCapture;
    if(!capture || capture.w!==w || capture.h!==h) {
        const source=new OffscreenCanvas(w,h);
        const output=new OffscreenCanvas(out_w,out_h);
        capture=Module.hazePixelCapture={w,h,source,output,
            sourceContext:source.getContext('2d'),outputContext:output.getContext('2d'),
            pixels:new Uint8Array(w*h*4)};
    }
    GLctx.readPixels(0,0,w,h,GLctx.RGBA,GLctx.UNSIGNED_BYTE,capture.pixels);
    capture.sourceContext.putImageData(new ImageData(new Uint8ClampedArray(capture.pixels.buffer),w,h),0,0);
    capture.outputContext.setTransform(1,0,0,1,0,0);
    capture.outputContext.drawImage(capture.source,0,0,out_w,out_h);
    const frame=capture.output.transferToImageBitmap();
    postMessage({type:'haze-frame',frame},[frame]);
});

namespace {
struct Texture {
    GLenum target = GL_TEXTURE_2D, internal = GL_RGBA8, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    u32 w = 0, h = 0, mips = 1;
    bool compressed = false, depth = false;
    GLuint object=0;
    std::array<GLint,4> remap{0,1,2,3};
};
struct Pipeline { rsx_be_render_state state; rsx_vertex_layout_plan layout; std::array<GLint,20> remap_locations; };
std::unordered_map<GLuint, Texture> textures;
std::unordered_map<GLuint, Pipeline> pipelines;
GLuint draw_fbo, read_fbo, temp_fbo, vao, vbo, ibo, vs_ubo, ps_ubo, active_pipeline;
GLuint depth_read_fbo, depth_write_fbo, bound_depth, original_depth;
std::unordered_map<uint64_t, GLuint> depth_attachments;
u32 width, height, draws, frames;
void check_gl(const char* operation, u32 w=0, u32 h=0) {
    const GLenum error=glGetError();
    static unsigned reports=0;
    if(error && reports++<32) fprintf(stderr,"[webgl] %s error=0x%X size=%ux%u\n",operation,error,w,h);
}
GLenum blend_equation(u32 value) {
    switch(value) {
    case GL_FUNC_ADD:case GL_FUNC_SUBTRACT:case GL_FUNC_REVERSE_SUBTRACT:
    case GL_MIN:case GL_MAX:return value;
    default:return GL_FUNC_ADD;
    }
}
GLenum gl_topology(rsx_topology topology) {
    switch(topology) {
    case RSX_TOPOLOGY_POINTS:return GL_POINTS;
    case RSX_TOPOLOGY_LINES:return GL_LINES;
    case RSX_TOPOLOGY_LINE_STRIP:return GL_LINE_STRIP;
    case RSX_TOPOLOGY_TRIANGLE_STRIP:return GL_TRIANGLE_STRIP;
    default:return GL_TRIANGLES;
    }
}
void toggle(GLenum cap, bool on) { if(on) glEnable(cap); else glDisable(cap); }
GLenum blend_factor(u32 value) {
    switch(value) {
    case 0:return GL_ZERO; case 1:return GL_ONE;
    case 0x300:return GL_SRC_COLOR; case 0x301:return GL_ONE_MINUS_SRC_COLOR;
    case 0x302:return GL_SRC_ALPHA; case 0x303:return GL_ONE_MINUS_SRC_ALPHA;
    case 0x304:return GL_DST_ALPHA; case 0x305:return GL_ONE_MINUS_DST_ALPHA;
    case 0x306:return GL_DST_COLOR; case 0x307:return GL_ONE_MINUS_DST_COLOR;
    case 0x308:return GL_SRC_ALPHA_SATURATE;
    case 0x8001:return GL_CONSTANT_COLOR; case 0x8002:return GL_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003:return GL_CONSTANT_ALPHA; case 0x8004:return GL_ONE_MINUS_CONSTANT_ALPHA;
    default:return GL_ONE;
    }
}
GLenum compare(u32 value) { return value >= 0x200 && value <= 0x207 ? value : GL_ALWAYS; }
GLenum stencil_op(u32 value) {
    switch(value) {
    case GL_KEEP:case GL_ZERO:case GL_REPLACE:case GL_INCR:case GL_DECR:
    case GL_INVERT:case GL_INCR_WRAP:case GL_DECR_WRAP:return value;
    default:return GL_KEEP;
    }
}
GLenum wrap(u8 value) {
    switch(value) {case 1:return GL_REPEAT;case 2:return GL_MIRRORED_REPEAT;default:return GL_CLAMP_TO_EDGE;}
}
Texture descriptor(rsx_be_format format, u32 w, u32 h, u32 mips, u32 faces) {
    Texture t; t.w=w;t.h=h;t.mips=mips;t.target=faces==6?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
    switch(format) {
    case RSX_BE_FMT_R8:t.internal=GL_R8;t.format=GL_RED;break;
    case RSX_BE_FMT_R8G8:t.internal=GL_RG8;t.format=GL_RG;break;
    case RSX_BE_FMT_BC1:t.internal=0x83F1;t.compressed=true;break;
    case RSX_BE_FMT_BC2:t.internal=0x83F2;t.compressed=true;break;
    case RSX_BE_FMT_BC3:t.internal=0x83F3;t.compressed=true;break;
    case RSX_BE_FMT_R16:t.internal=GL_R16F;t.format=GL_RED;t.type=GL_FLOAT;break;
    case RSX_BE_FMT_R16G16:t.internal=GL_RG16F;t.format=GL_RG;t.type=GL_FLOAT;break;
    case RSX_BE_FMT_R16G16F:t.internal=GL_RG16F;t.format=GL_RG;t.type=GL_HALF_FLOAT;break;
    case RSX_BE_FMT_R16G16B16A16F:t.internal=GL_RGBA16F;t.type=GL_HALF_FLOAT;break;
    case RSX_BE_FMT_R32F:t.internal=GL_R32F;t.format=GL_RED;t.type=GL_FLOAT;break;
    case RSX_BE_FMT_R32G32B32A32F:t.internal=GL_RGBA32F;t.type=GL_FLOAT;break;
    default:break;
    }
    return t;
}
GLuint allocate(Texture t) {
    GLuint texture;glGenTextures(1,&texture);glBindTexture(t.target,texture);
    glTexStorage2D(t.target,std::max(1u,t.mips),t.internal,t.w,t.h);
    glTexParameteri(t.target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(t.target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(t.target,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(t.target,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    check_gl("texture allocation",t.w,t.h);
    textures.emplace(texture,t);return texture;
}
int init(void*,u32 w,u32 h) {
    width=w;height=h;
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion=2;attributes.minorVersion=0;
    attributes.alpha=EM_TRUE;attributes.antialias=EM_FALSE;
    attributes.preserveDrawingBuffer=EM_TRUE;
    attributes.explicitSwapControl=EM_TRUE;
    attributes.proxyContextToMainThread=EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
    const auto context=emscripten_webgl_create_context("#canvas",&attributes);
    if(context<=0 || emscripten_webgl_make_context_current(context)!=EMSCRIPTEN_RESULT_SUCCESS) return -1;
    emscripten_webgl_enable_extension(context,"EXT_color_buffer_float");
    emscripten_webgl_enable_extension(context,"OES_texture_float_linear");
    emscripten_webgl_enable_extension(context,"WEBGL_compressed_texture_s3tc");
    glGenFramebuffers(1,&draw_fbo);glGenFramebuffers(1,&read_fbo);glGenFramebuffers(1,&temp_fbo);
    glGenFramebuffers(1,&depth_read_fbo);glGenFramebuffers(1,&depth_write_fbo);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenBuffers(1,&vbo);glGenBuffers(1,&ibo);glGenBuffers(1,&vs_ubo);glGenBuffers(1,&ps_ubo);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();emscripten_webgl_commit_frame();
    fprintf(stderr,"[webgl] WebGL2 draw engine initialized: %s\n",glGetString(GL_RENDERER));
    return 0;
}
void shutdown(void*) {}
void submit(void*,u32) { glFinish(); }
void set_remap(Texture& t,u32 remap,u32 format) {
    u8 lanes[4];rsx_texture_component_remap(remap,format&0x9Fu,lanes);
    // B8 presents its single byte in every source lane, including alpha.
    if((format&0x9Fu)==0x81)for(auto& lane:lanes)if(lane<4)lane=0;
    t.remap={lanes[1],lanes[2],lanes[3],lanes[0]};
}
u32 texture_create(void*,rsx_be_format format,u32 w,u32 h,u32 mips,u32 faces,u32 remap,u32 rsx_fmt) {
    auto t=descriptor(format,w,h,mips,faces);set_remap(t,remap,rsx_fmt);
    static unsigned reported=0;
    if((format==RSX_BE_FMT_R8 || format==RSX_BE_FMT_R8G8) && reported++<16)
        fprintf(stderr,"[webgl] texture %ux%u format=%X remap=%X lanes=%d,%d,%d,%d\n",w,h,rsx_fmt,remap,
            t.remap[0],t.remap[1],t.remap[2],t.remap[3]);
    return allocate(t);
}
void texture_upload(void*,u32 handle,u32 face,u32 mip,u32 w,u32 h,const void* source,u32 row_bytes,u32 rows) {
    auto& t=textures.at(handle);glBindTexture(t.target,handle);
    const GLenum target=t.target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:t.target;
    if(t.compressed) {
        glCompressedTexSubImage2D(target,mip,0,0,w,h,t.internal,row_bytes*rows,source);
    } else if(t.internal==GL_R16F || (t.internal==GL_RG16F && t.type==GL_FLOAT)) {
        // WebGL2 has no normalized 16-bit texture formats. Convert the SDK's
        // decoded UNORM16 channels to floats without changing guest values.
        const u32 channels=t.format==GL_RG?2:1;
        std::vector<float> data(w*h*channels);
        for(u32 y=0;y<h;y++)for(u32 x=0;x<w*channels;x++) {
            uint16_t value;memcpy(&value,(const u8*)source+y*row_bytes+x*2,2);
            data[y*w*channels+x]=value/65535.0f;
        }
        glTexSubImage2D(target,mip,0,0,w,h,t.format,GL_FLOAT,data.data());
    } else {
        glTexSubImage2D(target,mip,0,0,w,h,t.format,t.type,source);
    }
}
void release(void*,u32 handle) {
    for(auto it=textures.begin();it!=textures.end();) {
        if(it->second.object==handle)it=textures.erase(it);else ++it;
    }
    for(auto it=depth_attachments.begin();it!=depth_attachments.end();) {
        if((it->first>>32)==handle) {
            glDeleteTextures(1,&it->second);textures.erase(it->second);
            it=depth_attachments.erase(it);
        } else ++it;
    }
    glDeleteTextures(1,&handle);textures.erase(handle);
}
u32 color_create(void* user,rsx_be_format format,u32 w,u32 h,const void* seed,u32 row_bytes) {
    const u32 handle=texture_create(user,format,w,h,1,1,0,0);
    if(seed)texture_upload(user,handle,0,0,w,h,seed,row_bytes,h);
    return handle;
}
u32 view(void*,u32 surface,u32 remap,u32 format) {
    auto t=textures.at(surface);set_remap(t,remap,format);
    if(t.remap==textures.at(surface).remap) return surface;
    for(const auto& [handle,other]:textures)
        if(other.object==surface && other.remap==t.remap)return handle;
    static GLuint next_view=0x40000000;
    t.object=surface;const GLuint handle=next_view++;textures.emplace(handle,t);return handle;
}
u32 depth_create(void*,u32 w,u32 h) {
    Texture t;t.w=w;t.h=h;t.internal=GL_DEPTH24_STENCIL8;t.format=GL_DEPTH_STENCIL;
    t.type=GL_UNSIGNED_INT_24_8;t.depth=true;return allocate(t);
}
void attach(GLenum target,GLenum attachment,u32 handle) {
    glFramebufferTexture2D(target,attachment,GL_TEXTURE_2D,handle,0);
}
void copy_depth(GLuint source,GLuint destination) {
    const auto& a=textures.at(source);const auto& b=textures.at(destination);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,depth_read_fbo);
    attach(GL_READ_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,source);glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,depth_write_fbo);
    attach(GL_DRAW_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,destination);
    const GLenum none=GL_NONE;glDrawBuffers(1,&none);
    const bool scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_SCISSOR_TEST);
    const u32 w=std::min(a.w,b.w),h=std::min(a.h,b.h);
    glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT,GL_NEAREST);
    toggle(GL_SCISSOR_TEST,scissor);
}
u32 snapshot(void* user,u32 depth,u32 w,u32 h) {
    const u32 copy=depth_create(user,w,h);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);attach(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0);
    attach(GL_READ_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,depth);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,temp_fbo);attach(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0);
    attach(GL_DRAW_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,copy);
    const GLenum none=GL_NONE;glDrawBuffers(1,&none);
    glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    return copy;
}
GLuint compile_shader(GLenum stage,const char* hlsl) {
    std::string source,error;
    if(!haze_hlsl_to_glsl(hlsl,stage==GL_VERTEX_SHADER,source,error)) {
        fprintf(stderr,"[webgl-shader] translation failed: %s\n",error.c_str());return 0;
    }
    const char* capture=getenv("HAZE_WASM_GPU_CAPTURE");
    if(capture && *capture) {
        static unsigned index=0;
        char path[80];snprintf(path,sizeof path,"/wasm-gpu-%03u-%s.glsl",index++,stage==GL_VERTEX_SHADER?"vs":"ps");
        if(FILE* file=fopen(path,"w")){fwrite(source.data(),1,source.size(),file);fclose(file);}
    }
    const char* text=source.c_str();GLuint shader=glCreateShader(stage);
    glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);
    GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {char log[4096];glGetShaderInfoLog(shader,sizeof log,nullptr,log);
        fprintf(stderr,"[webgl-shader] compile failed: %s\n%s\n",log,text);glDeleteShader(shader);return 0;}
    return shader;
}
u32 pipeline_create(void*,const char* vs,const char* ps,const rsx_be_render_state* state,
                    const rsx_vertex_layout_plan* layout,u32,rsx_be_format,u32) {
    GLuint v=compile_shader(GL_VERTEX_SHADER,vs),f=compile_shader(GL_FRAGMENT_SHADER,ps);
    if(!v || !f){glDeleteShader(v);glDeleteShader(f);return 0;}
    GLuint program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    glDeleteShader(v);glDeleteShader(f);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096];glGetProgramInfoLog(program,sizeof log,nullptr,log);
        fprintf(stderr,"[webgl-shader] link failed: %s\n",log);glDeleteProgram(program);return 0;}
    glUseProgram(program);
    static unsigned reported=0;
    if(reported++<16) {
        fprintf(stderr,"[webgl] pipeline=%u attrs=0x%X color=0x%X depth=%u/%u cull=%u/%X/%X\n",
            program,layout->mask,state->color_mask,state->depth_test,state->depth_func,
            state->cull_enable,state->cull_face,state->front_face);
        GLint count;glGetProgramiv(program,GL_ACTIVE_ATTRIBUTES,&count);
        for(GLint i=0;i<count;i++){char name[160];GLint size;GLenum type;
            glGetActiveAttrib(program,i,sizeof name,nullptr,&size,&type,name);
            fprintf(stderr,"[webgl] attribute %s location=%d\n",name,glGetAttribLocation(program,name));}
    }
    for(auto [name,binding]:{std::pair{"VPConst",0},std::pair{"PSConstants",1}}) {
        GLuint block=glGetUniformBlockIndex(program,name);
        if(block!=GL_INVALID_INDEX)glUniformBlockBinding(program,block,binding);
    }
    GLint location=glGetUniformLocation(program,"rsx_tex");
    std::array<GLint,16> units;for(int i=0;i<16;i++)units[i]=i;
    if(location>=0)glUniform1iv(location,units.size(),units.data());
    for(int i=0;i<16;i++){char name[40];snprintf(name,sizeof name,"rsx_tex%d",i);
        location=glGetUniformLocation(program,name);if(location>=0)glUniform1i(location,i);}
    for(int i=0;i<4;i++){char name[40];snprintf(name,sizeof name,"rsx_vtex%d",i);
        location=glGetUniformLocation(program,name);if(location>=0)glUniform1i(location,16+i);}
    Pipeline pipeline{*state,*layout,{}};
    for(unsigned i=0;i<20;i++) {char name[40];snprintf(name,sizeof name,"haze_remap[%u]",i);
        pipeline.remap_locations[i]=glGetUniformLocation(program,name);}
    pipelines.emplace(program,pipeline);return program;
}
void pipeline_release(void*,u32 program) {glDeleteProgram(program);pipelines.erase(program);}
void bind_targets(void*,const u32* surfaces,u32 count,u32 depth) {
    original_depth=bound_depth=depth;
    if(depth && count) {
        const auto color=textures.at(surfaces[0]);const auto z=textures.at(depth);
        // WebGL requires identical attachment dimensions. RSX and the native
        // backends allow a larger depth image; crop it without scaling and
        // copy writes back so later passes observe the same guest depth data.
        if(z.w!=color.w || z.h!=color.h) {
            const uint64_t key=(uint64_t(depth)<<32)|(uint64_t(color.w)<<16)|color.h;
            auto found=depth_attachments.find(key);
            if(found==depth_attachments.end())
                found=depth_attachments.emplace(key,depth_create(nullptr,color.w,color.h)).first;
            bound_depth=found->second;copy_depth(depth,bound_depth);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER,draw_fbo);
    std::array<GLenum,4> buffers;
    for(u32 i=0;i<4;i++){attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,i<count?surfaces[i]:0);buffers[i]=GL_COLOR_ATTACHMENT0+i;}
    attach(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,bound_depth);
    glDrawBuffers(count,buffers.data());
    const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    static unsigned reports=0;
    if(status!=GL_FRAMEBUFFER_COMPLETE && reports++<12) {
        fprintf(stderr,"[webgl] target status=0x%X colors=%u depth=%u",status,count,depth);
        for(u32 i=0;i<count;i++) {const auto& t=textures.at(surfaces[i]);
            fprintf(stderr," color%u=%u/%ux%u/%X",i,surfaces[i],t.w,t.h,t.internal);}
        if(depth){const auto& t=textures.at(depth);fprintf(stderr," depth=%ux%u",t.w,t.h);}
        fprintf(stderr,"\n");
    }
}
void bind_pipeline(void*,u32 handle) {
    active_pipeline=handle;glUseProgram(handle);
    const auto& s=pipelines.at(handle).state;
    toggle(GL_DEPTH_TEST,s.depth_test);glDepthMask(s.depth_write);glDepthFunc(compare(s.depth_func));
    toggle(GL_BLEND,s.blend_enable);glBlendFuncSeparate(blend_factor(s.sf_rgb),blend_factor(s.df_rgb),blend_factor(s.sf_a),blend_factor(s.df_a));
    glBlendEquationSeparate(blend_equation(s.eq_rgb),blend_equation(s.eq_a));
    toggle(GL_CULL_FACE,s.cull_enable);glCullFace(s.cull_face==GL_FRONT?GL_FRONT:GL_BACK);
    glFrontFace(s.front_face==GL_CCW?GL_CW:GL_CCW); // vertex epilogue flips Y
    glColorMask((s.color_mask&0x10000)!=0,(s.color_mask&0x100)!=0,(s.color_mask&1)!=0,(s.color_mask&0x1000000)!=0);
    toggle(GL_STENCIL_TEST,s.stencil_enable);glStencilMask(s.s_write_mask);
    glStencilOpSeparate(GL_FRONT,stencil_op(s.s_fail),stencil_op(s.s_zfail),stencil_op(s.s_zpass));
    glStencilOpSeparate(GL_BACK,stencil_op(s.stencil_two_sided?s.bs_fail:s.s_fail),
        stencil_op(s.stencil_two_sided?s.bs_zfail:s.s_zfail),stencil_op(s.stencil_two_sided?s.bs_zpass:s.s_zpass));
}
void constants(GLuint buffer,GLuint binding,const void* data,u32 bytes) {
    glBindBuffer(GL_UNIFORM_BUFFER,buffer);glBufferData(GL_UNIFORM_BUFFER,bytes,data,GL_STREAM_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,binding,buffer);
}
void vs_constants(void*,const void* data,u32 bytes){constants(vs_ubo,0,data,bytes);}
void ps_constants(void*,const void* data,u32 bytes){constants(ps_ubo,1,data,bytes);}
void bind_texture_units(const u32* handles,const rsx_be_sampler_desc* samplers,u32 mask,u32 count,u32 base) {
    for(u32 i=0;i<count;i++) {
        glActiveTexture(GL_TEXTURE0+base+i);
        auto found=textures.find(handles[i]);
        glBindTexture(GL_TEXTURE_2D,0);glBindTexture(GL_TEXTURE_CUBE_MAP,0);
        if(!(mask&(1u<<i)) || found==textures.end())continue;
        const auto& t=found->second;const auto& s=samplers[i];glBindTexture(t.target,t.object?t.object:handles[i]);
        const auto location=pipelines.at(active_pipeline).remap_locations[base+i];
        if(location>=0)glUniform4i(location,t.remap[0],t.remap[1],t.remap[2],t.remap[3]);
        GLenum min=s.min_linear?GL_LINEAR:GL_NEAREST;
        if(s.mip_present && t.mips>1)min=s.min_linear?(s.mip_linear?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_NEAREST)
            :(s.mip_linear?GL_NEAREST_MIPMAP_LINEAR:GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(t.target,GL_TEXTURE_MIN_FILTER,min);glTexParameteri(t.target,GL_TEXTURE_MAG_FILTER,s.mag_linear?GL_LINEAR:GL_NEAREST);
        glTexParameteri(t.target,GL_TEXTURE_WRAP_S,wrap(s.wrap_s));glTexParameteri(t.target,GL_TEXTURE_WRAP_T,wrap(s.wrap_t));
        glTexParameteri(t.target,GL_TEXTURE_WRAP_R,wrap(s.wrap_r));
        glTexParameterf(t.target,GL_TEXTURE_MIN_LOD,s.min_lod);glTexParameterf(t.target,GL_TEXTURE_MAX_LOD,s.max_lod);
    }
}
void bind_textures(void*,const u32* handles,const rsx_be_sampler_desc* samplers,u32 mask){bind_texture_units(handles,samplers,mask,16,0);}
void bind_vertex_textures(void*,const u32* handles,const rsx_be_sampler_desc* samplers,u32 mask){bind_texture_units(handles,samplers,mask,4,16);}
void viewport(void*,float x,float y,float w,float h){glViewport(x,y,w,h);}
void scissor(void*,u32 x,u32 y,u32 w,u32 h){glEnable(GL_SCISSOR_TEST);glScissor(x,y,w,h);}
void stencil_ref(void*,u32 ref){const auto& s=pipelines.at(active_pipeline).state;
    glStencilFuncSeparate(GL_FRONT,compare(s.s_func),ref,s.s_func_mask);
    glStencilFuncSeparate(GL_BACK,compare(s.stencil_two_sided?s.bs_func:s.s_func),ref,s.s_func_mask);}
void draw(void*,rsx_topology topology,const void* vertices,u32 count,u32 stride,const u32* indices,u32 index_count) {
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,(size_t)count*stride,vertices,GL_STREAM_DRAW);
    const auto& layout=pipelines.at(active_pipeline).layout;
    for(u32 i=0;i<16;i++) {if(layout.slot_by_attr[i]>=0){glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,stride,(void*)(uintptr_t)(layout.slot_by_attr[i]*16));}
        else glDisableVertexAttribArray(i);}
    if(indices){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(size_t)index_count*4,indices,GL_STREAM_DRAW);
        glDrawElements(gl_topology(topology),index_count,GL_UNSIGNED_INT,nullptr);}
    else glDrawArrays(gl_topology(topology),0,count);
    ++draws;
    check_gl("draw");
    if(bound_depth!=original_depth && pipelines.at(active_pipeline).state.depth_write) {
        copy_depth(bound_depth,original_depth);
        glBindFramebuffer(GL_FRAMEBUFFER,draw_fbo);
    }
}
void clear_color(void*,u32 surface,const float rgba[4]) {
    glBindFramebuffer(GL_FRAMEBUFFER,temp_fbo);attach(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,0);
    attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,surface);const GLenum buffer=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&buffer);
    // WebGL's clearBufferfv overload rejects the whole multi-GiB Wasm heap
    // view. Scalar clear values avoid that browser offset/length limitation.
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(rgba[0],rgba[1],rgba[2],rgba[3]);glClear(GL_COLOR_BUFFER_BIT);
    check_gl("color clear");
}
void clear_depth(void*,u32 depth,u32 flags,float value,u8 stencil) {
    glBindFramebuffer(GL_FRAMEBUFFER,temp_fbo);attach(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0);
    attach(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,depth);const GLenum none=GL_NONE;glDrawBuffers(1,&none);
    glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glStencilMask(0xff);
    glClearDepthf(value);glClearStencil(stencil);
    glClear(((flags&RSX_BE_CLEAR_DEPTH)?GL_DEPTH_BUFFER_BIT:0)|
            ((flags&RSX_BE_CLEAR_STENCIL)?GL_STENCIL_BUFFER_BIT:0));
    check_gl("depth clear");
}
void present(void*,u32 surface) {
    const auto& t=textures.at(surface);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);attach(GL_READ_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,0);
    attach(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,surface);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glDisable(GL_SCISSOR_TEST);
    const GLenum read_status=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    const GLenum draw_status=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if((frames&127)==0) {
        unsigned char pixel[4]{};
        glReadPixels(t.w/2,t.h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        fprintf(stderr,"[webgl] present surface=%u %ux%u format=%X fbo=%X/%X pixel=%u,%u,%u,%u\n",
                surface,t.w,t.h,t.internal,read_status,draw_status,pixel[0],pixel[1],pixel[2],pixel[3]);
    }
    if(draw_status==GL_FRAMEBUFFER_COMPLETE) {
        glBlitFramebuffer(0,0,t.w,t.h,0,height,width,0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glFlush();emscripten_webgl_commit_frame();
    }
    const GLenum error=glGetError();
    if((++frames&127)==0)fprintf(stderr,"[webgl] frames=%u draws=%u error=0x%X\n",frames,draws,error);
    draws=0;
    if(draw_status==GL_FRAMEBUFFER_COMPLETE && error==GL_NO_ERROR) haze_publish_canvas();
    else haze_publish_pixels(t.w,t.h,width,height);
}
void readback(void*,u32 surface,u32 x,u32 y,u32 w,u32 h,void* out,u32 pitch) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);attach(GL_READ_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,0);
    attach(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,surface);glReadBuffer(GL_COLOR_ATTACHMENT0);
    for(u32 row=0;row<h;row++)glReadPixels(x,y+row,w,1,GL_RGBA,GL_UNSIGNED_BYTE,(u8*)out+row*pitch);
}
rsx_draw_backend backend={nullptr,init,shutdown,submit,texture_create,texture_upload,release,
    color_create,release,view,depth_create,release,snapshot,pipeline_create,pipeline_release,
    bind_targets,bind_pipeline,vs_constants,ps_constants,bind_textures,bind_vertex_textures,
    viewport,scissor,stencil_ref,draw,clear_color,clear_depth,present,readback};
}
extern "C" int haze_webgl_backend_init(u32 w,u32 h,const char*) {
    rsx_draw_engine_set_backend(&backend);rsx_draw_engine_set_default(1);
    return rsx_draw_engine_init(w,h);
}
extern "C" u32 cellGcmGetCurrentDisplayBufferId(void);
extern "C" void haze_webgl_backend_present() {
    // cellGcmSys consumes driver FLIP methods before forwarding ordinary 3D
    // methods to the draw engine. Present the buffer selected by that flip.
    rsx_draw_engine_present_buffer(cellGcmGetCurrentDisplayBufferId());
}
extern "C" int haze_webgl_backend_pump_messages() {return 0;}
