#include "Engine/Core/CrashHandler.hpp"

#include "Engine/Core/Log.hpp"

#ifndef _WIN32
// Android (and other POSIX systems): the fatal signals are caught, the stack is walked with the
// unwinder, and each frame is logged as library + offset (ndk-stack or llvm-symbolizer against
// the unstripped libshar.so names them). Then the signal goes on to the system.
#include <dlfcn.h>
#include <signal.h>
#include <unwind.h>

#include <cstdio>
#include <cstring>

namespace eng {

namespace {

struct Walk {
    uintptr_t pc[48];
    int count = 0;
};

_Unwind_Reason_Code on_frame(_Unwind_Context* ctx, void* arg) {
    auto* w = static_cast<Walk*>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc && w->count < 48) w->pc[w->count++] = pc;
    return w->count < 48 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

struct sigaction g_previous[32];

void on_signal(int sig, siginfo_t* info, void* context) {
    LOG_ERROR("CRASH: signal %d at address %p", sig, info ? info->si_addr : nullptr);
    Walk w;
    _Unwind_Backtrace(on_frame, &w);
    for (int i = 0; i < w.count; ++i) {
        Dl_info dl{};
        if (dladdr(reinterpret_cast<void*>(w.pc[i]), &dl) && dl.dli_fname) {
            const char* lib = std::strrchr(dl.dli_fname, '/');
            LOG_ERROR("  #%02d %s+0x%zx %s", i, lib ? lib + 1 : dl.dli_fname, size_t(w.pc[i] - uintptr_t(dl.dli_fbase)),
                      dl.dli_sname ? dl.dli_sname : "");
        } else {
            LOG_ERROR("  #%02d %p", i, reinterpret_cast<void*>(w.pc[i]));
        }
    }
    // The system's own handler next (the tombstone, the "app has stopped" dialog).
    sigaction(sig, &g_previous[sig], nullptr);
    if (g_previous[sig].sa_flags & SA_SIGINFO) {
        if (g_previous[sig].sa_sigaction) g_previous[sig].sa_sigaction(sig, info, context);
    } else if (g_previous[sig].sa_handler != SIG_DFL && g_previous[sig].sa_handler != SIG_IGN) {
        g_previous[sig].sa_handler(sig);
    }
    raise(sig);
}

}  // namespace

void install_crash_handler() {
    struct sigaction sa{};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) sigaction(sig, &sa, &g_previous[sig]);
}

std::string build_id() { return std::string(__DATE__) + " " + __TIME__; }

}  // namespace eng

#else
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#pragma comment(lib, "dbghelp.lib")

namespace eng {

namespace {

// The CodeView record the linker leaves in the exe: the PDB's GUID and age, which name the one
// PDB that fits this build.
struct CvInfo {
    DWORD signature;   // 'RSDS'
    GUID guid;
    DWORD age;
    char path[1];
};

const CvInfo* codeview(HMODULE module) {
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    const auto* dbg = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    for (DWORD i = 0; dir.VirtualAddress && i < dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i)
        if (dbg[i].Type == IMAGE_DEBUG_TYPE_CODEVIEW) {
            const auto* cv = reinterpret_cast<const CvInfo*>(base + dbg[i].AddressOfRawData);
            if (cv->signature == 'SDSR') return cv;
        }
    return nullptr;
}

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    LOG_ERROR("CRASH: exception 0x%08lx at %p (build %s)", code, info->ExceptionRecord->ExceptionAddress, build_id().c_str());
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        LOG_ERROR("  %s address %p", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                  reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 512];
    for (int i = 0; i < 32; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
        const DWORD64 pc = frame.AddrPC.Offset;
        if (!pc) break;
        // Where, as module + offset: a player's log names the code even with no PDB beside the
        // exe (Source/Tools/symbolize.py reads it against the PDB kept for the build).
        char module[MAX_PATH] = "?";
        DWORD64 offset = pc;
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(pc), &mod)) {
            char full[MAX_PATH]{};
            GetModuleFileNameA(mod, full, MAX_PATH);
            const char* slash = std::strrchr(full, '\\');
            std::snprintf(module, sizeof(module), "%s", slash ? slash + 1 : full);
            offset = pc - reinterpret_cast<DWORD64>(mod);
        }
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ldisp = 0;
        const bool has_sym = SymFromAddr(process, pc, &disp, sym);
        const bool has_line = SymGetLineFromAddr64(process, pc, &ldisp, &line);
        LOG_ERROR("  #%02d %s+0x%llx  %s%s%s:%lu", i, module, (unsigned long long)offset, has_sym ? sym->Name : "?", has_line ? "  " : "",
                  has_line ? line.FileName : "", has_line ? line.LineNumber : 0);
    }
    log::shutdown();
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install_crash_handler() { SetUnhandledExceptionFilter(on_crash); }

std::string build_id() {
    HMODULE self = GetModuleHandleW(nullptr);
    const auto* base = reinterpret_cast<const unsigned char*>(self);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const time_t linked = time_t(nt->FileHeader.TimeDateStamp);
    tm t{};
    gmtime_s(&t, &linked);
    char when[32];
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M UTC", &t);
    char pdb[64] = "none";
    if (const CvInfo* cv = codeview(self)) {
        const GUID& g = cv->guid;
        std::snprintf(pdb, sizeof(pdb), "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1],
                      g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], cv->age);
    }
    return std::string(when) + ", pdb " + pdb;
}

}  // namespace eng
#endif
