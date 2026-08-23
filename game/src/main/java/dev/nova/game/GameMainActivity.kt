package dev.nova.game

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import android.view.KeyEvent
import dev.nova.editor.audio.AudioEngine
import dev.nova.editor.gameruntime.GameSurfaceView
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneJson
import dev.nova.editor.scene.buildRenderScene
import dev.nova.editor.scene.deserializeScene
import kotlinx.serialization.encodeToString
import java.io.File
import java.nio.ByteBuffer

/**
 * Standalone game: loads the packaged project's scene, textures, scripts,
 * and audio from APK assets, then runs full-screen. Touch = left half move
 * axis, right half jump. Back button exits.
 */
class GameMainActivity : Activity() {

    private var surface: GameSurfaceView? = null
    private var audio: AudioEngine? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Extract project to cache so file-based audio playback has a path.
        val cacheRoot = File(cacheDir, "project")
        extractAssetTree("project", cacheRoot)
        audio = AudioEngine(cacheRoot.absolutePath)

        val view = GameSurfaceView(this)
        surface = view
        view.onSoundEvent = { path -> audio?.play(path) }
        setContentView(view)

        val scene = loadScene() ?: return
        val render = buildRenderScene(scene, selectedId = null)
        view.setScene(SceneJson.encodeToString(render))
        loadTexturesFor(view, scene)
        loadScriptsFor(view, scene)
        startAutoplay(scene)
    }

    private fun loadScene(): Scene? = runCatching {
        deserializeScene(assets.open("project/scenes/main.scene.json").readBytes().decodeToString())
    }.getOrNull()

    private fun loadTexturesFor(view: GameSurfaceView, scene: Scene) {
        val keys = LinkedHashSet<String>()
        for (e in scene.entities) {
            e.sprite?.texturePath?.let(keys::add)
            e.tilemap?.tilesetPath?.let(keys::add)
        }
        for (key in keys) {
            runCatching {
                val bytes = assets.open("project/$key").readBytes()
                val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return@runCatching
                val argb = if (bitmap.config != Bitmap.Config.ARGB_8888) {
                    bitmap.copy(Bitmap.Config.ARGB_8888, false)
                } else bitmap
                val buffer = ByteBuffer.allocate(argb.byteCount)
                argb.copyPixelsToBuffer(buffer)
                view.addTexture(key, buffer.array(), argb.width, argb.height)
            }
        }
    }

    private fun loadScriptsFor(view: GameSurfaceView, scene: Scene) {
        for (e in scene.entities) {
            val path = e.script?.scriptPath ?: continue
            runCatching {
                view.addScript(path, assets.open("project/$path").readBytes().decodeToString())
            }
        }
    }

    private fun startAutoplay(scene: Scene) {
        for (e in scene.entities) {
            val a = e.audioSource ?: continue
            if (!e.enabled || !a.autoplay || a.audioPath == null) continue
            if (a.music) audio?.playMusic(a.audioPath, a.volume, a.loop)
            else audio?.play(a.audioPath, a.volume, a.pitch, a.loop)
        }
    }

    /** Copies an assets sub-tree to [target] on disk (idempotent, cheap). */
    private fun extractAssetTree(assetPath: String, target: File) {
        val names = assets.list(assetPath) ?: return
        if (names.isEmpty()) {
            target.parentFile?.mkdirs()
            assets.open(assetPath).use { input -> target.outputStream().use { input.copyTo(it) } }
            return
        }
        target.mkdirs()
        for (name in names) {
            extractAssetTree("$assetPath/$name", File(target, name))
        }
    }

    override fun onDestroy() {
        surface?.release()
        surface = null
        audio?.release()
        audio = null
        super.onDestroy()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            finish()
            return true
        }
        return super.onKeyDown(keyCode, event)
    }
}
