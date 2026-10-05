#include "Engine/Platform/Gamepad.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <xinput.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#endif

namespace eng {

namespace {

#ifdef _WIN32
float stick(SHORT v, SHORT dead) {
    const float f = float(v);
    const float d = float(dead);
    if (std::fabs(f) < d) return 0;
    const float s = (std::fabs(f) - d) / (32767.0f - d);
    return f < 0 ? -std::min(s, 1.0f) : std::min(s, 1.0f);
}
#endif

// A Sony stick byte (0..255, 128 the middle) in -1..1 with its dead zone taken out.
float sony_axis(u8 v, bool flip) {
    float f = (float(v) - 127.5f) / 127.5f;
    if (flip) f = -f;
    const float dead = 0.18f;
    if (std::fabs(f) < dead) return 0;
    const float s = std::min(1.0f, (std::fabs(f) - dead) / (1 - dead));
    return f < 0 ? -s : s;
}

struct PadState {
    u32 buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
};

bool busy(const PadState& s) {
    return s.buttons != 0 || std::fabs(s.lx) > 0.3f || std::fabs(s.ly) > 0.3f || std::fabs(s.rx) > 0.3f || std::fabs(s.ry) > 0.3f ||
           s.lt > 0.3f || s.rt > 0.3f;
}

// The Sony pads' buttons in the Xbox layout: `faces` holds the d-pad's hat (low nibble: 0 up,
// clockwise to 7, 8 none) and Square/Cross/Circle/Triangle (0x10..0x80); `shoulders` L1 R1 L2
// R2 Share Options L3 R3; `extra` PS and the touch pad's click.
u32 sony_buttons(u8 faces, u8 shoulders, u8 extra) {
    u32 b = 0;
    static const u32 hat[9] = {kPadUp, kPadUp | kPadRight, kPadRight, kPadDown | kPadRight, kPadDown, kPadDown | kPadLeft, kPadLeft, kPadUp | kPadLeft, 0};
    b |= hat[std::min<int>(faces & 0x0F, 8)];
    if (faces & 0x10) b |= kPadX;          // Square
    if (faces & 0x20) b |= kPadA;          // Cross
    if (faces & 0x40) b |= kPadB;          // Circle
    if (faces & 0x80) b |= kPadY;          // Triangle
    if (shoulders & 0x01) b |= kPadLB;
    if (shoulders & 0x02) b |= kPadRB;
    if (shoulders & 0x10) b |= kPadBack;   // Share / Create
    if (shoulders & 0x20) b |= kPadStart;  // Options
    if (shoulders & 0x40) b |= kPadLThumb;
    if (shoulders & 0x80) b |= kPadRThumb;
    if (extra & 0x02) b |= kPadBack;       // the touch pad pressed
    return b;
}

// One input report of a DualShock 4 or DualSense into a state; false when it is not one of theirs.
//   DS4, USB (0x01, 64 bytes) and either pad's Bluetooth "simple" report (0x01, 10 bytes):
//       1 LX, 2 LY, 3 RX, 4 RY, 5 hat+faces, 6 shoulders, 7 PS/pad, 8 L2, 9 R2
//   DS4, Bluetooth full (0x11): the same two bytes further on
//   DualSense, USB (0x01, 64 bytes): 1 LX, 2 LY, 3 RX, 4 RY, 5 L2, 6 R2, 7 count, 8 hat+faces, 9 shoulders, 10 PS/pad
//   DualSense, Bluetooth full (0x31): the same one byte further on
bool parse_sony(const u8* r, size_t n, bool dualsense, PadState& s) {
    if (n < 10) return false;
    auto ds4 = [&](size_t o) {
        if (n < o + 9) return false;
        s.lx = sony_axis(r[o + 0], false);
        s.ly = sony_axis(r[o + 1], true);
        s.rx = sony_axis(r[o + 2], false);
        s.ry = sony_axis(r[o + 3], true);
        s.buttons = sony_buttons(r[o + 4], r[o + 5], r[o + 6]);
        s.lt = r[o + 7] / 255.0f;
        s.rt = r[o + 8] / 255.0f;
        return true;
    };
    auto dsense = [&](size_t o) {
        if (n < o + 10) return false;
        s.lx = sony_axis(r[o + 0], false);
        s.ly = sony_axis(r[o + 1], true);
        s.rx = sony_axis(r[o + 2], false);
        s.ry = sony_axis(r[o + 3], true);
        s.lt = r[o + 4] / 255.0f;
        s.rt = r[o + 5] / 255.0f;
        s.buttons = sony_buttons(r[o + 7], r[o + 8], r[o + 9]);
        return true;
    };
    bool ok = false;
    if (r[0] == 0x01 && (n <= 16 || !dualsense)) ok = ds4(1);
    else if (r[0] == 0x01 && dualsense) ok = dsense(1);
    else if (r[0] == 0x11 && !dualsense) ok = ds4(3);
    else if (r[0] == 0x31 && dualsense) ok = dsense(2);
    if (!ok) return false;
    if (s.lt > 0.5f) s.buttons |= kPadLT;
    if (s.rt > 0.5f) s.buttons |= kPadRT;
    if (s.lt < 0.12f) s.lt = 0;
    if (s.rt < 0.12f) s.rt = 0;
    return true;
}

u32 crc32(const u8* d, size_t n, u32 crc = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return crc;
}

}  // namespace

// ── Sony's pads over HID ───────────────────────────────────────────────────────

#ifdef _WIN32
struct Gamepad::Sony {
    struct Device {
        std::wstring path;
        HANDLE file = INVALID_HANDLE_VALUE;
        PadKind kind = PadKind::DualShock4;
        bool bluetooth = false;
        size_t in_len = 64, out_len = 32;
        std::thread reader;
        std::atomic<bool> dead{false};
        std::mutex m;
        PadState state;
        u64 changed = 0;      // when its input last changed (ms)
        u8 seq = 0;
    };
    std::vector<std::unique_ptr<Device>> devices;
    std::mutex list_m;
    std::thread scanner;
    std::atomic<bool> stop{false};
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    Sony() { scanner = std::thread([this] { scan_loop(); }); }
    ~Sony() {
        stop = true;
        SetEvent(wake);
        if (scanner.joinable()) scanner.join();
        std::lock_guard lock(list_m);
        for (auto& d : devices) close(*d);
        CloseHandle(wake);
    }

