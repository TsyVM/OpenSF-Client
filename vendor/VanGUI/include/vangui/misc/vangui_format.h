// vangui_format.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — human-readable numbers (header-only, no macro gate).
//
//   FormatBytes(buf, n, 1536000)         -> "1.5 MB"      (binary: "1.5 MiB")
//   FormatDuration(buf, n, 7512)         -> "2h 5m"       (parts: how many units)
//   FormatRelativeTime(buf, n, 180)      -> "3 min ago"   (negative = future: "in 3 min")
//
// Each writes into `buf` and returns it. The one-argument forms return a string
// from a small rotating pool (eight at a time), so they can go straight into a
// widget call:  VanGui::Text("%s", VanGui::FormatBytes(size));
// English wording; wrap the result in Tr() (vangui_i18n.h) to translate.
// -----------------------------------------------------------------------------

#pragma once

#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>

namespace VanGui {

inline const char* FormatBytes(char* buf, size_t size, double bytes, bool binary = false, int decimals = 1)
{
    static const char* const si[]  = { "B", "kB", "MB", "GB", "TB", "PB", "EB" };
    static const char* const iec[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    const double step = binary ? 1024.0 : 1000.0;
    const bool neg = bytes < 0.0;
    double v = neg ? -bytes : bytes;
    int unit = 0;
    while (v >= step && unit < 6) { v /= step; ++unit; }
    // Whole bytes never show decimals, and a value that rounds up moves to the next unit.
    const double p = pow(10.0, decimals);
    if (unit < 6 && floor(v * p + 0.5) / p >= step) { v /= step; ++unit; }
    if (unit == 0)
        snprintf(buf, size, "%s%.0f %s", neg ? "-" : "", v, "B");
    else
        snprintf(buf, size, "%s%.*f %s", neg ? "-" : "", decimals, v, binary ? iec[unit] : si[unit]);
    return buf;
}

inline const char* FormatDuration(char* buf, size_t size, double seconds, int parts = 2)
{
    const bool neg = seconds < 0.0;
    double s = neg ? -seconds : seconds;
    if (s < 1.0)
    {
        snprintf(buf, size, "%s%.0fms", neg ? "-" : "", s * 1000.0);
        return buf;
    }
    static const double unit_s[] = { 86400.0, 3600.0, 60.0, 1.0 };
    static const char* const unit_n[] = { "d", "h", "m", "s" };
    long long total = (long long)floor(s + 0.5);
    size_t n = 0;
    if (neg && n + 1 < size) buf[n++] = '-';
    int shown = 0;
    for (int u = 0; u < 4 && shown < (parts > 0 ? parts : 1); ++u)
    {
        const long long q = total / (long long)unit_s[u];
        if (q == 0 && (shown == 0 || u == 3))
            continue;
        if (shown > 0 && q == 0)
            break;   // "2h 0m" reads better as "2h"
        total -= q * (long long)unit_s[u];
        const int w = snprintf(buf + n, n < size ? size - n : 0, "%s%lld%s", shown ? " " : "", q, unit_n[u]);
        n += w > 0 ? (size_t)w : 0;
        ++shown;
    }
    if (shown == 0)
        snprintf(buf, size, "0s");
    return buf;
}

// `seconds_ago`: positive for the past, negative for the future.
inline const char* FormatRelativeTime(char* buf, size_t size, double seconds_ago)
{
    const bool future = seconds_ago < 0.0;
    const double s = future ? -seconds_ago : seconds_ago;
    if (s < 45.0) { snprintf(buf, size, "%s", future ? "in a moment" : "just now"); return buf; }
    // Each unit covers ages below its limit: under 45 min in minutes, under 22 h in hours, and so on.
    struct Unit { double Below; double Seconds; const char* One; const char* Many; };
    static const Unit units[] = {
        { 2700.0,     60.0,       "1 min",   "%d min"    },
        { 79200.0,    3600.0,     "1 hour",  "%d hours"  },
        { 604800.0,   86400.0,    "1 day",   "%d days"   },
        { 2592000.0,  604800.0,   "1 week",  "%d weeks"  },
        { 28927800.0, 2629800.0,  "1 month", "%d months" },
        { 1e300,      31557600.0, "1 year",  "%d years"  },
    };
    int u = 0;
    while (s >= units[u].Below)
        ++u;
    int n = (int)floor(s / units[u].Seconds + 0.5);
    if (n < 1)
        n = 1;
    char amount[32];
    if (n <= 1)
        snprintf(amount, sizeof(amount), "%s", units[u].One);
    else
        snprintf(amount, sizeof(amount), units[u].Many, n);
    if (!future && u == 2 && n == 1)
        snprintf(buf, size, "yesterday");
    else if (future && u == 2 && n == 1)
        snprintf(buf, size, "tomorrow");
    else
        snprintf(buf, size, future ? "in %s" : "%s ago", amount);
    return buf;
}

inline const char* FormatRelativeTime(char* buf, size_t size, time_t when, time_t now)
{
    return FormatRelativeTime(buf, size, difftime(now, when));
}

namespace FormatDetail {
inline char* NextBuffer()
{
    static char ring[8][64];
    static int next = 0;
    next = (next + 1) & 7;
    return ring[next];
}
} // namespace FormatDetail

inline const char* FormatBytes(double bytes, bool binary = false, int decimals = 1) { return FormatBytes(FormatDetail::NextBuffer(), 64, bytes, binary, decimals); }
inline const char* FormatDuration(double seconds, int parts = 2)                   { return FormatDuration(FormatDetail::NextBuffer(), 64, seconds, parts); }
inline const char* FormatRelativeTime(double seconds_ago)                          { return FormatRelativeTime(FormatDetail::NextBuffer(), 64, seconds_ago); }
inline const char* FormatRelativeTimeSince(time_t when)                            { return FormatRelativeTime(FormatDetail::NextBuffer(), 64, when, time(nullptr)); }

} // namespace VanGui
