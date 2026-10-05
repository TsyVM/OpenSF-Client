// VanGUI: backdrop blur for the OpenGL 3 renderer backend. See vangui_impl_opengl3_blur.h.
//
// Runs inside VanGui_ImplOpenGL3_RenderDrawData(), at the point in the draw list where
// DrawBackdropBlur() queued it, so everything drawn before it is in the framebuffer. It
// leaves its own program, VAO and blend bound; the ResetRenderState callback queued right
// after it restores the backend's, and the framebuffer bindings are put back here.
//
// The backend's loader only carries what the backend itself calls, so the framebuffer
// entry points are fetched here through the same loader (imgl3wGetProcAddress).

#include "vangui_impl_opengl3_blur.h"
#ifndef VANGUI_DISABLE

#if __has_include("misc/vangui_effects.h")
#include "misc/vangui_effects.h"
#else
#include "vangui_effects.h"
#endif

#if defined(VANGUI_ENABLE_EFFECTS) && !defined(VANGUI_IMPL_OPENGL_ES2) && !defined(VANGUI_IMPL_OPENGL_LOADER_CUSTOM)

#if defined(VANGUI_IMPL_OPENGL_ES3)
#if defined(__APPLE__)
#include <OpenGLES/ES3/gl.h>
#else
#include <GLES3/gl3.h>
#endif
#else
#include "vangui_impl_opengl3_loader.h"
#define VANGUI_BLUR_GL_LOADER 1
#endif

#include "vangui_impl_blur_common.h"
#include <stdio.h>
#include <string.h>

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER           0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER           0x8CA9
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING   0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING   0x8CAA
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0          0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE       0x8CD5
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F                    0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT                 0x140B
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE              0x812F
#endif
#ifndef GL_RGBA8
#define GL_RGBA8                      0x8058
#endif

namespace {

typedef void   (APIENTRYP PfnGenFramebuffers)(GLsizei, GLuint*);
typedef void   (APIENTRYP PfnDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void   (APIENTRYP PfnBindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRYP PfnFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRYP PfnCheckFramebufferStatus)(GLenum);
typedef void   (APIENTRYP PfnBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void   (APIENTRYP PfnDrawArrays)(GLenum, GLint, GLsizei);
typedef void   (APIENTRYP PfnUniform4fv)(GLint, GLsizei, const GLfloat*);

struct Surface
{
    GLuint Tex = 0, Fbo = 0;
    int    W = 0, H = 0;
};

struct BlurGL
{
    PfnGenFramebuffers        GenFramebuffers = nullptr;
    PfnDeleteFramebuffers     DeleteFramebuffers = nullptr;
    PfnBindFramebuffer        BindFramebuffer = nullptr;
    PfnFramebufferTexture2D   FramebufferTexture2D = nullptr;
    PfnCheckFramebufferStatus CheckFramebufferStatus = nullptr;
    PfnBlitFramebuffer        BlitFramebuffer = nullptr;
    PfnDrawArrays             DrawArrays = nullptr;
    PfnUniform4fv             Uniform4fv = nullptr;

    GLuint  Down = 0, Blur = 0, Composite = 0, Vao = 0;
    Surface Source, A, B;
    GLint   LowInternal = GL_RGBA16F;
    GLenum  LowType = GL_HALF_FLOAT;
};
BlurGL* g_GL = nullptr;

const char* kVertex = R"(
out vec2 v_uv;
void main()
{
    vec2 uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kCommon = R"(
#ifdef GL_ES
precision highp float;
#endif
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec4 u_step;     // xy: one texel along the pass; zw: down-pass tap offset
uniform vec4 u_scale;    // xy: output uv -> source uv; zw: largest source uv to sample
uniform vec4 u_rect;     // panel, top-down framebuffer px (x0, y0, x1, y1)
uniform vec4 u_region;   // copied region's GL origin; zw: framebuffer px -> blurred uv
uniform vec4 u_misc;     // x: rounding px, y: saturation, z: taps, w: framebuffer height
uniform vec4 u_taps[12]; // x: offset (texels), y: weight
vec4 Fetch(vec2 uv) { return texture(u_tex, min(uv, u_scale.zw)); }
)";

const char* kDown = R"(
void main()
{
    vec2 uv = v_uv * u_scale.xy, d = u_step.zw;
    o_color = 0.25 * (Fetch(uv + vec2(-d.x, -d.y)) + Fetch(uv + vec2(d.x, -d.y)) +
                      Fetch(uv + vec2(-d.x,  d.y)) + Fetch(uv + vec2(d.x,  d.y)));
}
)";

const char* kBlur = R"(
void main()
{
    vec2 uv = v_uv * u_scale.xy;
    vec4 c = Fetch(uv) * u_taps[0].y;
    int n = int(u_misc.z);
    for (int k = 1; k < 12; ++k)
    {
        if (k < n)
        {
            vec2 o = u_step.xy * u_taps[k].x;
            c += (Fetch(uv + o) + Fetch(uv - o)) * u_taps[k].y;
        }
    }
    o_color = c;
}
)";

const char* kComposite = R"(
void main()
{
    vec2 frag = gl_FragCoord.xy;
    vec3 c = Fetch((frag - u_region.xy) * u_region.zw).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, u_misc.y);
    vec2 p = vec2(frag.x, u_misc.w - frag.y);
    vec2 halfsz = (u_rect.zw - u_rect.xy) * 0.5;
    vec2 centre = (u_rect.xy + u_rect.zw) * 0.5;
    float r = min(u_misc.x, min(halfsz.x, halfsz.y));
    vec2 q = abs(p - centre) - (halfsz - r);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    o_color = vec4(c, clamp(0.5 - d, 0.0, 1.0));
}
)";