    static u64 now_ms() {
        return u64(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void close(Device& d) {
        d.dead = true;
        if (d.file != INVALID_HANDLE_VALUE) CancelIoEx(d.file, nullptr);
        if (d.reader.joinable()) d.reader.join();
        if (d.file != INVALID_HANDLE_VALUE) CloseHandle(d.file);
        d.file = INVALID_HANDLE_VALUE;
    }

    // Every couple of seconds: pads plugged in or paired since, opened; the ones gone, let go.
    void scan_loop() {
        while (!stop) {
            scan();
            WaitForSingleObject(wake, 2000);
        }
    }

    void scan() {
        {
            std::lock_guard lock(list_m);
            for (auto it = devices.begin(); it != devices.end();) {
                if ((*it)->dead) {
                    LOG_INFO("Pad: %s let go", (*it)->kind == PadKind::DualSense ? "DualSense" : "DualShock 4");
                    close(**it);
                    it = devices.erase(it);
                } else {
                    ++it;
                }
            }
        }
        GUID hid;
        HidD_GetHidGuid(&hid);
        HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return;
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        for (DWORD i = 0; !stop && SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &iface); ++i) {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &need, nullptr);
            if (need == 0) continue;
            std::vector<u8> buf(need);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, need, nullptr, nullptr)) continue;
            const std::wstring path = detail->DevicePath;
            {
                std::lock_guard lock(list_m);
                if (std::any_of(devices.begin(), devices.end(), [&](const auto& d) { return d->path == path; })) continue;
            }
            open(path);
        }
        SetupDiDestroyDeviceInfoList(set);
    }

    void open(const std::wstring& path) {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        HIDD_ATTRIBUTES a{};
        a.Size = sizeof(a);
        PadKind kind = PadKind::None;
        if (HidD_GetAttributes(f, &a) && a.VendorID == 0x054C) {
            if (a.ProductID == 0x05C4 || a.ProductID == 0x09CC || a.ProductID == 0x0BA0) kind = PadKind::DualShock4;
            else if (a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2) kind = PadKind::DualSense;
        }
        HIDP_CAPS caps{};
        PHIDP_PREPARSED_DATA pre = nullptr;
        if (kind != PadKind::None && HidD_GetPreparsedData(f, &pre)) {
            HidP_GetCaps(pre, &caps);
            HidD_FreePreparsedData(pre);
        }
        // Only the pad's game-pad collection (a DS4 also shows up as audio and other parts).
        if (kind == PadKind::None || caps.UsagePage != 0x01 || (caps.Usage != 0x05 && caps.Usage != 0x04) || caps.InputReportByteLength == 0) {
            CloseHandle(f);
            return;
        }
        auto d = std::make_unique<Device>();
        d->path = path;
        d->file = f;
        d->kind = kind;
        d->in_len = caps.InputReportByteLength;
        d->out_len = caps.OutputReportByteLength;
        d->bluetooth = caps.InputReportByteLength != 64;
        if (d->bluetooth && caps.FeatureReportByteLength > 0) {
            // By Bluetooth both pads send a cut-down report (no analog triggers) until something
            // reads their calibration: DS4 feature report 0x02, DualSense 0x05.
            std::vector<u8> feature(caps.FeatureReportByteLength, 0);
            feature[0] = kind == PadKind::DualSense ? 0x05 : 0x02;
            if (!HidD_GetFeature(f, feature.data(), ULONG(feature.size()))) LOG_WARN("Pad: the calibration report could not be read");
        }
        Device* dp = d.get();
        d->reader = std::thread([this, dp] { read_loop(*dp); });
        LOG_INFO("Pad: %s by %s", kind == PadKind::DualSense ? (a.ProductID == 0x0DF2 ? "DualSense Edge" : "DualSense") : "DualShock 4",
                 d->bluetooth ? "Bluetooth" : "USB");
        std::lock_guard lock(list_m);
        devices.push_back(std::move(d));
        // Soldier Front gold on the light bar, and the pad knows it is heard.
        send(*devices.back(), 0, 0);
    }

    void read_loop(Device& d) {
        std::vector<u8> buf(std::max<size_t>(d.in_len, 64));
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        while (!d.dead) {
            ResetEvent(ov.hEvent);
            DWORD got = 0;
            if (!ReadFile(d.file, buf.data(), DWORD(d.in_len), &got, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    d.dead = true;
                    break;
                }
                while (!d.dead && WaitForSingleObject(ov.hEvent, 250) == WAIT_TIMEOUT) {}
                if (d.dead) break;
                if (!GetOverlappedResult(d.file, &ov, &got, FALSE)) {
                    d.dead = true;   // unplugged, or out of range
                    break;
                }
            }
            PadState s;
            if (!parse_sony(buf.data(), got, d.kind == PadKind::DualSense, s)) continue;
            std::lock_guard lock(d.m);
            if (s.buttons != d.state.buttons || std::fabs(s.lx - d.state.lx) + std::fabs(s.ly - d.state.ly) + std::fabs(s.rx - d.state.rx) +
                                                        std::fabs(s.ry - d.state.ry) + std::fabs(s.lt - d.state.lt) + std::fabs(s.rt - d.state.rt) >
                                                    0.05f)
                d.changed = now_ms();
            d.state = s;
        }
        CancelIoEx(d.file, &ov);
        CloseHandle(ov.hEvent);
    }

    // Rumble (0..1 each motor) and the light bar, by the report the connection wants.
    void send(Device& d, float low, float high) {
        if (d.out_len == 0 && !d.bluetooth) return;
        const u8 lo = u8(std::clamp(low, 0.0f, 1.0f) * 255), hi = u8(std::clamp(high, 0.0f, 1.0f) * 255);
        const u8 R = 0xD6, G = 0xAC, B = 0x56;   // Soldier Front gold
        std::vector<u8> r;
        if (d.kind == PadKind::DualShock4 && !d.bluetooth) {
            r.assign(std::max<size_t>(d.out_len, 32), 0);
            r[0] = 0x05;
            r[1] = 0x07;   // rumble, light bar, flash
            r[4] = hi;
            r[5] = lo;
            r[6] = R;
            r[7] = G;
            r[8] = B;
        } else if (d.kind == PadKind::DualShock4) {
            r.assign(78, 0);
            r[0] = 0x11;
            r[1] = 0xC0;
            r[3] = 0x07;
            r[6] = hi;
            r[7] = lo;
            r[8] = R;
            r[9] = G;
            r[10] = B;
        } else {
            // DualSense: USB 0x02 (the report from byte 1); Bluetooth 0x31 (from byte 2, and a CRC).
            const size_t o = d.bluetooth ? 2 : 1;
            r.assign(d.bluetooth ? 78 : std::max<size_t>(d.out_len, 48), 0);
            r[0] = d.bluetooth ? 0x31 : 0x02;
            if (d.bluetooth) r[1] = u8((d.seq++ & 0x0F) << 4);
            r[o + 0] = 0x03;   // rumble (compatible mode)
            r[o + 1] = 0x04;   // light bar
            r[o + 2] = hi;
            r[o + 3] = lo;
            r[o + 38] = 0x02;  // light bar set-up: let the pad's own animation go
            r[o + 41] = 0x02;
            r[o + 44] = R;
            r[o + 45] = G;
            r[o + 46] = B;
        }
        if (d.bluetooth) {
            // Bluetooth reports close with a CRC-32 over 0xA2 and the report.
            const u8 head = 0xA2;
            u32 c = crc32(&head, 1);
            c = ~crc32(r.data(), r.size() - 4, c);
            std::memcpy(r.data() + r.size() - 4, &c, 4);
        }
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD wrote = 0;
        if (!WriteFile(d.file, r.data(), DWORD(r.size()), &wrote, &ov) && GetLastError() == ERROR_IO_PENDING)
            WaitForSingleObject(ov.hEvent, 50);
        CancelIoEx(d.file, &ov);
        CloseHandle(ov.hEvent);
    }

    // The pad used most lately: its state and kind.
    bool latest(PadState& out, PadKind& kind, u64& when) {
        std::lock_guard lock(list_m);
        Device* best = nullptr;
        for (auto& d : devices) {
            if (d->dead) continue;
            std::lock_guard dl(d->m);
            if (!best || d->changed > when) {
                best = d.get();
                when = d->changed;
                out = d->state;
                kind = d->kind;
            }
        }
        return best != nullptr;
    }

    void rumble_all(float low, float high) {
        std::lock_guard lock(list_m);
        for (auto& d : devices)
            if (!d->dead) send(*d, low, high);
    }
};

