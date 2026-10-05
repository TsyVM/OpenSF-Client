// What the game asks of the operating system beyond the window: its memory, a last word to the
// player, the region's units.
#pragma once

#include <cstddef>
#include <string>

namespace eng::platform {

// The process's own memory, in bytes (private on Windows, resident on Android).
size_t process_memory();

// A message the player must see before the game stops (a dialog on Windows; on Android it goes
// to the log, and the app's own screen shows it when the game returns).
void fatal_message(const char* title, const char* text, void* window = nullptr);

// Whether the system's region measures in miles (the U.S. and the few others).
bool uses_mph();
// Android: the device's country (two letters), given by the platform layer at start.
void set_country(const std::string& country);

// Typing on a device with no keyboard of its own (a phone): its on-screen keyboard over the
// game, typing into the game's own line (the game draws it). What the line holds comes back as it
// changes (typed_text), and once when the keyboard closes (text_answer: Done confirms it, closing
// the keyboard otherwise does not). Windows types straight into the game: there all of this
// answers nothing.
bool text_input_native();
void start_typing(const std::string& initial, int max_length, bool capitals);
// The game moved on: the keyboard goes, nothing more is answered.
void stop_typing();
// What the line holds now, when it changed since the last call.
bool typed_text(std::string& text);
// The keyboard closed: what was typed, and whether it was confirmed.
bool text_answer(std::string& text, bool& confirmed);

}  // namespace eng::platform
