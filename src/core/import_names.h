// The names the Android import keeps. One implementation, used by the Java importer through the
// game library and tested on the host in tests/import_tests.cpp.
#pragma once
#include <string>

namespace ImportNames {

// The name in the game folder for a chosen file, or "" when it is not a file the import copies
// (only MageBros.exe, Music01..06.mid and a .sf2 are kept).
std::string TargetName(const std::string& displayName);

}  // namespace ImportNames
