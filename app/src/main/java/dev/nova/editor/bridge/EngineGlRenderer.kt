package dev.nova.editor.bridge

import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Bridges GLSurfaceView callbacks to the native engine and re-applies the
 * latest scene/viewport/textures after (re)creation of the GL context.
 */
class EngineGlRenderer : GLSurfaceView.Renderer {

    @Volatile
    var handle: Long = 0
        private set

    private var glView: GLSurfaceView? = null

    // Latest editor state, re-applied when the GL context is (re)created.
    @Volatile private var pendingSceneJson: String? = null
    @Volatile private var viewportCenterX = 0f
    @Volatile private var viewportCenterY = 0f
    @Volatile private var viewportPpu = 100f
    @Volatile private var gridVisible = true

    class TextureData(val rgba: ByteArray, val width: Int, val height: Int)
    private val textures = LinkedHashMap<String, TextureData>()

    fun attach(view: GLSurfaceView) {
        glView = view
    }

    /** Runs [block] on the GL thread with the engine handle, if available. */
    fun queue(block: (Long) -> Unit) {
        val view = glView ?: return
        view.queueEvent {
            val h = handle
            if (h != 0L) block(h)
        }
    }

    /**
     * Runs [block] on the GL thread and waits (up to 100 ms) for its result.
     * For draining native events/stats — never call from the GL thread itself.
     */
    fun <T> callBlocking(block: (Long) -> T): T? {
        val view = glView ?: return null
        val latch = java.util.concurrent.CountDownLatch(1)
        var result: T? = null
        view.queueEvent {
            val h = handle
            if (h != 0L) result = block(h)
            latch.countDown()
        }
        latch.await(100, java.util.concurrent.TimeUnit.MILLISECONDS)
        return result
    }

    fun submitScene(json: String) {
        pendingSceneJson = json
        queue { NativeEngine.nativeSetScene(it, json) }
    }

    fun submitViewport(centerX: Float, centerY: Float, pixelsPerUnit: Float) {
        viewportCenterX = centerX
        viewportCenterY = centerY
        viewportPpu = pixelsPerUnit
        queue { NativeEngine.nativeSetViewport(it, centerX, centerY, pixelsPerUnit) }
    }

    // 3D orbit camera state, re-applied after GL (re)creation.
    @Volatile private var cam3dYaw = 45f
    @Volatile private var cam3dPitch = 30f
    @Volatile private var cam3dDist = 12f
    @Volatile private var cam3dTx = 0f
    @Volatile private var cam3dTy = 0.5f
    @Volatile private var cam3dTz = 0f
    @Volatile private var cam3dFov = 50f

    fun submitViewport3D(yaw: Float, pitch: Float, distance: Float,
                         tx: Float, ty: Float, tz: Float, fov: Float) {
        cam3dYaw = yaw; cam3dPitch = pitch; cam3dDist = distance
        cam3dTx = tx; cam3dTy = ty; cam3dTz = tz; cam3dFov = fov
        queue { NativeEngine.nativeSetViewport3D(it, yaw, pitch, distance, tx, ty, tz, fov) }
    }

    fun submitGridVisible(visible: Boolean) {
        gridVisible = visible
        queue { NativeEngine.nativeSetGridVisible(it, visible) }
    }

    /** Re-applies runtime flags (game camera, physics debug) after GL (re)creation. */
    @Volatile private var useGameCamera = false
    @Volatile private var showPhysicsDebug = false

    fun submitUseGameCamera(use: Boolean) {
        useGameCamera = use
        queue { NativeEngine.nativeSetUseGameCamera(it, use) }
    }

    fun submitShowPhysicsDebug(show: Boolean) {
        showPhysicsDebug = show
        queue { NativeEngine.nativeSetShowPhysicsDebug(it, show) }
    }

    fun submitTexture(key: String, data: TextureData) {
        synchronized(textures) { textures[key] = data }
        queue { NativeEngine.nativeLoadTexture(it, key, data.rgba, data.width, data.height) }
    }

    fun removeTexture(key: String) {
        synchronized(textures) { textures.remove(key) }
        queue { NativeEngine.nativeRemoveTexture(it, key) }
    }

    override fun onSurfaceCreated(unused: GL10?, config: EGLConfig?) {
        val h = NativeEngine.nativeCreate()
        handle = h
        NativeEngine.nativeSurfaceCreated(h)
        // Re-apply editor state (covers GL context loss / surface recreation).
        NativeEngine.nativeSetGridVisible(h, gridVisible)
        NativeEngine.nativeSetViewport(h, viewportCenterX, viewportCenterY, viewportPpu)
        NativeEngine.nativeSetViewport3D(h, cam3dYaw, cam3dPitch, cam3dDist, cam3dTx, cam3dTy, cam3dTz, cam3dFov)
        NativeEngine.nativeSetUseGameCamera(h, useGameCamera)
        NativeEngine.nativeSetShowPhysicsDebug(h, showPhysicsDebug)
        synchronized(textures) {
            for ((key, data) in textures) {
                NativeEngine.nativeLoadTexture(h, key, data.rgba, data.width, data.height)
            }
        }
        pendingSceneJson?.let { NativeEngine.nativeSetScene(h, it) }
    }

    override fun onSurfaceChanged(unused: GL10?, width: Int, height: Int) {
        val h = handle
        if (h != 0L) NativeEngine.nativeSurfaceChanged(h, width, height)
    }

    override fun onDrawFrame(unused: GL10?) {
        val h = handle
        if (h != 0L) NativeEngine.nativeDrawFrame(h)
    }

    fun destroy() {
        queue { h ->
            NativeEngine.nativeDestroy(h)
        }
        handle = 0
    }
}
