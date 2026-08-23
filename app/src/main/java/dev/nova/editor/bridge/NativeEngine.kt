package dev.nova.editor.bridge

/**
 * JNI facade for the native Nova engine (libnovaengine.so).
 * All functions except [nativeGetVersion] must be called on the GL thread.
 */
object NativeEngine {

    init {
        System.loadLibrary("novaengine")
    }

    external fun nativeGetVersion(): String

    /** Creates an Engine instance; returns an opaque handle (0 on failure). */
    external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)

    external fun nativeSurfaceCreated(handle: Long)
    external fun nativeSurfaceChanged(handle: Long, width: Int, height: Int)
    external fun nativeDrawFrame(handle: Long)

    /** Pushes the serialized flat render scene (see RenderScene in scene/Model.kt). */
    external fun nativeSetScene(handle: Long, json: String)

    external fun nativeSetViewport(handle: Long, centerX: Float, centerY: Float, pixelsPerUnit: Float)
    external fun nativeSetGridVisible(handle: Long, visible: Boolean)

    /** Uploads an RGBA8888 texture keyed by [key] (usually the project-relative path). */
    external fun nativeLoadTexture(handle: Long, key: String, rgba: ByteArray, width: Int, height: Int)
    external fun nativeRemoveTexture(handle: Long, key: String)

    // ---- Simulation (Play mode / runtime) ----
    external fun nativeStartSimulation(handle: Long)
    external fun nativeStopSimulation(handle: Long)
    external fun nativeStepSimulation(handle: Long, dt: Float)
    external fun nativeSnapshotPositions(handle: Long): String

    // ---- Scripting ----
    external fun nativeLoadScript(handle: Long, name: String, source: String)

    // ---- Events / profiling (drain per frame on the GL thread) ----
    external fun nativeConsumeSoundEvents(handle: Long): String
    external fun nativeConsumeLogs(handle: Long): String
    external fun nativeGetStats(handle: Long): String

    // ---- Game view / runtime ----
    external fun nativeSetUseGameCamera(handle: Long, use: Boolean)
    external fun nativeSetShowGameCamera(handle: Long, show: Boolean)
    external fun nativeSetShowPhysicsDebug(handle: Long, show: Boolean)

    // ---- Input (runtime) ----
    external fun nativeSetInputAxis(handle: Long, x: Float, y: Float)
    external fun nativeSetInputJump(handle: Long, pressed: Boolean)
}
