package dev.nova.editor.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class Camera2DTest {

    @Test
    fun `world and screen conversions are inverses`() {
        val camera = Camera2D(centerX = 3f, centerY = -2f, pixelsPerUnit = 50f)
        val w = 800f
        val h = 600f

        val wx = 4.2f
        val wy = -1.7f
        val sx = camera.worldToScreenX(wx, w)
        val sy = camera.worldToScreenY(wy, h)

        assertEquals(wx, camera.screenToWorldX(sx, w), 1e-4f)
        assertEquals(wy, camera.screenToWorldY(sy, h), 1e-4f)
    }

    @Test
    fun `screen y is flipped relative to world y`() {
        val camera = Camera2D(centerX = 0f, centerY = 0f, pixelsPerUnit = 100f)
        // Point above origin in world space appears above center on screen.
        assertTrue(camera.worldToScreenY(1f, 600f) < 300f)
        assertTrue(camera.worldToScreenY(-1f, 600f) > 300f)
    }

    @Test
    fun `zoom at keeps focus point stable`() {
        val camera = Camera2D(centerX = 1f, centerY = 1f, pixelsPerUnit = 100f)
        val w = 1000f
        val h = 800f
        val focusX = 250f
        val focusY = 620f

        val beforeX = camera.screenToWorldX(focusX, w)
        val beforeY = camera.screenToWorldY(focusY, h)

        val zoomed = camera.zoomAt(1.5f, focusX, focusY, w, h)
        assertEquals(150f, zoomed.pixelsPerUnit, 1e-4f)
        assertEquals(beforeX, zoomed.screenToWorldX(focusX, w), 1e-3f)
        assertEquals(beforeY, zoomed.screenToWorldY(focusY, h), 1e-3f)
    }

    @Test
    fun `zoom is clamped`() {
        val camera = Camera2D()
        val zoomedOut = camera.zoomAt(0.0001f, 0f, 0f, 100f, 100f)
        assertEquals(Camera2D.MIN_PPU, zoomedOut.pixelsPerUnit, 1e-6f)
        val zoomedIn = camera.zoomAt(1e6f, 0f, 0f, 100f, 100f)
        assertEquals(Camera2D.MAX_PPU, zoomedIn.pixelsPerUnit, 1e-6f)
    }

    @Test
    fun `pan moves center opposite to drag`() {
        val camera = Camera2D(centerX = 0f, centerY = 0f, pixelsPerUnit = 100f)
        val panned = camera.panByScreen(100f, 50f)
        assertEquals(-1f, panned.centerX, 1e-6f)
        assertEquals(0.5f, panned.centerY, 1e-6f)
    }

    @Test
    fun `snap rounds to nearest step`() {
        assertEquals(1.5f, Camera2D.snap(1.37f, 0.5f, true), 1e-6f)
        assertEquals(1.37f, Camera2D.snap(1.37f, 0.5f, false), 1e-6f)
        assertEquals(0f, Camera2D.snap(0.24f, 0.5f, true), 1e-6f)
    }

    @Test
    fun `hit test respects rotation`() {
        // 2x1 sprite rotated 90 degrees: long axis is now vertical.
        assertTrue(hitTestSprite(0f, 0.9f, 0f, 0f, 90f, 1f, 0.5f))
        assertFalse(hitTestSprite(0.9f, 0f, 0f, 0f, 90f, 1f, 0.5f))
        // Unrotated: the opposite.
        assertTrue(hitTestSprite(0.9f, 0f, 0f, 0f, 0f, 1f, 0.5f))
        assertFalse(hitTestSprite(0f, 0.9f, 0f, 0f, 0f, 1f, 0.5f))
    }
}
