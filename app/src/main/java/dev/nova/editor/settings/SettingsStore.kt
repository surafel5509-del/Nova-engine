package dev.nova.editor.settings

import android.content.Context

/** Persisted editor + engine settings. */
data class EditorSettings(
    val layoutMode: LayoutMode = LayoutMode.AUTO,
    val masterVolume: Float = 1f,
    val showGridDefault: Boolean = true,
    val targetFps: Int = 60,
    val vsyncHint: Boolean = true,
)

enum class LayoutMode(val label: String) {
    AUTO("Auto (by screen)"),
    DESKTOP("Desktop / Window"),
    MOBILE("Mobile"),
}

class SettingsStore(context: Context) {

    private val prefs = context.getSharedPreferences("nova_settings", Context.MODE_PRIVATE)

    fun load(): EditorSettings {
        val modeName = prefs.getString(KEY_LAYOUT, LayoutMode.AUTO.name) ?: LayoutMode.AUTO.name
        return EditorSettings(
            layoutMode = runCatching { LayoutMode.valueOf(modeName) }.getOrDefault(LayoutMode.AUTO),
            masterVolume = prefs.getFloat(KEY_VOLUME, 1f),
            showGridDefault = prefs.getBoolean(KEY_GRID, true),
            targetFps = prefs.getInt(KEY_FPS, 60),
            vsyncHint = prefs.getBoolean(KEY_VSYNC, true),
        )
    }

    fun save(settings: EditorSettings) {
        prefs.edit().apply {
            putString(KEY_LAYOUT, settings.layoutMode.name)
            putFloat(KEY_VOLUME, settings.masterVolume)
            putBoolean(KEY_GRID, settings.showGridDefault)
            putInt(KEY_FPS, settings.targetFps)
            putBoolean(KEY_VSYNC, settings.vsyncHint)
            apply()
        }
    }

    private companion object {
        const val KEY_LAYOUT = "layout_mode"
        const val KEY_VOLUME = "master_volume"
        const val KEY_GRID = "show_grid"
        const val KEY_FPS = "target_fps"
        const val KEY_VSYNC = "vsync"
    }
}
