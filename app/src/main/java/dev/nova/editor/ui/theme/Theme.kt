package dev.nova.editor.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.sp

/** Larger, more readable defaults than Material's (addresses "text too small"). */
private val NovaTypography = Typography(
    headlineLarge = TextStyle(fontSize = 30.sp, lineHeight = 36.sp),
    headlineMedium = TextStyle(fontSize = 26.sp, lineHeight = 32.sp),
    titleLarge = TextStyle(fontSize = 22.sp, lineHeight = 28.sp),
    titleMedium = TextStyle(fontSize = 18.sp, lineHeight = 24.sp),
    titleSmall = TextStyle(fontSize = 16.sp, lineHeight = 22.sp),
    bodyLarge = TextStyle(fontSize = 17.sp, lineHeight = 24.sp),
    bodyMedium = TextStyle(fontSize = 15.sp, lineHeight = 22.sp),
    bodySmall = TextStyle(fontSize = 13.sp, lineHeight = 18.sp),
    labelLarge = TextStyle(fontSize = 14.sp, lineHeight = 20.sp),
    labelMedium = TextStyle(fontSize = 12.sp, lineHeight = 16.sp),
    labelSmall = TextStyle(fontSize = 11.sp, lineHeight = 15.sp),
)

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
        typography = NovaTypography,
        content = content,
    )
}
