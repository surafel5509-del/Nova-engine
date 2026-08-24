package dev.nova.editor.scene

/**
 * 4-neighbor bitmask auto-tiling. Computes a tile index from which of the
 * four neighbors are filled, using a standard 16-tile blob layout:
 * bit0 = filled above, bit1 = filled right, bit2 = filled below, bit3 = left.
 * Pure functions — unit-tested.
 */
object AutoTiler {

    const val MASK_UP = 1
    const val MASK_RIGHT = 2
    const val MASK_DOWN = 4
    const val MASK_LEFT = 8

    /** Bitmask of filled orthogonal neighbors of (col, row). */
    fun neighborMask(map: TilemapComponent, col: Int, row: Int): Int {
        var mask = 0
        if (map.tileAt(col, row + 1) >= 0) mask = mask or MASK_UP
        if (map.tileAt(col + 1, row) >= 0) mask = mask or MASK_RIGHT
        if (map.tileAt(col, row - 1) >= 0) mask = mask or MASK_DOWN
        if (map.tileAt(col - 1, row) >= 0) mask = mask or MASK_LEFT
        return mask
    }

    /**
     * Maps a 4-bit mask to a tile index in a 16-tile blob tileset
     * (tiles 0..15 laid out as the standard bitmask order — the mask IS the
     * index for blob tilesets that follow this convention).
     */
    fun tileIndexForMask(mask: Int, baseIndex: Int = 0): Int = baseIndex + (mask and 0xF)

    /**
     * Recomputes the tile at (col,row) and its 4 neighbors after a paint at
     * (col,row) with [filled]. Returns the updated component.
     */
    fun paintAutoTiled(
        map: TilemapComponent,
        col: Int,
        row: Int,
        filled: Boolean,
        baseIndex: Int = 0,
    ): TilemapComponent {
        var next = map.withTile(col, row, if (filled) tileIndexForMask(0, baseIndex) else -1)
        // Update the cell + its orthogonal neighbors.
        for ((dc, dr) in listOf(0 to 0, 0 to 1, 1 to 0, 0 to -1, -1 to 0)) {
            val c = col + dc
            val r = row + dr
            if (c !in 0 until map.cols || r !in 0 until map.rows) continue
            if (next.tileAt(c, r) >= 0) {
                val mask = neighborMask(next, c, r)
                next = next.withTile(c, r, tileIndexForMask(mask, baseIndex))
            }
        }
        return next
    }
}
