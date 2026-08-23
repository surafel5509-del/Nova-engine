package dev.nova.editor.ui.editor

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.scene.SceneOps

@Composable
fun RenameEntityDialog(
    initialName: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var name by remember { mutableStateOf(initialName) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Rename entity") },
        text = {
            OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true)
        },
        confirmButton = {
            Button(onClick = { onConfirm(name) }, enabled = name.isNotBlank()) { Text("Rename") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun ReparentDialog(
    viewModel: EditorViewModel,
    entityId: String,
    onDismiss: () -> Unit,
) {
    val descendants = SceneOps.collectWithDescendants(viewModel.scene, entityId)
    val candidates = viewModel.scene.entities.filter { it.id !in descendants }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Reparent entity") },
        text = {
            LazyColumn {
                item {
                    TextButton(
                        onClick = { viewModel.reparentEntity(entityId, null); onDismiss() },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("(Scene root)") }
                }
                items(candidates, key = { it.id }) { candidate ->
                    TextButton(
                        onClick = { viewModel.reparentEntity(entityId, candidate.id); onDismiss() },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text(candidate.name) }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

/** Shared section header used by the inspector. */
@Composable
fun SectionHeader(title: String, modifier: Modifier = Modifier) {
    Column(modifier.fillMaxWidth()) {
        Text(
            title,
            style = androidx.compose.material3.MaterialTheme.typography.titleSmall,
            color = dev.nova.editor.ui.theme.NovaColors.Primary,
        )
    }
}
