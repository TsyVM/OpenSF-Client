// VanGUI: Renderer + Platform Backend for Allegro 5
// (Info: Allegro 5 is a cross-platform general purpose library for handling windows, inputs, graphics, etc.)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ALLEGRO_BITMAP*' as texture identifier. Read the FAQ about VanTextureID/VanTextureRef!
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass VanGuiKey values to all key functions e.g. VanGui::IsKeyPressed(VanGuiKey_Space). [Legacy ALLEGRO_KEY_* values are obsolete since 1.87 and not supported since 1.91.5]
//  [X] Platform: Clipboard support (from Allegro 5.1.12).
//  [X] Platform: Mouse cursor shape and visibility (VanGuiBackendFlags_HasMouseCursors). Disable with 'io.ConfigFlags |= VanGuiConfigFlags_NoMouseCursorChange'.
// Missing features or Issues:
//  [ ] Renderer: The renderer is suboptimal as we need to unindex our buffers and convert vertices manually.
//  [ ] Renderer: Missing support for DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest callbacks: Allegro5 cannot enable/disable LINEAR bitmap flags after creation.
//  [ ] Platform: Missing gamepad support.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-04-23: Added support for standard draw callbacks (in platform_io): DrawCallback_ResetRenderState (others cannot be supported by Allegro5). (#9378)
//  2025-09-18: Call platform_io.ClearRendererHandlers() and platform_io.ClearPlatformHandlers() on shutdown.
//  2025-08-12: Inputs: fixed missing support for VanGuiKey_PrintScreen under Windows, as raw Allegro 5 does not receive it.
//  2025-08-12: Added VanGui_ImplAllegro5_SetDisplay() function to change current ALLEGRO_DISPLAY, as Allegro applications often need to do that.
//  2025-07-07: Fixed texture update broken on some platforms where ALLEGRO_LOCK_WRITEONLY needed all texels to be rewritten.
//  2025-06-11: Added support for VanGuiBackendFlags_RendererHasTextures, for dynamic font atlas. Removed VanGui_ImplSDLGPU3_CreateFontsTexture() and VanGui_ImplSDLGPU3_DestroyFontsTexture().
//  2025-02-18: Added VanGuiMouseCursor_Wait and VanGuiMouseCursor_Progress mouse cursor support.
//  2025-01-06: Avoid calling al_set_mouse_cursor() repeatedly since it appears to leak on on X11 (#8256).
//  2024-08-22: moved some OS/backend related function pointers from VanGuiIO to VanGuiPlatformIO:
//               - io.GetClipboardTextFn    -> platform_io.Platform_GetClipboardTextFn
//               - io.SetClipboardTextFn    -> platform_io.Platform_SetClipboardTextFn
//  2022-11-30: Renderer: Restoring using al_draw_indexed_prim() when Allegro version is >= 5.2.5.
//  2022-10-11: Using 'nullptr' instead of 'nullptr' as per our switch to C++11.
//  2022-09-26: Inputs: Renamed VanGuiKey_ModXXX introduced in 1.87 to VanGuiMod_XXX (old names still supported).
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using VanGuiKey_ModXXX flags. Sorry for the confusion.
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-17: Inputs: always calling io.AddKeyModsEvent() next and before key event (not in NewFrame) to fix input queue with very low framerates.
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full VanGuiKey range.
//  2021-12-08: Renderer: Fixed mishandling of the VanDrawCmd::IdxOffset field! This is an old bug but it never had an effect until some internal rendering changes in 1.86.
//  2021-08-17: Calling io.AddFocusEvent() on ALLEGRO_EVENT_DISPLAY_SWITCH_OUT/ALLEGRO_EVENT_DISPLAY_SWITCH_IN events.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: Renderer: Replaced direct access to VanDrawCmd::TextureId with a call to VanDrawCmd::GetTexID(). (will become a requirement)
//  2021-02-18: Change blending equation to preserve alpha in output buffer.
//  2020-08-10: Inputs: Fixed horizontal mouse wheel direction.
//  2019-12-05: Inputs: Added support for VanGuiMouseCursor_NotAllowed mouse cursor.
//  2019-07-21: Inputs: Added mapping for VanGuiKey_KeyPadEnter.
//  2019-05-11: Inputs: Don't filter character value from ALLEGRO_EVENT_KEY_CHAR before calling AddInputCharacter().
//  2019-04-30: Renderer: Added support for special VanDrawCallback_ResetRenderState callback to reset render state.
//  2018-11-30: Platform: Added touchscreen support.
//  2018-11-30: Misc: Setting up io.BackendPlatformName/io.BackendRendererName so they can be displayed in the About Window.
//  2018-06-13: Platform: Added clipboard support (from Allegro 5.1.12).
//  2018-06-13: Renderer: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-06-13: Renderer: Stopped using al_draw_indexed_prim() as it is buggy in Allegro's DX9 backend.
//  2018-06-13: Renderer: Backup/restore transform and clipping rectangle.
//  2018-06-11: Misc: Setup io.BackendFlags VanGuiBackendFlags_HasMouseCursors flag + honor VanGuiConfigFlags_NoMouseCursorChange flag.
//  2018-04-18: Misc: Renamed file from vangui_impl_a5.cpp to vangui_impl_allegro5.cpp.
//  2018-04-18: Misc: Added support for 32-bit vertex indices to avoid conversion at runtime. Added vanconfig_allegro5.h to enforce 32-bit indices when included from vangui.h.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed VanGui_ImplAllegro5_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to VanGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-02-06: Inputs: Added mapping for VanGuiKey_Space.