// ── The pad ────────────────────────────────────────────────────────────────────

void Gamepad::poll_platform() {
    // XInput: probing absent slots is slow, so a new pad is looked for only every so often.
    PadState xs;
    bool have_x = false;
    if (xinput_ < 0 && retry_-- <= 0) {
        retry_ = 60;
        for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE s{};
            if (XInputGetState(DWORD(i), &s) == ERROR_SUCCESS) {
                xinput_ = i;
                LOG_INFO("Pad: XInput pad %d", i);
                break;
            }
        }
    }
    if (xinput_ >= 0) {
        XINPUT_STATE s{};
        if (XInputGetState(DWORD(xinput_), &s) != ERROR_SUCCESS) {
            xinput_ = -1;
        } else {
            have_x = true;
            xs.buttons = s.Gamepad.wButtons & 0xF3FF;
            xs.lx = stick(s.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            xs.ly = stick(s.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            xs.rx = stick(s.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            xs.ry = stick(s.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            xs.lt = s.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? s.Gamepad.bLeftTrigger / 255.0f : 0;
            xs.rt = s.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? s.Gamepad.bRightTrigger / 255.0f : 0;
            if (xs.lt > 0.5f) xs.buttons |= kPadLT;
            if (xs.rt > 0.5f) xs.buttons |= kPadRT;
        }
    }
    PadState ss;
    PadKind sk = PadKind::None;
    u64 when = 0;
    const bool have_s = sony_ && sony_->latest(ss, sk, when);

    // Which one is read: the one in use now; else the one read last, while it is there; else any.
    const bool x_busy = have_x && busy(xs), s_busy = have_s && busy(ss);
    PadKind use = PadKind::None;
    if (s_busy) use = sk;
    else if (x_busy) use = PadKind::Xbox;
    else if (kind_ == PadKind::Xbox && have_x) use = PadKind::Xbox;
    else if (kind_ != PadKind::None && kind_ != PadKind::Xbox && have_s) use = sk;
    else if (have_s) use = sk;
    else if (have_x) use = PadKind::Xbox;
    const PadState& s = use == PadKind::Xbox ? xs : ss;
    if (use != kind_ && use != PadKind::None)
        LOG_INFO("Pad: reading the %s", use == PadKind::Xbox ? "Xbox (XInput) pad" : use == PadKind::DualSense ? "DualSense" : "DualShock 4");
    kind_ = use;
    connected_ = use != PadKind::None;
    name_ = use == PadKind::Xbox ? "Xbox controller" : use == PadKind::DualSense ? "DualSense" : use == PadKind::DualShock4 ? "DualShock 4" : "";
    if (!connected_) {
        buttons_ = 0;
        lx_ = ly_ = rx_ = ry_ = lt_ = rt_ = 0;
        active_ = false;
        return;
    }
    buttons_ = s.buttons;
    lx_ = s.lx;
    ly_ = s.ly;
    rx_ = s.rx;
    ry_ = s.ry;
    lt_ = s.lt;
    rt_ = s.rt;
    active_ = buttons_ != prev_ || busy(s);
}
#elif !defined(__ANDROID__)

// ── Linux and macOS: desktop_pad (the kernel's events here; GameController on a Mac) ──

#if !defined(__APPLE__)
}  // namespace eng

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace eng {

namespace {

struct Axis {
    int min = -32768, max = 32767;
};

struct EvPad {
    int fd = -1;
    std::string name;
    int vendor = 0;
    Axis axes[ABS_CNT];
    int raw[ABS_CNT] = {};
    u32 keys = 0;
    double next_scan = 0;
} g_ev;

bool has_bit(const unsigned long* bits, int n) { return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1; }

// The first event device with a gamepad's buttons (the kernel's gamepad layout: BTN_SOUTH is
// A / Cross whoever made the pad).
void scan() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return;
    while (dirent* e = readdir(dir)) {
        if (std::strncmp(e->d_name, "event", 5) != 0) continue;
        const std::string path = std::string("/dev/input/") + e->d_name;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long keys[(KEY_MAX + 1) / (8 * sizeof(long)) + 1] = {};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0 || !(has_bit(keys, BTN_SOUTH) || has_bit(keys, BTN_GAMEPAD))) {
            close(fd);
            continue;
        }
        char name[128] = {};
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        input_id id{};
        ioctl(fd, EVIOCGID, &id);
        g_ev.fd = fd;
        g_ev.name = name[0] ? name : "Controller";
        g_ev.vendor = id.vendor;
        for (int a = 0; a < ABS_CNT; ++a) {
            input_absinfo info{};
            if (ioctl(fd, EVIOCGABS(a), &info) == 0 && info.maximum > info.minimum) {
                g_ev.axes[a].min = info.minimum, g_ev.axes[a].max = info.maximum;
                g_ev.raw[a] = info.value;
            }
        }
        g_ev.keys = 0;
        LOG_INFO("Pad: %s (vendor %04x) at %s", g_ev.name.c_str(), unsigned(g_ev.vendor), path.c_str());
        break;
    }
    closedir(dir);
}

float centred(int a) {
    const Axis& x = g_ev.axes[a];
    const float mid = 0.5f * float(x.min + x.max), half = 0.5f * float(x.max - x.min);
    const float v = half > 0 ? (float(g_ev.raw[a]) - mid) / half : 0;
    const float dead = 0.15f, m = std::fabs(v);
    return m < dead ? 0 : std::copysign(std::min(1.0f, (m - dead) / (1 - dead)), v);
}

float trigger(int a) {
    const Axis& x = g_ev.axes[a];
    const float v = x.max > x.min ? (float(g_ev.raw[a]) - float(x.min)) / float(x.max - x.min) : 0;
    return v > 0.05f ? std::min(v, 1.0f) : 0;
}

u32 button_of(int code) {
    switch (code) {
        case BTN_SOUTH: return kPadA;
        case BTN_EAST: return kPadB;
        case BTN_NORTH: return kPadY;
        case BTN_WEST: return kPadX;
        case BTN_TL: return kPadLB;
        case BTN_TR: return kPadRB;
        case BTN_TL2: return kPadLT;
        case BTN_TR2: return kPadRT;
        case BTN_SELECT: return kPadBack;
        case BTN_START: return kPadStart;
        case BTN_THUMBL: return kPadLThumb;
        case BTN_THUMBR: return kPadRThumb;
        case BTN_DPAD_UP: return kPadUp;
        case BTN_DPAD_DOWN: return kPadDown;
        case BTN_DPAD_LEFT: return kPadLeft;
        case BTN_DPAD_RIGHT: return kPadRight;
        default: return 0;
    }
}

double seconds_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

namespace desktop_pad {

void poll(State& out) {
    if (g_ev.fd < 0) {
        out.connected = false;
        const double now = seconds_now();
        if (now < g_ev.next_scan) return;
        g_ev.next_scan = now + 2.0;   // a pad plugged in later is found within two seconds
        scan();
        if (g_ev.fd < 0) return;
    }
    input_event ev[64];
    for (;;) {
        const ssize_t n = read(g_ev.fd, ev, sizeof(ev));
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {   // unplugged
                close(g_ev.fd);
                g_ev.fd = -1;
                out.connected = false;
                LOG_INFO("Pad: %s went away", g_ev.name.c_str());
                return;
            }
            break;
        }
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
            const input_event& e = ev[i];
            if (e.type == EV_KEY) {
                if (const u32 b = button_of(e.code)) g_ev.keys = e.value ? (g_ev.keys | b) : (g_ev.keys & ~b);
            } else if (e.type == EV_ABS && e.code < ABS_CNT) {
                g_ev.raw[e.code] = e.value;
            }
        }
    }
    out.connected = true;
    out.name = g_ev.name;
    out.vendor = g_ev.vendor;
    // The kernel's y points down; the game's up.
    out.lx = centred(ABS_X), out.ly = -centred(ABS_Y);
    out.rx = centred(ABS_RX), out.ry = -centred(ABS_RY);
    out.lt = trigger(ABS_Z), out.rt = trigger(ABS_RZ);
    u32 b = g_ev.keys;
    if (g_ev.raw[ABS_HAT0X] < 0) b |= kPadLeft;
    if (g_ev.raw[ABS_HAT0X] > 0) b |= kPadRight;
    if (g_ev.raw[ABS_HAT0Y] < 0) b |= kPadUp;
    if (g_ev.raw[ABS_HAT0Y] > 0) b |= kPadDown;
    out.buttons = b;
}

}  // namespace desktop_pad
#endif

