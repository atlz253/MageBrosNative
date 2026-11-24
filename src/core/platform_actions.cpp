#include "platform_actions.h"
#include <cstdlib>

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

namespace PlatformActions {

bool CanImportGameFiles() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

bool CanManageWindows() {
#if defined(__ANDROID__)
    return false;
#else
    return std::getenv("MBN_MOBILE_VIDEO") == nullptr;
#endif
}

bool ImportGameFiles() {
#if defined(__ANDROID__)
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!env || !activity) return false;
    jclass cls = env->GetObjectClass(activity);
    if (!cls) return false;
    jmethodID method = env->GetStaticMethodID(cls, "openGameFilesImport", "(Landroid/app/Activity;)V");
    if (!method) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return false;
    }
    env->CallStaticVoidMethod(cls, method, activity);
    env->DeleteLocalRef(cls);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return true;
#else
    return false;
#endif
}

}  // namespace PlatformActions