#include "vangui.h"
#ifndef VANGUI_DISABLE
#include "vangui_impl_allegro5.h"
#include <stdint.h>     // uint64_t
#include <cstring>      // memcpy

// Allegro
#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#ifdef _WIN32
#include <allegro5/allegro_windows.h>
#endif
#define ALLEGRO_HAS_CLIPBOARD           ((ALLEGRO_VERSION_INT & ~ALLEGRO_UNSTABLE_BIT) >= ((5 << 24) | (1 << 16) | (12 << 8))) // Clipboard only supported from Allegro 5.1.12
#define ALLEGRO_HAS_DRAW_INDEXED_PRIM   ((ALLEGRO_VERSION_INT & ~ALLEGRO_UNSTABLE_BIT) >= ((5 << 24) | (2 << 16) | ( 5 << 8))) // DX9 implementation of al_draw_indexed_prim() got fixed in Allegro 5.2.5

// Visual Studio warnings
#ifdef _MSC_VER
#pragma warning (disable: 4127) // condition expression is constant
#endif

struct VanDrawVertAllegro
{
    VanVec2          pos;
    VanVec2          uv;
    ALLEGRO_COLOR   col;
};

// FIXME-OPT: Unfortunately Allegro doesn't support 32-bit packed colors so we have to convert them to 4 float as well..
// FIXME-OPT: Consider inlining al_map_rgba()?
// see https://github.com/liballeg/allegro5/blob/master/src/pixels.c#L554
// and https://github.com/liballeg/allegro5/blob/master/include/allegro5/internal/aintern_pixels.h
#define DRAW_VERT_VANGUI_TO_ALLEGRO(DST, SRC)  { (DST)->pos = (SRC)->pos; (DST)->uv = (SRC)->uv; unsigned char* c = (unsigned char*)&(SRC)->col; (DST)->col = al_map_rgba(c[0], c[1], c[2], c[3]); }

// Allegro Data
struct VanGui_ImplAllegro5_Data
{
    ALLEGRO_DISPLAY*            Display;
    ALLEGRO_BITMAP*             Texture;
    double                      Time;
    ALLEGRO_MOUSE_CURSOR*       MouseCursorInvisible;
    ALLEGRO_VERTEX_DECL*        VertexDecl;
    char*                       ClipboardTextData;
    VanGuiMouseCursor            LastCursor;

    VanVector<VanDrawVertAllegro> BufVertices;
    VanVector<int>               BufIndices;

    VanGui_ImplAllegro5_Data()   { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple VanGUI contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single VanGUI context + multiple windows) instead of multiple VanGUI contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
static VanGui_ImplAllegro5_Data* VanGui_ImplAllegro5_GetBackendData()     { return VanGui::GetCurrentContext() ? static_cast<VanGui_ImplAllegro5_Data*>(VanGui::GetIO().BackendPlatformUserData) : nullptr; }

static void VanGui_ImplAllegro5_SetupRenderState(VanDrawData* draw_data)
{
    // Setup blending
    al_set_separate_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA, ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);

    // Setup orthographic projection matrix
    // Our visible vangui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
    {
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        ALLEGRO_TRANSFORM transform;
        al_identity_transform(&transform);
        al_use_transform(&transform);
        al_orthographic_transform(&transform, L, T, 1.0f, R, B, -1.0f);
        al_use_projection_transform(&transform);
    }
}

// Draw callbacks
static void VanGui_ImplAllegro5_DrawCallback_ResetRenderState(const VanDrawList*, const VanDrawCmd*) {} // Intentionally empty. Used as an identifier for rendering loop to call its code. Simpler to implement this way.

