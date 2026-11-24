// Where the game keeps its files on platforms that cannot keep them beside the program.
//
// On Android the program lives inside the APK, which is read-only, so the configuration, log,
// screenshots, imported game files and saves share one per-user folder in the app's external
// storage (or the pref path when external storage is unavailable). On Windows, macOS and Linux
// this stays empty and the existing per-platform rules apply.
#pragma once
#include <string>

namespace AppPaths {

// The per-user data folder with a trailing separator, or "" on platforms that do not use one.
std::string DataRoot();

// The folder holding the player's imported game files with a trailing separator, created on
// first use, or "" on platforms that read them beside the program.
std::string GameFolder();

// Copies the bundled General MIDI bank out of the package into the data root on first run.
// A no-op on platforms that ship it beside the program.
void UnpackBundledBank();

}  // namespace AppPaths
