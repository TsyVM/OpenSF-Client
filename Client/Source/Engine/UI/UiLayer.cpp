#include "Engine/UI/UiLayer.hpp"

#include "Engine/Core/Log.hpp"

#include <vangui/vangui.h>
#include <vangui/misc/vangui_enhance.h>
#ifdef _WIN32
#include <vangui_impl_win32.h>

#include <windows.h>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
extern VANGUI_IMPL_API LRESULT VanGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace eng {

namespace {

// The system's fonts: Windows' own, or Android's.
std::string font_dir() {
#ifdef _WIN32
    char windir[MAX_PATH];
    UINT n = GetWindowsDirectoryA(windir, MAX_PATH);
    return std::string(windir, n) + "\\Fonts\\";
#else
    return "/system/fonts/";
#endif
}

VanFont* load_font(const char* file, float size) {
    const std::string path = font_dir() + file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nullptr;
    VanGuiIO& io = VanGui::GetIO();
    return io.Fonts->AddFontFromFileTTF(path.c_str(), size);
}

#if !defined(_WIN32) && !defined(__ANDROID__)
// Linux and macOS: every font file in the system's font folders, by file name (found once).
const std::map<std::string, std::string>& desktop_fonts() {
    static const std::map<std::string, std::string> found = [] {
        std::map<std::string, std::string> out;
        std::vector<std::filesystem::path> dirs;
        const char* home = std::getenv("HOME");
#if defined(__APPLE__)
        dirs = {"/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts"};
        if (home) dirs.push_back(std::filesystem::path(home) / "Library" / "Fonts");
#else
        dirs = {"/usr/share/fonts", "/usr/local/share/fonts"};
        if (home) dirs.push_back(std::filesystem::path(home) / ".local" / "share" / "fonts"), dirs.push_back(std::filesystem::path(home) / ".fonts");
#endif
        for (const auto& d : dirs) {
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(d, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it.depth() > 4) it.disable_recursion_pending();
                const std::string ext = it->path().extension().string();
                if (ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".TTF")
                    out.emplace(it->path().filename().string(), it->path().string());
            }
        }
        return out;
    }();
    return found;
}

// The first of these faces the system has.
VanFont* load_any(std::initializer_list<const char*> files, float size) {
    const auto& fonts = desktop_fonts();
    for (const char* f : files)
        if (auto it = fonts.find(f); it != fonts.end())
            if (VanFont* font = VanGui::GetIO().Fonts->AddFontFromFileTTF(it->second.c_str(), size)) return font;
    return nullptr;
}
#endif

}  // namespace

