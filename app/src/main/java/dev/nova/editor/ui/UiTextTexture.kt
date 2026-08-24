package dev.nova.editor.ui

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface

/**
 * Renders UI text into an RGBA bitmap the native engine draws as a texture.
 * (Native side has no font stack, so the editor pre-renders text.)
 */
object UiTextTexture {

    class Result(val rgba: ByteArray, val width: Int, val height: Int)

    fun render(
        text: String,
        fontSizePx: Float,
        colorArgb: Int,
        maxWidthPx: Int = 512,
    ): Result? {
        if (text.isBlank()) return null
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            this.color = colorArgb
            textSize = fontSizePx
            typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
            textAlign = Paint.Align.LEFT
        }
        val textWidth = paint.measureText(text).toInt() + 8
        val width = minOf(maxOf(textWidth, 16), maxWidthPx)
        val fm = paint.fontMetrics
        val height = maxOf((fm.descent - fm.ascent).toInt() + 8, 16)

        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawText(text, 4f, -fm.ascent + 4f, paint)

        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)
        bitmap.recycle()

        val rgba = ByteArray(width * height * 4)
        for (i in pixels.indices) {
            val p = pixels[i]
            rgba[i * 4] = ((p shr 16) and 0xFF).toByte()
            rgba[i * 4 + 1] = ((p shr 8) and 0xFF).toByte()
            rgba[i * 4 + 2] = (p and 0xFF).toByte()
            rgba[i * 4 + 3] = ((p shr 24) and 0xFF).toByte()
        }
        return Result(rgba, width, height)
    }
}
