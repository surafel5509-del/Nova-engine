package dev.nova.editor.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object NovaColors {
    val Background = Color(0xFF14171C)
    val Surface = Color(0xFF1B1F27)
    val SurfaceVariant = Color(0xFF232833)
    val PanelBorder = Color(0xFF2E3542)
    val Primary = Color(0xFF4FC3F7)
    val OnPrimary = Color(0xFF06222F)
    val Text = Color(0xFFD7DEE8)
    val TextDim = Color(0xFF8A94A6)
    val Accent = Color(0xFF7EE787)
    val Warning = Color(0xFFE3B341)
    val Error = Color(0xFFF47067)
    val Selection = Color(0xFF4FC3F7)
}

private val NovaDarkScheme = darkColorScheme(
    primary = NovaColors.Primary,
    onPrimary = NovaColors.OnPrimary,
    background = NovaColors.Background,
    onBackground = NovaColors.Text,
    surface = NovaColors.Surface,
    onSurface = NovaColors.Text,
    surfaceVariant = NovaColors.SurfaceVariant,
    onSurfaceVariant = NovaColors.Text,
    outline = NovaColors.PanelBorder,
    secondary = NovaColors.Accent,
    error = NovaColors.Error,
)

@Composable
fun NovaTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = NovaDarkScheme,
        content = content,
    )
}
