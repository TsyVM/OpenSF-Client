<div align="center">

<img src="Docs/img/legacy-shield.jpg" width="260" alt="Soldier Front Legacy"/>

# Soldier Front Legacy

<p><em>Soldier Front, reconstructed: a new game that plays the original's maps, weapons and soldiers, at any frame rate, with a controller or a mouse, on Windows, Linux, macOS and Android</em></p>

<a href="#">
<img src="https://readme-typing-svg.demolab.com/?lines=Soldier+Front%2C+reconstructed.;Past+the+old+32+fps+cap%3A+up+to+360.;Xbox%2C+DualShock+4+and+DualSense+controllers.;One+Team+Vanilla+account+on+every+server.;Windows%2C+Linux%2C+macOS+and+Android.;40+maps.+12+game+types.+82+weapons.&font=Fira%20Code&center=true&width=700&height=45&color=E4C694&vCenter=true&size=20&pause=1800"/>
</a>

<br/>

[![C++23](https://img.shields.io/badge/C%2B%2B-23-E4C694?style=for-the-badge&labelColor=3F2B16&logo=cplusplus&logoColor=E4C694)](https://en.cppreference.com/w/cpp/23)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-E4C694?style=for-the-badge&labelColor=3F2B16&logo=windows&logoColor=E4C694)](#-building)
[![Android](https://img.shields.io/badge/Android-8%2B%2064--bit-E4C694?style=for-the-badge&labelColor=3F2B16&logo=android&logoColor=E4C694)](#-building)
[![Linux](https://img.shields.io/badge/Linux-x86--64-E4C694?style=for-the-badge&labelColor=3F2B16&logo=linux&logoColor=E4C694)](#-other-systems)
[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-E4C694?style=for-the-badge&labelColor=3F2B16&logo=apple&logoColor=E4C694)](#-other-systems)
[![VanGUI](https://img.shields.io/badge/UI-VanGUI-E4C694?style=for-the-badge&labelColor=3F2B16)](vendor/VanGUI/BUILD-INFO.md)
[![TeamVanilla](https://img.shields.io/badge/Team-TeamVanilla-E4C694?style=for-the-badge&labelColor=3F2B16)](https://teamvanilla.dev)

<br/>

[![Stars](https://img.shields.io/github/stars/tsyvm/soldierfrontlegacy?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../stargazers)
[![Issues](https://img.shields.io/github/issues/tsyvm/soldierfrontlegacy?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../issues)
[![Last Commit](https://img.shields.io/github/last-commit/tsyvm/soldierfrontlegacy?style=for-the-badge&color=E4C694&labelColor=3F2B16)](../../commits)
[![Website](https://img.shields.io/badge/Website-sf.teamvanilla.dev-E4C694?style=for-the-badge&labelColor=3F2B16)](https://sf.teamvanilla.dev)

<br/>

[![Renderers](https://img.shields.io/badge/Renderers-DirectX%2012%20%C2%B7%2011%20%C2%B7%20OpenGL-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Frame rate](https://img.shields.io/badge/Frame%20rate-30%20to%20360-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Controllers](https://img.shields.io/badge/Controllers-Xbox%20%C2%B7%20DualShock%204%20%C2%B7%20DualSense-E4C694?style=flat-square&labelColor=3F2B16)](#-playing)
[![Maps](https://img.shields.io/badge/Maps-40-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Game types](https://img.shields.io/badge/Game%20types-12-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Weapons](https://img.shields.io/badge/Weapons-82-E4C694?style=flat-square&labelColor=3F2B16)](#-features-at-a-glance)
[![Protocol](https://img.shields.io/badge/Protocol-25-E4C694?style=flat-square&labelColor=3F2B16)](#-the-server)

</div>

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

**Soldier Front** is the tactical online shooter Dragonfly made in Korea as *Special Force* (2004), which North America played on ijji and then Aeria Games. **Soldier Front Legacy** is TeamVanilla's reconstruction of it: a new program, `legacysf.exe`, that plays Soldier Front's own 40 maps, 82 weapons, ['ARTC', 'Delta Force', 'ROKMC', 'KSF', 'SAS', 'GSG-9', 'GIGN', 'Spetsnaz', 'SRG', 'Force Recon', 'PSU', 'Mulan'] forces, sounds and lobby art, read in place from a player's own Soldier Front installation. The program around them is new: the renderer, the menus, the netcode, the rules and the options.

It draws with **DirectX 12** (DirectX 11 and OpenGL as fallbacks) on Windows 10 and 11, with OpenGL ES 3 on Android, Linux and macOS, at whatever frame rate the player chooses, and plays with a keyboard and mouse or an **Xbox, DualShock 4 or DualSense controller**. Players sign in once with a **Team Vanilla account** and keep one rank and record on every server, Team Vanilla's or anyone's. The server is its own repository: [Soldier Front Legacy Server](https://github.com/tsyvm/soldierfrontlegacy-server).

Ready-to-play downloads for Windows, Linux and Android, and the guide for players and server owners, are at **[sf.teamvanilla.dev](https://sf.teamvanilla.dev)**.

<p align="center">
<img src="Docs/img/hero.jpg" width="32%" alt="Desert Camp in third person, which a room may allow: running past the truck"/>
<img src="Docs/img/fps-match.jpg" width="32%" alt="Venezia with the net graph on, drawn larger in the box: 141 frames a second, the lowest 102 over the last few seconds, at a 14 ms ping"/>
<img src="Docs/img/controller.jpg" width="32%" alt="Options, Controls, Controller: the sticks, dead zones and vibration on the left, each action&#x27;s button on the right"/>
<br/>
<img src="Docs/img/pirate.jpg" width="32%" alt="Pirate Mode on the Pirate Ship: pirates and marines fight over three strongholds on the beach"/>
<img src="Docs/img/server-list.jpg" width="32%" alt="The first page: the server list with its All, Official, Community and Favourites tabs, and This PC to play and host on your own machine"/>
<img src="Docs/img/room.jpg" width="32%" alt="The waiting room: two sides of bots, the map&#x27;s briefing and the room&#x27;s five options"/>
<br/>
<img src="Docs/img/weapon-shop.jpg" width="32%" alt="The Weapon Shop: each gun for 7, 30 or 90 days, or for good"/>
<img src="Docs/img/emblem.jpg" width="32%" alt="The emblem maker: a clan&#x27;s emblem built from shapes and pictures, shown at 64, 32 and 16 pixels"/>
<img src="Docs/img/undead.jpg" width="32%" alt="Horror Mode 2 on Village Horror: an undead at arm&#x27;s length, your health down to 43"/>
<br/>
<sub>Taken from the game itself on DirectX 12 at 1920 x 1080, by its own automated walk through every screen.</sub>
</p>

<div align="center">

### 📑 Contents

[Features](#-features-at-a-glance) · [Legacy and the Original](#-legacy-and-the-original) · [Playing](#-playing) · [Requirements](#-requirements)

[Building](#-building) · [Other Systems](#-other-systems) · [The Server](#-the-server) · [Repository Layout](#-repository-layout)

[Known Limitations](#-known-limitations) · [Credits and Ownership](#-credits-and-ownership) · [Links](#-links)

</div>

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## ✨ Features at a Glance

**Past the old frame cap**
- **Your frame rate**: the original holds itself to 32 frames a second; Legacy has no cap of its own. The limit is yours, from 30 to 360, 144 to begin with, and vertical sync is a switch
- **Three renderers on Windows**: DirectX 12 first, then DirectX 11, then OpenGL (through ANGLE); one that cannot start hands over to the next and says so
- **Any screen**: real pixels at any Windows scaling, a window or borderless full screen, in 4:3, 5:4, 16:10, 16:9 or 21:9, stretched as the original was or kept in shape between bars
- **Anti-aliasing** at 2x, 4x or 8x and texture filtering up to 16x

**Controllers as well as the mouse**
- **Xbox controllers** (anything XInput), the **DualShock 4** and the **DualSense**, by cable or Bluetooth
- Every action is a button you set as you set a key; in the menus the left stick is the pointer, so every lobby page works without a mouse
- Look speed, dead zones, invert, a smooth or a linear stick, and vibration for hits, blasts and shots
- **No aim assist**: a controller plays the same match as a mouse, on the same terms

**The whole game**
- **All 12 game types** of the original's waiting room, each played by its own rules: Team Battle with its Destroy, Take Back, Escape and Dual missions, Team Deathmatch, Single Battle, Sniper Mode, Capture the Captain, Captain Mode, Horror Mode, Horror Mode 2, Team Slayer, Occupy, Pirate Mode and Training
- **Bots** fill a room and play the objective: they plant and defuse, carry the item home, take the consoles and the strongholds
- **The lobby as it was**: the server and channel lists, rooms, the Weapon, Character and Item Shops, capsules, clans and their emblems, friends, whispers and mail
- **Rewards and events**: sign-in days, hours of play, three quests a day, Duffle Bags and gift boxes
- **Replays**: every match the server keeps, watched again behind any soldier, wound on or back along its time line

**One account on every server**
- A **Team Vanilla account** made in the game: a username and a password, no email address
- Rank, record, SP, Coins, weapons, clan, friends and mail follow you to every server, official or community
- A server never sees your password: your game shows it a short-lived ticket that Team Vanilla signed
- **This PC**: a server of your own, inside the game, for bots or friends on your network

**The same game for everyone**
- Base weapons, forces and parts keep their official numbers on every server: an owner can add, nobody can rebalance
- Team Vanilla checks every match report, and one that could not have happened is refused
- A map keeps its own light, hour and sky, and smoke and flash-bangs are drawn in full: no option thins them

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🧭 Legacy and the Original

| | The original `soldierfront.exe` | **`legacysf.exe`** |
|---|---|---|
| **Frame rate** | Held to 32 frames a second | **No cap of its own: 30 to 360, 144 to begin with** |
| **Graphics** | Direct3D 9 | **DirectX 12, DirectX 11 or OpenGL on Windows; OpenGL ES 3 elsewhere** |
| **Controllers** | None | **Xbox (XInput), DualShock 4, DualSense; touch on Android** |
| **Servers** | Its publisher's own | **Team Vanilla's, anyone's, and This PC on your own machine** |
| **Runs on** | Windows | **Windows 10 and 11, Linux, macOS (Apple Silicon), Android 8 and newer** |
| **The data** | Its own archives | **The same files, read in place from your installation** |
| **Installing** | A launcher and a patcher | **Drag it into your Soldier Front folder and run it** |

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🎮 Playing

Download the game from **[sf.teamvanilla.dev](https://sf.teamvanilla.dev/download/)**. On Windows, drag everything in the zip into your Soldier Front folder (the one holding `data`) and run `legacysf.exe` there: nothing of Soldier Front's own is replaced, and nothing is installed. Make a Team Vanilla account in the game, make your soldier, pick a server.

The keys are the original's own, from its `config.cfg`; the controller's buttons are ours, since the original had none. Every one can be set again in **Options, Controls**.

| Action | Key | Xbox | PlayStation |
|---|---|---|---|
| Forward, back, left, right | <kbd>W</kbd> <kbd>S</kbd> <kbd>A</kbd> <kbd>D</kbd> | left stick | left stick |
| Look | the mouse | right stick | right stick |
| Jump | <kbd>Space</kbd> | A | Cross |
| Crouch | <kbd>Left Shift</kbd> | B | Circle |
| Walk | <kbd>Left Ctrl</kbd> | left stick, clicked | L3 |
| Fire · Scope | left button · right button | RT · LT | R2 · L2 |
| Reload | <kbd>R</kbd> | X | Square |
| Use: plant, defuse, open, take | <kbd>E</kbd> | X, held | Square, held |
| Primary · sidearm · melee | <kbd>1</kbd> <kbd>2</kbd> <kbd>3</kbd> | | |
| Throwables (each press the next) | <kbd>4</kbd> | LB | L1 |
| Last weapon | <kbd>F</kbd> | RB | R1 |
| Radio: command, general, reply | <kbd>Z</kbd> <kbd>X</kbd> <kbd>C</kbd> | d-pad | d-pad |
| Scores | <kbd>Tab</kbd> | View | Share or Create |
| The match menu | <kbd>Esc</kbd> | Menu | Options |

Every key, the chat keys and the touch controls are on the website's [Controls and controllers](https://sf.teamvanilla.dev/docs/controls/) page.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🧰 Requirements

| | |
|---|---|
| **To play** | Your own Soldier Front installation (a `data` folder holding `area`, `force`, `weapon` ...): the game plays its data in place |
| **Compiler** | MSVC with C++23 (Visual Studio 2026, generator `Visual Studio 18 2026`) |
| **CMake** | 3.28 or newer |
| **Python** | `py` on the PATH: the shaders are embedded at build time by `Tools/shaders_gen.py` |
| **Windows target** | x64 only (configure with `-A x64`) |
| **Android** | Android SDK in `%LOCALAPPDATA%\Android\Sdk` (build-tools 36.1.0, platform android-36, NDK 29) and a JDK; no Gradle |

Vendored in `vendor/`: VanGUI (TeamVanilla's interface library, prebuilt `/MT` for Windows and per ABI for Android, with its public headers), ANGLE, the Khronos headers, DXC with SPIRV-Cross for the shader step, stb, minimp3, and the Soldier Front SDK's file readers.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🔨 Building

### Windows

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target legacysf
```

The game comes out as `bin/legacysf.exe`, one file: the C runtime is linked in, ANGLE rides inside the exe, and the server's core is in it for This PC. Put it in a Soldier Front folder and run it there, or run it anywhere and tell it where Soldier Front is the first time.

### Android

```bat
py Client/Android/build_apk.py                                  :: the game alone
py Client/Android/build_apk.py --data <Soldier Front data>      :: with the game data inside
py Client/Android/build_apk.py --install --push-data <data>     :: onto the phone adb sees
```

`build_apk.py` builds `liblegacysf.so` with the NDK, compiles the Java side, links the resources with `aapt2`, then zipaligns and signs the APK: `bin/android/legacysf.apk` (package `org.teamvanilla.legacysf`). With `--data` the Soldier Front data goes inside it, deflated, and the first start sets it up once.

> ⚠️ The first build makes a release key in `Client/Android/keystore/`. Keep it, and keep it private and off GitHub (`.gitignore` already leaves it out): Android installs an update over the app only when both are signed with the same key.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🐧 Other Systems

The Linux game (`Client/Desktop`, X11 or Wayland through XWayland, OpenGL ES 3 on the system's own driver) and the macOS app (`Client/Mac`, Apple Silicon, ANGLE on Metal) compile VanGUI from its own source, which is not part of this repository: they build where that source is at hand. Their entry points, windows, sound and controller code are all here, and the ready-made Linux game is on the [download page](https://sf.teamvanilla.dev/download/).

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🖥️ The Server

`LegacySFServer`, the server anyone can run on Windows or Linux, is its own repository: **[Soldier Front Legacy Server](https://github.com/tsyvm/soldierfrontlegacy-server)**. The game carries the server's core too (`Server/Source` here), so This PC can hold a match on the player's own machine. Game and server must speak the same protocol, 25 in this build.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 📁 Repository Layout

```
SoldierFrontLegacy/
├── Client/
│   ├── Source/          ← the game: engine (window, input, DirectX 12 / 11, OpenGL ES, audio) and Game/ (screens, match, HUD, net)
│   ├── PC/              ← the Windows entry point, icon and resources
│   ├── Android/         ← NativeActivity, Java, manifest, build_apk.py; keystore/ is made here (never shared)
│   ├── Desktop/         ← the Linux entry point
│   ├── Mac/             ← the macOS app: CMake, build_mac.sh, Info.plist
│   └── Content/         ← the game's own few files
├── Shared/
│   ├── Engine/          ← core, files, crypto, network, collision
│   ├── SF/              ← Soldier Front's file formats: archives, maps, models, motions, lobby pages
│   └── Game/            ← the rules, weapons, movement, game types and the protocol, shared with the server
├── Server/Source/       ← the server's core, for This PC
├── Tools/               ← packres (files packed into the exe), shaders_gen.py
├── cmake/               ← source lists, the game's targets, the shader step
├── vendor/              ← VanGUI, ANGLE, Khronos, DXC + SPIRV-Cross, stb, minimp3, the Soldier Front SDK
├── Docs/img/            ← the pictures on this page
└── CMakeLists.txt
```

Made by the build: `build/`, `bin/`.

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🚧 Known Limitations

| Item | Status |
|---|---|
| **Soldier Front's data is not here** | The game plays a player's own installation; this repository carries none of it. |
| **Windows x64 only** | CMake stops on a 32-bit configuration. |
| **The exe is not code-signed** | Windows SmartScreen may not recognise it: More info, then Run anyway. |
| **Linux and macOS need VanGUI's source** | Not part of this repository (see [Other Systems](#-other-systems)). |
| **Not on Google Play** | The APK installs from a file; Android asks to allow the browser or file manager to install it. |
| **Team Vanilla's account service is not here** | Sign-in, rank and the server list are Team Vanilla's service at `api.teamvanilla.dev`. |

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🙏 Credits and Ownership

**Soldier Front** (*Special Force*) is Dragonfly GF Co., Ltd.'s game. Its maps, weapons, soldiers, sounds, radio voices and lobby art are theirs; Soldier Front Legacy plays them from the player's own installation, and none of them is in this repository.

| The original game | |
|---|---|
| **Dragonfly** (Dragonfly GF Co., Ltd., Seoul) | Created and developed Special Force, released in Korea in July 2004 |
| **Neowiz** (Pmang) | Published it in Korea |
| **NHN USA** | Opened it in North America as Soldier Front on ijji.com, 14 February 2007 |
| **Aeria Games** | Carried it on after buying ijji in 2011 |

| Soldier Front Legacy | |
|---|---|
| **[TeamVanilla](https://teamvanilla.dev)**, 2026 | The game, the server, Team Vanilla's account service and the website |
| **The community** | The donors and beta testers named in the in-game credits (Options, Credits) |
| **[VanGUI](vendor/VanGUI/BUILD-INFO.md)** | TeamVanilla's interface library, MIT, for every menu and dialog |
| **[ANGLE](https://chromium.googlesource.com/angle/angle)** | The OpenGL renderer on Windows and macOS, BSD 3-Clause |
| **[stb](https://github.com/nothings/stb)** · **[minimp3](https://github.com/lieff/minimp3)** | Images and compression; the game's MP3 sounds; public domain or MIT, CC0 |
| **[DXC](https://github.com/microsoft/DirectXShaderCompiler)** · **[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)** | The shader step at build time, University of Illinois/NCSA and Apache 2.0 |

<img width="100%" src="https://capsule-render.vercel.app/api?type=rect&color=0:3F2B16,50:E4C694,100:3F2B16&height=3"/>

## 🔗 Links

| | |
|---|---|
| **[sf.teamvanilla.dev](https://sf.teamvanilla.dev)** | The website: downloads, screenshots, the guide for players and server owners, the live server list |
| **[Download](https://sf.teamvanilla.dev/download/)** | The game for Windows, Linux and Android, and the server for Windows and Linux |
| **[The guide](https://sf.teamvanilla.dev/docs/)** | Getting started, controls, options, game types, your account |
| **[TeamVanilla Discord](https://discord.gg/TeJHcV9EJW)** | TeamVanilla: send `game.log` with a few words on what happened |

<div align="center">

<sub>Built and maintained by <a href="https://github.com/TsyVM">TsyVM</a> · <a href="https://teamvanilla.dev">TeamVanilla</a></sub>

<img width="100%" src="https://capsule-render.vercel.app/api?type=waving&color=0:E4C694,100:3F2B16&height=80&section=footer"/>

</div>
