#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "impostorgpu.hpp"
#include "bakegl.hpp"
#include "gl_loader.h"
#include <algorithm>
#include <cmath>

namespace impostorgpu {
namespace {
// Context-local entry points: never overwrite the viewport's global loader.
struct Api {
#define FIELD(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
    TYRA_GL_FUNCS(FIELD)
#undef FIELD
    GLenum (*GetError)() = nullptr;
    bool load() {
#define LOAD(ret, name, ...) name = reinterpret_cast<decltype(name)>(glfwGetProcAddress("gl" #name)); if (!name) return false;
        TYRA_GL_FUNCS(LOAD)
#undef LOAD
        GetError = reinterpret_cast<decltype(GetError)>(glfwGetProcAddress("glGetError"));
        return GetError != nullptr;
    }
};
struct Backend {
    GLFWwindow* win = nullptr;
    Api gl;
    GLuint program = 0, padding = 0;
    bool tried = false;
    std::string why;
    bool init() {
        if (tried) return program != 0 && padding != 0;
        tried = true;
        win = bakegl::create(3, 3, "tyrax-impostor-gpu", why);
        if (!win) return false;
        bakegl::ScopedCurrent current(win);
        if (!gl.load()) { why = "Missing OpenGL entry points"; return false; }
        const char* vs = R"GLSL(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord;
uniform vec4 capture; // cos, sin, projected center, horizontal half extent
uniform vec3 vertical; // center, half extent, depth extent
out vec2 uv;
void main() {
    float x = position.x*capture.x - position.z*capture.y;
    float z = position.x*capture.y + position.z*capture.x;
    // Readback row zero is atlas top: invert Y here, not in readback.
    gl_Position = vec4((x-capture.z)/capture.w,
        -(position.y-vertical.x)/vertical.y, -z/vertical.z, 1.0);
    uv = texcoord;
})GLSL";
        const char* fs = R"GLSL(#version 330 core
in vec2 uv;
uniform sampler2D albedo;
uniform vec3 kd;
out vec4 color;
void main() {
    ivec2 dims = textureSize(albedo, 0);
    vec4 texel = texelFetch(albedo, clamp(ivec2(fract(uv)*vec2(dims)), ivec2(0), dims-1), 0);
    if (texel.a < 128.0/255.0) discard;
    color = vec4(floor(clamp(texel.rgb*255.0*kd, 0.0, 255.0))/255.0, 1.0);
})GLSL";
        auto shader = [&](GLenum type, const char* code) {
            GLuint sh = gl.CreateShader(type);
            gl.ShaderSource(sh, 1, &code, nullptr); gl.CompileShader(sh);
            GLint ok; gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048] = {}; gl.GetShaderInfoLog(sh, sizeof(log), nullptr, log);
                why = log; gl.DeleteShader(sh); return GLuint(0);
            }
            return sh;
        };
        GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) { if(v) gl.DeleteShader(v); if(f) gl.DeleteShader(f); return false; }
        program = gl.CreateProgram(); gl.AttachShader(program,v); gl.AttachShader(program,f);
        gl.LinkProgram(program); gl.DeleteShader(v); gl.DeleteShader(f);
        GLint ok; gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {}; gl.GetProgramInfoLog(program,sizeof(log),nullptr,log);
            why=log; gl.DeleteProgram(program); program=0;
        }
        if (!program) return false;
        const char* pvs = R"GLSL(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p*2.0-1.0, 0.0, 1.0);
})GLSL";
        const char* pfs = R"GLSL(#version 330 core
uniform sampler2D source;
uniform int tileSize;
out vec4 color;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy), dims=textureSize(source,0);
    vec4 own=texelFetch(source,p,0);
    if(own.a>0.0) { color=own; return; }
    ivec3 sum=ivec3(0); int count=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        ivec2 q=p+ivec2(x,y);
        if(any(lessThan(q,ivec2(0))) || any(greaterThanEqual(q,dims)) || any(notEqual(q/tileSize,p/tileSize))) continue;
        vec4 sampleColor=texelFetch(source,q,0);
        if(sampleColor.a>0.0) { sum+=ivec3(round(sampleColor.rgb*255.0)); ++count; }
    }
    // Temporary alpha 128 marks filled RGB, distinct from original opaque 255.
    color=count>0 ? vec4(vec3(sum/count)/255.0,128.0/255.0) : own;
})GLSL";
        v=shader(GL_VERTEX_SHADER,pvs); f=shader(GL_FRAGMENT_SHADER,pfs);
        if(!v || !f) { if(v) gl.DeleteShader(v); if(f) gl.DeleteShader(f); return false; }
        padding=gl.CreateProgram(); gl.AttachShader(padding,v); gl.AttachShader(padding,f);
        gl.LinkProgram(padding); gl.DeleteShader(v); gl.DeleteShader(f);
        gl.GetProgramiv(padding,GL_LINK_STATUS,&ok);
        if(!ok) { why="Cannot link RGB padding shader"; gl.DeleteProgram(padding); padding=0; }
        return padding != 0;
    }
};
// Process-lifetime context/program, just like GI: a warm batch pays setup once.
Backend& backend() { static Backend b; return b; }
}

