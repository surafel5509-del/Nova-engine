package dev.nova.editor.ui.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.ui.theme.NovaColors

/** Live profiler HUD: native engine stats + JVM memory. Rendered over the viewport. */
@Composable
fun ProfilerOverlay(viewModel: EditorViewModel, modifier: Modifier = Modifier) {
    val s = viewModel.stats
    val runtime = Runtime.getRuntime()
    val usedMb = (runtime.totalMemory() - runtime.freeMemory()) / (1024 * 1024)
    val maxMb = runtime.maxMemory() / (1024 * 1024)

    Column(
        modifier
            .background(NovaColors.Surface.copy(alpha = 0.85f))
            .padding(8.dp),
    ) {
        Row {
            Stat("FPS", if (s.fps > 0) "%.0f".format(s.fps) else "—")
            Spacer(Modifier.width(12.dp))
            Stat("Frame", "%.2f ms".format(s.frameMs))
            Spacer(Modifier.width(12.dp))
            Stat("Draw calls", "${s.drawCalls}")
        }
        Row {
            Stat("Sprites", "${s.sprites}")
            Spacer(Modifier.width(12.dp))
            Stat("Bodies", "${s.bodies}")
            Spacer(Modifier.width(12.dp))
            Stat("Particles", "${s.particles}")
            Spacer(Modifier.width(12.dp))
            Stat("Heap", "${usedMb}/${maxMb} MB")
        }
    }
}

@Composable
private fun Stat(label: String, value: String) {
    Text("$label  $value", style = MaterialTheme.typography.labelSmall, color = NovaColors.Text)
}
