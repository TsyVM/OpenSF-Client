#!/bin/bash
# Builds Soldier Front Legacy for macOS (Apple Silicon): "Soldier Front Legacy.app", then checks it.
#
#   ./Client/Mac/build_mac.sh [--angle <folder>] [--build-angle] [--data <Soldier Front data>] [--clean]
#
# Run from anywhere inside the kit (made on Windows by Tools/make_mac_kit.py). Needs an Apple
# Silicon Mac with macOS 12 or later, Apple's command-line tools (xcode-select --install) and
# CMake (brew install cmake, or cmake.org). What it makes, in bin/macos/:
#
#   Soldier Front Legacy.app           the game (ANGLE inside it, signed for this Mac only: "ad hoc")
#   SoldierFrontLegacy-macOS-app.zip   the app alone, to send back to Team Vanilla
#   test/first-screen.png, test/game.log    with --data: the game's first screen and its log
#
# ANGLE (OpenGL ES on Metal: two files, libEGL.dylib and libGLESv2.dylib) comes from, in order:
#   --angle <folder>           a folder holding both (arm64 or universal)
#   vendor/ANGLE/macos-arm64   in the kit, if it was put there
#   an installed Google Chrome, Microsoft Edge or Brave (each carries ANGLE's own build)
#   --build-angle              ANGLE built from its source here (Xcode, git, about an hour, ~10 GB)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KIT="$(cd "$HERE/../.." && pwd)"
BUILD="$KIT/build-mac"
OUT="$KIT/bin/macos"
APP_NAME="Soldier Front Legacy.app"
ANGLE_ARG="" DATA="" BUILD_ANGLE=0 CLEAN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --angle) ANGLE_ARG="$2"; shift 2 ;;
        --build-angle) BUILD_ANGLE=1; shift ;;
        --data) DATA="$2"; shift 2 ;;
        --clean) CLEAN=1; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)"; exit 2 ;;
    esac
done

say() { printf '\n== %s\n' "$*"; }
fail() { printf '\nFAILED: %s\n' "$*" >&2; exit 1; }

# ── What the Mac needs ────────────────────────────────────────────────────────
say "Checking this Mac"
[ "$(uname -s)" = "Darwin" ] || fail "this is not a Mac"
[ "$(uname -m)" = "arm64" ] || fail "this Mac is not Apple Silicon (an Intel Mac cannot build or play this game)"
xcode-select -p >/dev/null 2>&1 || fail "Apple's command-line tools are missing: run  xcode-select --install"
command -v cmake >/dev/null 2>&1 || fail "CMake is missing: brew install cmake (or https://cmake.org/download/)"
echo "macOS $(sw_vers -productVersion), $(sysctl -n machdep.cpu.brand_string), $(cmake --version | head -1)"

VANGUI_SRC="$KIT/VanGUI"
SHADERS="$HERE/generated/Shaders.cpp"
[ -f "$VANGUI_SRC/src/vangui.cpp" ] || fail "VanGUI's source is not in the kit ($VANGUI_SRC)"
[ -f "$SHADERS" ] || fail "the shaders are not in the kit ($SHADERS): the kit is made with Tools/make_mac_kit.py"
[ "$CLEAN" = 1 ] && rm -rf "$BUILD"
mkdir -p "$BUILD" "$OUT"

# ── ANGLE ─────────────────────────────────────────────────────────────────────
angle_from() {   # a folder holding libEGL.dylib and libGLESv2.dylib, or nothing
    [ -n "$1" ] && [ -f "$1/libEGL.dylib" ] && [ -f "$1/libGLESv2.dylib" ] && echo "$1"
}

build_angle() {   # its progress on stderr; the folder it made, alone, on stdout
    local src="$BUILD/angle-src"
    {
        say "Building ANGLE from its source (this takes a while)"
        command -v git >/dev/null 2>&1 || fail "git is missing"
        xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1 || fail "the macOS SDK is missing (install Xcode)"
        mkdir -p "$src"
        [ -d "$src/depot_tools" ] || git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$src/depot_tools"
        export PATH="$src/depot_tools:$PATH" DEPOT_TOOLS_UPDATE=0
        [ -d "$src/angle" ] || git clone https://chromium.googlesource.com/angle/angle "$src/angle"
        cd "$src/angle"
        python3 scripts/bootstrap.py
        gclient sync --no-history
        gn gen out/Release --args='is_debug=false target_cpu="arm64" is_component_build=false angle_enable_metal=true angle_enable_vulkan=false angle_enable_gl=false angle_enable_null=false angle_enable_swiftshader=false angle_build_tests=false angle_has_frame_capture=false'
        autoninja -C out/Release libEGL libGLESv2
    } >&2
    echo "$src/angle/out/Release"
}

