#include <jni.h>

#include <cstdint>
#include <memory>
#include <string>

#include "core/Engine.h"
#include "core/Log.h"

namespace {

nova::Engine* fromHandle(jlong handle) {
    return reinterpret_cast<nova::Engine*>(static_cast<uintptr_t>(handle));
}

} // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeGetVersion(JNIEnv* env, jobject /*thiz*/) {
    return env->NewStringUTF("NovaEngine 0.1.0 (Phase 1, GLES3)");
}

JNIEXPORT jlong JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeCreate(JNIEnv* /*env*/, jobject /*thiz*/) {
    auto* engine = new (std::nothrow) nova::Engine();
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(engine));
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeDestroy(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    delete fromHandle(handle);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSurfaceCreated(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) e->onSurfaceCreated();
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSurfaceChanged(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jint width, jint height) {
    if (auto* e = fromHandle(handle)) e->onSurfaceChanged(width, height);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeDrawFrame(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) e->onDrawFrame();
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetScene(JNIEnv* env, jobject /*thiz*/, jlong handle, jstring json) {
    if (auto* e = fromHandle(handle)) {
        if (json == nullptr) return;
        const char* chars = env->GetStringUTFChars(json, nullptr);
        if (chars != nullptr) {
            e->setSceneJson(chars);
            env->ReleaseStringUTFChars(json, chars);
        }
    }
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetViewport(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle,
                                                           jfloat centerX, jfloat centerY, jfloat pixelsPerUnit) {
    if (auto* e = fromHandle(handle)) e->setViewport(centerX, centerY, pixelsPerUnit);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetGridVisible(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jboolean visible) {
    if (auto* e = fromHandle(handle)) e->setGridVisible(visible == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeLoadTexture(JNIEnv* env, jobject /*thiz*/, jlong handle,
                                                           jstring key, jbyteArray rgba, jint width, jint height) {
    auto* e = fromHandle(handle);
    if (e == nullptr || key == nullptr || rgba == nullptr) return;

    const jsize length = env->GetArrayLength(rgba);
    if (length != width * height * 4) {
        LOGE("nativeLoadTexture: expected %d bytes, got %d", width * height * 4, length);
        return;
    }
    jbyte* data = env->GetByteArrayElements(rgba, nullptr);
    if (data == nullptr) return;

    const char* keyChars = env->GetStringUTFChars(key, nullptr);
    if (keyChars != nullptr) {
        e->loadTexture(keyChars, reinterpret_cast<const unsigned char*>(data), width, height);
        env->ReleaseStringUTFChars(key, keyChars);
    }
    env->ReleaseByteArrayElements(rgba, data, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeRemoveTexture(JNIEnv* env, jobject /*thiz*/, jlong handle, jstring key) {
    if (auto* e = fromHandle(handle)) {
        if (key == nullptr) return;
        const char* keyChars = env->GetStringUTFChars(key, nullptr);
        if (keyChars != nullptr) {
            e->removeTexture(keyChars);
            env->ReleaseStringUTFChars(key, keyChars);
        }
    }
}

} // extern "C"
