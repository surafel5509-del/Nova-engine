package dev.nova.editor.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Tests for 3D render projection (objects, light, mode3d) and animations. */
class Render3DTest {

    @Test
    fun `mesh entity becomes a 3D object with world transform`() {
        val cube = SceneOps.createEntity(EntityKind.MESH3D, "Cube").copy(
            transform = TransformComponent(x = 2f, y = 1f, z = -3f, rotationY = 45f, scaleX = 2f, scaleZ = 2f),
            mesh = MeshComponent(shape = "cylinder", r = 0.8f, g = 0.2f, b = 0.3f),
        )
        val render = buildRenderScene(Scene(entities = listOf(cube)), null, mode3d = true)
        assertTrue(render.mode3d)
        assertEquals(1, render.objects3d.size)
        val obj = render.objects3d[0]
        assertEquals("cylinder", obj.shape)
        assertEquals(-3f, obj.z, 1e-4f)
        assertEquals(45f, obj.ry, 1e-4f)
        assertEquals(2f, obj.sx, 1e-4f)
        assertEquals(0.8f, obj.r, 1e-4f)
    }

    @Test
    fun `light entity becomes the scene light`() {
        val sun = SceneOps.createEntity(EntityKind.LIGHT3D, "Sun").copy(
            light = LightComponent(dirY = -0.8f, ambientR = 0.3f),
        )
        val render = buildRenderScene(Scene(entities = listOf(sun)), null, mode3d = true)
        assertNotNull(render.light)
        assertEquals(-0.8f, render.light!!.dirY, 1e-4f)
        assertEquals(0.3f, render.light!!.ambientR, 1e-4f)
    }

    @Test
    fun `animation clip becomes render tracks`() {
        val hero = SceneOps.createEntity(EntityKind.SPRITE, "Hero").copy(
            animation = AnimationClipComponent(
                tracks = listOf(
                    AnimationTrackData(
                        property = "x",
                        keys = listOf(AnimationKey(0f, 0f), AnimationKey(2f, 5f)),
                    ),
                ),
            ),
        )
        val render = buildRenderScene(Scene(entities = listOf(hero)), null)
        assertEquals(1, render.animations.size)
        val track = render.animations[0]
        assertEquals(hero.id, track.entityId)
        assertEquals("x", track.property)
        assertEquals(2, track.keys.size)
        assertTrue(track.loop)
    }

    @Test
    fun `3D objects and animations survive scene serialization`() {
        var scene = Scene(name = "3D")
        scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.MESH3D, "Box"))
        scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.LIGHT3D, "Sun"))
        val decoded = deserializeScene(serializeScene(scene))
        assertEquals(2, decoded.entities.size)
        assertNotNull(decoded.entities[0].mesh)
        assertNotNull(decoded.entities[1].light)
    }

    @Test
    fun `compose chains z and 3D rotation`() {
        val parent = TransformComponent(x = 1f, z = 2f, rotationY = 10f, scaleZ = 2f)
        val child = TransformComponent(x = 1f, z = 3f, rotationY = 5f)
        val out = SceneOps.compose(parent, child)
        // z: p.z + c.z * p.scaleZ = 2 + 3*2 = 8; rotationY: 10 + 5 = 15.
        assertEquals(8f, out.z, 1e-4f)
        assertEquals(15f, out.rotationY, 1e-4f)
    }
}