bool UiLayer::init(Window& window, Device& device, void (*apply_theme)(VanGuiStyle&)) {
    window_ = &window;
    device_ = &device;
    apply_theme_ = apply_theme;
    (void)VanGui::CreateContext();
    VanGuiIO& io = VanGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= VanGuiConfigFlags_NavEnableKeyboard;

#ifdef _WIN32
    fonts_.body = load_font("segoeui.ttf", 16.0f);
    fonts_.bold = load_font("seguisb.ttf", 16.0f);
    if (!fonts_.bold) fonts_.bold = load_font("segoeuib.ttf", 16.0f);
    fonts_.heading = load_font("bahnschrift.ttf", 16.0f);
    fonts_.mono = load_font("consola.ttf", 14.0f);
    fonts_.display = load_font("seguibl.ttf", 16.0f);
    if (!fonts_.display) fonts_.display = load_font("segoeuib.ttf", 16.0f);
    fonts_.page = load_font("tahoma.ttf", 16.0f);
    fonts_.page_bold = load_font("tahomabd.ttf", 16.0f);
    // The kit's plates were lettered in a rounded Korean system face; Malgun Gothic is its
    // successor and ships with every Windows since Vista.
    fonts_.kit = load_font("malgunbd.ttf", 16.0f);
#elif !defined(__ANDROID__)
    // Linux and macOS: the free faces most desktops carry (DejaVu, Noto, Liberation), then a Mac's.
    fonts_.body = load_any({"DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf", "Helvetica.ttc", "Arial.ttf"}, 16.0f);
    fonts_.bold = load_any({"DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "LiberationSans-Bold.ttf", "HelveticaNeue.ttc", "Arial Bold.ttf"}, 16.0f);
    fonts_.heading = load_any({"DejaVuSansCondensed-Bold.ttf", "NotoSans-CondensedBold.ttf", "LiberationSansNarrow-Bold.ttf", "Avenir Next Condensed.ttc",
                               "Arial Narrow Bold.ttf"}, 16.0f);
    fonts_.mono = load_any({"DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf", "LiberationMono-Regular.ttf", "Menlo.ttc"}, 14.0f);
    fonts_.display = load_any({"DejaVuSans-Bold.ttf", "NotoSans-Black.ttf", "Arial Black.ttf"}, 16.0f);
    fonts_.page = load_any({"Tahoma.ttf", "tahoma.ttf", "DejaVuSans.ttf", "NotoSans-Regular.ttf", "Verdana.ttf"}, 16.0f);
    fonts_.page_bold = load_any({"Tahoma Bold.ttf", "tahomabd.ttf", "DejaVuSans-Bold.ttf", "NotoSans-Bold.ttf", "Verdana Bold.ttf"}, 16.0f);
#else
    fonts_.body = load_font("Roboto-Regular.ttf", 16.0f);
    fonts_.bold = load_font("Roboto-Medium.ttf", 16.0f);
    fonts_.heading = load_font("RobotoCondensed-Bold.ttf", 16.0f);
    fonts_.mono = load_font("DroidSansMono.ttf", 14.0f);
    fonts_.display = load_font("Roboto-Black.ttf", 16.0f);
    if (!fonts_.display) fonts_.display = load_font("Roboto-Bold.ttf", 16.0f);
#endif
    if (!fonts_.body) fonts_.body = io.Fonts->AddFontDefault();
    if (!fonts_.bold) fonts_.bold = fonts_.body;
    if (!fonts_.heading) fonts_.heading = fonts_.bold;
    if (!fonts_.mono) fonts_.mono = fonts_.body;
    if (!fonts_.display) fonts_.display = fonts_.bold;
    if (!fonts_.page) fonts_.page = fonts_.body;
    if (!fonts_.page_bold) fonts_.page_bold = fonts_.bold;
    if (!fonts_.kit) fonts_.kit = fonts_.page_bold;
    io.FontDefault = fonts_.body;

#ifdef _WIN32
    if (!VanGui_ImplWin32_Init(window.hwnd())) return false;
    window.message_hook = [](void* hwnd, unsigned msg, u64 wp, i64 lp) {
        return VanGui_ImplWin32_WndProcHandler(HWND(hwnd), msg, WPARAM(wp), LPARAM(lp)) != 0;
    };
#else
    if (!window.ui_init()) return false;
#endif
    if (!device.ui_init()) return false;
    ready_ = true;
    return true;
}

void UiLayer::shutdown() {
    if (!ready_) return;
    device_->ui_shutdown();
#ifdef _WIN32
    if (window_) window_->message_hook = nullptr;
    VanGui_ImplWin32_Shutdown();
#else
    window_->ui_shutdown();
#endif
    VanGui::DestroyContext();
    ready_ = false;
}

void UiLayer::begin_frame(float requested) {
    float scale = std::clamp(requested, 0.5f, 4.0f);
    scale = std::round(scale * 40.0f) / 40.0f;
    if (std::fabs(scale - scale_) > 0.001f) {
        scale_ = scale;
        VanGuiStyle& style = VanGui::GetStyle();
        style = VanGuiStyle();
        if (apply_theme_) apply_theme_(style);
        style.ScaleAllSizes(scale_);
        style.FontScaleMain = scale_;
    }
    device_->ui_new_frame();
#ifdef _WIN32
    VanGui_ImplWin32_NewFrame();
    // The backend reads the real pointer whenever its window is in front. A test's window can end
    // up there (a click on its title bar): what the test points at stays the test's.
    if (window_->background()) {
        if (test_x_ < -1.0e37f) VanGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        else VanGui::GetIO().AddMousePosEvent(test_x_, test_y_);
    }
#else
    window_->ui_new_frame();
#endif
    VanGui::NewFrame();
    VanGui::NewFrameExtras();
}

void UiLayer::end_frame() {
    // Toasts and the suite's other overlays, over everything drawn this frame. They stack from
    // the main viewport's work rect, which NewFrame rebuilds, so narrowing it here only moves them.
    if (overlay_top_ > 0) {
        VanGuiViewport* vp = VanGui::GetMainViewport();
        const float inset = std::min(overlay_top_, vp->WorkSize.y * 0.5f);
        vp->WorkPos.y += inset;
        vp->WorkSize.y -= inset;
    }
    VanGui::RenderExtras();
    VanGui::Render();
    device_->ui_render();
}

bool UiLayer::wants_keyboard() const { return ready_ && VanGui::GetIO().WantTextInput; }
bool UiLayer::wants_mouse() const { return ready_ && VanGui::GetIO().WantCaptureMouse; }

}  // namespace eng
