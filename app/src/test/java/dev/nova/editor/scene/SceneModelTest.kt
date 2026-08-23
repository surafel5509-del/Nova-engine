package dev.nova.editor.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SceneModelTest {

    private fun sceneWith(vararg entities: Entity): Scene =
        Scene(entities = entities.toList())

    @Test
    fun `add assigns unique names`() {
        var scene = Scene()
        scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.SPRITE))
        scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.SPRITE))
        scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.SPRITE))
        assertEquals(listOf("Sprite", "Sprite 2", "Sprite 3"), scene.entities.map { it.name })
    }

    @Test
    fun `remove deletes descendants`() {
        val parent = SceneOps.createEntity(EntityKind.EMPTY, "Parent")
        val child = SceneOps.createEntity(EntityKind.SPRITE, "Child", parentId = parent.id)
        val grandChild = SceneOps.createEntity(EntityKind.SPRITE, "GrandChild", parentId = child.id)
        val unrelated = SceneOps.createEntity(EntityKind.CAMERA, "Camera")
        val scene = sceneWith(parent, child, grandChild, unrelated)

        val next = SceneOps.remove(scene, parent.id)
        assertEquals(1, next.entities.size)
        assertEquals("Camera", next.entities[0].name)
    }

    @Test
    fun `duplicate copies subtree with fresh ids and unique name`() {
        val parent = SceneOps.createEntity(EntityKind.EMPTY, "Parent")
        val child = SceneOps.createEntity(EntityKind.SPRITE, "Child", parentId = parent.id)
        val scene = sceneWith(parent, child)

        val (next, newRootId) = SceneOps.duplicate(scene, parent.id)
        assertNotNull(newRootId)
        assertEquals(4, next.entities.size)
        val newRoot = SceneOps.find(next, newRootId!!)!!
        assertNotEquals(parent.id, newRoot.id)
        assertEquals("Parent 2", newRoot.name)
        val newChild = next.entities.first { it.parentId == newRoot.id }
        assertEquals("Child", newChild.name)
        assertNotEquals(child.id, newChild.id)
    }

    @Test
    fun `reparent prevents cycles`() {
        val a = SceneOps.createEntity(EntityKind.EMPTY, "A")
        val b = SceneOps.createEntity(EntityKind.EMPTY, "B", parentId = a.id)
        val scene = sceneWith(a, b)

        // A cannot become a child of its own descendant.
        val result = SceneOps.reparent(scene, a.id, b.id)
        assertNull(SceneOps.find(result, a.id)!!.parentId)

        // B can move to root.
        val moved = SceneOps.reparent(scene, b.id, null)
        assertNull(SceneOps.find(moved, b.id)!!.parentId)
    }

    @Test
    fun `hierarchy order is depth first`() {
        val a = SceneOps.createEntity(EntityKind.EMPTY, "A")
        val a1 = SceneOps.createEntity(EntityKind.EMPTY, "A1", parentId = a.id)
        val a2 = SceneOps.createEntity(EntityKind.EMPTY, "A2", parentId = a.id)
        val b = SceneOps.createEntity(EntityKind.EMPTY, "B")
        val scene = sceneWith(b, a2, a, a1) // deliberately shuffled

        // Roots keep list order (B before A); children keep list order too (A2 before A1).
        val order = SceneOps.hierarchyOrder(scene).map { it.name }
        assertEquals(listOf("B", "A", "A2", "A1"), order)
    }

    @Test
    fun `world transform composes parent chain`() {
        val parent = SceneOps.createEntity(EntityKind.EMPTY, "P")
            .copy(transform = TransformComponent(x = 10f, y = 0f, rotation = 90f, scaleX = 2f, scaleY = 2f))
        val child = SceneOps.createEntity(EntityKind.SPRITE, "C", parentId = parent.id)
            .copy(transform = TransformComponent(x = 1f, y = 0f))
        val scene = sceneWith(parent, child)

        val wt = SceneOps.worldTransform(scene, child.id)
        // Child local (1,0) scaled by 2 then rotated 90deg -> (0, 2); then translated by (10, 0).
        assertEquals(10f, wt.x, 1e-3f)
        assertEquals(2f, wt.y, 1e-3f)
        assertEquals(90f, wt.rotation, 1e-3f)
        assertEquals(2f, wt.scaleX, 1e-3f)
    }

    @Test
    fun `render scene skips disabled chains and marks selection`() {
        val parent = SceneOps.createEntity(EntityKind.EMPTY, "P").copy(enabled = false)
        val child = SceneOps.createEntity(EntityKind.SPRITE, "C", parentId = parent.id)
        val visible = SceneOps.createEntity(EntityKind.SPRITE, "V")
        val scene = sceneWith(parent, child, visible)

        val render = buildRenderScene(scene, selectedId = visible.id)
        assertEquals(1, render.sprites.size)
        assertEquals(visible.id, render.sprites[0].id)
        assertTrue(render.sprites[0].selected)
    }

    @Test
    fun `render scene sorts by sorting order`() {
        val back = SceneOps.createEntity(EntityKind.SPRITE, "Back")
            .copy(sprite = SpriteComponent(sortingOrder = -1))
        val front = SceneOps.createEntity(EntityKind.SPRITE, "Front")
            .copy(sprite = SpriteComponent(sortingOrder = 5))
        val scene = sceneWith(front, back)

        val render = buildRenderScene(scene, selectedId = null)
        assertEquals(listOf(back.id, front.id), render.sprites.map { it.id })
    }

    @Test
    fun `serialization round trips`() {
        var scene = Scene(name = "RoundTrip")
        scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.CAMERA, "Main Camera"))
        scene = SceneOps.add(
            scene,
            SceneOps.createEntity(EntityKind.SPRITE, "Hero").copy(
                transform = TransformComponent(x = 1.5f, y = -2.25f, rotation = 45f, scaleX = 2f, scaleY = 0.5f),
                sprite = SpriteComponent(texturePath = "assets/textures/hero.png", width = 3f, r = 0.25f, a = 0.9f),
                physicsBody = PhysicsBodyComponent(bodyType = "dynamic", mass = 2.5f),
            ),
        )

        val text = serializeScene(scene)
        val restored = deserializeScene(text)
        assertEquals(scene, restored)
        assertTrue(text.contains("\"version\": 1"))
    }

    @Test
    fun `deserialize rejects newer format versions`() {
        val future = """{"version": 999, "name": "X", "entities": []}"""
        try {
            deserializeScene(future)
            throw AssertionError("Expected rejection of future version")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message!!.contains("999"))
        }
    }

    @Test
    fun `deserialize ignores unknown fields for forward compatibility`() {
        val text = """{"version": 1, "name": "X", "futureField": 42, "entities": []}"""
        val scene = deserializeScene(text)
        assertEquals("X", scene.name)
        assertFalse(scene.entities.isNotEmpty())
    }
}
