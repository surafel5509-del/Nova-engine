package dev.nova.editor.assets

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class AssetStoreTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private lateinit var root: java.io.File
    private lateinit var store: AssetStore

    @Before
    fun setUp() {
        root = tmp.newFolder("proj")
        java.io.File(root, "assets/textures").mkdirs()
        java.io.File(root, "assets/audio").mkdirs()
        java.io.File(root, "assets/textures/hero.png").writeBytes(byteArrayOf(1, 2, 3))
        java.io.File(root, "assets/audio/jump.wav").writeBytes(byteArrayOf(4, 5))
        store = AssetStore(root.absolutePath)
    }

    @Test
    fun `lists directories before files`() {
        val entries = store.list("assets")
        assertTrue(entries.size >= 2)
        assertTrue(entries[0].isDirectory)
        assertTrue(entries[1].isDirectory)
    }

    @Test
    fun `texture and audio detection`() {
        val textures = store.list("assets/textures")
        assertEquals(1, textures.size)
        assertTrue(textures[0].isTexture)
        assertFalse(textures[0].isAudio)
        val audio = store.list("assets/audio")
        assertTrue(audio[0].isAudio)
    }

    @Test
    fun `create folder and navigate`() {
        assertTrue(store.createFolder("assets", "sprites"))
        val entries = store.list("assets")
        assertTrue(entries.any { it.name == "sprites" && it.isDirectory })
    }

    @Test
    fun `create folder rejects blank and unsafe names`() {
        assertFalse(store.createFolder("assets", "   "))
        assertFalse(store.createFolder("assets", ""))
    }

    @Test
    fun `delete removes a file`() {
        assertTrue(store.delete("assets/textures/hero.png"))
        assertTrue(store.list("assets/textures").isEmpty())
    }

    @Test
    fun `rename moves file and returns new path`() {
        val newPath = store.rename("assets/audio/jump.wav", "coin.wav")
        assertNotNull(newPath)
        assertEquals("assets/audio/coin.wav", newPath)
        assertNull(store.rename("assets/audio/jump.wav", "x.wav")) // original gone
    }

    @Test
    fun `read bytes returns content`() {
        val bytes = store.readBytes("assets/textures/hero.png")
        assertNotNull(bytes)
        assertEquals(3, bytes!!.size)
    }
}
