package dev.nova.editor.editor

import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.scene.TransformComponent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class UndoTest {

    @Test
    fun `add entity is undoable and redoable`() {
        val stack = UndoStack()
        var scene = Scene()

        scene = stack.push(scene, AddEntityCommand(EntityKind.SPRITE))
        assertEquals(1, scene.entities.size)
        assertTrue(stack.canUndo)

        scene = stack.undo(scene)
        assertEquals(0, scene.entities.size)
        assertTrue(stack.canRedo)

        scene = stack.redo(scene)
        assertEquals(1, scene.entities.size)
        assertEquals("Sprite", scene.entities[0].name)
    }

    @Test
    fun `delete restores full subtree`() {
        val stack = UndoStack()
        val parent = SceneOps.createEntity(EntityKind.EMPTY, "P")
        val child = SceneOps.createEntity(EntityKind.SPRITE, "C", parentId = parent.id)
        var scene = Scene(entities = listOf(parent, child))

        scene = stack.push(scene, DeleteEntityCommand(parent.id))
        assertEquals(0, scene.entities.size)

        scene = stack.undo(scene)
        assertEquals(2, scene.entities.size)
        assertNotNull(SceneOps.find(scene, child.id))
    }

    @Test
    fun `update entity restores property snapshots`() {
        val stack = UndoStack()
        val entity = SceneOps.createEntity(EntityKind.SPRITE, "Hero")
        var scene = Scene(entities = listOf(entity))

        scene = stack.push(scene, UpdateEntityCommand(entity.id, "Rename") { it.copy(name = "Villain") })
        assertEquals("Villain", SceneOps.find(scene, entity.id)!!.name)

        scene = stack.undo(scene)
        assertEquals("Hero", SceneOps.find(scene, entity.id)!!.name)

        scene = stack.redo(scene)
        assertEquals("Villain", SceneOps.find(scene, entity.id)!!.name)
    }

    @Test
    fun `reparent undo restores original parent`() {
        val stack = UndoStack()
        val a = SceneOps.createEntity(EntityKind.EMPTY, "A")
        val b = SceneOps.createEntity(EntityKind.EMPTY, "B")
        var scene = Scene(entities = listOf(a, b))

        scene = stack.push(scene, ReparentEntityCommand(b.id, a.id))
        assertEquals(a.id, SceneOps.find(scene, b.id)!!.parentId)

        scene = stack.undo(scene)
        assertNull(SceneOps.find(scene, b.id)!!.parentId)
    }

    @Test
    fun `snapshot command handles pre-applied edits`() {
        val stack = UndoStack()
        val entity = SceneOps.createEntity(EntityKind.SPRITE, "Hero")
        var scene = Scene(entities = listOf(entity))

        // Simulate a drag: mutate outside the stack, then record endpoints.
        val before = SceneOps.find(scene, entity.id)!!
        scene = SceneOps.update(scene, entity.id) {
            it.copy(transform = it.transform.copy(x = 5f))
        }
        val after = SceneOps.find(scene, entity.id)!!

        stack.pushPreApplied(scene, SnapshotEntityCommand(entity.id, "Move", before, after))
        assertEquals(5f, SceneOps.find(scene, entity.id)!!.transform.x, 1e-6f)

        scene = stack.undo(scene)
        assertEquals(0f, SceneOps.find(scene, entity.id)!!.transform.x, 1e-6f)

        scene = stack.redo(scene)
        assertEquals(5f, SceneOps.find(scene, entity.id)!!.transform.x, 1e-6f)
    }

    @Test
    fun `new command clears redo stack`() {
        val stack = UndoStack()
        var scene = Scene()

        scene = stack.push(scene, AddEntityCommand(EntityKind.SPRITE))
        scene = stack.undo(scene)
        assertTrue(stack.canRedo)

        scene = stack.push(scene, AddEntityCommand(EntityKind.CAMERA))
        assertFalse(stack.canRedo)
    }

    @Test
    fun `stack capacity drops oldest entries`() {
        val stack = UndoStack(capacity = 3)
        var scene = Scene()
        repeat(5) { scene = stack.push(scene, AddEntityCommand(EntityKind.EMPTY)) }
        assertEquals(5, scene.entities.size)

        repeat(5) { scene = stack.undo(scene) }
        // Only 3 undos possible -> 2 entities remain.
        assertEquals(2, scene.entities.size)
        assertFalse(stack.canUndo)
    }
}
