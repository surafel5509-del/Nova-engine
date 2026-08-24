package dev.nova.editor.assets

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class AssetZipTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private fun makeZip(entries: Map<String, ByteArray>): ByteArray {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { zip ->
            for ((name, data) in entries) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(data)
                zip.closeEntry()
            }
        }
        return out.toByteArray()
    }

    @Test
    fun `import zip extracts files safely`() {
        val root = tmp.newFolder("proj")
        val store = AssetStore(root.absolutePath)
        val zip = makeZip(mapOf(
            "textures/hero.png" to byteArrayOf(1, 2),
            "scripts/main.lua" to byteArrayOf(3),
            "evil/../escape.txt" to byteArrayOf(9),   // traversal attempt
        ))
        val count = store.importZip(zip, "assets/library")
        assertTrue(count >= 2)
        assertTrue(File(root, "assets/library/textures/hero.png").exists())
        assertTrue(File(root, "assets/library/scripts/main.lua").exists())
        // Traversal is blocked (file either skipped or inside the library).
        assertTrue(!File(root, "assets/escape.txt").exists())
    }

    @Test
    fun `create file and move it`() {
        val root = tmp.newFolder("proj")
        val store = AssetStore(root.absolutePath)
        assertNotNull(store.createFile("assets", "readme.txt", "hello"))
        assertTrue(File(root, "assets/readme.txt").exists())
        store.createFolder("assets", "docs")
        val moved = store.move("assets/readme.txt", "assets/docs")
        assertEquals("assets/docs/readme.txt", moved)
        assertTrue(File(root, "assets/docs/readme.txt").exists())
    }
}
