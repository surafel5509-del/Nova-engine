package dev.nova.editor.scene

import org.junit.Assert.assertEquals
import org.junit.Test

class AutoTilerTest {

    @Test
    fun `empty neighbors give mask 0`() {
        val map = TilemapComponent(cols = 3, rows = 3)
        assertEquals(0, AutoTiler.neighborMask(map, 1, 1))
    }

    @Test
    fun `filled neighbors set the right bits`() {
        var map = TilemapComponent(cols = 3, rows = 3)
        map = map.withTile(1, 2, 0)   // above center
        map = map.withTile(2, 1, 0)   // right of center
        val mask = AutoTiler.neighborMask(map, 1, 1)
        assertEquals(AutoTiler.MASK_UP or AutoTiler.MASK_RIGHT, mask)
    }

    @Test
    fun `tile index is base plus mask`() {
        assertEquals(5, AutoTiler.tileIndexForMask(5, 0))
        assertEquals(21, AutoTiler.tileIndexForMask(5, 16))
    }

    @Test
    fun `paint auto updates the cell and neighbors`() {
        var map = TilemapComponent(cols = 3, rows = 3)
        map = AutoTiler.paintAutoTiled(map, 1, 1, true, 0)
        // Cell with no neighbors -> mask 0 -> tile 0.
        assertEquals(0, map.tileAt(1, 1))
        // Now paint above it: both cells get recomputed masks.
        map = AutoTiler.paintAutoTiled(map, 1, 2, true, 0)
        // (1,2) has filled below -> MASK_DOWN; (1,1) has filled above -> MASK_UP.
        assertEquals(AutoTiler.MASK_DOWN, map.tileAt(1, 2))
        assertEquals(AutoTiler.MASK_UP, map.tileAt(1, 1))
    }

    @Test
    fun `erase clears the cell and recomputes neighbors`() {
        var map = TilemapComponent(cols = 3, rows = 3)
        map = AutoTiler.paintAutoTiled(map, 1, 1, true, 0)
        map = AutoTiler.paintAutoTiled(map, 1, 2, true, 0)
        map = AutoTiler.paintAutoTiled(map, 1, 2, false, 0)
        assertEquals(-1, map.tileAt(1, 2))
        assertEquals(0, map.tileAt(1, 1))   // neighbor recomputed: no filled neighbors now
    }
}
