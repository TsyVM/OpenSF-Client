// Logs a stack trace when the process crashes, then lets Windows end it. Each frame is written as
// module + offset (and by name when the PDB is beside the exe), so a log from a player's machine
// can be read later against the PDB kept for that build (Source/Tools/symbolize.py).
#pragma once

#include <string>

namespace eng {

void install_crash_handler();
// This build: when the exe was linked, and its PDB's GUID and age (the key to its symbols).
std::string build_id();

}  // namespace eng