bool capture(const std::vector<impostorbake::Part>& parts, int size, int views,
             float cy, float hh, const float* centers, const float* halves,
             std::vector<unsigned char>& pixels, std::string& error) {
    Backend& b = backend();
    if (!b.init()) { error=b.why; return false; }
    bakegl::ScopedCurrent current(b.win);
    Api& gl=b.gl;
    while(gl.GetError()!=0) {}
    const int width=size*4, height=size*(views/4);
    struct Resources {
        Api& gl;
        GLuint fbo=0, color=0, depth=0, vao=0, buffer=0;
        std::vector<GLuint> textures;
        ~Resources() {
            gl.BindFramebuffer(GL_FRAMEBUFFER,0); gl.BindVertexArray(0);
            gl.DeleteTextures((GLsizei)textures.size(),textures.data());
            gl.DeleteTextures(1,&color); gl.DeleteRenderbuffers(1,&depth);
            gl.DeleteFramebuffers(1,&fbo); gl.DeleteBuffers(1,&buffer); gl.DeleteVertexArrays(1,&vao);
        }
    } r{gl};
    auto upload = [&](int w,int h,const void* data) {
        // Allocate then fill: direct TexImage2D(data) crashes some AMD drivers.
        gl.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        if(data) gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    };
    gl.GenFramebuffers(1,&r.fbo); gl.BindFramebuffer(GL_FRAMEBUFFER,r.fbo);
    gl.GenTextures(1,&r.color); gl.BindTexture(GL_TEXTURE_2D,r.color); upload(width,height,nullptr);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,r.color,0);
    gl.GenRenderbuffers(1,&r.depth); gl.BindRenderbuffer(GL_RENDERBUFFER,r.depth);
    gl.RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,r.depth);
    if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        error="Incomplete capture framebuffer"; return false;
    }
    gl.Disable(GL_BLEND); gl.Disable(GL_CULL_FACE); gl.Disable(0x0BD0); // DITHER
    gl.Enable(GL_DEPTH_TEST); gl.DepthFunc(0x0201); gl.DepthMask(GL_TRUE); // LESS
    gl.ClearColor(0,0,0,0); gl.Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gl.UseProgram(b.program);
    gl.Uniform1i(gl.GetUniformLocation(b.program,"albedo"),0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.GenVertexArrays(1,&r.vao); gl.BindVertexArray(r.vao);
    gl.GenBuffers(1,&r.buffer); gl.BindBuffer(GL_ARRAY_BUFFER,r.buffer);
    std::vector<float> vertices;
    std::vector<GLint> starts;
    float depthExtent=1;
    for(const auto& part:parts) {
        starts.push_back((GLint)(vertices.size()/8));
        vertices.insert(vertices.end(),part.vertices.begin(),part.vertices.end());
        for(size_t i=0;i<part.vertices.size();i+=8)
            depthExtent=std::max(depthExtent,std::hypot(part.vertices[i],part.vertices[i+2])+1.0f);
        GLuint tex; gl.GenTextures(1,&tex); r.textures.push_back(tex);
        gl.BindTexture(GL_TEXTURE_2D,tex); upload(part.texture.w,part.texture.h,part.texture.rgba.data());
    }
    gl.BufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_STATIC_DRAW);
    gl.EnableVertexAttribArray(0); gl.VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);
    gl.EnableVertexAttribArray(1); gl.VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));
    gl.Uniform3f(gl.GetUniformLocation(b.program,"vertical"),cy,hh,depthExtent);
    for(int view=0;view<views;++view) {
        const float angle=view*6.283185307f/views;
        gl.Viewport((view%4)*size,(view/4)*size,size,size);
        gl.Uniform4f(gl.GetUniformLocation(b.program,"capture"),std::cos(angle),std::sin(angle),centers[view],halves[view]);
        for(size_t p=0;p<parts.size();++p) {
            gl.BindTexture(GL_TEXTURE_2D,r.textures[p]);
            gl.Uniform3f(gl.GetUniformLocation(b.program,"kd"),parts[p].kd[0],parts[p].kd[1],parts[p].kd[2]);
            gl.DrawArrays(GL_TRIANGLES,starts[p],(GLsizei)(parts[p].vertices.size()/8));
        }
    }
    // RGB border dilation stays on GPU too. Otherwise this common CPU pass
    // dominates small bakes and erases the benefit of accelerating capture.
    GLuint ping; gl.GenTextures(1,&ping); r.textures.push_back(ping);
    gl.BindTexture(GL_TEXTURE_2D,ping); upload(width,height,nullptr);
    GLuint src=r.color, dst=ping;
    gl.Disable(GL_DEPTH_TEST); gl.Viewport(0,0,width,height);
    gl.UseProgram(b.padding);
    gl.Uniform1i(gl.GetUniformLocation(b.padding,"source"),0);
    gl.Uniform1i(gl.GetUniformLocation(b.padding,"tileSize"),size);
    for(int pass=0;pass<4;++pass) {
        gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,dst,0);
        gl.BindTexture(GL_TEXTURE_2D,src);
        gl.DrawArrays(GL_TRIANGLES,0,3);
        std::swap(src,dst);
    }
    gl.PixelStorei(GL_PACK_ALIGNMENT,1);
    gl.ReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    if(gl.GetError()!=0) { error="OpenGL capture/upload/readback failed"; return false; }
    for(size_t i=3;i<pixels.size();i+=4) pixels[i]=pixels[i]==255 ? 255 : 0;
    return true;
}
}
