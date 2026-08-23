package dev.nova.editor.ui.editor

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.opengl.GLSurfaceView
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
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
import dev.nova.editor.editor.EditorTool
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer

private enum class GestureMode { NONE, ENTITY_DRAG, CAMERA_PAN, CAMERA_ZOOM }

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
    var viewportSizePx by remember { mutableStateOf(Offset.Zero) }

    // Push editor state into the native engine whenever it changes.
    LaunchedEffect(viewModel.renderRevision) {
        renderer.submitScene(viewModel.renderSceneJson())
    }
    LaunchedEffect(viewModel.camera) {
        val cam = viewModel.camera
        renderer.submitViewport(cam.centerX, cam.centerY, cam.pixelsPerUnit)
    }
    LaunchedEffect(viewModel.gridVisible) {
        renderer.submitGridVisible(viewModel.gridVisible)
    }
    LaunchedEffect(viewModel.textureRevision) {
        for (key in viewModel.requiredTextures) {
            loadAndSubmitTexture(viewModel, renderer, key)
        }
    }

    AndroidView(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { viewportSizePx = Offset(it.width.toFloat(), it.height.toFloat()) }
            .pointerInput(viewModel.activeTool) {
                handleViewportGestures(viewModel) { viewportSizePx }
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
                        viewModel.select(viewModel.pickAt(world.x, world.y))
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
                    mode = when (viewModel.activeTool) {
                        EditorTool.PAN -> GestureMode.CAMERA_PAN
                        EditorTool.ZOOM -> GestureMode.CAMERA_ZOOM
                        EditorTool.SELECT, EditorTool.MOVE -> {
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
                            viewModel.moveEntityBy(id, delta.x / ppu, -delta.y / ppu)
                        }
                        GestureMode.CAMERA_PAN ->
                            viewModel.updateCamera(viewModel.camera.panByScreen(delta.x, delta.y))
                        GestureMode.CAMERA_ZOOM -> {
                            // Vertical drag zooms: up = in, down = out.
                            val factor = 1f + (-delta.y) / 600f
                            if (factor > 0f) {
                                val size = viewportSizePx()
                                viewModel.updateCamera(
                                    viewModel.camera.zoomAt(factor, position.x, position.y, size.x, size.y),
                                )
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