say "Finding ANGLE"
ANGLE_SRC=""
ANGLE_SRC="$(angle_from "$ANGLE_ARG" || true)"
[ -n "$ANGLE_ARG" ] && [ -z "$ANGLE_SRC" ] && fail "--angle $ANGLE_ARG does not hold libEGL.dylib and libGLESv2.dylib"
[ -z "$ANGLE_SRC" ] && ANGLE_SRC="$(angle_from "$KIT/vendor/ANGLE/macos-arm64" || true)"
if [ -z "$ANGLE_SRC" ] && [ "$BUILD_ANGLE" = 0 ]; then
    for browser in "/Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Framework.framework/Versions/Current/Libraries" \
                   "/Applications/Microsoft Edge.app/Contents/Frameworks/Microsoft Edge Framework.framework/Versions/Current/Libraries" \
                   "/Applications/Brave Browser.app/Contents/Frameworks/Brave Browser Framework.framework/Versions/Current/Libraries"; do
        ANGLE_SRC="$(angle_from "$browser" || true)"
        [ -n "$ANGLE_SRC" ] && break
    done
fi
[ -z "$ANGLE_SRC" ] && [ "$BUILD_ANGLE" = 1 ] && ANGLE_SRC="$(angle_from "$(build_angle)" || true)"
[ -n "$ANGLE_SRC" ] || fail "no ANGLE: install Google Chrome (its copy is used), or pass --angle <folder>, or --build-angle"
echo "ANGLE from: $ANGLE_SRC"

# Our own copy: arm64 only, named @rpath/<name> so the app finds it in Contents/Frameworks, and
# libEGL's own reference to libGLESv2 made the same.
ANGLE="$BUILD/angle"
rm -rf "$ANGLE"
mkdir -p "$ANGLE"
for lib in libEGL.dylib libGLESv2.dylib; do
    if lipo -archs "$ANGLE_SRC/$lib" | grep -qw x86_64; then
        lipo -thin arm64 "$ANGLE_SRC/$lib" -output "$ANGLE/$lib" || fail "$lib has no arm64 code"
    else
        cp "$ANGLE_SRC/$lib" "$ANGLE/$lib"
    fi
    chmod u+w "$ANGLE/$lib"
    codesign --remove-signature "$ANGLE/$lib" 2>/dev/null || true
    install_name_tool -id "@rpath/$lib" "$ANGLE/$lib"
done
otool -L "$ANGLE/libEGL.dylib" | awk 'NR > 1 { print $1 }' | grep 'libGLESv2' | while read -r dep; do
    install_name_tool -change "$dep" "@rpath/libGLESv2.dylib" "$ANGLE/libEGL.dylib"
done
lipo -archs "$ANGLE/libEGL.dylib" | grep -qw arm64 || fail "ANGLE is not arm64"

# ── The game ──────────────────────────────────────────────────────────────────
say "Building the game"
cmake -S "$HERE" -B "$BUILD/game" -DCMAKE_BUILD_TYPE=Release \
      -DVANGUI_SRC="$VANGUI_SRC" -DANGLE_DIR="$ANGLE" -DSHADERS_CPP="$SHADERS" >/dev/null
cmake --build "$BUILD/game" --config Release -j "$(sysctl -n hw.ncpu)"

BUILT="$BUILD/game/legacysf.app"
[ -d "$BUILT" ] || fail "the build made no app ($BUILT)"
APP="$OUT/$APP_NAME"
rm -rf "$APP"
ditto "$BUILT" "$APP"
# The app is signed for this Mac only (ad hoc): Apple Silicon runs nothing unsigned. Players allow
# it once (System Settings, Privacy & Security, Open Anyway): Team Vanilla has no Apple membership.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP" || fail "the app's signature does not check"
# No server in a Mac's game (PF-1, PF-11): lsf::Server's own functions would be in it.
if nm "$APP/Contents/MacOS/legacysf" 2>/dev/null | grep -q "3lsf6Server"; then fail "server code is linked into the Mac game"; fi
echo "Made: $APP ($(du -sh "$APP" | cut -f1))"

ZIP="$OUT/SoldierFrontLegacy-macOS-app.zip"
rm -f "$ZIP"
# No resource forks or extended attributes ("._" files): the app is packed again on Windows.
ditto -c -k --norsrc --noextattr --keepParent "$APP" "$ZIP"
echo "To send back: $ZIP ($(du -h "$ZIP" | cut -f1))"

# ── A first check: the game opens, draws its first screen with Metal, and closes ──
if [ -n "$DATA" ]; then
    say "Checking the game (its first screen, then it closes)"
    TEST="$OUT/test"
    rm -rf "$TEST"
    mkdir -p "$TEST"
    # Its own settings file, and an account service that is not there (127.0.0.1:9): this check
    # never reaches Team Vanilla's real one.
    "$APP/Contents/MacOS/legacysf" --data "$DATA" --shot "$TEST/first-screen.png" --settings "$TEST/settings.cfg" \
        --tvas http://127.0.0.1:9 > "$TEST/console.txt" 2>&1 &
    GAME=$!
    for _ in $(seq 1 120); do
        kill -0 "$GAME" 2>/dev/null || break
        sleep 1
    done
    kill "$GAME" 2>/dev/null || true
    wait "$GAME" 2>/dev/null || true
    LOG="$HOME/Library/Application Support/Soldier Front Legacy/game.log"
    [ -f "$LOG" ] && cp "$LOG" "$TEST/game.log"
    if [ -f "$TEST/first-screen.png" ]; then
        echo "It works: $TEST/first-screen.png (and game.log beside it). Send both back with the zip."
    else
        echo "No picture was made: send back $TEST/game.log and $TEST/console.txt with the zip."
        exit 1
    fi
fi
