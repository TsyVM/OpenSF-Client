// VanGUI: Renderer Backend for SDL_Renderer for SDL2
// (Requires: SDL 2.0.17+)

// Note that SDL_Renderer is an _optional_ component of SDL2, which IMHO is now largely obsolete.
// For a multi-platform app consider using other technologies:
// - SDL3+SDL_GPU: SDL_GPU is SDL3 new graphics abstraction API. You will need to update to SDL3.
// - SDL2+DirectX, SDL2+OpenGL, SDL2+Vulkan: combine SDL with dedicated renderers.
// If your application wants to render any non trivial amount of graphics other than UI,
// please be aware that SDL_Renderer currently offers a limited graphic API to the end-user
// and it might be difficult to step out of those boundaries.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(VanGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.
// Missing features or Issues:
//  [ ] Renderer: Missing support for DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest callbacks: SDLRenderer2 does not support changing SDL_SCALE_MODE while rendering.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-23: Added support for standard draw callbacks (in platform_io): DrawCallback_ResetRenderState (others cannot be supported). (#9378)
//  2026-03-12: Fixed invalid assert in VanGui_ImplSDLRenderer2_UpdateTexture() if VanTextureID_Invalid is defined to be != 0, which became the default since 2026-03-12. (#9295)
//  2025-09-18: Call platform_io.ClearRendererHandlers() on shutdown.
//  2025-06-11: Added support for VanGuiBackendFlags_RendererHasTextures, for dynamic font atlas. Removed VanGui_ImplSDLRenderer2_CreateFontsTexture() and VanGui_ImplSDLRenderer2_DestroyFontsTexture().
//  2025-01-18: Use endian-dependent RGBA32 texture format, to match SDL_Color.
//  2024-10-09: Expose selected render state in VanGui_ImplSDLRenderer2_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2024-05-14: *BREAKING CHANGE* VanGui_ImplSDLRenderer3_RenderDrawData() requires SDL_Renderer* passed as parameter.
//  2023-05-30: Renamed vangui_impl_sdlrenderer.h/.cpp to vangui_impl_sdlrenderer2.h/.cpp to accommodate for upcoming SDL3.
//  2022-10-11: Using 'nullptr' instead of 'nullptr' as per our switch to C++11.
//  2021-12-21: Update SDL_RenderGeometryRaw() format to work with SDL 2.0.19.
//  2021-12-03: Added support for large mesh (64K+ vertices), enable VanGuiBackendFlags_RendererHasVtxOffset flag.
//  2021-10-06: Backup and restore modified ClipRect/Viewport.
//  2021-09-21: Initial version.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_sdlrenderer2.h"
#include <stdint.h>     // intptr_t

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"    // warning: implicit conversion changes signedness
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wfloat-equal"                      // warning: comparing floating-point with '==' or '!=' is unsafe
#endif

// SDL
#include <SDL.h>
#if !SDL_VERSION_ATLEAST(2,0,17)
#error This backend requires SDL 2.0.17+ because of SDL_RenderGeometry() function
#endif

// SDL_Renderer data
struct VanGui_ImplSDLRenderer2_Data
{
    SDL_Renderer*   Renderer;       // Main viewport's renderer

