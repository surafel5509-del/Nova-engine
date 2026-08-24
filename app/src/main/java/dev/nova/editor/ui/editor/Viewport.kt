package dev.nova.editor.ui.editor

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.opengl.GLSurfaceView
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.viewinterop.AndroidView
import dev.nova.editor.bridge.EngineGlRenderer
import dev.nova.editor.bridge.NativeEngine
import dev.nova.editor.editor.EditorTool
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.EngineStats
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.editor.PlayState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlinx.coroutines.isActive
import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer

private enum class GestureMode { NONE, ENTITY_DRAG, CAMERA_PAN, CAMERA_ZOOM, TILE_PAINT }

/**
 * 2D scene viewport: native GLES surface + editor gesture layer.
 * Gestures: tap = select, drag = move entity / pan (per tool), pinch = zoom,
 * two-finger drag = pan + zoom (any tool).
 */
@Composable
fun Viewport(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val renderer = remember { EngineGlRenderer() }
    val audio = remember { dev.nova.editor.audio.AudioEngine(viewModel.projectPath) }
    DisposableEffect(Unit) {
        onDispose { audio.release() }
    }
    var viewportSizePx by remember { mutableStateOf(Offset.Zero) }

    // Push editor state into the native engine whenever it changes.
    LaunchedEffect(viewModel.renderRevision) {
        renderer.submitScene(viewModel.renderSceneJson())
    }
    LaunchedEffect(viewModel.camera) {
        val cam = viewModel.camera
        renderer.submitViewport(cam.centerX, cam.centerY, cam.pixelsPerUnit)
    }
    LaunchedEffect(viewModel.camera3d) {
        val c = viewModel.camera3d
        renderer.submitViewport3D(c.yaw, c.pitch, c.distance, c.targetX, c.targetY, c.targetZ, c.fov)
    }
    LaunchedEffect(viewModel.gridVisible) {
        renderer.submitGridVisible(viewModel.gridVisible)
    }
    LaunchedEffect(viewModel.textureRevision) {
        for (key in viewModel.requiredTextures) {
            loadAndSubmitTexture(viewModel, renderer, key)
        }
    }

    // UI text textures: re-render whenever the scene changes (undoable edits
    // bump renderRevision, which also re-pushes textures for ui:// keys).
    LaunchedEffect(viewModel.renderRevision) {
        for (entity in viewModel.scene.entities) {
            val ui = entity.ui ?: continue
            if (!entity.enabled || ui.text.isBlank()) continue
            val color = android.graphics.Color.argb(
                (ui.a * 255).toInt().coerceIn(0, 255),
                (ui.textR * 255).toInt().coerceIn(0, 255),
                (ui.textG * 255).toInt().coerceIn(0, 255),
                (ui.textB * 255).toInt().coerceIn(0, 255),
            )
            dev.nova.editor.ui.UiTextTexture.render(ui.text, ui.fontSizeSp * 4f, color)?.let { tex ->
                renderer.submitTexture(
                    "ui://text/${entity.id}",
                    EngineGlRenderer.TextureData(tex.rgba, tex.width, tex.height),
                )
            }
        }
    }

    // Game view + physics debug flags.
    LaunchedEffect(viewModel.gameView) {
        renderer.submitUseGameCamera(viewModel.gameView)
    }
    LaunchedEffect(viewModel.physicsDebug) {
        renderer.submitShowPhysicsDebug(viewModel.physicsDebug)
    }

    // Play mode: start/stop the native simulation and step it on a loop.
    // The engine owns the moving scene; physics writes positions back into the
    // render scene natively, so no Kotlin re-push is needed during play.
    LaunchedEffect(viewModel.playState) {
        when (viewModel.playState) {
            PlayState.PLAYING -> {
                val scripts = viewModel.loadScriptSources()
                // Push all script sources, then start the simulation.
                for ((name, source) in scripts) {
                    renderer.queue { h -> NativeEngine.nativeLoadScript(h, name, source) }
                }
                renderer.queue { NativeEngine.nativeStartSimulation(it) }
                // Autoplay audio sources.
                withContext(Dispatchers.Default) {
                    for (e in viewModel.scene.entities) {
                        val a = e.audioSource ?: continue
                        if (!e.enabled || !a.autoplay || a.audioPath == null) continue
                        if (a.music) audio.playMusic(a.audioPath, a.volume, a.loop)
                        else audio.play(a.audioPath, a.volume, a.pitch, a.loop)
                    }
                }
                var last = System.nanoTime()
                var statsAccum = 0f
                while (isActive && viewModel.playState == PlayState.PLAYING) {
                    val now = System.nanoTime()
                    val dt = ((now - last) / 1_000_000_000f).coerceAtMost(0.05f)
                    last = now
                    renderer.queue { NativeEngine.nativeStepSimulation(it, dt) }

                    // Drain script logs + sound events + stats off the GL thread.
                    withContext(Dispatchers.Default) {
                        val logsJson = renderer.callBlocking { h -> NativeEngine.nativeConsumeLogs(h) }
                        parseStringArray(logsJson).forEach { msg ->
                            viewModel.log(LogLevel.INFO, msg)
                        }
                        val soundsJson = renderer.callBlocking { h -> NativeEngine.nativeConsumeSoundEvents(h) }
                        for (path in parseStringArray(soundsJson)) {
                            audio.play(path)
                        }
                        // Script-driven UI text updates: re-render the texture.
                        val uiTextJson = renderer.callBlocking { h -> NativeEngine.nativeConsumeUiTextEvents(h) }
                        for ((id, text) in parseUiTextEvents(uiTextJson)) {
                            val color = android.graphics.Color.WHITE
                            dev.nova.editor.ui.UiTextTexture.render(text, 64f, color)?.let { tex ->
                                renderer.submitTexture(
                                    "ui://text/$id",
                                    EngineGlRenderer.TextureData(tex.rgba, tex.width, tex.height),
                                )
                            }
                        }
                        statsAccum += dt
                        if (statsAccum >= 0.5f) {
                            statsAccum = 0f
                            val statsJson = renderer.callBlocking { h -> NativeEngine.nativeGetStats(h) }
                            parseStats(statsJson)?.let(viewModel::updateStats)
                        }
                    }
                    delay(16)
                }
            }
            PlayState.PAUSED -> Unit
            PlayState.STOPPED -> {
                withContext(Dispatchers.Default) { audio.stopAll() }
                renderer.queue { NativeEngine.nativeStopSimulation(it) }
            }
        }
    }

    AndroidView(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { viewportSizePx = Offset(it.width.toFloat(), it.height.toFloat()) }
            .pointerInput(viewModel.activeTool) {
                handleViewportGestures(viewModel, renderer) { viewportSizePx }
            },
        factory = { context ->
            GLSurfaceView(context).apply {
                setEGLContextClientVersion(3)
                preserveEGLContextOnPause = true
                setRenderer(renderer)
                renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
                renderer.attach(this)
            }
        },
    )
}