struct Gamepad::Sony {};

void Gamepad::poll_platform() {
    desktop_pad::State s;
    desktop_pad::poll(s);
    if (!s.connected) {
        connected_ = false;
        kind_ = PadKind::None;
        buttons_ = 0;
        lx_ = ly_ = rx_ = ry_ = lt_ = rt_ = 0;
        active_ = false;
        return;
    }
    u32 b = s.buttons;
    if (s.lt > 0.5f) b |= kPadLT;
    if (s.rt > 0.5f) b |= kPadRT;
    // Pads that send the triggers as buttons only.
    lt_ = (b & kPadLT) && s.lt == 0 ? 1.0f : s.lt;
    rt_ = (b & kPadRT) && s.rt == 0 ? 1.0f : s.rt;
    buttons_ = b;
    lx_ = s.lx, ly_ = s.ly, rx_ = s.rx, ry_ = s.ry;
    connected_ = true;
    kind_ = s.vendor == 0x054C ? PadKind::DualSense : PadKind::Xbox;
    name_ = s.name;
    active_ = buttons_ != prev_ || std::fabs(lx_) + std::fabs(ly_) + std::fabs(rx_) + std::fabs(ry_) + lt_ + rt_ > 0.2f;
}

#else

// ── Android: the system's pad events ───────────────────────────────────────────

