// The Android launcher verifies the player's executable before starting the game. The check
// stays in C++ so there is one known-build table and one SHA-256 implementation.
#include <jni.h>
#include <SDL3/SDL.h>
#include "../core/assets.h"
#include "../core/import_names.h"
#include "../core/known_builds.h"
#include "../core/sha256.h"
#include <string>
#include <vector>

namespace {

std::string ToString(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

std::string Verify(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!Assets::ReadFile(path, &bytes)) return "ERROR:Could not read the executable.";
    if (bytes.size() < 2 || bytes[0] != 'M' || bytes[1] != 'Z')
        return "ERROR:That file is not a Windows executable.";
    std::string hash = Sha256Hex(bytes.data(), bytes.size());
    if (const KnownBuilds::Build* build = KnownBuilds::Find(hash))
        return std::string("OK:") + build->label;
    return std::string("UNKNOWN:") + hash;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_magebros_game_GameFilesActivity_nativeVerifyExecutable(JNIEnv* env, jclass, jstring path) {
    return env->NewStringUTF(Verify(ToString(env, path)).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_magebros_game_GameFilesActivity_nativeTargetName(JNIEnv* env, jclass, jstring name) {
    return env->NewStringUTF(ImportNames::TargetName(ToString(env, name)).c_str());
}

// Android's Back action, delivered to the game as the AC_BACK key SDL already understands.
extern "C" JNIEXPORT void JNICALL
Java_com_magebros_game_MageBrosActivity_nativeBack(JNIEnv*, jclass) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_AC_BACK;
    event.key.key = SDLK_AC_BACK;
    event.key.down = true;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    SDL_PushEvent(&event);
}