char g_Version[32] = "#version 130\n";

GLuint Shader(GLenum type, const char* body)
{
    const GLchar* src[3] = {g_Version, type == GL_FRAGMENT_SHADER ? kCommon : "", body};
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 3, src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "VanGui_ImplOpenGL3_InitBlur: shader failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint Program(GLuint vs, const char* fragment)
{
    const GLuint fs = Shader(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) { if (fs) glDeleteShader(fs); return 0; }
    const GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDetachShader(p, fs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 0);
    return p;
}

void Release(Surface& s)
{
    if (s.Fbo && g_GL->DeleteFramebuffers) g_GL->DeleteFramebuffers(1, &s.Fbo);
    if (s.Tex) glDeleteTextures(1, &s.Tex);
    s = Surface();
}

// Grow `s` to at least w x h (never shrinks, so a steady UI stops allocating).
bool Ensure(Surface& s, int w, int h, GLint internal, GLenum type)
{
    if (s.Tex && s.W >= w && s.H >= h) return true;
    const int nw = s.W > w ? s.W : w, nh = s.H > h ? s.H : h;
    Release(s);
    glGenTextures(1, &s.Tex);
    glBindTexture(GL_TEXTURE_2D, s.Tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, nw, nh, 0, GL_RGBA, type, nullptr);
    g_GL->GenFramebuffers(1, &s.Fbo);
    g_GL->BindFramebuffer(GL_FRAMEBUFFER, s.Fbo);
    g_GL->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.Tex, 0);
    if (g_GL->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { Release(s); return false; }
    s.W = nw;
    s.H = nh;
    return true;
}

void Set4(GLuint prog, const char* name, float a, float b, float c, float d)
{
    const GLfloat v[4] = {a, b, c, d};
    g_GL->Uniform4fv(glGetUniformLocation(prog, name), 1, v);
}

void Pass(GLuint prog, const Surface& src, const Surface& dst, int w, int h)
{
    g_GL->BindFramebuffer(GL_FRAMEBUFFER, dst.Fbo);
    glViewport(0, 0, w, h);
    glUseProgram(prog);
    glBindTexture(GL_TEXTURE_2D, src.Tex);
    g_GL->DrawArrays(GL_TRIANGLES, 0, 3);
}

void BlurHandler(const VanDrawList*, const VanDrawCmd*, const VanGui::VanBlurRequest& req, void*)
{
    BlurGL* bd = g_GL;
    const VanDrawData* dd = VanGui::GetDrawData();
    if (!bd || !dd) return;
    const int fb_w = (int)(dd->DisplaySize.x * dd->FramebufferScale.x), fb_h = (int)(dd->DisplaySize.y * dd->FramebufferScale.y);
    GLint draw_fbo = 0, read_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);

    // Framebuffer pixels (top-down) of the panel, its clip and the region to copy.
    const float sx = dd->FramebufferScale.x, sy = dd->FramebufferScale.y;
    const float px0 = (req.Rect.x - dd->DisplayPos.x) * sx, py0 = (req.Rect.y - dd->DisplayPos.y) * sy;
    const float px1 = (req.Rect.z - dd->DisplayPos.x) * sx, py1 = (req.Rect.w - dd->DisplayPos.y) * sy;
    const float sigma = VanBlur::SigmaFor(req.Radius * sx);
    VanBlur::Region rg;
    if (!VanBlur::RegionFor(px0, py0, px1, py1, VanBlur::Margin(sigma), fb_w, fb_h, &rg)) return;
    int cx0 = (int)((req.ClipRect.x - dd->DisplayPos.x) * sx), cy0 = (int)((req.ClipRect.y - dd->DisplayPos.y) * sy);
    int cx1 = (int)((req.ClipRect.z - dd->DisplayPos.x) * sx), cy1 = (int)((req.ClipRect.w - dd->DisplayPos.y) * sy);
    if (cx0 < (int)px0) cx0 = (int)px0;
    if (cy0 < (int)py0) cy0 = (int)py0;
    if (cx1 > (int)(px1 + 1.0f)) cx1 = (int)(px1 + 1.0f);
    if (cy1 > (int)(py1 + 1.0f)) cy1 = (int)(py1 + 1.0f);
    if (cx1 <= cx0 || cy1 <= cy0) return;

    const int rw = rg.X1 - rg.X0, rh = rg.Y1 - rg.Y0;
    const int f = VanBlur::Downsample(sigma);
    const int dw = (rw + f - 1) / f, dh = (rh + f - 1) / f;
    bool ok = Ensure(bd->Source, rw, rh, GL_RGBA8, GL_UNSIGNED_BYTE) && Ensure(bd->A, dw, dh, bd->LowInternal, bd->LowType) &&
              Ensure(bd->B, dw, dh, bd->LowInternal, bd->LowType);
    if (ok)
    {
        // Copy the region out (the blit also resolves a multisampled framebuffer). Scissor
        // clips blits, so it goes off first; the reset that follows turns it back on.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        bd->BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)draw_fbo);
        bd->BindFramebuffer(GL_DRAW_FRAMEBUFFER, bd->Source.Fbo);
        bd->BlitFramebuffer(rg.X0, fb_h - rg.Y1, rg.X1, fb_h - rg.Y0, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindVertexArray(bd->Vao);
        glActiveTexture(GL_TEXTURE0);
#ifdef VANGUI_BLUR_GL_LOADER
        if (imgl3wProcs.gl.BindSampler) glBindSampler(0, 0);   // our textures carry their own filtering
#else
        glBindSampler(0, 0);
#endif
        const VanBlur::Kernel k = VanBlur::MakeKernel(sigma / float(f));
        GLfloat taps[VanBlur::kMaxTaps * 4] = {};
        for (int i = 0; i < k.Taps; ++i) { taps[i * 4] = k.Offsets[i]; taps[i * 4 + 1] = k.Weights[i]; }
        const float dw_u = float(dw) / float(bd->A.W), dh_u = float(dh) / float(bd->A.H);
        const float dw_max = (float(dw) - 0.5f) / float(bd->A.W), dh_max = (float(dh) - 0.5f) / float(bd->A.H);

        // Down: region -> A at 1/f, each texel averaging its f x f block.
        glUseProgram(bd->Down);
        Set4(bd->Down, "u_step", 0.0f, 0.0f, f > 1 ? float(f) * 0.25f / float(bd->Source.W) : 0.0f, f > 1 ? float(f) * 0.25f / float(bd->Source.H) : 0.0f);
        Set4(bd->Down, "u_scale", float(f * dw) / float(bd->Source.W), float(f * dh) / float(bd->Source.H),
             (float(rw) - 0.5f) / float(bd->Source.W), (float(rh) - 0.5f) / float(bd->Source.H));
        Pass(bd->Down, bd->Source, bd->A, dw, dh);

        // Blur: A -> B across, B -> A up and down.
        glUseProgram(bd->Blur);
        bd->Uniform4fv(glGetUniformLocation(bd->Blur, "u_taps"), VanBlur::kMaxTaps, taps);
        Set4(bd->Blur, "u_misc", 0.0f, 0.0f, float(k.Taps), 0.0f);
        Set4(bd->Blur, "u_scale", dw_u, dh_u, dw_max, dh_max);
        Set4(bd->Blur, "u_step", 1.0f / float(bd->A.W), 0.0f, 0.0f, 0.0f);
        Pass(bd->Blur, bd->A, bd->B, dw, dh);
        Set4(bd->Blur, "u_step", 0.0f, 1.0f / float(bd->A.H), 0.0f, 0.0f);
        Pass(bd->Blur, bd->B, bd->A, dw, dh);

        // Composite A inside the panel's rounded rectangle, on the original framebuffer.
        bd->BindFramebuffer(GL_FRAMEBUFFER, (GLuint)draw_fbo);
        glViewport(0, 0, fb_w, fb_h);
        glEnable(GL_SCISSOR_TEST);
        glScissor(cx0, fb_h - cy1, cx1 - cx0, cy1 - cy0);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(bd->Composite);
        Set4(bd->Composite, "u_scale", dw_u, dh_u, dw_max, dh_max);
        Set4(bd->Composite, "u_rect", px0, py0, px1, py1);
        Set4(bd->Composite, "u_region", float(rg.X0), float(fb_h - rg.Y1), 1.0f / (float(f) * float(bd->A.W)), 1.0f / (float(f) * float(bd->A.H)));
        Set4(bd->Composite, "u_misc", req.Rounding * sx, req.Saturation, 0.0f, float(fb_h));
        glBindTexture(GL_TEXTURE_2D, bd->A.Tex);
        bd->DrawArrays(GL_TRIANGLES, 0, 3);
    }
    bd->BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw_fbo);
    bd->BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read_fbo);
}

