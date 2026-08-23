package dev.nova.editor.ui.editor

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.nova.editor.build.BuildExporter
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.ui.theme.NovaColors
import java.io.File

/**
 * Build/export dialog: exports a portable .novapkg immediately (real) and
 * shows the exact command for producing a standalone game APK.
 */
@Composable
fun BuildExportDialog(viewModel: EditorViewModel, onDismiss: () -> Unit) {
    var resultMessage by remember { mutableStateOf<String?>(null) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Build & Export") },
        text = {
            Column {
                Text(
                    "Export a portable game package (.novapkg) of this project.",
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.Text,
                )
                if (resultMessage != null) {
                    Spacer(Modifier.height(8.dp))
                    Text(resultMessage!!, style = MaterialTheme.typography.bodySmall, color = NovaColors.Primary)
                }
                Spacer(Modifier.height(12.dp))
                Text(
                    "Standalone game APK (on a dev machine):",
                    style = MaterialTheme.typography.labelSmall,
                    color = NovaColors.TextDim,
                )
                Text(
                    BuildExporter.apkBuildCommand(viewModel.projectPath),
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.Text,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = {
                viewModel.save()
                val outFile = File(viewModel.projectPath, "${viewModel.config.name.lowercase().replace(' ', '-')}.novapkg")
                runCatching {
                    BuildExporter.exportPackage(viewModel.projectPath, outFile)
                }.onSuccess { entries ->
                    resultMessage = "Exported ${outFile.name} ($entries files)"
                    viewModel.log(LogLevel.INFO, "Exported package '${outFile.absolutePath}' ($entries entries)")
                }.onFailure { error ->
                    resultMessage = "Export failed: ${error.message}"
                    viewModel.log(LogLevel.ERROR, "Export failed: ${error.message}")
                }
            }) { Text("Export .novapkg") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Close") }
        },
    )
}