    VanGui_ImplSDLRenderer2_Data()   { memset(static_cast<void*>(this), 0, sizeof(*this)); }
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
static VanGui_ImplSDLRenderer2_Data* VanGui_ImplSDLRenderer2_GetBackendData()
{
    return VanGui::GetCurrentContext() ? static_cast<VanGui_ImplSDLRenderer2_Data*>(VanGui::GetIO().BackendRendererUserData) : nullptr;
}

// Functions
static void VanGui_ImplSDLRenderer2_SetupRenderState(SDL_Renderer* renderer)
{
    // Clear out any viewports and cliprect set by the user
    // FIXME: Technically speaking there are lots of other things we could backup/setup/restore during our render process.
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetClipRect(renderer, nullptr);
}

void VanGui_ImplSDLRenderer2_NewFrame()
{
    VanGui_ImplSDLRenderer2_Data* bd = VanGui_ImplSDLRenderer2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplSDLRenderer2_Init()?");
    VAN_UNUSED(bd);
}

// Draw callbacks
static void VanGui_ImplSDLRenderer2_DrawCallback_ResetRenderState(const VanDrawList*, const VanDrawCmd*) {} // Intentionally empty. Used as an identifier for rendering loop to call its code. Simpler to implement this way.

void VanGui_ImplSDLRenderer2_RenderDrawData(VanDrawData* draw_data, SDL_Renderer* renderer)
{
    // If there's a scale factor set by the user, use that instead
    // If the user has specified a scale factor to SDL_Renderer already via SDL_RenderSetScale(), SDL will scale whatever we pass
    // to SDL_RenderGeometryRaw() by that scale factor. In that case we don't want to be also scaling it ourselves here.
    float rsx = 1.0f;
    float rsy = 1.0f;
    SDL_RenderGetScale(renderer, &rsx, &rsy);
    VanVec2 render_scale;
    render_scale.x = (rsx == 1.0f) ? draw_data->FramebufferScale.x : 1.0f;
    render_scale.y = (rsy == 1.0f) ? draw_data->FramebufferScale.y : 1.0f;

    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = static_cast<int>(draw_data->DisplaySize.x * render_scale.x);
    int fb_height = static_cast<int>(draw_data->DisplaySize.y * render_scale.y);
    if (fb_width == 0 || fb_height == 0)
        return;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to VanGui::GetPlatformIO().Textures[] but is part of VanDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (VanTextureData* tex : *draw_data->Textures)
            if (tex->Status != VanTextureStatus_OK)
                VanGui_ImplSDLRenderer2_UpdateTexture(tex);

    // Backup SDL_Renderer state that will be modified to restore it afterwards
    struct BackupSDLRendererState
    {
        SDL_Rect    Viewport;
        bool        ClipEnabled;
        SDL_Rect    ClipRect;
    };
    BackupSDLRendererState old = {};
    old.ClipEnabled = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
    SDL_RenderGetViewport(renderer, &old.Viewport);
    SDL_RenderGetClipRect(renderer, &old.ClipRect);

    // Setup desired state
    VanGui_ImplSDLRenderer2_SetupRenderState(renderer);

    // Setup render state structure (for callbacks and custom texture bindings)
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    VanGui_ImplSDLRenderer2_RenderState render_state;
    render_state.Renderer = renderer;
    platform_io.Renderer_RenderState = &render_state;

    // Will project scissor/clipping rectangles into framebuffer space
    VanVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    VanVec2 clip_scale = render_scale;

    // Render command lists
    for (const VanDrawList* draw_list : draw_data->CmdLists)
    {
        const VanDrawVert* vtx_buffer = draw_list->VtxBuffer.Data;
        const VanDrawIdx* idx_buffer = draw_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const VanDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback, registered via VanDrawList::AddCallback()
                if (pcmd->UserCallback == VanGui_ImplSDLRenderer2_DrawCallback_ResetRenderState)
                    VanGui_ImplSDLRenderer2_SetupRenderState(renderer);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                VanVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                VanVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if (clip_max.x > static_cast<float>(fb_width)) { clip_max.x = static_cast<float>(fb_width); }
                if (clip_max.y > static_cast<float>(fb_height)) { clip_max.y = static_cast<float>(fb_height); }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                SDL_Rect r = { static_cast<int>(clip_min.x), static_cast<int>(clip_min.y), static_cast<int>(clip_max.x - clip_min.x), static_cast<int>(clip_max.y - clip_min.y) };
                SDL_RenderSetClipRect(renderer, &r);

                const float* xy = reinterpret_cast<const float*>(reinterpret_cast<const char*>(vtx_buffer + pcmd->VtxOffset) + offsetof(VanDrawVert, pos));
                const float* uv = reinterpret_cast<const float*>(reinterpret_cast<const char*>(vtx_buffer + pcmd->VtxOffset) + offsetof(VanDrawVert, uv));
#if SDL_VERSION_ATLEAST(2,0,19)
                const SDL_Color* color = reinterpret_cast<const SDL_Color*>(reinterpret_cast<const char*>(vtx_buffer + pcmd->VtxOffset) + offsetof(VanDrawVert, col)); // SDL 2.0.19+
#else
                const int* color = reinterpret_cast<const int*>(reinterpret_cast<const char*>(vtx_buffer + pcmd->VtxOffset) + offsetof(VanDrawVert, col)); // SDL 2.0.17 and 2.0.18
#endif

                // Bind texture, Draw
                SDL_Texture* tex = reinterpret_cast<SDL_Texture*>(pcmd->GetTexID());
                SDL_RenderGeometryRaw(renderer, tex,
                    xy, static_cast<int>(sizeof(VanDrawVert)),
                    color, static_cast<int>(sizeof(VanDrawVert)),
                    uv, static_cast<int>(sizeof(VanDrawVert)),
                    draw_list->VtxBuffer.Size - pcmd->VtxOffset,
                    idx_buffer + pcmd->IdxOffset, pcmd->ElemCount, sizeof(VanDrawIdx));
            }
        }
    }
    platform_io.Renderer_RenderState = nullptr;

    // Restore modified SDL_Renderer state
    SDL_RenderSetViewport(renderer, &old.Viewport);
    SDL_RenderSetClipRect(renderer, old.ClipEnabled ? &old.ClipRect : nullptr);
}

