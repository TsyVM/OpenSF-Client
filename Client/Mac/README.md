# Building Soldier Front Legacy for macOS

This kit builds the Mac version of Soldier Front Legacy into **Soldier Front Legacy.app**. It also
runs a short check that the app opens and draws. It takes about ten minutes on an Apple Silicon Mac.

## What the Mac needs

- An **Apple Silicon** Mac (M1 or newer) on **macOS 12** or later. Intel Macs can't build or play it.
- Apple's command-line tools. Install them in Terminal with `xcode-select --install`.
- **CMake**, from `brew install cmake` or <https://cmake.org/download/>.
- **ANGLE**, the OpenGL ES library that runs on Metal. The simplest way to get it is to have
  **Google Chrome** installed (Microsoft Edge or Brave work too), because the script copies the
  browser's own ANGLE. The other options are below.
- For the check, the **Soldier Front game data folder** (the one holding `area`, `lobby`,
  `weapon` and so on). Team Vanilla can send it with the kit.

## Build it

In Terminal, from the folder the kit unzipped to:

```bash
./Client/Mac/build_mac.sh --data /path/to/the/data
```

The script finishes with one of two lines:

- **"It works: …/first-screen.png"**: the app opened, drew its first screen with Metal and closed.
- **"No picture was made"**: something went wrong, and the log tells Team Vanilla what.

The check uses its own settings and never contacts Team Vanilla's account service.

## What to send back

Send these files from `bin/macos/` to Team Vanilla:

- `SoldierFrontLegacy-macOS-app.zip`: the app.
- `test/first-screen.png` and `test/game.log` (and `test/console.txt` if the check failed).

## Other ways to get ANGLE

- `--angle <folder>`: a folder holding `libEGL.dylib` and `libGLESv2.dylib` for arm64 (or universal).
- `--build-angle`: builds ANGLE from its source. This needs the full Xcode, git, about an hour
  and about 10 GB of disk.

## Good to know

- The app is signed for this Mac only ("ad hoc"), because Team Vanilla has no Apple Developer
  membership. The first time anyone opens it, macOS asks them to allow it once, in
  **System Settings → Privacy & Security → Open Anyway**.
- `--clean` starts the build over from nothing.
- The Mac game has no "This PC": a Mac plays on Team Vanilla's servers and the community's, but
  hosts nothing.