private suspend fun PointerInputScope.handleViewportGestures(
    viewModel: EditorViewModel,
    rendererRef: EngineGlRenderer?,
    viewportSizePx: () -> Offset,
) {
    fun screenToWorld(pos: Offset): Offset {
        val size = viewportSizePx()
        val cam = viewModel.camera
        return Offset(cam.screenToWorldX(pos.x, size.x), cam.screenToWorldY(pos.y, size.y))
    }

    while (true) {
        val down = awaitPointerEventScope { awaitFirstDown(requireUnconsumed = false) }
        val startPos = down.position
        var mode = GestureMode.NONE
        var draggedEntityId: String? = null
        var sawSecondPointer = false
        var moved = false
        var prevPinchDistance = 0f
        var prevCentroid = Offset.Zero

        awaitPointerEventScope {
            while (true) {
                val event = awaitPointerEvent()
                val pressed = event.changes.filter { it.pressed }

                if (pressed.isEmpty()) {
                    if (!moved && !sawSecondPointer) {
                        val world = screenToWorld(startPos)
                        if (viewModel.is3D) {
                            // 3D: tap picks the nearest 3D object via camera ray.
                            val size = viewportSizePx()
                            val nx = (startPos.x / size.x) * 2f - 1f
                            val ny = 1f - (startPos.y / size.y) * 2f
                            val aspect = if (size.y > 0) size.x / size.y else 1f
                            viewModel.select(viewModel.pick3D(nx, ny, aspect))
                        } else if (viewModel.playState == PlayState.PLAYING) {
                            // During play, taps hit-test UI elements for scripts.
                            rendererRef?.queue { h -> NativeEngine.nativeOnTap(h, world.x, world.y) }
                        } else if (viewModel.activeTool == EditorTool.TILE) {
                            viewModel.activeTilemapId()?.let { mapId ->
                                viewModel.paintTileAt(mapId, world.x, world.y, viewModel.tileBrush)
                            }
                        } else {
                            viewModel.select(viewModel.pickAt(world.x, world.y))
                        }
                    }
                    if (mode == GestureMode.ENTITY_DRAG) draggedEntityId?.let(viewModel::endEntityDrag)
                    break
                }

                if (pressed.size >= 2) {
                    // Two-finger: pinch zoom + pan (any tool).
                    val p1 = pressed[0].position
                    val p2 = pressed[1].position
                    val centroid = Offset((p1.x + p2.x) / 2f, (p1.y + p2.y) / 2f)
                    val distance = (p1 - p2).getDistance()
                    if (!sawSecondPointer) {
                        sawSecondPointer = true
                        moved = true
                        if (mode == GestureMode.ENTITY_DRAG) {
                            draggedEntityId?.let(viewModel::endEntityDrag)
                            mode = GestureMode.NONE
                        }
                    } else {
                        if (viewModel.is3D) {
                            // 3D: pinch = dolly, two-finger drag = pan target.
                            if (prevPinchDistance > 0f && distance > 0f) {
                                viewModel.updateCamera3D(viewModel.camera3d.zoom(distance / prevPinchDistance))
                            }
                            val panDelta = centroid - prevCentroid
                            if (panDelta != Offset.Zero) {
                                viewModel.updateCamera3D(viewModel.camera3d.pan(panDelta.x, panDelta.y))
                            }
                        } else {
                            if (prevPinchDistance > 0f && distance > 0f) {
                                val size = viewportSizePx()
                                viewModel.updateCamera(
                                    viewModel.camera.zoomAt(
                                        distance / prevPinchDistance,
                                        centroid.x, centroid.y, size.x, size.y,
                                    ),
                                )
                            }
                            val panDelta = centroid - prevCentroid
                            if (panDelta != Offset.Zero) {
                                viewModel.updateCamera(viewModel.camera.panByScreen(panDelta.x, panDelta.y))
                            }
                        }
                    }
                    prevPinchDistance = distance
                    prevCentroid = centroid
                    pressed.forEach { if (it.positionChanged()) it.consume() }
                    continue
                }

                val change = pressed[0]
                val position = change.position

                if (!moved && !sawSecondPointer &&
                    (position - startPos).getDistance() > viewConfiguration.touchSlop
                ) {
                    moved = true
                    mode = when {
                        viewModel.is3D -> GestureMode.CAMERA_PAN   // 1-finger: orbit
                        viewModel.activeTool == EditorTool.PAN -> GestureMode.CAMERA_PAN
                        viewModel.activeTool == EditorTool.ZOOM -> GestureMode.CAMERA_ZOOM
                        viewModel.activeTool == EditorTool.TILE -> GestureMode.TILE_PAINT
                        viewModel.activeTool == EditorTool.TILE_BRUSH -> GestureMode.TILE_PAINT
                        viewModel.activeTool == EditorTool.TILE_ERASER -> GestureMode.TILE_PAINT
                        viewModel.activeTool == EditorTool.TILE_PICKER -> GestureMode.TILE_PAINT
                        else -> {
                            val world = screenToWorld(startPos)
                            val hit = viewModel.pickAt(world.x, world.y)
                            if (hit != null) {
                                viewModel.select(hit)
                                draggedEntityId = hit
                                viewModel.beginEntityDrag(hit)
                                GestureMode.ENTITY_DRAG
                            } else {
                                GestureMode.CAMERA_PAN
                            }
                        }
                    }
                }

                if (moved && !sawSecondPointer) {
                    val delta = position - change.previousPosition
                    when (mode) {
                        GestureMode.ENTITY_DRAG -> draggedEntityId?.let { id ->
                            val ppu = viewModel.camera.pixelsPerUnit
                            when (viewModel.activeTool) {
                                EditorTool.ROTATE ->
                                    viewModel.rotateEntityBy(id, -delta.x * 0.4f)
                                EditorTool.SCALE -> {
                                    viewModel.scaleEntityBy(id, -delta.y / 200f)
                                }
                                EditorTool.RECT -> {
                                    viewModel.moveEntityBy(id, delta.x / ppu, -delta.y / ppu)
                                    viewModel.scaleEntityBy(id, -delta.y / 400f)
                                }
                                else ->
                                    viewModel.moveEntityBy(id, delta.x / ppu, -delta.y / ppu)
                            }
                        }
                        GestureMode.TILE_PAINT -> {
                            val world = screenToWorld(position)
                            viewModel.activeTilemapId()?.let { mapId ->
                                viewModel.paintTileAt(mapId, world.x, world.y, viewModel.tileBrush)
                            }
                        }
                        GestureMode.CAMERA_PAN -> {
                            if (viewModel.is3D) {
                                // 1-finger drag orbits the 3D camera.
                                viewModel.updateCamera3D(viewModel.camera3d.rotate(-delta.x * 0.3f, delta.y * 0.3f))
                            } else {
                                viewModel.updateCamera(viewModel.camera.panByScreen(delta.x, delta.y))
                            }
                        }
                        GestureMode.CAMERA_ZOOM -> {
                            if (viewModel.is3D) {
                                val factor = 1f + (-delta.y) / 600f
                                if (factor > 0f) viewModel.updateCamera3D(viewModel.camera3d.zoom(factor))
                            } else {
                                val factor = 1f + (-delta.y) / 600f
                                if (factor > 0f) {
                                    val size = viewportSizePx()
                                    viewModel.updateCamera(
                                        viewModel.camera.zoomAt(factor, position.x, position.y, size.x, size.y),
                                    )
                                }
                            }
                        }
                        GestureMode.NONE -> Unit
                    }
                }
                if (change.positionChanged()) change.consume()
            }
        }
    }
}