// Render function
void VanGui_ImplAllegro5_RenderDrawData(VanDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to VanGui::GetPlatformIO().Textures[] but is part of VanDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (VanTextureData* tex : *draw_data->Textures)
            if (tex->Status != VanTextureStatus_OK)
                VanGui_ImplAllegro5_UpdateTexture(tex);

    // Backup Allegro state that will be modified
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
    ALLEGRO_TRANSFORM last_transform = *al_get_current_transform();
    ALLEGRO_TRANSFORM last_projection_transform = *al_get_current_projection_transform();
    int last_clip_x, last_clip_y, last_clip_w, last_clip_h;
    al_get_clipping_rectangle(&last_clip_x, &last_clip_y, &last_clip_w, &last_clip_h);
    int last_blender_op, last_blender_src, last_blender_dst;
    al_get_blender(&last_blender_op, &last_blender_src, &last_blender_dst);

    // Setup desired render state
    VanGui_ImplAllegro5_SetupRenderState(draw_data);

    // Render command lists
    for (const VanDrawList* draw_list : draw_data->CmdLists)
    {
        VanVector<VanDrawVertAllegro>& vertices = bd->BufVertices;
#if ALLEGRO_HAS_DRAW_INDEXED_PRIM
        vertices.resize(draw_list->VtxBuffer.Size);
        for (int i = 0; i < draw_list->VtxBuffer.Size; i++)
        {
            const VanDrawVert* src_v = &draw_list->VtxBuffer[i];
            VanDrawVertAllegro* dst_v = &vertices[i];
            DRAW_VERT_VANGUI_TO_ALLEGRO(dst_v, src_v);
        }
        const int* indices = nullptr;
        if (sizeof(VanDrawIdx) == 2)
        {
            // FIXME-OPT: Allegro doesn't support 16-bit indices.
            // You can '#define VanDrawIdx int' in vanconfig.h to request VanGUI to output 32-bit indices.
            // Otherwise, we convert them from 16-bit to 32-bit at runtime here, which works perfectly but is a little wasteful.
            bd->BufIndices.resize(draw_list->IdxBuffer.Size);
            for (int i = 0; i < draw_list->IdxBuffer.Size; ++i)
                bd->BufIndices[i] = static_cast<int>(draw_list->IdxBuffer.Data[i]);
            indices = bd->BufIndices.Data;
        }
        else if (sizeof(VanDrawIdx) == 4)
        {
            indices = static_cast<const int*>(static_cast<const void*>(draw_list->IdxBuffer.Data));
        }
#else
        // Allegro's implementation of al_draw_indexed_prim() for DX9 was broken until 5.2.5. Unindex buffers ourselves while converting vertex format.
        vertices.resize(draw_list->IdxBuffer.Size);
        for (int i = 0; i < draw_list->IdxBuffer.Size; i++)
        {
            const VanDrawVert* src_v = &draw_list->VtxBuffer[draw_list->IdxBuffer[i]];
            VanDrawVertAllegro* dst_v = &vertices[i];
            DRAW_VERT_VANGUI_TO_ALLEGRO(dst_v, src_v);
        }
#endif

        // Render command lists
        VanVec2 clip_off = draw_data->DisplayPos;
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const VanDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback, registered via VanDrawList::AddCallback()
                if (pcmd->UserCallback == VanGui_ImplAllegro5_DrawCallback_ResetRenderState)
                    VanGui_ImplAllegro5_SetupRenderState(draw_data);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                VanVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                VanVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle, Draw
                ALLEGRO_BITMAP* texture = static_cast<ALLEGRO_BITMAP*>(reinterpret_cast<void*>(pcmd->GetTexID()));
                al_set_clipping_rectangle(clip_min.x, clip_min.y, clip_max.x - clip_min.x, clip_max.y - clip_min.y);
#if ALLEGRO_HAS_DRAW_INDEXED_PRIM
                al_draw_indexed_prim(&vertices[0], bd->VertexDecl, texture, &indices[pcmd->IdxOffset], pcmd->ElemCount, ALLEGRO_PRIM_TRIANGLE_LIST);
#else
                al_draw_prim(&vertices[0], bd->VertexDecl, texture, pcmd->IdxOffset, pcmd->IdxOffset + pcmd->ElemCount, ALLEGRO_PRIM_TRIANGLE_LIST);
#endif
            }
        }
    }

    // Restore modified Allegro state
    al_set_blender(last_blender_op, last_blender_src, last_blender_dst);
    al_set_clipping_rectangle(last_clip_x, last_clip_y, last_clip_w, last_clip_h);
    al_use_transform(&last_transform);
    al_use_projection_transform(&last_projection_transform);
}

bool VanGui_ImplAllegro5_CreateDeviceObjects()
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();

    // Create an invisible mouse cursor
    // Because al_hide_mouse_cursor() seems to mess up with the actual inputs..
    ALLEGRO_BITMAP* mouse_cursor = al_create_bitmap(8, 8);
    bd->MouseCursorInvisible = al_create_mouse_cursor(mouse_cursor, 0, 0);
    al_destroy_bitmap(mouse_cursor);

    return true;
}

