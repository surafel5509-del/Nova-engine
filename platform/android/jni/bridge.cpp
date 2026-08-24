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
Java_dev_nova_editor_bridge_NativeEngine_nativeSetViewport3D(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle,
    jfloat yawDeg, jfloat pitchDeg, jfloat distance,
    jfloat targetX, jfloat targetY, jfloat targetZ, jfloat fovDeg) {
    if (auto* e = fromHandle(handle)) {
        e->setViewport3D(yawDeg, pitchDeg, distance, targetX, targetY, targetZ, fovDeg);
    }
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

// ---- Simulation ----

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeStartSimulation(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) e->startSimulation();
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeStopSimulation(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) e->stopSimulation();
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeStepSimulation(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jfloat dt) {
    if (auto* e = fromHandle(handle)) e->stepSimulation(dt);
}

JNIEXPORT jstring JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSnapshotPositions(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) {
        return env->NewStringUTF(e->snapshotPositionsJson().c_str());
    }
    return env->NewStringUTF("{}");
}

// ---- Game view / runtime ----

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetUseGameCamera(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jboolean use) {
    if (auto* e = fromHandle(handle)) e->setUseGameCamera(use == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetShowGameCamera(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jboolean show) {
    if (auto* e = fromHandle(handle)) e->setShowGameCamera(show == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetShowPhysicsDebug(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jboolean show) {
    if (auto* e = fromHandle(handle)) e->setShowPhysicsDebug(show == JNI_TRUE);
}

// ---- Input ----

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetInputAxis(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jfloat x, jfloat y) {
    if (auto* e = fromHandle(handle)) e->setInputAxis(x, y);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeSetInputJump(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jboolean pressed) {
    if (auto* e = fromHandle(handle)) e->setInputJump(pressed == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeLoadScript(JNIEnv* env, jobject /*thiz*/, jlong handle, jstring name, jstring source) {
    if (auto* e = fromHandle(handle)) {
        if (name == nullptr || source == nullptr) return;
        const char* n = env->GetStringUTFChars(name, nullptr);
        const char* s = env->GetStringUTFChars(source, nullptr);
        if (n != nullptr && s != nullptr) {
            e->loadScript(n, s);
        }
        if (n != nullptr) env->ReleaseStringUTFChars(name, n);
        if (s != nullptr) env->ReleaseStringUTFChars(source, s);
    }
}

JNIEXPORT jstring JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeConsumeSoundEvents(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) {
        return env->NewStringUTF(e->consumeSoundEventsJson().c_str());
    }
    return env->NewStringUTF("[]");
}

JNIEXPORT jstring JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeConsumeLogs(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) {
        return env->NewStringUTF(e->consumeLogsJson().c_str());
    }
    return env->NewStringUTF("[]");
}

JNIEXPORT jstring JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeGetStats(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) {
        return env->NewStringUTF(e->statsJson().c_str());
    }
    return env->NewStringUTF("{}");
}

JNIEXPORT jstring JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeConsumeUiTextEvents(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    if (auto* e = fromHandle(handle)) {
        return env->NewStringUTF(e->consumeUiTextEventsJson().c_str());
    }
    return env->NewStringUTF("[]");
}

JNIEXPORT void JNICALL
Java_dev_nova_editor_bridge_NativeEngine_nativeOnTap(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jfloat worldX, jfloat worldY) {
    if (auto* e = fromHandle(handle)) e->onTap(worldX, worldY);
}

} // extern "C"