private suspend fun loadAndSubmitTexture(
    viewModel: EditorViewModel,
    renderer: EngineGlRenderer,
    key: String,
) {
    val decoded = withContext(Dispatchers.IO) {
        val bytes = viewModel.readTextureBytes(key) ?: return@withContext null
        val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return@withContext null
        val argb = if (bitmap.config != Bitmap.Config.ARGB_8888) {
            bitmap.copy(Bitmap.Config.ARGB_8888, false)
        } else {
            bitmap
        }
        val buffer = ByteBuffer.allocate(argb.byteCount)
        argb.copyPixelsToBuffer(buffer)
        // ARGB_8888 in a ByteBuffer is stored as R,G,B,A byte order — matches GL_RGBA.
        EngineGlRenderer.TextureData(buffer.array(), argb.width, argb.height)
    }
    if (decoded != null) {
        renderer.submitTexture(key, decoded)
    } else {
        viewModel.log(LogLevel.WARNING, "Could not decode texture '$key'")
    }
}

private fun parseStringArray(json: String?): List<String> {
    if (json.isNullOrBlank()) return emptyList()
    return runCatching {
        val array = JSONArray(json)
        (0 until array.length()).map { array.getString(it) }
    }.getOrDefault(emptyList())
}

private fun parseUiTextEvents(json: String?): List<Pair<String, String>> {
    if (json.isNullOrBlank()) return emptyList()
    return runCatching {
        val array = JSONArray(json)
        (0 until array.length()).map {
            val obj = array.getJSONObject(it)
            obj.getString("id") to obj.getString("text")
        }
    }.getOrDefault(emptyList())
}

private fun parseStats(json: String?): EngineStats? {
    if (json.isNullOrBlank()) return null
    return runCatching {
        val obj = JSONObject(json)
        EngineStats(
            fps = obj.optDouble("fps", 0.0).toFloat(),
            frameMs = obj.optDouble("frameMs", 0.0).toFloat(),
            drawCalls = obj.optInt("drawCalls", 0),
            sprites = obj.optInt("sprites", 0),
            bodies = obj.optInt("bodies", 0),
            particles = obj.optInt("particles", 0),
            scripts = obj.optInt("scripts", 0),
        )
    }.getOrNull()
}