void VanGui_ImplAllegro5_UpdateTexture(VanTextureData* tex)
{
    if (tex->Status == VanTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //VANGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        VAN_ASSERT(tex->TexID == VanTextureID_Invalid && tex->BackendUserData == nullptr);
        VAN_ASSERT(tex->Format == VanTextureFormat_RGBA32);

        // Create texture
        // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= VanFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
        const int new_bitmap_flags = al_get_new_bitmap_flags();
        int new_bitmap_format = al_get_new_bitmap_format();
        al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP | ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);
        al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE);
        ALLEGRO_BITMAP* cpu_bitmap = al_create_bitmap(tex->Width, tex->Height);
        VAN_ASSERT(cpu_bitmap != nullptr && "Backend failed to create texture!");

        // Upload pixels
        ALLEGRO_LOCKED_REGION* locked_region = al_lock_bitmap(cpu_bitmap, al_get_bitmap_format(cpu_bitmap), ALLEGRO_LOCK_WRITEONLY);
        VAN_ASSERT(locked_region != nullptr && "Backend failed to create texture!");
        memcpy(locked_region->data, tex->GetPixels(), tex->GetSizeInBytes());
        al_unlock_bitmap(cpu_bitmap);

        // Convert software texture to hardware texture.
        al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);
        al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ANY_32_WITH_ALPHA);
        ALLEGRO_BITMAP* gpu_bitmap = al_clone_bitmap(cpu_bitmap);
        al_destroy_bitmap(cpu_bitmap);
        VAN_ASSERT(gpu_bitmap != nullptr && "Backend failed to create texture!");

        al_set_new_bitmap_flags(new_bitmap_flags);
        al_set_new_bitmap_format(new_bitmap_format);

        // Store identifiers
        tex->SetTexID(static_cast<VanTextureID>(reinterpret_cast<intptr_t>(gpu_bitmap)));
        tex->SetStatus(VanTextureStatus_OK);
    }
    else if (tex->Status == VanTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        VanTextureRect r = tex->UpdateRect; // Bounding box encompassing all individual updates
        ALLEGRO_BITMAP* gpu_bitmap = reinterpret_cast<ALLEGRO_BITMAP*>(static_cast<intptr_t>(tex->TexID));
        ALLEGRO_LOCKED_REGION* locked_region = al_lock_bitmap_region(gpu_bitmap, r.x, r.y, r.w, r.h, al_get_bitmap_format(gpu_bitmap), ALLEGRO_LOCK_WRITEONLY);
        VAN_ASSERT(locked_region && "Backend failed to update texture!");
        for (int y = 0; y < r.h; y++)
            memcpy(static_cast<unsigned char*>(locked_region->data) + locked_region->pitch * y, tex->GetPixelsAt(r.x, r.y + y), r.w * tex->BytesPerPixel); // dst, src, block pitch
        al_unlock_bitmap(gpu_bitmap);
        tex->SetStatus(VanTextureStatus_OK);
    }
    else if (tex->Status == VanTextureStatus_WantDestroy)
    {
        ALLEGRO_BITMAP* backend_tex = reinterpret_cast<ALLEGRO_BITMAP*>(static_cast<intptr_t>(tex->TexID));
        if (backend_tex)
            al_destroy_bitmap(backend_tex);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(VanTextureID_Invalid);
        tex->SetStatus(VanTextureStatus_Destroyed);
    }
}

void VanGui_ImplAllegro5_InvalidateDeviceObjects()
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();

    // Destroy all textures
    for (VanTextureData* tex : VanGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
        {
            tex->SetStatus(VanTextureStatus_WantDestroy);
            VanGui_ImplAllegro5_UpdateTexture(tex);
        }

    // Destroy mouse cursor
    if (bd->MouseCursorInvisible)
    {
        al_destroy_mouse_cursor(bd->MouseCursorInvisible);
        bd->MouseCursorInvisible = nullptr;
    }
}

#if ALLEGRO_HAS_CLIPBOARD
static const char* VanGui_ImplAllegro5_GetClipboardText(VanGuiContext*)
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    if (bd->ClipboardTextData)
        al_free(bd->ClipboardTextData);
    bd->ClipboardTextData = al_get_clipboard_text(bd->Display);
    return bd->ClipboardTextData;
}

static void VanGui_ImplAllegro5_SetClipboardText(VanGuiContext*, const char* text)
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    al_set_clipboard_text(bd->Display, text);
}
#endif

