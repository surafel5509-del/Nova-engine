package dev.nova.editor.ui.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.ui.theme.NovaColors
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Editor console: real log stream of editor actions and engine events. */
@Composable
fun ConsolePanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val listState = rememberLazyListState()

    LaunchedEffect(viewModel.console.size) {
        if (viewModel.console.isNotEmpty()) {
            listState.animateScrollToItem(viewModel.console.size - 1)
        }
    }

    androidx.compose.foundation.layout.Column(modifier.fillMaxSize()) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "Console",
                style = MaterialTheme.typography.titleSmall,
                color = NovaColors.Text,
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 8.dp),
            )
            TextButton(onClick = { viewModel.clearConsole() }) { Text("Clear") }
        }
        LazyColumn(
            state = listState,
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(NovaColors.Background)
                .padding(horizontal = 8.dp),
        ) {
            items(viewModel.console) { entry ->
                val color = when (entry.level) {
                    LogLevel.INFO -> NovaColors.TextDim
                    LogLevel.WARNING -> NovaColors.Warning
                    LogLevel.ERROR -> NovaColors.Error
                }
                Text(
                    "[${formatTime(entry.timestampMs)}] ${entry.message}",
                    style = MaterialTheme.typography.bodySmall,
                    color = color,
                )
            }
        }
    }
}

private fun formatTime(epochMs: Long): String =
    SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(epochMs))
