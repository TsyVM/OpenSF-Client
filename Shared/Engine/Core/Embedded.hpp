// Files the exe carries inside itself (a pack made by Source/Tools/packres, embedded as an
// RCDATA resource), unpacked when first needed: the game ships as one exe.
//
// A pack unpacks into a folder of its own named by its contents (runtime\<prefix>-<hash> beside
// the exe, or under %LOCALAPPDATA%\TeamVanilla\LegacySF when the exe's folder cannot be
// written), so a newer exe with other files never mixes them with an older one's; the older
// folders are cleared away.
#pragma once

#include <filesystem>
#include <string>

namespace eng::embedded {

// The folder holding the pack's files (unpacked now if they were not already), or empty when the
// exe has no such pack or it could not be written anywhere.
std::filesystem::path unpack(const wchar_t* resource_name, const std::wstring& prefix);

}  // namespace eng::embedded