template <typename T> bool Load(T& fn, const char* name)
{
#ifdef VANGUI_BLUR_GL_LOADER
    fn = reinterpret_cast<T>(imgl3wGetProcAddress(name));
#else
    (void)name;
#endif
    return fn != nullptr;
}

} // namespace

bool VanGui_ImplOpenGL3_InitBlur(const char* glsl_version)
{
    VanGui_ImplOpenGL3_ShutdownBlur();
    if (glsl_version && glsl_version[0])
        snprintf(g_Version, sizeof(g_Version), "%s\n", glsl_version);
    else
#if defined(VANGUI_IMPL_OPENGL_ES3)
        snprintf(g_Version, sizeof(g_Version), "#version 300 es\n");
#elif defined(__APPLE__)
        snprintf(g_Version, sizeof(g_Version), "#version 150\n");
#else
        snprintf(g_Version, sizeof(g_Version), "#version 130\n");
#endif
    g_GL = new BlurGL();
#ifdef VANGUI_BLUR_GL_LOADER
    bool ok = Load(g_GL->GenFramebuffers, "glGenFramebuffers") && Load(g_GL->DeleteFramebuffers, "glDeleteFramebuffers") &&
              Load(g_GL->BindFramebuffer, "glBindFramebuffer") && Load(g_GL->FramebufferTexture2D, "glFramebufferTexture2D") &&
              Load(g_GL->CheckFramebufferStatus, "glCheckFramebufferStatus") && Load(g_GL->BlitFramebuffer, "glBlitFramebuffer") &&
              Load(g_GL->DrawArrays, "glDrawArrays") && Load(g_GL->Uniform4fv, "glUniform4fv");
#else
    g_GL->GenFramebuffers = glGenFramebuffers;
    g_GL->DeleteFramebuffers = glDeleteFramebuffers;
    g_GL->BindFramebuffer = glBindFramebuffer;
    g_GL->FramebufferTexture2D = glFramebufferTexture2D;
    g_GL->CheckFramebufferStatus = glCheckFramebufferStatus;
    g_GL->BlitFramebuffer = glBlitFramebuffer;
    g_GL->DrawArrays = glDrawArrays;
    g_GL->Uniform4fv = glUniform4fv;
    g_GL->LowInternal = GL_RGBA8;   // half-float targets are an extension on GLES 3.0
    g_GL->LowType = GL_UNSIGNED_BYTE;
    bool ok = true;
#endif
    GLint last_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    const GLuint vs = ok ? Shader(GL_VERTEX_SHADER, kVertex) : 0;
    ok = ok && vs;
    if (ok)
    {
        g_GL->Down = Program(vs, kDown);
        g_GL->Blur = Program(vs, kBlur);
        g_GL->Composite = Program(vs, kComposite);
        ok = g_GL->Down && g_GL->Blur && g_GL->Composite;
    }
    if (vs) glDeleteShader(vs);
    glUseProgram((GLuint)last_program);
    if (ok)
        glGenVertexArrays(1, &g_GL->Vao);
    if (!ok) { VanGui_ImplOpenGL3_ShutdownBlur(); return false; }
    VanGui::SetBlurHandler(BlurHandler, nullptr);
    return true;
}

void VanGui_ImplOpenGL3_ShutdownBlur()
{
    if (!g_GL) return;
    VanGui::SetBlurHandler(nullptr, nullptr);
    Release(g_GL->Source);
    Release(g_GL->A);
    Release(g_GL->B);
    if (g_GL->Down) glDeleteProgram(g_GL->Down);
    if (g_GL->Blur) glDeleteProgram(g_GL->Blur);
    if (g_GL->Composite) glDeleteProgram(g_GL->Composite);
    if (g_GL->Vao) glDeleteVertexArrays(1, &g_GL->Vao);
    delete g_GL;
    g_GL = nullptr;
}

#else // no effects module, GLES 2, or a custom loader: nothing to install

bool VanGui_ImplOpenGL3_InitBlur(const char*) { return false; }
void VanGui_ImplOpenGL3_ShutdownBlur() {}

#endif
#endif // #ifndef VANGUI_DISABLE
