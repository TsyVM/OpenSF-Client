# The source lists, shared by the Windows build (CMakeLists.txt) and Android's
# (Client/Android/CMakeLists.txt).
get_filename_component(LSF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Soldier Front's own file formats (vendored SFSDK data readers).
set(LSF_SFSDK_SOURCES
    ${LSF_ROOT}/vendor/SFSDK/src/data/sff.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/mrg.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/archive.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/asset_library.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/map.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/cft.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/msf.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/wld.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/env.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/worldscript.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/xml.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/osf.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/map_bundle.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/tga.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/dds.cpp
    ${LSF_ROOT}/vendor/SFSDK/src/data/cfg.cpp)

# What the client and the server share: the engine's core, the network, the Soldier Front
# loaders and the game's rules and protocol.
set(LSF_SHARED_SOURCES
    ${LSF_ROOT}/Shared/Engine/Core/Log.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Time.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Strings.cpp
    ${LSF_ROOT}/Shared/Engine/Core/FileSystem.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Config.cpp
    ${LSF_ROOT}/Shared/Engine/Core/CrashHandler.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Xml.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Crypto.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Ed25519.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Json.cpp
    ${LSF_ROOT}/Shared/Engine/Physics/CollisionMesh.cpp
    ${LSF_ROOT}/Shared/Engine/Asset/ImageDecode.cpp
    ${LSF_ROOT}/Shared/Engine/Asset/ModelFile.cpp
    ${LSF_ROOT}/Shared/Engine/Asset/TextureFile.cpp
    ${LSF_ROOT}/Shared/Engine/Net/Socket.cpp
    ${LSF_ROOT}/Shared/Engine/Net/Connection.cpp
    ${LSF_ROOT}/Shared/Engine/Net/NetHost.cpp
    ${LSF_ROOT}/Shared/Engine/Net/Https.cpp
    ${LSF_ROOT}/Shared/SF/Data.cpp
    ${LSF_ROOT}/Shared/SF/Image.cpp
    ${LSF_ROOT}/Shared/SF/Lma.cpp
    ${LSF_ROOT}/Shared/SF/Level.cpp
    ${LSF_ROOT}/Shared/SF/Model.cpp
    ${LSF_ROOT}/Shared/SF/UiData.cpp
    ${LSF_ROOT}/Shared/SF/ClanMarks.cpp
    ${LSF_ROOT}/Shared/SF/Tables.cpp
    ${LSF_ROOT}/Shared/SF/SoundTables.cpp
    ${LSF_ROOT}/Shared/Game/Rules.cpp
    ${LSF_ROOT}/Shared/Game/Events.cpp
    ${LSF_ROOT}/Shared/Game/Modes.cpp
    ${LSF_ROOT}/Shared/Game/Items.cpp
    ${LSF_ROOT}/Shared/Game/Protocol.cpp
    ${LSF_ROOT}/Shared/Game/Replay.cpp
    ${LSF_ROOT}/Shared/Game/Navigation.cpp
    ${LSF_ROOT}/Shared/Game/Movement.cpp
    ${LSF_ROOT}/Shared/Game/Ballistics.cpp
    ${LSF_ROOT}/Shared/Game/Wear.cpp
    ${LSF_ROOT}/Shared/Game/Shop.cpp
    ${LSF_ROOT}/Shared/Game/Registry.cpp
    ${LSF_ROOT}/Shared/Game/TvasJson.cpp
    ${LSF_ROOT}/Shared/Game/TvasProto.cpp
    ${LSF_ROOT}/Shared/Game/Tvas.cpp
    ${LSF_ROOT}/Shared/Game/Pack.cpp
    ${LSF_ROOT}/Shared/Game/PackMount.cpp
    ${LSF_SFSDK_SOURCES})

# The server's core: also linked into the game, which hosts a server for a player's own machine.
set(LSF_SERVER_CORE_SOURCES
    ${LSF_ROOT}/Server/Source/Accounts.cpp
    ${LSF_ROOT}/Server/Source/Server.cpp
    ${LSF_ROOT}/Server/Source/Social.cpp
    ${LSF_ROOT}/Server/Source/Shop.cpp
    ${LSF_ROOT}/Server/Source/Staff.cpp
    ${LSF_ROOT}/Server/Source/TvasLink.cpp
    ${LSF_ROOT}/Server/Source/Content.cpp
    ${LSF_ROOT}/Server/Source/Maps.cpp
    ${LSF_ROOT}/Server/Source/Bots.cpp
    ${LSF_ROOT}/Server/Source/Match.cpp
    ${LSF_ROOT}/Server/Source/Modes.cpp)
set(LSF_SERVER_SOURCES ${LSF_ROOT}/Server/Source/main.cpp)

# Window, input, graphics, sound, interface: every platform (each file keeps its own platform parts).
set(LSF_ENGINE_CLIENT_SOURCES
    ${LSF_ROOT}/Client/Source/Engine/Platform/Input.cpp
    ${LSF_ROOT}/Client/Source/Engine/Platform/Gamepad.cpp
    ${LSF_ROOT}/Client/Source/Engine/Platform/System.cpp
    ${LSF_ROOT}/Client/Source/Engine/Render/Device.cpp
    ${LSF_ROOT}/Client/Source/Engine/Render/GLES/DeviceGLES.cpp
    ${LSF_ROOT}/Client/Source/Engine/Render/GpuTexture.cpp
    ${LSF_ROOT}/Client/Source/Engine/Render/GpuProfiler.cpp
    ${LSF_ROOT}/Client/Source/Engine/UI/UiLayer.cpp
    ${LSF_ROOT}/Client/Source/Engine/Audio/Mixer.cpp
    ${LSF_ROOT}/Client/Source/Engine/Audio/Pcm.cpp)

# Windows only: the Win32 window, Direct3D 11 and 12, the exe's packed files.
set(LSF_ENGINE_WINDOWS_SOURCES
    ${LSF_ROOT}/Client/Source/Engine/Platform/Window.cpp
    ${LSF_ROOT}/Client/Source/Engine/Render/D3D11/Device11.cpp
    ${LSF_ROOT}/Client/Source/Engine/Render/D3D12/Device12.cpp
    ${LSF_ROOT}/Shared/Engine/Core/Embedded.cpp)

# The game (the entry point is each platform's own: Client/PC/main.cpp, Client/Android/AndroidMain.cpp).
set(LSF_CLIENT_SOURCES
    ${LSF_ROOT}/Client/Source/Game/App.cpp
    ${LSF_ROOT}/Client/Source/Game/Settings.cpp
    ${LSF_ROOT}/Client/Source/Game/Audio/Sounds.cpp
    ${LSF_ROOT}/Client/Source/Game/Net/Session.cpp
    ${LSF_ROOT}/Client/Source/Game/Net/SessionTvas.cpp
    ${LSF_ROOT}/Client/Source/Game/Net/TvasClient.cpp
    ${LSF_ROOT}/Client/Source/Game/Ui/Ui.cpp
    ${LSF_ROOT}/Client/Source/Game/Ui/Atlas.cpp
    ${LSF_ROOT}/Client/Source/Game/Ui/Page.cpp
    ${LSF_ROOT}/Client/Source/Game/Ui/Kit.cpp
    ${LSF_ROOT}/Client/Source/Game/Ui/Crosshair.cpp
    ${LSF_ROOT}/Client/Source/Game/Ui/Emblem.cpp
    ${LSF_ROOT}/Client/Source/Game/Render/WorldRenderer.cpp
    ${LSF_ROOT}/Client/Source/Game/Render/FramePipe.cpp
    ${LSF_ROOT}/Client/Source/Game/Render/ModelStage.cpp
    ${LSF_ROOT}/Client/Source/Game/World/GameWorld.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Hud.cpp
    ${LSF_ROOT}/Client/Source/Game/World/KillEffects.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Effects.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Watch.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Death.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Objectives.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Arms.cpp
    ${LSF_ROOT}/Client/Source/Game/World/TouchControls.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Staff.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Rewards.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/EventsEditor.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Replay.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Recordings.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/ShopEditor.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/IdCard.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Capsule.cpp
    ${LSF_ROOT}/Client/Source/Game/World/Radar.cpp
    ${LSF_ROOT}/Client/Source/Game/World/MatchAudio.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Common.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Options.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Credits.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Front.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Pages.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Lobby.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Shop.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Clan.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Social.cpp
    ${LSF_ROOT}/Client/Source/Game/Screens/Match.cpp)
