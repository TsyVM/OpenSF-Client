// VanGUI: Renderer Backend for OpenGL2 (legacy OpenGL, fixed pipeline)
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'GLuint' OpenGL texture as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
// Missing features or Issues:
//  [ ] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [ ] Renderer: Use of DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest is emulated by poking to glTexParameter(), as legacy OpenGL doesn't have glBindSampler().

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// **DO NOT USE THIS CODE IF YOUR CODE/ENGINE IS USING MODERN OPENGL (SHADERS, VBO, VAO, etc.)**
// **Prefer using the code in vangui_impl_opengl3.cpp**
// This code is mostly provided as a reference to learn how VanGui integration works, because it is shorter to read.
// If your code is using GL3+ context or any semi modern OpenGL calls, using this is likely to make everything more
// complicated, will require your code to reset every single OpenGL attributes to their initial state, and might
// confuse your GPU driver.
// The GL2 code is unable to reset attributes or even call e.g. "glUseProgram(0)" because they don't exist in that API.

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-23: OpenGL: Added support for standard draw callbacks (in platform_io): DrawCallback_ResetRenderState, DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest. (#9378)
//  2026-03-12: OpenGL: Fixed invalid assert in VanGui_ImplOpenGL3_UpdateTexture() if VanTextureID_Invalid is defined to be != 0, which became the default since 2026-03-12. (#9295)
//  2025-09-18: Call platform_io.ClearRendererHandlers() on shutdown.
//  2025-07-15: OpenGL: Set GL_UNPACK_ALIGNMENT to 1 before updating textures. (#8802)
//  2025-06-11: OpenGL: Added support for VanGuiBackendFlags_RendererHasTextures, for dynamic font atlas. Removed VanGui_ImplOpenGL2_CreateFontsTexture() and VanGui_ImplOpenGL2_DestroyFontsTexture().
//  2024-10-07: OpenGL: Changed default texture sampler to Clamp instead of Repeat/Wrap.
//  2024-06-28: OpenGL: VanGui_ImplOpenGL2_NewFrame() recreates font texture if it has been destroyed by VanGui_ImplOpenGL2_DestroyFontsTexture(). (#7748)
//  2022-10-11: Using 'nullptr' instead of 'nullptr' as per our switch to C++11.
//  2021-12-08: OpenGL: Fixed mishandling of the VanDrawCmd::IdxOffset field! This is an old bug but it never had an effect until some internal rendering changes in 1.86.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: OpenGL: Replaced direct access to VanDrawCmd::TextureId with a call to VanDrawCmd::GetTexID(). (will become a requirement)
//  2021-01-03: OpenGL: Backup, setup and restore GL_SHADE_MODEL state, disable GL_STENCIL_TEST and disable GL_NORMAL_ARRAY client state to increase compatibility with legacy OpenGL applications.
//  2020-01-23: OpenGL: Backup, setup and restore GL_TEXTURE_ENV to increase compatibility with legacy OpenGL applications.
//  2019-04-30: OpenGL: Added support for special VanDrawCallback_ResetRenderState callback to reset render state.
//  2019-02-11: OpenGL: Projecting clipping rectangles correctly using draw_data->FramebufferScale to allow multi-viewports for retina display.
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-08-03: OpenGL: Disabling/restoring GL_LIGHTING and GL_COLOR_MATERIAL to increase compatibility with legacy OpenGL applications.
//  2018-06-08: Misc: Extracted vangui_impl_opengl2.cpp/.h away from the old combined GLFW/SDL+OpenGL2 examples.
//  2018-06-08: OpenGL: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed VanGui_ImplOpenGL2_RenderDrawData() in the .h file so you can call it yourself.
//  2017-09-01: OpenGL: Save and restore current polygon mode.
//  2016-09-10: OpenGL: Uploading font texture as RGBA32 to increase compatibility with users shaders (not ideal).
//  2016-09-05: OpenGL: Fixed save and restore of current scissor rectangle.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_opengl2.h"
#include <stdint.h>     // intptr_t

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"                      // warning: macro is not used
#pragma clang diagnostic ignored "-Wnonportable-system-include-path"
#endif

// Include OpenGL header (without an OpenGL loader) requires a bit of fiddling
#if defined(_WIN32) && !defined(APIENTRY)
#define APIENTRY __stdcall                  // It is customary to use APIENTRY for OpenGL function pointer declarations on all platforms.  Additionally, the Windows OpenGL header needs APIENTRY.
#endif
#if defined(_WIN32) && !defined(WINGDIAPI)
#define WINGDIAPI __declspec(dllimport)     // Some Windows OpenGL headers need this
#endif
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// [Debugging]
//#define VANGUI_IMPL_OPENGL_DEBUG
#ifdef VANGUI_IMPL_OPENGL_DEBUG
#include <cstdio>
#define GL_CALL(_CALL)      do { _CALL; GLenum gl_err = glGetError(); if (gl_err != 0) fprintf(stderr, "GL error 0x%x returned from '%s'.\n", gl_err, #_CALL); } while (0)  // Call with error check
#else
#define GL_CALL(_CALL)      _CALL   // Call without error check
#endif

// OpenGL data
struct VanGui_ImplOpenGL2_Data
{
    bool        UseTexParameterToSetSampler = false;
    GLuint      NextSampler                 = 0;
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
static VanGui_ImplOpenGL2_Data* VanGui_ImplOpenGL2_GetBackendData()
{
    return VanGui::GetCurrentContext() ? static_cast<VanGui_ImplOpenGL2_Data*>(VanGui::GetIO().BackendRendererUserData) : nullptr;
}

// Functions
void    VanGui_ImplOpenGL2_NewFrame()
{
    VanGui_ImplOpenGL2_Data* bd = VanGui_ImplOpenGL2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplOpenGL2_Init()?");
    VAN_UNUSED(bd);
}

static void VanGui_ImplOpenGL2_SetupRenderState(VanDrawData* draw_data, int fb_width, int fb_height)
{
    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, vertex/texcoord/color pointers, polygon fill.
    VanGui_ImplOpenGL2_Data* bd = VanGui_ImplOpenGL2_GetBackendData();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // In order to composite our output buffer we need to preserve alpha
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glEnable(GL_SCISSOR_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glEnable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_SMOOTH);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    bd->NextSampler = GL_LINEAR;

    // If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using vangui_impl_opengl3.cpp!!),
    // you may need to backup/reset/restore other state, e.g. for current shader using the commented lines below.
    // (DO NOT MODIFY THIS FILE! Add the code in your calling function)
    //   GLint last_program;
    //   glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    //   glUseProgram(0);
    //   VanGui_ImplOpenGL2_RenderDrawData(...);
    //   glUseProgram(last_program)
    // There are potentially many more states you could need to clear/setup that we can't access from default headers.
    // e.g. glBindBuffer(GL_ARRAY_BUFFER, 0), glDisable(GL_TEXTURE_CUBE_MAP).

    // Setup viewport, orthographic projection matrix
    // Our visible vangui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    GL_CALL(glViewport(0, 0, static_cast<GLsizei>(fb_width), static_cast<GLsizei>(fb_height)));
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(draw_data->DisplayPos.x, draw_data->DisplayPos.x + draw_data->DisplaySize.x, draw_data->DisplayPos.y + draw_data->DisplaySize.y, draw_data->DisplayPos.y, -1.0f, +1.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

// Draw callbacks
static void VanGui_ImplOpenGL2_DrawCallback_ResetRenderState(const VanDrawList*, const VanDrawCmd*)    {} // Intentionally empty. Used as an identifier for rendering loop to call its code. Simpler to implement this way.
static void VanGui_ImplOpenGL2_DrawCallback_SetSamplerLinear(const VanDrawList*, const VanDrawCmd*)    { VanGui_ImplOpenGL2_Data* bd = VanGui_ImplOpenGL2_GetBackendData(); bd->UseTexParameterToSetSampler = true; bd->NextSampler = GL_LINEAR; }
static void VanGui_ImplOpenGL2_DrawCallback_SetSamplerNearest(const VanDrawList*, const VanDrawCmd*)   { VanGui_ImplOpenGL2_Data* bd = VanGui_ImplOpenGL2_GetBackendData(); bd->UseTexParameterToSetSampler = true; bd->NextSampler = GL_NEAREST; }

// OpenGL2 Render function.
// Note that this implementation is little overcomplicated because we are saving/setting up/restoring every OpenGL state explicitly.
// This is in order to be able to run within an OpenGL engine that doesn't do so.
void VanGui_ImplOpenGL2_RenderDrawData(VanDrawData* draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    VanGui_ImplOpenGL2_Data* bd = VanGui_ImplOpenGL2_GetBackendData();
    int fb_width = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to VanGui::GetPlatformIO().Textures[] but is part of VanDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (VanTextureData* tex : *draw_data->Textures)
            if (tex->Status != VanTextureStatus_OK)
                VanGui_ImplOpenGL2_UpdateTexture(tex);

    // Backup GL state
    GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_polygon_mode[2]; glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLint last_shade_model; glGetIntegerv(GL_SHADE_MODEL, &last_shade_model);
    GLint last_tex_env_mode; glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &last_tex_env_mode);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);

    // Setup desired GL state
    VanGui_ImplOpenGL2_SetupRenderState(draw_data, fb_width, fb_height);

    // Will project scissor/clipping rectangles into framebuffer space
    VanVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    VanVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    for (const VanDrawList* draw_list : draw_data->CmdLists)
    {
        const VanDrawVert* vtx_buffer = draw_list->VtxBuffer.Data;
        const VanDrawIdx* idx_buffer = draw_list->IdxBuffer.Data;
        glVertexPointer(2, GL_FLOAT, sizeof(VanDrawVert), reinterpret_cast<const GLvoid*>(reinterpret_cast<const char*>(vtx_buffer) + offsetof(VanDrawVert, pos)));
        glTexCoordPointer(2, GL_FLOAT, sizeof(VanDrawVert), reinterpret_cast<const GLvoid*>(reinterpret_cast<const char*>(vtx_buffer) + offsetof(VanDrawVert, uv)));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(VanDrawVert), reinterpret_cast<const GLvoid*>(reinterpret_cast<const char*>(vtx_buffer) + offsetof(VanDrawVert, col)));

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const VanDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback, registered via VanDrawList::AddCallback()
                if (pcmd->UserCallback == VanGui_ImplOpenGL2_DrawCallback_ResetRenderState)
                    VanGui_ImplOpenGL2_SetupRenderState(draw_data, fb_width, fb_height);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                VanVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                VanVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle (Y is inverted in OpenGL)
                glScissor(static_cast<int>(clip_min.x), static_cast<int>(static_cast<float>(fb_height) - clip_max.y), static_cast<int>(clip_max.x - clip_min.x), static_cast<int>(clip_max.y - clip_min.y));

                // Bind texture, Draw
                GLuint pcmd_texture = static_cast<GLuint>(pcmd->GetTexID());
                glBindTexture(GL_TEXTURE_2D, pcmd_texture);

                // Emulate sampler change (even though it is technically part of texture data)
                // As a sort of hack/workaround, we only start writing using glTextParameter() if sampler is ever changed explicitly.
                if (bd->UseTexParameterToSetSampler)
                {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, bd->NextSampler);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, bd->NextSampler);
                }

                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(pcmd->ElemCount), sizeof(VanDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer + pcmd->IdxOffset);
            }
        }
    }

    // Restore modified GL state
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(last_texture));
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glPolygonMode(GL_FRONT, static_cast<GLenum>(last_polygon_mode[0])); glPolygonMode(GL_BACK, static_cast<GLenum>(last_polygon_mode[1]));
    glViewport(last_viewport[0], last_viewport[1], static_cast<GLsizei>(last_viewport[2]), static_cast<GLsizei>(last_viewport[3]));
    glScissor(last_scissor_box[0], last_scissor_box[1], static_cast<GLsizei>(last_scissor_box[2]), static_cast<GLsizei>(last_scissor_box[3]));
    glShadeModel(last_shade_model);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, last_tex_env_mode);
}

