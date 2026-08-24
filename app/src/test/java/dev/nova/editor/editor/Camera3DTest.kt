package dev.nova.editor.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class Camera3DTest {

    @Test
    fun `default camera orbits around the origin`() {
        val cam = Camera3D()
        // Eye is at distance from target on the orbit sphere.
        val dx = cam.eyeX - cam.targetX
        val dy = cam.eyeY - cam.targetY
        val dz = cam.eyeZ - cam.targetZ
        val dist = kotlin.math.sqrt(dx * dx + dy * dy + dz * dz)
        assertEquals(cam.distance, dist, 0.01f)
    }

    @Test
    fun `rotate clamps pitch`() {
        val cam = Camera3D().rotate(0f, 200f)
        assertEquals(89f, cam.pitch, 0.01f)
        val cam2 = Camera3D().rotate(0f, -200f)
        assertEquals(-89f, cam2.pitch, 0.01f)
    }

    @Test
    fun `zoom clamps distance`() {
        val far = Camera3D().zoom(0.01f)
        assertEquals(60f, far.distance, 0.01f)
        val near = Camera3D().zoom(100f)
        assertEquals(2f, near.distance, 0.01f)
    }

    @Test
    fun `screen ray from center points at target`() {
        val cam = Camera3D()
        val (origin, dir) = cam.screenRay(0f, 0f, 1.78f)
        // Direction should point from eye toward target.
        val tx = cam.targetX - origin[0]
        val ty = cam.targetY - origin[1]
        val tz = cam.targetZ - origin[2]
        val dot = dir[0] * tx + dir[1] * ty + dir[2] * tz
        assertTrue("center ray aims at target", dot > 0)
    }

    @Test
    fun `rayAabb hits a box`() {
        val t = Camera3D.rayAabb(
            0f, 0f, 10f,   // origin
            0f, 0f, -1f,   // direction -Z
            -1f, -1f, -1f, // min
            1f, 1f, 1f,    // max
        )
        assertNotNull(t)
        assertEquals(9f, t!!, 0.01f)
    }

    @Test
    fun `rayAabb misses off-axis`() {
        val t = Camera3D.rayAabb(
            0f, 10f, 10f, 0f, 0f, -1f,
            -1f, -1f, -1f, 1f, 1f, 1f,
        )
        assertNull(t)
    }
}
