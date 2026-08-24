package dev.nova.editor.editor

import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * 3D orbit editor camera: yaw/pitch around a target with distance + fov.
 * Also provides ray construction for screen-space picking of 3D objects.
 */
data class Camera3D(
    val yaw: Float = 45f,             // degrees around Y
    val pitch: Float = 30f,           // degrees above horizon
    val distance: Float = 12f,
    val targetX: Float = 0f,
    val targetY: Float = 0.5f,
    val targetZ: Float = 0f,
    val fov: Float = 50f,             // vertical fov degrees
) {
    val eyeX: Float get() = targetX + distance * cosDeg(pitch) * sinDeg(yaw)
    val eyeY: Float get() = targetY + distance * sinDeg(pitch)
    val eyeZ: Float get() = targetZ + distance * cosDeg(pitch) * cosDeg(yaw)

    fun rotate(dYaw: Float, dPitch: Float): Camera3D = copy(
        yaw = (yaw + dYaw) % 360f,
        pitch = (pitch + dPitch).coerceIn(-89f, 89f),
    )

    fun zoom(factor: Float): Camera3D = copy(distance = (distance / factor).coerceIn(2f, 60f))

    fun pan(dxScreen: Float, dyScreen: Float): Camera3D {
        // Pan target in the camera's right/up plane, scaled by distance.
        val scale = distance * 0.0018f
        val rad = Math.toRadians(yaw.toDouble())
        val rightX = Math.cos(rad).toFloat()
        val rightZ = -Math.sin(rad).toFloat()
        // Up is screen-space vertical: approximate with world Y.
        return copy(
            targetX = targetX - dxScreen * scale * rightX,
            targetZ = targetZ - dxScreen * scale * rightZ,
            targetY = (targetY + dyScreen * scale).coerceIn(-10f, 30f),
        )
    }

    /**
     * Builds a world-space ray from a screen point (0..1 normalized).
     * Returns origin + direction (normalized) as float arrays of 3.
     */
    fun screenRay(nx: Float, ny: Float, aspect: Float): Pair<FloatArray, FloatArray> {
        val f = forward()
        val r = FloatArray(3).also { cross(f, floatArrayOf(0f, 1f, 0f), it) }
        normalize(r)
        val u = FloatArray(3).also { cross(r, f, it) }
        normalize(u)
        val tanHalf = Math.tan(Math.toRadians((fov / 2f).toDouble())).toFloat()
        val dx = nx * tanHalf * aspect
        val dy = ny * tanHalf
        val dir = floatArrayOf(
            f[0] + dx * r[0] + dy * u[0],
            f[1] + dx * r[1] + dy * u[1],
            f[2] + dx * r[2] + dy * u[2],
        )
        normalize(dir)
        return floatArrayOf(eyeX, eyeY, eyeZ) to dir
    }

    private fun forward(): FloatArray {
        val f = floatArrayOf(targetX - eyeX, targetY - eyeY, targetZ - eyeZ)
        normalize(f)
        return f
    }

    companion object {
        private fun cosDeg(deg: Float) = cos(Math.toRadians(deg.toDouble())).toFloat()
        private fun sinDeg(deg: Float) = sin(Math.toRadians(deg.toDouble())).toFloat()

        private fun cross(a: FloatArray, b: FloatArray, out: FloatArray) {
            out[0] = a[1] * b[2] - a[2] * b[1]
            out[1] = a[2] * b[0] - a[0] * b[2]
            out[2] = a[0] * b[1] - a[1] * b[0]
        }

        private fun normalize(v: FloatArray) {
            val len = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
            if (len > 1e-6f) {
                v[0] /= len; v[1] /= len; v[2] /= len
            }
        }

        /** Ray vs AABB (slab). Returns t along ray, or null. */
        fun rayAabb(
            ox: Float, oy: Float, oz: Float,
            dx: Float, dy: Float, dz: Float,
            minX: Float, minY: Float, minZ: Float,
            maxX: Float, maxY: Float, maxZ: Float,
        ): Float? {
            var tmin = 0f
            var tmax = Float.MAX_VALUE
            fun slab(o: Float, d: Float, lo: Float, hi: Float): Boolean {
                if (kotlin.math.abs(d) < 1e-8f) return o in lo..hi
                var t1 = (lo - o) / d
                var t2 = (hi - o) / d
                if (t1 > t2) { val tmp = t1; t1 = t2; t2 = tmp }
                tmin = maxOf(tmin, t1)
                tmax = minOf(tmax, t2)
                return tmin <= tmax
            }
            if (!slab(ox, dx, minX, maxX)) return null
            if (!slab(oy, dy, minY, maxY)) return null
            if (!slab(oz, dz, minZ, maxZ)) return null
            return tmin
        }
    }
}