namespace {

struct AndroidPad {
    u32 keys = 0;                // from key events
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    float hat_x = 0, hat_y = 0;
    bool seen = false;
    std::string name = "Controller";
    int vendor = 0;
} g_pad;

u32 key_button(int keycode) {
    switch (keycode) {
        case 96: return kPadA;          // BUTTON_A
        case 97: return kPadB;          // BUTTON_B
        case 99: return kPadX;          // BUTTON_X
        case 100: return kPadY;         // BUTTON_Y
        case 102: return kPadLB;        // BUTTON_L1
        case 103: return kPadRB;        // BUTTON_R1
        case 104: return kPadLT;        // BUTTON_L2
        case 105: return kPadRT;        // BUTTON_R2
        case 106: return kPadLThumb;    // BUTTON_THUMBL
        case 107: return kPadRThumb;    // BUTTON_THUMBR
        case 108: return kPadStart;     // BUTTON_START
        case 109: return kPadBack;      // BUTTON_SELECT
        case 19: return kPadUp;         // DPAD_UP
        case 20: return kPadDown;
        case 21: return kPadLeft;
        case 22: return kPadRight;
        default: return 0;
    }
}

float dead(float v, float zone) {
    const float a = std::fabs(v);
    if (a < zone) return 0;
    return std::copysign(std::min(1.0f, (a - zone) / (1.0f - zone)), v);
}

}  // namespace

