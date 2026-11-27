#include <jni.h>

extern "C"
JNIEXPORT jstring JNICALL
Java_com_kesho_blend_CadNative_ping(JNIEnv *env, jclass) {
    return env->NewStringUTF("native ok from CI");
}