void VanGui_ImplSDLRenderer2_UpdateTexture(VanTextureData* tex)
{
    VanGui_ImplSDLRenderer2_Data* bd = VanGui_ImplSDLRenderer2_GetBackendData();

    if (tex->Status == VanTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //VANGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        VAN_ASSERT(tex->TexID == VanTextureID_Invalid && tex->BackendUserData == nullptr);
        VAN_ASSERT(tex->Format == VanTextureFormat_RGBA32);

        // Create texture
        // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= VanFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
        SDL_Texture* sdl_texture = SDL_CreateTexture(bd->Renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tex->Width, tex->Height);
        VAN_ASSERT(sdl_texture != nullptr && "Backend failed to create texture!");
        SDL_UpdateTexture(sdl_texture, nullptr, tex->GetPixels(), tex->GetPitch());
        SDL_SetTextureBlendMode(sdl_texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(sdl_texture, SDL_ScaleModeLinear);

        // Store identifiers
        tex->SetTexID(reinterpret_cast<VanTextureID>(sdl_texture));
        tex->SetStatus(VanTextureStatus_OK);
    }
    else if (tex->Status == VanTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        SDL_Texture* sdl_texture = reinterpret_cast<SDL_Texture*>(tex->TexID);
        for (VanTextureRect& r : tex->Updates)
        {
            SDL_Rect sdl_r = { r.x, r.y, r.w, r.h };
            SDL_UpdateTexture(sdl_texture, &sdl_r, tex->GetPixelsAt(r.x, r.y), tex->GetPitch());
        }
        tex->SetStatus(VanTextureStatus_OK);
    }
    else if (tex->Status == VanTextureStatus_WantDestroy)
    {
        if (tex->TexID != VanTextureID_Invalid)
            if (SDL_Texture* sdl_texture = reinterpret_cast<SDL_Texture*>(tex->TexID))
                SDL_DestroyTexture(sdl_texture);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(VanTextureID_Invalid);
        tex->SetStatus(VanTextureStatus_Destroyed);
    }
}

void VanGui_ImplSDLRenderer2_CreateDeviceObjects()
{
}

void VanGui_ImplSDLRenderer2_DestroyDeviceObjects()
{
    // Destroy all textures
    for (VanTextureData* tex : VanGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
        {
            tex->SetStatus(VanTextureStatus_WantDestroy);
            VanGui_ImplSDLRenderer2_UpdateTexture(tex);
        }
}

bool VanGui_ImplSDLRenderer2_Init(SDL_Renderer* renderer)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    VAN_ASSERT(renderer != nullptr && "SDL_Renderer not initialized!");

    // Setup backend capabilities flags
    VanGui_ImplSDLRenderer2_Data* bd = VAN_NEW(VanGui_ImplSDLRenderer2_Data)();
    io.BackendRendererUserData = static_cast<void*>(bd);
    io.BackendRendererName = "vangui_impl_sdlrenderer2";
    io.BackendFlags |= VanGuiBackendFlags_RendererHasVtxOffset;  // We can honor the VanDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= VanGuiBackendFlags_RendererHasTextures;   // We can honor VanGuiPlatformIO::Textures[] requests during render.

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    platform_io.DrawCallback_ResetRenderState = VanGui_ImplSDLRenderer2_DrawCallback_ResetRenderState;

    bd->Renderer = renderer;

    return true;
}

void VanGui_ImplSDLRenderer2_Shutdown()
{
    VanGui_ImplSDLRenderer2_Data* bd = VanGui_ImplSDLRenderer2_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    VanGui_ImplSDLRenderer2_DestroyDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_RendererHasVtxOffset | VanGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    VAN_DELETE(bd);
}

//-----------------------------------------------------------------------------

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef VANGUI_DISABLE