void VanGui_ImplOpenGL2_UpdateTexture(VanTextureData* tex)
{
    if (tex->Status == VanTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //VANGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        VAN_ASSERT(tex->TexID == VanTextureID_Invalid && tex->BackendUserData == nullptr);
        VAN_ASSERT(tex->Format == VanTextureFormat_RGBA32);
        const void* pixels = tex->GetPixels();
        GLuint gl_texture_id = 0;

        // Upload texture to graphics system
        // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= VanFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
        GLint last_texture;
        GL_CALL(glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
        GL_CALL(glGenTextures(1, &gl_texture_id));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, gl_texture_id));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
        GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->Width, tex->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

        // Store identifiers
        tex->SetTexID(static_cast<VanTextureID>(gl_texture_id));
        tex->SetStatus(VanTextureStatus_OK);

        // Restore state
        GL_CALL(glBindTexture(GL_TEXTURE_2D, last_texture));
    }
    else if (tex->Status == VanTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        GLint last_texture;
        GL_CALL(glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));

        GLuint gl_tex_id = static_cast<GLuint>(tex->TexID);
        GL_CALL(glBindTexture(GL_TEXTURE_2D, gl_tex_id));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->Width));
        GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
        for (VanTextureRect& r : tex->Updates)
            GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h, GL_RGBA, GL_UNSIGNED_BYTE, tex->GetPixelsAt(r.x, r.y)));
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, last_texture)); // Restore state
        tex->SetStatus(VanTextureStatus_OK);
    }
    else if (tex->Status == VanTextureStatus_WantDestroy)
    {
        GLuint gl_tex_id = static_cast<GLuint>(tex->TexID);
        glDeleteTextures(1, &gl_tex_id);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(VanTextureID_Invalid);
        tex->SetStatus(VanTextureStatus_Destroyed);
    }
}