namespace android_pad {

void key(int keycode, bool down) {
    const u32 b = key_button(keycode);
    if (!b) return;
    g_pad.seen = true;
    if (down) g_pad.keys |= b;
    else g_pad.keys &= ~b;
}

void axes(float lx, float ly, float rx, float ry, float lt, float rt, float hat_x, float hat_y) {
    g_pad.seen = true;
    // The system's y points down; the game's up.
    g_pad.lx = dead(lx, 0.15f);
    g_pad.ly = -dead(ly, 0.15f);
    g_pad.rx = dead(rx, 0.15f);
    g_pad.ry = -dead(ry, 0.15f);
    g_pad.lt = lt > 0.05f ? std::min(lt, 1.0f) : 0;
    g_pad.rt = rt > 0.05f ? std::min(rt, 1.0f) : 0;
    g_pad.hat_x = hat_x;
    g_pad.hat_y = hat_y;
}

void connected(const char* name, int vendor) {
    g_pad.name = name ? name : "Controller";
    g_pad.vendor = vendor;
}

}  // namespace android_pad

struct Gamepad::Sony {};

void Gamepad::poll_platform() {
    if (!g_pad.seen) {
        connected_ = false;
        kind_ = PadKind::None;
        buttons_ = 0;
        lx_ = ly_ = rx_ = ry_ = lt_ = rt_ = 0;
        active_ = false;
        return;
    }
    u32 b = g_pad.keys;
    if (g_pad.hat_x < -0.5f) b |= kPadLeft;
    if (g_pad.hat_x > 0.5f) b |= kPadRight;
    if (g_pad.hat_y < -0.5f) b |= kPadUp;
    if (g_pad.hat_y > 0.5f) b |= kPadDown;
    if (g_pad.lt > 0.5f) b |= kPadLT;
    if (g_pad.rt > 0.5f) b |= kPadRT;
    // Pads that send the triggers as buttons only.
    lt_ = (b & kPadLT) && g_pad.lt == 0 ? 1.0f : g_pad.lt;
    rt_ = (b & kPadRT) && g_pad.rt == 0 ? 1.0f : g_pad.rt;
    buttons_ = b;
    lx_ = g_pad.lx;
    ly_ = g_pad.ly;
    rx_ = g_pad.rx;
    ry_ = g_pad.ry;
    connected_ = true;
    // Sony's pads by their maker (USB vendor 0x054C); anything else reads as Xbox.
    kind_ = g_pad.vendor == 0x054C ? PadKind::DualSense : PadKind::Xbox;
    name_ = g_pad.name;
    active_ = buttons_ != prev_ || std::fabs(lx_) + std::fabs(ly_) + std::fabs(rx_) + std::fabs(ry_) + lt_ + rt_ > 0.2f;
}
#endif


