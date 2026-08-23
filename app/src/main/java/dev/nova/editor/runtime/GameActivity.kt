package dev.nova.editor.runtime

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.scene.SceneJson
import dev.nova.editor.scene.buildRenderScene
import kotlinx.serialization.encodeToString
import java.io.File
import java.nio.ByteBuffer

/**
 * Standalone game runtime: loads a project's scene and runs it full-screen
 * with the game camera, physics, and animation. Launched from the editor.
 */
class GameActivity : ComponentActivity() {

    private var surfaceView: GameSurfaceView? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val projectPath = intent.getStringExtra(EXTRA_PROJECT_PATH)
        val (sceneJson, textureKeys) = rememberScene(projectPath)

        setContent {
            Surface(Modifier.fillMaxSize(), color = Color.Black) {
                Box(Modifier.fillMaxSize()) {
                    AndroidView(
                        factory = { ctx ->
                            GameSurfaceView(ctx).also { view ->
                                surfaceView = view
                                view.setScene(sceneJson)
                                loadTextures(view, projectPath, textureKeys)
                            }
                        },
                        modifier = Modifier.fillMaxSize(),
                    )
                    TextButton(
                        onClick = { finish() },
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .padding(8.dp),
                    ) {
                        Text("Exit", color = Color.White, style = MaterialTheme.typography.labelLarge)
                    }
                }
            }
        }
    }

    private fun rememberScene(projectPath: String?): Pair<String, Set<String>> {
        if (projectPath == null) return "{}" to emptySet()
        return runCatching {
            val repo = ProjectRepository(File(projectPath))
            val (_, scene) = repo.loadProjectScene(projectPath)
            val render = buildRenderScene(scene, selectedId = null)
            val keys = scene.entities.mapNotNull { it.sprite?.texturePath }.toSet()
            SceneJson.encodeToString(render) to keys
        }.getOrElse { "{}" to emptySet() }
    }

    private fun loadTextures(view: GameSurfaceView, projectPath: String?, keys: Set<String>) {
        if (projectPath == null) return
        for (key in keys) {
            runCatching {
                val bytes = File(projectPath, key).readBytes()
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

    override fun onDestroy() {
        surfaceView?.release()
        surfaceView = null
        super.onDestroy()
    }

    companion object {
        const val EXTRA_PROJECT_PATH = "dev.nova.editor.extra.PROJECT_PATH"
    }
}
