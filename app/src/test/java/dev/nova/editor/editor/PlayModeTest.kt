package dev.nova.editor.editor

import dev.nova.editor.project.ProjectConfig
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/** Tests for Play mode state transitions and scene snapshot/restore. */
class PlayModeTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private lateinit var viewModel: EditorViewModel

    @Before
    fun setUp() {
        val root = tmp.newFolder("proj")
        val repo = ProjectRepository(root)
        val scene = Scene(name = "Main")
        viewModel = EditorViewModel(
            projectPath = root.absolutePath,
            config = ProjectConfig(name = "Test"),
            initialScene = scene,
            repository = repo,
        )
    }

    @Test
    fun `play from stopped snapshots scene and enters playing`() {
        viewModel.addEntity(EntityKind.SPRITE)
        val before = viewModel.scene
        viewModel.play()
        assertEquals(PlayState.PLAYING, viewModel.playState)
        // Scene content unchanged immediately on play.
        assertEquals(before.entities.size, viewModel.scene.entities.size)
    }

    @Test
    fun `pause and resume`() {
        viewModel.play()
        viewModel.pause()
        assertEquals(PlayState.PAUSED, viewModel.playState)
        viewModel.play()
        assertEquals(PlayState.PLAYING, viewModel.playState)
    }

    @Test
    fun `stop restores the pre-play scene`() {
        viewModel.addEntity(EntityKind.PHYSICS_BODY)
        val id = viewModel.scene.entities[0].id
        viewModel.play()
        // Simulate the engine writing a new position back during play.
        viewModel.applySimulatedPositions(mapOf(id to (10f to 20f)))
        val moved = SceneOps.find(viewModel.scene, id)!!
        assertEquals(10f, moved.transform.x, 1e-4f)
        // Stop should restore the original position.
        viewModel.stop()
        assertEquals(PlayState.STOPPED, viewModel.playState)
        val restored = SceneOps.find(viewModel.scene, id)!!
        assertEquals(0f, restored.transform.x, 1e-4f)
        assertEquals(0f, restored.transform.y, 1e-4f)
    }

    @Test
    fun `simulated positions ignored when stopped`() {
        viewModel.addEntity(EntityKind.PHYSICS_BODY)
        val id = viewModel.scene.entities[0].id
        // Not playing: applySimulatedPositions must be a no-op.
        viewModel.applySimulatedPositions(mapOf(id to (5f to 5f)))
        val e = SceneOps.find(viewModel.scene, id)!!
        assertEquals(0f, e.transform.x, 1e-4f)
    }

    @Test
    fun `toggle game view and physics debug`() {
        assertEquals(false, viewModel.gameView)
        viewModel.toggleGameView()
        assertEquals(true, viewModel.gameView)
        viewModel.togglePhysicsDebug()
        assertEquals(true, viewModel.physicsDebug)
    }
}