Gamepad::Gamepad() : sony_(std::make_unique<Sony>()) {}
Gamepad::~Gamepad() = default;

void Gamepad::poll() {
    prev_ = buttons_;
    if (!fed_) {
        poll_platform();
        return;
    }
    connected_ = true;
    kind_ = feed_.kind;
    name_ = "Test controller";
    buttons_ = feed_.buttons;
    lx_ = feed_.lx, ly_ = feed_.ly, rx_ = feed_.rx, ry_ = feed_.ry;
    lt_ = feed_.lt, rt_ = feed_.rt;
    if (lt_ > 0.5f) buttons_ |= kPadLT;
    if (rt_ > 0.5f) buttons_ |= kPadRT;
    active_ = buttons_ != prev_ || std::fabs(lx_) + std::fabs(ly_) + std::fabs(rx_) + std::fabs(ry_) + lt_ + rt_ > 0.2f;
}

void Gamepad::feed(const Feed* state) {
    fed_ = state != nullptr;
    if (state) feed_ = *state;
}

u32 Gamepad::any_pressed() const {
    const u32 fresh = buttons_ & ~prev_;
    for (u32 b = 1; b <= kPadRT; b <<= 1)
        if (fresh & b) return b;
    return 0;
}

void Gamepad::rumble(float low, float high) {
    if (fed_) {
        rumble_low_ = low;
        rumble_high_ = high;
        return;
    }
#ifndef _WIN32
    rumble_low_ = low;
    rumble_high_ = high;
    return;
#else
    if (kind_ == PadKind::Xbox && xinput_ >= 0) {
        XINPUT_VIBRATION v{};
        v.wLeftMotorSpeed = WORD(std::clamp(low, 0.0f, 1.0f) * 65535);
        v.wRightMotorSpeed = WORD(std::clamp(high, 0.0f, 1.0f) * 65535);
        XInputSetState(DWORD(xinput_), &v);
    } else if (sony_ && (std::fabs(low - rumble_low_) > 0.01f || std::fabs(high - rumble_high_) > 0.01f)) {
        sony_->rumble_all(low, high);
    }
    rumble_low_ = low;
    rumble_high_ = high;
#endif
}

std::string Gamepad::button_key(u32 b) {
    switch (b) {
        case kPadA: return "A";
        case kPadB: return "B";
        case kPadX: return "X";
        case kPadY: return "Y";
        case kPadLB: return "LB";
        case kPadRB: return "RB";
        case kPadLT: return "LT";
        case kPadRT: return "RT";
        case kPadLThumb: return "LS";
        case kPadRThumb: return "RS";
        case kPadStart: return "START";
        case kPadBack: return "BACK";
        case kPadUp: return "DPAD_UP";
        case kPadDown: return "DPAD_DOWN";
        case kPadLeft: return "DPAD_LEFT";
        case kPadRight: return "DPAD_RIGHT";
        default: return "";
    }
}

