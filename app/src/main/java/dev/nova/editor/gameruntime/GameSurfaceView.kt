package dev.nova.editor.gameruntime

import android.content.Context
import android.opengl.GLSurfaceView
import android.view.MotionEvent
import dev.nova.editor.bridge.NativeEngine

/**
 * Full-screen game surface: runs the native engine with the game camera,
 * auto-starts the simulation, and maps touch to the input axis + jump.
 * Left half of the screen = move (horizontal by drag direction), tap right = jump.
 */
class GameSurfaceView(context: Context) : GLSurfaceView(context) {

    private var engineHandle: Long = 0
    private var sceneJson: String = "{}"
    private val textures = LinkedHashMap<String, TexturePayload>()
    private val scripts = LinkedHashMap<String, String>()

    /** Called (on the GL thread) when a script plays a sound. */
    var onSoundEvent: ((String) -> Unit)? = null

    class TexturePayload(val rgba: ByteArray, val width: Int, val height: Int)

    private val renderer = object : Renderer {
        override fun onSurfaceCreated(unused: javax.microedition.khronos.opengles.GL10?, config: javax.microedition.khronos.egl.EGLConfig?) {
            engineHandle = NativeEngine.nativeCreate()
            NativeEngine.nativeSurfaceCreated(engineHandle)
            NativeEngine.nativeSetUseGameCamera(engineHandle, true)
            NativeEngine.nativeSetShowGameCamera(engineHandle, false)
            NativeEngine.nativeSetGridVisible(engineHandle, false)
            synchronized(textures) {
                for ((key, t) in textures) {
                    NativeEngine.nativeLoadTexture(engineHandle, key, t.rgba, t.width, t.height)
                }
            }
            NativeEngine.nativeSetScene(engineHandle, sceneJson)
            synchronized(scripts) {
                for ((name, source) in scripts) {
                    NativeEngine.nativeLoadScript(engineHandle, name, source)
                }
            }
            NativeEngine.nativeStartSimulation(engineHandle)
        }

        override fun onSurfaceChanged(unused: javax.microedition.khronos.opengles.GL10?, width: Int, height: Int) {
            if (engineHandle != 0L) NativeEngine.nativeSurfaceChanged(engineHandle, width, height)
        }

        private var last = System.nanoTime()
        override fun onDrawFrame(unused: javax.microedition.khronos.opengles.GL10?) {
            if (engineHandle == 0L) return
            val now = System.nanoTime()
            val dt = ((now - last) / 1_000_000_000f).coerceAtMost(0.05f)
            last = now
            NativeEngine.nativeStepSimulation(engineHandle, dt)
            // Drain script-triggered sounds.
            val events = NativeEngine.nativeConsumeSoundEvents(engineHandle)
            if (events.length > 2) {
                parsePaths(events).forEach { onSoundEvent?.invoke(it) }
            }
            NativeEngine.nativeDrawFrame(engineHandle)
        }

        private fun parsePaths(json: String): List<String> = runCatching {
            val array = org.json.JSONArray(json)
            (0 until array.length()).map { array.getString(it) }
        }.getOrDefault(emptyList())
    }

    init {
        setEGLContextClientVersion(3)
        preserveEGLContextOnPause = true
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun setScene(json: String) {
        sceneJson = json
        queueEvent { if (engineHandle != 0L) NativeEngine.nativeSetScene(engineHandle, json) }
    }

    fun addTexture(key: String, rgba: ByteArray, width: Int, height: Int) {
        synchronized(textures) { textures[key] = TexturePayload(rgba, width, height) }
        queueEvent {
            if (engineHandle != 0L) NativeEngine.nativeLoadTexture(engineHandle, key, rgba, width, height)
        }
    }

    /** Registers a Lua script source (before surface creation or live). */
    fun addScript(name: String, source: String) {
        synchronized(scripts) { scripts[name] = source }
        queueEvent {
            if (engineHandle != 0L) NativeEngine.nativeLoadScript(engineHandle, name, source)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val half = width / 2f
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (event.x < half) {
                    // Left side: held to move. Direction set by subsequent move events.
                    setAxis(if (event.x < half / 2f) -1f else 1f)
                } else {
                    setJump(true)
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (event.x < half) setAxis(if (event.x < half / 2f) -1f else 1f)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                setAxis(0f)
                setJump(false)
            }
        }
        return true
    }

    private fun setAxis(x: Float) {
        queueEvent { if (engineHandle != 0L) NativeEngine.nativeSetInputAxis(engineHandle, x, 0f) }
    }

    private fun setJump(pressed: Boolean) {
        queueEvent { if (engineHandle != 0L) NativeEngine.nativeSetInputJump(engineHandle, pressed) }
    }

    fun release() {
        queueEvent { if (engineHandle != 0L) NativeEngine.nativeDestroy(engineHandle) }
        engineHandle = 0
    }
}
