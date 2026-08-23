package dev.nova.editor.project

import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.SceneOps
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class ProjectRepositoryTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    private lateinit var repository: ProjectRepository

    @Before
    fun setUp() {
        repository = ProjectRepository(tempFolder.newFolder("projects"))
    }

    @Test
    fun `create project writes structure and config`() {
        val dir = repository.createProject(
            name = "My Game",
            packageName = "com.example.mygame",
            projectVersion = "1.0.0",
            orientation = ProjectOrientation.LANDSCAPE,
            dimension = ProjectDimension.TWO_D,
            template = ProjectTemplate.EMPTY,
            nowEpochMs = 1000L,
        )

        assertTrue(File(dir, "project.json").exists())
        assertTrue(File(dir, "scenes/main.scene.json").exists())
        assertTrue(File(dir, "assets/textures").isDirectory)

        val (config, scene) = repository.openProject(dir.absolutePath, nowEpochMs = 2000L)
        assertEquals("My Game", config.name)
        assertEquals("com.example.mygame", config.packageName)
        // Empty template still provides a camera entity.
        assertTrue(scene.entities.any { it.camera != null })
    }

    @Test
    fun `create project generates unique directories`() {
        val first = repository.createProject("Game", "com.x", "1", ProjectOrientation.PORTRAIT, ProjectDimension.TWO_D, ProjectTemplate.EMPTY)
        val second = repository.createProject("Game", "com.x", "1", ProjectOrientation.PORTRAIT, ProjectDimension.TWO_D, ProjectTemplate.EMPTY)
        assertNotEquals(first.absolutePath, second.absolutePath)
    }

    @Test
    fun `platformer template creates ground and player`() {
        val dir = repository.createProject("P", "com.x", "1", ProjectOrientation.LANDSCAPE, ProjectDimension.TWO_D, ProjectTemplate.PLATFORMER)
        val (_, scene) = repository.openProject(dir.absolutePath)
        assertNotNull(scene.entities.firstOrNull { it.name == "Ground" })
        assertNotNull(scene.entities.firstOrNull { it.name == "Player" })
        assertNotNull(scene.entities.firstOrNull { it.camera != null })
        // Player is a dynamic body.
        val player = scene.entities.first { it.name == "Player" }
        assertEquals("dynamic", player.physicsBody?.bodyType)
    }

    @Test
    fun `unimplemented templates are rejected`() {
        try {
            repository.createProject("R", "com.x", "1", ProjectOrientation.LANDSCAPE, ProjectDimension.TWO_D, ProjectTemplate.RPG)
            throw AssertionError("Expected IllegalArgumentException")
        } catch (e: IllegalArgumentException) {
            // expected
        }
    }

    @Test
    fun `list projects sorted by last opened descending`() {
        val a = repository.createProject("Alpha", "com.x", "1", ProjectOrientation.LANDSCAPE, ProjectDimension.TWO_D, ProjectTemplate.EMPTY, nowEpochMs = 100L)
        repository.createProject("Beta", "com.x", "1", ProjectOrientation.LANDSCAPE, ProjectDimension.TWO_D, ProjectTemplate.EMPTY, nowEpochMs = 300L)
        repository.openProject(a.absolutePath, nowEpochMs = 500L) // re-open Alpha last

        val names = repository.listProjects().map { it.name }
        assertEquals(listOf("Alpha", "Beta"), names)
    }

    @Test
    fun `save scene persists and reloads`() {
        val dir = repository.createProject("S", "com.x", "1", ProjectOrientation.LANDSCAPE, ProjectDimension.TWO_D, ProjectTemplate.EMPTY)
        val (_, scene) = repository.openProject(dir.absolutePath)
        val edited = SceneOps.add(scene, SceneOps.createEntity(EntityKind.SPRITE, "Hero"))
        repository.saveScene(dir.absolutePath, edited)

        val (_, reloaded) = repository.openProject(dir.absolutePath)
        assertNotNull(SceneOps.find(reloaded, edited.entities.first { it.name == "Hero" }.id))
        assertEquals(edited, reloaded)
    }

    @Test
    fun `texture import deduplicates file names`() {
        val dir = repository.createProject("T", "com.x", "1", ProjectOrientation.LANDSCAPE, ProjectDimension.TWO_D, ProjectTemplate.EMPTY)
        val first = repository.importTexture(dir.absolutePath, "hero.png", byteArrayOf(1, 2, 3))
        val second = repository.importTexture(dir.absolutePath, "hero.png", byteArrayOf(4, 5, 6))
        assertNotEquals(first, second)
        assertTrue(first.startsWith("assets/textures/"))
        assertEquals(3, repository.readTexture(dir.absolutePath, first)!!.size)
        assertTrue(repository.readTexture(dir.absolutePath, second)!!.contentEquals(byteArrayOf(4, 5, 6)))
    }
}
