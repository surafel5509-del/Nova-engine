package dev.nova.editor.build

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.util.zip.ZipFile

class BuildExporterTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private fun makeProject(): File {
        val root = tmp.newFolder("proj")
        File(root, "project.json").writeText("{}")
        File(root, "scenes").mkdir()
        File(root, "scenes/main.scene.json").writeText("{}")
        File(root, "scripts").mkdir()
        File(root, "scripts/player.lua").writeText("-- hi")
        File(root, "assets/textures").mkdirs()
        File(root, "assets/textures/hero.png").writeBytes(byteArrayOf(1, 2))
        return root
    }

    @Test
    fun `export zips project content`() {
        val root = makeProject()
        val out = File(tmp.root, "game.novapkg")
        val entries = BuildExporter.exportPackage(root.absolutePath, out)
        assertTrue(entries >= 4)

        ZipFile(out).use { zip ->
            val names = zip.entries().asSequence().map { it.name }.toList()
            assertTrue(names.contains("project.json"))
            assertTrue(names.contains("scenes/main.scene.json"))
            assertTrue(names.contains("scripts/player.lua"))
            assertTrue(names.contains("assets/textures/hero.png"))
        }
    }

    @Test
    fun `export rejects missing project json`() {
        val root = tmp.newFolder("not-a-project")
        val out = File(tmp.root, "bad.novapkg")
        try {
            BuildExporter.exportPackage(root.absolutePath, out)
            throw AssertionError("expected IllegalArgumentException")
        } catch (e: IllegalArgumentException) {
            // expected
        }
    }

    @Test
    fun `apk build command includes project path`() {
        val cmd = BuildExporter.apkBuildCommand("/tmp/my-game")
        assertEquals("./gradlew :game:assembleDebug -PnovaProjectPath=\"/tmp/my-game\"", cmd)
    }
}
