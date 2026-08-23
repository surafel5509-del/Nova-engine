package dev.nova.editor.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Tests for tilemap data ops and the render-scene tilemap/emitter sections. */
class TilemapTest {

    @Test
    fun `withTile sets a cell and leaves others`() {
        val map = TilemapComponent(cols = 3, rows = 2)
        val next = map.withTile(1, 1, 7)
        assertEquals(7, next.tileAt(1, 1))
        assertEquals(-1, next.tileAt(0, 0))
        assertEquals(6, next.tiles.size)
    }

    @Test
    fun `withTile out of bounds is a no-op`() {
        val map = TilemapComponent(cols = 2, rows = 2)
        val same = map.withTile(5, 0, 3)
        assertEquals(map.tiles, same.tiles)
    }

    @Test
    fun `resized keeps overlapping cells`() {
        var map = TilemapComponent(cols = 2, rows = 2)
        map = map.withTile(1, 1, 9)
        val grown = map.resized(4, 3)
        assertEquals(9, grown.tileAt(1, 1))
        assertEquals(-1, grown.tileAt(3, 2))
        assertEquals(12, grown.tiles.size)
        val shrunk = map.resized(1, 1)
        assertEquals(-1, shrunk.tileAt(0, 0))
    }

    @Test
    fun `tilemap serializes round trip`() {
        val map = TilemapComponent(cols = 2, rows = 2, tiles = listOf(0, 1, -1, 5))
        val entity = Entity(name = "Map", tilemap = map)
        val scene = Scene(entities = listOf(entity))
        val decoded = deserializeScene(serializeScene(scene))
        assertEquals(map, decoded.entities[0].tilemap)
    }

    @Test
    fun `render scene includes tilemaps and emitters`() {
        val tileEntity = Entity(name = "Map", tilemap = TilemapComponent(cols = 4, rows = 3))
        val emitterEntity = Entity(name = "Torch", particles = ParticleEmitterComponent(emissionRate = 33f))
        val scriptEntity = Entity(name = "Player", script = ScriptComponent("scripts/p.lua"))
        val audioEntity = Entity(name = "Bgm", audioSource = AudioSourceComponent(audioPath = "assets/audio/bgm.ogg", music = true))
        val scene = Scene(entities = listOf(tileEntity, emitterEntity, scriptEntity, audioEntity))

        val render = buildRenderScene(scene, null)
        assertEquals(1, render.tilemaps.size)
        assertEquals(4, render.tilemaps[0].cols)
        assertEquals(12, render.tilemaps[0].tiles.size)
        assertEquals(1, render.emitters.size)
        assertEquals(33f, render.emitters[0].emissionRate, 1e-4f)
        assertEquals(1, render.scripts.size)
        assertEquals("scripts/p.lua", render.scripts[0].script)
        assertEquals(1, render.audioSources.size)
        assertTrue(render.audioSources[0].music)
    }

    @Test
    fun `disabled tilemap is excluded from render scene`() {
        val entity = Entity(name = "Map", enabled = false, tilemap = TilemapComponent())
        val render = buildRenderScene(Scene(entities = listOf(entity)), null)
        assertTrue(render.tilemaps.isEmpty())
    }
}
