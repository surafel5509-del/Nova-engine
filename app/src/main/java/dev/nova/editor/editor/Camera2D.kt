package dev.nova.editor.editor

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * Editor 2D camera. [centerX]/[centerY] are world-space focus,
 * [pixelsPerUnit] is zoom (screen px per world unit). Screen Y is down,
 * world Y is up.
 */
data class Camera2D(
    val centerX: Float = 0f,
    val centerY: Float = 0f,
    val pixelsPerUnit: Float = 100f,
) {
    fun worldToScreenX(wx: Float, viewportW: Float): Float =
        viewportW / 2f + (wx - centerX) * pixelsPerUnit

    fun worldToScreenY(wy: Float, viewportH: Float): Float =
        viewportH / 2f - (wy - centerY) * pixelsPerUnit

    fun screenToWorldX(sx: Float, viewportW: Float): Float =
        centerX + (sx - viewportW / 2f) / pixelsPerUnit

    fun screenToWorldY(sy: Float, viewportH: Float): Float =
        centerY - (sy - viewportH / 2f) / pixelsPerUnit

    fun panByScreen(dxPx: Float, dyPx: Float): Camera2D =
        copy(centerX = centerX - dxPx / pixelsPerUnit, centerY = centerY + dyPx / pixelsPerUnit)

    fun zoomAt(factor: Float, focusSx: Float, focusSy: Float, viewportW: Float, viewportH: Float): Camera2D {
        val newPpu = (pixelsPerUnit * factor).coerceIn(MIN_PPU, MAX_PPU)
        if (newPpu == pixelsPerUnit) return this
        // Keep the world point under the focus fixed on screen.
        val wx = screenToWorldX(focusSx, viewportW)
        val wy = screenToWorldY(focusSy, viewportH)
        val newCenterX = wx - (focusSx - viewportW / 2f) / newPpu
        val newCenterY = wy + (focusSy - viewportH / 2f) / newPpu
        return Camera2D(newCenterX, newCenterY, newPpu)
    }

    companion object {
        const val MIN_PPU = 4f
        const val MAX_PPU = 4000f

        /** Snap [value] to the nearest multiple of [step] when enabled. */
        fun snap(value: Float, step: Float, enabled: Boolean): Float =
            if (enabled && step > 0f) Math.round(value / step) * step else value
    }
}

/** Point-in-entity hit test in world space, honoring rotation. */
fun hitTestSprite(
    worldX: Float, worldY: Float,
    entityX: Float, entityY: Float, rotationDeg: Float,
    halfW: Float, halfH: Float,
): Boolean {
    val rad = Math.toRadians(-rotationDeg.toDouble())
    val cos = cos(rad).toFloat()
    val sin = sin(rad).toFloat()
    val dx = worldX - entityX
    val dy = worldY - entityY
    val localX = dx * cos - dy * sin
    val localY = dx * sin + dy * cos
    return abs(localX) <= halfW && abs(localY) <= halfH
}