// Not static to allow third-party code to use that if they want to (but undocumented)
VanGuiKey VanGui_ImplAllegro5_KeyCodeToVanGuiKey(int key_code);
VanGuiKey VanGui_ImplAllegro5_KeyCodeToVanGuiKey(int key_code)
{
    switch (key_code)
    {
        case ALLEGRO_KEY_TAB: return VanGuiKey_Tab;
        case ALLEGRO_KEY_LEFT: return VanGuiKey_LeftArrow;
        case ALLEGRO_KEY_RIGHT: return VanGuiKey_RightArrow;
        case ALLEGRO_KEY_UP: return VanGuiKey_UpArrow;
        case ALLEGRO_KEY_DOWN: return VanGuiKey_DownArrow;
        case ALLEGRO_KEY_PGUP: return VanGuiKey_PageUp;
        case ALLEGRO_KEY_PGDN: return VanGuiKey_PageDown;
        case ALLEGRO_KEY_HOME: return VanGuiKey_Home;
        case ALLEGRO_KEY_END: return VanGuiKey_End;
        case ALLEGRO_KEY_INSERT: return VanGuiKey_Insert;
        case ALLEGRO_KEY_DELETE: return VanGuiKey_Delete;
        case ALLEGRO_KEY_BACKSPACE: return VanGuiKey_Backspace;
        case ALLEGRO_KEY_SPACE: return VanGuiKey_Space;
        case ALLEGRO_KEY_ENTER: return VanGuiKey_Enter;
        case ALLEGRO_KEY_ESCAPE: return VanGuiKey_Escape;
        case ALLEGRO_KEY_QUOTE: return VanGuiKey_Apostrophe;
        case ALLEGRO_KEY_COMMA: return VanGuiKey_Comma;
        case ALLEGRO_KEY_MINUS: return VanGuiKey_Minus;
        case ALLEGRO_KEY_FULLSTOP: return VanGuiKey_Period;
        case ALLEGRO_KEY_SLASH: return VanGuiKey_Slash;
        case ALLEGRO_KEY_SEMICOLON: return VanGuiKey_Semicolon;
        case ALLEGRO_KEY_EQUALS: return VanGuiKey_Equal;
        case ALLEGRO_KEY_OPENBRACE: return VanGuiKey_LeftBracket;
        case ALLEGRO_KEY_BACKSLASH: return VanGuiKey_Backslash;
        case ALLEGRO_KEY_CLOSEBRACE: return VanGuiKey_RightBracket;
        case ALLEGRO_KEY_TILDE: return VanGuiKey_GraveAccent;
        case ALLEGRO_KEY_CAPSLOCK: return VanGuiKey_CapsLock;
        case ALLEGRO_KEY_SCROLLLOCK: return VanGuiKey_ScrollLock;
        case ALLEGRO_KEY_NUMLOCK: return VanGuiKey_NumLock;
        case ALLEGRO_KEY_PRINTSCREEN: return VanGuiKey_PrintScreen;
        case ALLEGRO_KEY_PAUSE: return VanGuiKey_Pause;
        case ALLEGRO_KEY_PAD_0: return VanGuiKey_Keypad0;
        case ALLEGRO_KEY_PAD_1: return VanGuiKey_Keypad1;
        case ALLEGRO_KEY_PAD_2: return VanGuiKey_Keypad2;
        case ALLEGRO_KEY_PAD_3: return VanGuiKey_Keypad3;
        case ALLEGRO_KEY_PAD_4: return VanGuiKey_Keypad4;
        case ALLEGRO_KEY_PAD_5: return VanGuiKey_Keypad5;
        case ALLEGRO_KEY_PAD_6: return VanGuiKey_Keypad6;
        case ALLEGRO_KEY_PAD_7: return VanGuiKey_Keypad7;
        case ALLEGRO_KEY_PAD_8: return VanGuiKey_Keypad8;
        case ALLEGRO_KEY_PAD_9: return VanGuiKey_Keypad9;
        case ALLEGRO_KEY_PAD_DELETE: return VanGuiKey_KeypadDecimal;
        case ALLEGRO_KEY_PAD_SLASH: return VanGuiKey_KeypadDivide;
        case ALLEGRO_KEY_PAD_ASTERISK: return VanGuiKey_KeypadMultiply;
        case ALLEGRO_KEY_PAD_MINUS: return VanGuiKey_KeypadSubtract;
        case ALLEGRO_KEY_PAD_PLUS: return VanGuiKey_KeypadAdd;
        case ALLEGRO_KEY_PAD_ENTER: return VanGuiKey_KeypadEnter;
        case ALLEGRO_KEY_PAD_EQUALS: return VanGuiKey_KeypadEqual;
        case ALLEGRO_KEY_LCTRL: return VanGuiKey_LeftCtrl;
        case ALLEGRO_KEY_LSHIFT: return VanGuiKey_LeftShift;
        case ALLEGRO_KEY_ALT: return VanGuiKey_LeftAlt;
        case ALLEGRO_KEY_LWIN: return VanGuiKey_LeftSuper;
        case ALLEGRO_KEY_RCTRL: return VanGuiKey_RightCtrl;
        case ALLEGRO_KEY_RSHIFT: return VanGuiKey_RightShift;
        case ALLEGRO_KEY_ALTGR: return VanGuiKey_RightAlt;
        case ALLEGRO_KEY_RWIN: return VanGuiKey_RightSuper;
        case ALLEGRO_KEY_MENU: return VanGuiKey_Menu;
        case ALLEGRO_KEY_0: return VanGuiKey_0;
        case ALLEGRO_KEY_1: return VanGuiKey_1;
        case ALLEGRO_KEY_2: return VanGuiKey_2;
        case ALLEGRO_KEY_3: return VanGuiKey_3;
        case ALLEGRO_KEY_4: return VanGuiKey_4;
        case ALLEGRO_KEY_5: return VanGuiKey_5;
        case ALLEGRO_KEY_6: return VanGuiKey_6;
        case ALLEGRO_KEY_7: return VanGuiKey_7;
        case ALLEGRO_KEY_8: return VanGuiKey_8;
        case ALLEGRO_KEY_9: return VanGuiKey_9;
        case ALLEGRO_KEY_A: return VanGuiKey_A;
        case ALLEGRO_KEY_B: return VanGuiKey_B;
        case ALLEGRO_KEY_C: return VanGuiKey_C;
        case ALLEGRO_KEY_D: return VanGuiKey_D;
        case ALLEGRO_KEY_E: return VanGuiKey_E;
        case ALLEGRO_KEY_F: return VanGuiKey_F;
        case ALLEGRO_KEY_G: return VanGuiKey_G;
        case ALLEGRO_KEY_H: return VanGuiKey_H;
        case ALLEGRO_KEY_I: return VanGuiKey_I;
        case ALLEGRO_KEY_J: return VanGuiKey_J;
        case ALLEGRO_KEY_K: return VanGuiKey_K;
        case ALLEGRO_KEY_L: return VanGuiKey_L;
        case ALLEGRO_KEY_M: return VanGuiKey_M;
        case ALLEGRO_KEY_N: return VanGuiKey_N;
        case ALLEGRO_KEY_O: return VanGuiKey_O;
        case ALLEGRO_KEY_P: return VanGuiKey_P;
        case ALLEGRO_KEY_Q: return VanGuiKey_Q;
        case ALLEGRO_KEY_R: return VanGuiKey_R;
        case ALLEGRO_KEY_S: return VanGuiKey_S;
        case ALLEGRO_KEY_T: return VanGuiKey_T;
        case ALLEGRO_KEY_U: return VanGuiKey_U;
        case ALLEGRO_KEY_V: return VanGuiKey_V;
        case ALLEGRO_KEY_W: return VanGuiKey_W;
        case ALLEGRO_KEY_X: return VanGuiKey_X;
        case ALLEGRO_KEY_Y: return VanGuiKey_Y;
        case ALLEGRO_KEY_Z: return VanGuiKey_Z;
        case ALLEGRO_KEY_F1: return VanGuiKey_F1;
        case ALLEGRO_KEY_F2: return VanGuiKey_F2;
        case ALLEGRO_KEY_F3: return VanGuiKey_F3;
        case ALLEGRO_KEY_F4: return VanGuiKey_F4;
        case ALLEGRO_KEY_F5: return VanGuiKey_F5;
        case ALLEGRO_KEY_F6: return VanGuiKey_F6;
        case ALLEGRO_KEY_F7: return VanGuiKey_F7;
        case ALLEGRO_KEY_F8: return VanGuiKey_F8;
        case ALLEGRO_KEY_F9: return VanGuiKey_F9;
        case ALLEGRO_KEY_F10: return VanGuiKey_F10;
        case ALLEGRO_KEY_F11: return VanGuiKey_F11;
        case ALLEGRO_KEY_F12: return VanGuiKey_F12;
        default: return VanGuiKey_None;
    }
}