u32 Gamepad::button_from_key(const std::string& key) {
    const std::string k = str::upper(key);
    for (u32 b = 1; b <= kPadRT; b <<= 1)
        if (!button_key(b).empty() && button_key(b) == k) return b;
    return 0;
}

std::string Gamepad::button_label(u32 b, PadKind kind) {
    const bool ps = kind == PadKind::DualShock4 || kind == PadKind::DualSense;
    switch (b) {
        case kPadA: return ps ? "Cross" : "A";
        case kPadB: return ps ? "Circle" : "B";
        case kPadX: return ps ? "Square" : "X";
        case kPadY: return ps ? "Triangle" : "Y";
        case kPadLB: return ps ? "L1" : "LB";
        case kPadRB: return ps ? "R1" : "RB";
        case kPadLT: return ps ? "L2" : "LT";
        case kPadRT: return ps ? "R2" : "RT";
        case kPadLThumb: return ps ? "L3" : "Left Stick";
        case kPadRThumb: return ps ? "R3" : "Right Stick";
        case kPadStart: return ps ? "Options" : "Menu";
        case kPadBack: return kind == PadKind::DualSense ? "Create" : ps ? "Share" : "View";
        case kPadUp: return "D-Pad Up";
        case kPadDown: return "D-Pad Down";
        case kPadLeft: return "D-Pad Left";
        case kPadRight: return "D-Pad Right";
        default: return "-";
    }
}

// ── Self-test: the report layouts, fed through the parser ───────────────────────

bool gamepad_self_test() {
    bool ok = true;
    auto check = [&](bool c, const char* what) {
        if (!c) LOG_ERROR("Pad self-test: %s", what);
        ok &= c;
    };
    PadState s;
    // DS4 by USB: left stick full left and up, Cross and R1 held, R2 fully in, d-pad right.
    u8 ds4[64]{0x01, 0x00, 0x00, 128, 128, u8(0x20 | 0x02), 0x02, 0x00, 0x00, 0xFF};
    check(parse_sony(ds4, 64, false, s), "DS4 USB report not read");
    check(s.lx < -0.9f && s.ly > 0.9f && s.rx == 0, "DS4 USB sticks");
    check((s.buttons & kPadA) && (s.buttons & kPadRB) && (s.buttons & kPadRight) && (s.buttons & kPadRT) && s.rt > 0.99f, "DS4 USB buttons");
    // DS4 by Bluetooth (0x11): Triangle and Options, the right stick down.
    u8 ds4bt[78]{0x11, 0xC0, 0x00, 128, 128, 128, 0xFF, u8(0x08 | 0x80), 0x20, 0x00, 0x00, 0x00};
    check(parse_sony(ds4bt, 78, false, s), "DS4 Bluetooth report not read");
    check((s.buttons & kPadY) && (s.buttons & kPadStart) && s.ry < -0.9f && !(s.buttons & (kPadUp | kPadDown)), "DS4 Bluetooth");
    // DualSense by USB: Circle, L2 half-way in, the d-pad up-left.
    u8 dsu[64]{0x01, 128, 128, 128, 128, 200, 0, 0x33, u8(0x07 | 0x40), 0x00, 0x02};
    check(parse_sony(dsu, 64, true, s), "DualSense USB report not read");
    check((s.buttons & kPadB) && (s.buttons & kPadUp) && (s.buttons & kPadLeft) && (s.buttons & kPadLT) && (s.buttons & kPadBack), "DualSense USB");
    // DualSense by Bluetooth, full (0x31): Square and L3.
    u8 dsb[78]{0x31, 0x10, 128, 128, 255, 128, 0, 0, 0, u8(0x08 | 0x10), 0x40, 0x00};
    check(parse_sony(dsb, 78, true, s), "DualSense Bluetooth report not read");
    check((s.buttons & kPadX) && (s.buttons & kPadLThumb) && s.rx > 0.9f, "DualSense Bluetooth");
    // Either pad's Bluetooth simple report (0x01, 10 bytes).
    u8 simple[10]{0x01, 128, 128, 128, 128, u8(0x08 | 0x20), 0x01, 0x00, 0x00, 0x00};
    check(parse_sony(simple, 10, true, s), "simple report not read");
    check((s.buttons & kPadA) && (s.buttons & kPadLB), "simple report");
    // The Bluetooth output reports' CRC-32: the standard check value.
    const u8 digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    check(~crc32(digits, sizeof(digits)) == 0xCBF43926u, "CRC-32");
    LOG_INFO("Pad self-test: %s", ok ? "all report layouts read right" : "FAILED");
    return ok;
}

}  // namespace eng
