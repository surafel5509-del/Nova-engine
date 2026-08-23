package dev.nova.editor.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Tests for the flat render-scene projection (sprites, bodies, game camera). */
class RenderSceneTest {

    private fun sceneWith(vararg entities: Entity): Scene =
        Scene(entities = entities.toList())

    @Test
    fun `physics body produces a render body with world extents`() {
        val body = SceneOps.createEntity(EntityKind.PHYSICS_BODY, "Box").copy(
            transform = TransformComponent(x = 2f, y = 3f, scaleX = 2f, scaleY = 1f),
            sprite = SpriteComponent(width = 1f, height = 1f),
            physicsBody = PhysicsBodyComponent(bodyType = "dynamic", colliderWidth = 1f, colliderHeight = 1f),
        )
        val render = buildRenderScene(sceneWith(body), selectedId = null)
        assertEquals(1, render.bodies.size)
        val rb = render.bodies[0]
        assertEquals(1, rb.bodyType) // dynamic
        assertEquals(2f, rb.x, 1e-4f)
        assertEquals(3f, rb.y, 1e-4f)
        // halfW = colliderWidth(1) * scaleX(2) / 2 = 1
        assertEquals(1f, rb.halfW, 1e-4f)
        assertEquals(0.5f, rb.halfH, 1e-4f)
    }

    @Test
    fun `static and kinematic body types map to 0 and 2`() {
        val staticBody = SceneOps.createEntity(EntityKind.SPRITE, "S").copy(
            physicsBody = PhysicsBodyComponent(bodyType = "static"),
        )
        val kinematic = SceneOps.createEntity(EntityKind.SPRITE, "K").copy(
            physicsBody = PhysicsBodyComponent(bodyType = "kinematic"),
        )
        val render = buildRenderScene(sceneWith(staticBody, kinematic), null)
        val byId = render.bodies.associateBy { it.id }
        assertEquals(0, byId.getValue(staticBody.id).bodyType)
        assertEquals(2, byId.getValue(kinematic.id).bodyType)
    }

    @Test
    fun `enabled camera entity becomes the game camera`() {
        val cam = SceneOps.createEntity(EntityKind.CAMERA, "Cam").copy(
            transform = TransformComponent(x = 5f, y = -1f),
            camera = CameraComponent(zoom = 80f, frustumWidth = 12f, frustumHeight = 7f),
        )
        val render = buildRenderScene(sceneWith(cam), null)
        assertNotNull(render.gameCamera)
        val gc = render.gameCamera!!
        assertEquals(5f, gc.x, 1e-4f)
        assertEquals(-1f, gc.y, 1e-4f)
        assertEquals(80f, gc.zoom, 1e-4f)
        assertEquals(12f, gc.width, 1e-4f)
    }

    @Test
    fun `no camera means no game camera`() {
        val sprite = SceneOps.createEntity(EntityKind.SPRITE, "S")
        val render = buildRenderScene(sceneWith(sprite), null)
        assertNull(render.gameCamera)
    }

    @Test
    fun `animator frame grid is forwarded to render sprite`() {
        val anim = SceneOps.createEntity(EntityKind.ANIMATED_SPRITE, "A").copy(
            animator = AnimatorComponent(frameCols = 6, frameRows = 2),
        )
        val render = buildRenderScene(sceneWith(anim), null)
        assertEquals(6, render.sprites[0].frameCols)
        assertEquals(2, render.sprites[0].frameRows)
    }

    @Test
    fun `parallax factor is forwarded`() {
        val bg = SceneOps.createEntity(EntityKind.SPRITE, "BG").copy(
            sprite = SpriteComponent(parallaxFactor = 0.3f),
        )
        val render = buildRenderScene(sceneWith(bg), null)
        assertEquals(0.3f, render.sprites[0].parallaxFactor, 1e-4f)
    }

    @Test
    fun `disabled entity body is excluded`() {
        val body = SceneOps.createEntity(EntityKind.PHYSICS_BODY, "B").copy(enabled = false)
        val render = buildRenderScene(sceneWith(body), null)
        assertTrue(render.bodies.isEmpty())
    }

    @Test
    fun `render scene round-trips through JSON`() {
        val cam = SceneOps.createEntity(EntityKind.CAMERA, "C")
        val body = SceneOps.createEntity(EntityKind.PHYSICS_BODY, "B")
        val render = buildRenderScene(sceneWith(cam, body), null)
        val json = SceneJson.encodeToString(RenderScene.serializer(), render)
        val decoded = SceneJson.decodeFromString(RenderScene.serializer(), json)
        assertEquals(render.sprites.size, decoded.sprites.size)
        assertEquals(render.bodies.size, decoded.bodies.size)
        assertEquals(render.gameCamera, decoded.gameCamera)
    }
}