bool VanGui_ImplAllegro5_Init(ALLEGRO_DISPLAY* display)
{
    VanGuiIO& io = VanGui::GetIO();
    VANGUI_CHECKVERSION();
    VAN_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    // Setup backend capabilities flags
    VanGui_ImplAllegro5_Data* bd = VAN_NEW(VanGui_ImplAllegro5_Data)();
    io.BackendPlatformUserData = static_cast<void*>(bd);
    io.BackendPlatformName = io.BackendRendererName = "vangui_impl_allegro5";
    io.BackendFlags |= VanGuiBackendFlags_HasMouseCursors;       // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= VanGuiBackendFlags_RendererHasTextures;   // We can honor VanGuiPlatformIO::Textures[] requests during render.

    bd->LastCursor = ALLEGRO_SYSTEM_MOUSE_CURSOR_NONE;

    VanGui_ImplAllegro5_SetDisplay(display);

    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();
#if ALLEGRO_HAS_CLIPBOARD
    platform_io.Platform_SetClipboardTextFn = VanGui_ImplAllegro5_SetClipboardText;
    platform_io.Platform_GetClipboardTextFn = VanGui_ImplAllegro5_GetClipboardText;
#endif
    platform_io.DrawCallback_ResetRenderState = VanGui_ImplAllegro5_DrawCallback_ResetRenderState;

    return true;
}

