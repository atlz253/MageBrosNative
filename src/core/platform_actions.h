// Platform actions the game's own UI offers where the platform needs them. Importing the game
// files is Android-only: desktop builds keep them beside the program and have nothing to import.
#pragma once

namespace PlatformActions {

bool CanImportGameFiles();

// False on Android (and when MBN_MOBILE_VIDEO=1): no monitors, window modes, resolutions or VSync.
bool CanManageWindows();

// Opens the import screen without leaving the game; returns false when the platform has no such
// action or it could not be started.
bool ImportGameFiles();

}  // namespace PlatformActions
