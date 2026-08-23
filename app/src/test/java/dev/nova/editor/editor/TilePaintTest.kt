package dev.nova.editor.editor

import dev.nova.editor.project.ProjectConfig
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.scene.Entity
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.scene.TilemapComponent
import dev.nova.editor.scene.TransformComponent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Tests for the TILE tool's paint logic (ViewModel-level, undoable). */
class TilePaintTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private lateinit var viewModel: EditorViewModel

    @Before
    fun setUp() {
        val root = tmp.newFolder("proj")
        val repo = ProjectRepository(root)
        val map = Entity(name = "Map", tilemap = TilemapComponent(cols = 4, rows = 3))
        viewModel = EditorViewModel(
            projectPath = root.absolutePath,
            config = ProjectConfig(name = "Test"),
            initialScene = Scene(entities = listOf(map)),
            repository = repo,
        )
    }

    private fun mapId(): String = viewModel.scene.entities[0].id

    @Test
    fun `paint sets the cell under the world position`() {
        // Cell (2, 1) with tileSize 1 at origin: world point (2.4, 1.2).
        assertTrue(viewModel.paintTileAt(mapId(), 2.4f, 1.2f, 5))
        val map = SceneOps.find(viewModel.scene, mapId())!!.tilemap!!
        assertEquals(5, map.tileAt(2, 1))
    }

    @Test
    fun `paint is undoable`() {
        viewModel.paintTileAt(mapId(), 0.5f, 0.5f, 3)
        viewModel.undo()
        val map = SceneOps.find(viewModel.scene, mapId())!!.tilemap!!
        assertEquals(-1, map.tileAt(0, 0))
    }

    @Test
    fun `erase with -1 clears the cell`() {
        viewModel.paintTileAt(mapId(), 0.5f, 0.5f, 3)
        viewModel.paintTileAt(mapId(), 0.5f, 0.5f, -1)
        val map = SceneOps.find(viewModel.scene, mapId())!!.tilemap!!
        assertEquals(-1, map.tileAt(0, 0))
    }

    @Test
    fun `paint outside the grid is ignored`() {
        val before = viewModel.scene
        assertTrue(!viewModel.paintTileAt(mapId(), 99f, 99f, 1))
        assertEquals(before, viewModel.scene)
    }

    @Test
    fun `paint respects tilemap entity offset`() {
        val offset = Entity(
            name = "Map2",
            transform = TransformComponent(x = 10f, y = 5f),
            tilemap = TilemapComponent(cols = 2, rows = 2),
        )
        viewModel.addEntity(dev.nova.editor.scene.EntityKind.EMPTY)
        val scene = SceneOps.add(viewModel.scene, offset)
        val vm2 = EditorViewModel(
            projectPath = viewModel.projectPath,
            config = viewModel.config,
            initialScene = scene,
            repository = ProjectRepository(java.io.File(viewModel.projectPath)),
        )
        val id = offset.id
        // World (10.3, 5.4) -> cell (0, 0) of the offset map.
        assertTrue(vm2.paintTileAt(id, 10.3f, 5.4f, 2))
        assertEquals(2, SceneOps.find(vm2.scene, id)!!.tilemap!!.tileAt(0, 0))
    }
}