void VanGui_ImplAllegro5_Shutdown()
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    VAN_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    VanGuiIO& io = VanGui::GetIO();
    VanGuiPlatformIO& platform_io = VanGui::GetPlatformIO();

    VanGui_ImplAllegro5_InvalidateDeviceObjects();
    if (bd->VertexDecl)
        al_destroy_vertex_decl(bd->VertexDecl);
    if (bd->ClipboardTextData)
        al_free(bd->ClipboardTextData);

    io.BackendPlatformName = io.BackendRendererName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(VanGuiBackendFlags_HasMouseCursors | VanGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    platform_io.ClearPlatformHandlers();
    VAN_DELETE(bd);
}

void VanGui_ImplAllegro5_SetDisplay(ALLEGRO_DISPLAY* display)
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    bd->Display = display;

    if (bd->VertexDecl)
    {
        al_destroy_vertex_decl(bd->VertexDecl);
        bd->VertexDecl = nullptr;
    }

    if (bd->Display && !bd->VertexDecl)
    {
        // Create custom vertex declaration.
        // Unfortunately Allegro doesn't support 32-bits packed colors so we have to convert them to 4 floats.
        // We still use a custom declaration to use 'ALLEGRO_PRIM_TEX_COORD' instead of 'ALLEGRO_PRIM_TEX_COORD_PIXEL' else we can't do a reliable conversion.
        ALLEGRO_VERTEX_ELEMENT elems[] =
        {
            { ALLEGRO_PRIM_POSITION, ALLEGRO_PRIM_FLOAT_2, offsetof(VanDrawVertAllegro, pos) },
            { ALLEGRO_PRIM_TEX_COORD, ALLEGRO_PRIM_FLOAT_2, offsetof(VanDrawVertAllegro, uv) },
            { ALLEGRO_PRIM_COLOR_ATTR, 0, offsetof(VanDrawVertAllegro, col) },
            { 0, 0, 0 }
        };
        bd->VertexDecl = al_create_vertex_decl(elems, sizeof(VanDrawVertAllegro));
    }
}

// ev->keyboard.modifiers seems always zero so using that...
static void VanGui_ImplAllegro5_UpdateKeyModifiers()
{
    VanGuiIO& io = VanGui::GetIO();
    ALLEGRO_KEYBOARD_STATE keys;
    al_get_keyboard_state(&keys);
    io.AddKeyEvent(VanGuiMod_Ctrl, al_key_down(&keys, ALLEGRO_KEY_LCTRL) || al_key_down(&keys, ALLEGRO_KEY_RCTRL));
    io.AddKeyEvent(VanGuiMod_Shift, al_key_down(&keys, ALLEGRO_KEY_LSHIFT) || al_key_down(&keys, ALLEGRO_KEY_RSHIFT));
    io.AddKeyEvent(VanGuiMod_Alt, al_key_down(&keys, ALLEGRO_KEY_ALT) || al_key_down(&keys, ALLEGRO_KEY_ALTGR));
    io.AddKeyEvent(VanGuiMod_Super, al_key_down(&keys, ALLEGRO_KEY_LWIN) || al_key_down(&keys, ALLEGRO_KEY_RWIN));
}

// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if VanGUI wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to VanGUI, and hide them from your application based on those two flags.
bool VanGui_ImplAllegro5_ProcessEvent(ALLEGRO_EVENT* ev)
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplAllegro5_Init()?");
    VanGuiIO& io = VanGui::GetIO();

    switch (ev->type)
    {
    case ALLEGRO_EVENT_MOUSE_AXES:
        if (ev->mouse.display == bd->Display)
        {
            io.AddMousePosEvent(ev->mouse.x, ev->mouse.y);
            io.AddMouseWheelEvent(-ev->mouse.dw, ev->mouse.dz);
        }
        return true;
    case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
    case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
        if (ev->mouse.display == bd->Display && ev->mouse.button > 0 && ev->mouse.button <= 5)
            io.AddMouseButtonEvent(ev->mouse.button - 1, ev->type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN);
        return true;
    case ALLEGRO_EVENT_TOUCH_MOVE:
        if (ev->touch.display == bd->Display)
            io.AddMousePosEvent(ev->touch.x, ev->touch.y);
        return true;
    case ALLEGRO_EVENT_TOUCH_BEGIN:
    case ALLEGRO_EVENT_TOUCH_END:
    case ALLEGRO_EVENT_TOUCH_CANCEL:
        if (ev->touch.display == bd->Display && ev->touch.primary)
            io.AddMouseButtonEvent(0, ev->type == ALLEGRO_EVENT_TOUCH_BEGIN);
        return true;
    case ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY:
        if (ev->mouse.display == bd->Display)
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return true;
    case ALLEGRO_EVENT_KEY_CHAR:
        if (ev->keyboard.display == bd->Display)
            if (ev->keyboard.unichar != 0)
                io.AddInputCharacter(static_cast<unsigned int>(ev->keyboard.unichar));
        return true;
    case ALLEGRO_EVENT_KEY_DOWN:
    case ALLEGRO_EVENT_KEY_UP:
        if (ev->keyboard.display == bd->Display)
        {
            VanGui_ImplAllegro5_UpdateKeyModifiers();
            VanGuiKey key = VanGui_ImplAllegro5_KeyCodeToVanGuiKey(ev->keyboard.keycode);
            io.AddKeyEvent(key, (ev->type == ALLEGRO_EVENT_KEY_DOWN));
            io.SetKeyEventNativeData(key, ev->keyboard.keycode, -1); // To support legacy indexing (<1.87 user code)
        }
        return true;
    case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
        if (ev->display.source == bd->Display)
            io.AddFocusEvent(false);
        return true;
    case ALLEGRO_EVENT_DISPLAY_SWITCH_IN:
        if (ev->display.source == bd->Display)
        {
            io.AddFocusEvent(true);
#if defined(ALLEGRO_UNSTABLE)
            al_clear_keyboard_state(bd->Display);
#endif
        }
        return true;
    }
    return false;
}

static void VanGui_ImplAllegro5_UpdateMouseCursor()
{
    VanGuiIO& io = VanGui::GetIO();
    if (io.ConfigFlags & VanGuiConfigFlags_NoMouseCursorChange)
        return;

    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    VanGuiMouseCursor vangui_cursor = VanGui::GetMouseCursor();

    // Hide OS mouse cursor if vangui is drawing it
    if (io.MouseDrawCursor)
        vangui_cursor = VanGuiMouseCursor_None;

    if (bd->LastCursor == vangui_cursor)
        return;
    bd->LastCursor = vangui_cursor;
    if (vangui_cursor == VanGuiMouseCursor_None)
    {
        al_set_mouse_cursor(bd->Display, bd->MouseCursorInvisible);
    }
    else
    {
        ALLEGRO_SYSTEM_MOUSE_CURSOR cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_DEFAULT;
        switch (vangui_cursor)
        {
        case VanGuiMouseCursor_TextInput:    cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_EDIT; break;
        case VanGuiMouseCursor_ResizeAll:    cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_MOVE; break;
        case VanGuiMouseCursor_ResizeNS:     cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_N; break;
        case VanGuiMouseCursor_ResizeEW:     cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_E; break;
        case VanGuiMouseCursor_ResizeNESW:   cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_NE; break;
        case VanGuiMouseCursor_ResizeNWSE:   cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_NW; break;
        case VanGuiMouseCursor_Wait:         cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_BUSY; break;
        case VanGuiMouseCursor_Progress:     cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_PROGRESS; break;
        case VanGuiMouseCursor_NotAllowed:   cursor_id = ALLEGRO_SYSTEM_MOUSE_CURSOR_UNAVAILABLE; break;
        }
        al_set_system_mouse_cursor(bd->Display, cursor_id);
    }
}

void VanGui_ImplAllegro5_NewFrame()
{
    VanGui_ImplAllegro5_Data* bd = VanGui_ImplAllegro5_GetBackendData();
    VAN_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call VanGui_ImplAllegro5_Init()?");

    if (!bd->MouseCursorInvisible)
        VanGui_ImplAllegro5_CreateDeviceObjects();

    // Setup display size (every frame to accommodate for window resizing)
    VanGuiIO& io = VanGui::GetIO();
    int w, h;
    w = al_get_display_width(bd->Display);
    h = al_get_display_height(bd->Display);
    io.DisplaySize = VanVec2(static_cast<float>(w), static_cast<float>(h));

    // Setup time step
    double current_time = al_get_time();
    io.DeltaTime = bd->Time > 0.0 ? static_cast<float>(current_time - bd->Time) : static_cast<float>(1.0f / 60.0f);
    bd->Time = current_time;

    // Allegro 5 doesn't receive PrintScreen under Windows
#ifdef _WIN32
    io.AddKeyEvent(VanGuiKey_PrintScreen, (::GetAsyncKeyState(VK_SNAPSHOT) & 0x8000) != 0);
#endif

    // Setup mouse cursor shape
    VanGui_ImplAllegro5_UpdateMouseCursor();
}

//-----------------------------------------------------------------------------

#endif // #ifndef VANGUI_DISABLE
