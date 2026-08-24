package dev.nova.editor.scene

import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Validates the shipped sample games: every sample's scene parses, referenced
 * scripts exist on disk, and the render-scene projection builds. Guards the
 * "3 sample games" deliverable against bit-rot.
 */
class SampleGamesTest {

    private fun samplesDir(): File {
        // JVM tests run with the module dir as cwd; samples/ is at repo root.
        val candidates = listOf(File("../samples"), File("../../samples"), File("samples"))
        return candidates.first { it.isDirectory }
    }

    private fun validateSample(name: String) {
        val dir = File(samplesDir(), name)
        assertTrue("sample dir missing: $name", dir.isDirectory)
        val sceneFile = File(dir, "scenes/main.scene.json")
        assertTrue("scene missing for $name", sceneFile.isFile)

        val scene = deserializeScene(sceneFile.readText())
        // Every referenced script file must exist.
        for (e in scene.entities) {
            e.script?.let { script ->
                val f = File(dir, script.scriptPath)
                assertTrue("script missing: ${script.scriptPath} in $name", f.isFile)
                val source = f.readText()
                assertTrue("script empty in $name", source.isNotBlank())
                assertTrue(
                    "script must define on_update in $name/${script.scriptPath}",
                    source.contains("function on_update"),
                )
            }
        }
        // Render projection must build without errors.
        val render = buildRenderScene(scene, null)
        assertNotNull(render)
        // Samples must include a camera and at least one script.
        assertTrue("$name should have a camera", scene.entities.any { it.camera != null })
        assertTrue("$name should have a script", scene.entities.any { it.script != null })
    }

    @Test
    fun `platformer sample is valid`() = validateSample("platformer")

    @Test
    fun `space shooter sample is valid`() = validateSample("space-shooter")

    @Test
    fun `brick breaker sample is valid`() = validateSample("brick-breaker")
}
