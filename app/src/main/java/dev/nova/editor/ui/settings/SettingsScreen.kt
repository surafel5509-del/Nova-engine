package dev.nova.editor.ui.settings

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.nova.editor.settings.EditorSettings
import dev.nova.editor.settings.LayoutMode
import dev.nova.editor.settings.SettingsStore
import dev.nova.editor.ui.theme.NovaColors

/**
 * Settings screen: display/layout mode, graphics, audio, editor, performance.
 * Layout mode actually switches the editor between desktop and mobile layouts.
 */
@Composable
fun SettingsScreen(
    onBack: () -> Unit,
    onSettingsChanged: (EditorSettings) -> Unit,
) {
    val context = LocalContext.current
    val store = remember { SettingsStore(context) }
    var settings by remember { mutableStateOf(store.load()) }

    fun update(next: EditorSettings) {
        settings = next
        store.save(next)
        onSettingsChanged(next)
    }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("← Back") }
            Text("Settings", style = MaterialTheme.typography.titleLarge, color = NovaColors.Text)
        }
        Spacer(Modifier.height(12.dp))

        SectionTitle("Display / Layout")
        Text(
            "Window mode shows the full desktop editor; Mobile mode adapts to touch.",
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.TextDim,
        )
        Row {
            LayoutMode.entries.forEach { mode ->
                FilterChip(
                    selected = settings.layoutMode == mode,
                    onClick = { update(settings.copy(layoutMode = mode)) },
                    label = { Text(mode.label) },
                    modifier = Modifier.padding(end = 6.dp),
                )
            }
        }
        Spacer(Modifier.height(8.dp))
        HorizontalDivider(color = NovaColors.PanelBorder)

        SectionTitle("Graphics")
        SettingSwitch("Show grid by default", settings.showGridDefault) {
            update(settings.copy(showGridDefault = it))
        }
        SettingSwitch("VSync hint", settings.vsyncHint) {
            update(settings.copy(vsyncHint = it))
        }
        HorizontalDivider(color = NovaColors.PanelBorder)

        SectionTitle("Audio")
        Text(
            "Master volume: ${(settings.masterVolume * 100).toInt()}%",
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.Text,
        )
        Slider(
            value = settings.masterVolume,
            onValueChange = { update(settings.copy(masterVolume = it)) },
            valueRange = 0f..1f,
        )
        HorizontalDivider(color = NovaColors.PanelBorder)

        SectionTitle("Performance")
        Text(
            "Target frame rate: ${settings.targetFps} fps",
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.Text,
        )
        Slider(
            value = settings.targetFps.toFloat(),
            onValueChange = { update(settings.copy(targetFps = it.toInt())) },
            valueRange = 30f..120f,
            steps = 8,
        )
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.titleSmall,
        color = NovaColors.Primary,
        modifier = Modifier.padding(vertical = 8.dp),
    )
}

@Composable
private fun SettingSwitch(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Text(label, style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}