bool    VanGui_ImplOpenGL2_CreateDeviceObjects()
{
    return true;
}

void    VanGui_ImplOpenGL2_DestroyDeviceObjects()
{
    for (VanTextureData* tex : VanGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
        {
            tex->SetStatus(VanTextureStatus_WantDestroy);
            VanGui_ImplOpenGL2_UpdateTexture(tex);
        }
}

bool    VanGui_ImplOpenGL2_Init()
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    VanGui_ImplOpenGL2_Data* bd = VAN_NEW(VanGui_ImplOpenGL2_Data)();
    io.BackendRendererUserData = static_cast<void*>(bd);
    io.BackendRendererName = "vangui_impl_opengl2";
    io.BackendFlags |= VanGuiBackendFlags_RendererHasTextures;       // We can honor VanGuiPlatformIO::Textures[] requests during render.

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.DrawCallback_ResetRenderState = VanGui_ImplOpenGL2_DrawCallback_ResetRenderState;
    platform_io.DrawCallback_SetSamplerLinear = VanGui_ImplOpenGL2_DrawCallback_SetSamplerLinear;
    platform_io.DrawCallback_SetSamplerNearest = VanGui_ImplOpenGL2_DrawCallback_SetSamplerNearest;

    return true;
}

void    VanGui_ImplOpenGL2_Shutdown()
{
    VanGui_ImplOpenGL2_Data* bd = VanGui_ImplOpenGL2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    VanGui_ImplOpenGL2_DestroyDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    VAN_DELETE(bd);
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef VANGUI_DISABLE
