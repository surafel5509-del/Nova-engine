package dev.nova.editor.ui.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.nova.editor.assets.AssetEntry
import dev.nova.editor.assets.AssetStore
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.ui.theme.NovaColors

/**
 * Asset browser: navigates the project's asset tree, previews metadata, and
 * wires texture assets to the selected entity. Real file operations via AssetStore.
 */
@Composable
fun AssetBrowserPanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val store = remember(viewModel.projectPath) { AssetStore(viewModel.projectPath) }
    // assetRevision forces re-listing after file operations.
    val revision = viewModel.assetRevision
    val entries = remember(viewModel.assetDir, revision) { store.list(viewModel.assetDir) }

    var newFolderDialog by remember { mutableStateOf(false) }
    var newFolderName by remember { mutableStateOf("") }

    Column(modifier.fillMaxSize().padding(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (viewModel.assetDir != "assets") {
                IconButton(onClick = {
                    val parent = viewModel.assetDir.substringBeforeLast('/', "assets")
                    viewModel.navigateAssets(parent)
                }) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Up", tint = NovaColors.Text)
                }
            }
            Text(
                "Assets / ${viewModel.assetDir.removePrefix("assets/")}",
                style = MaterialTheme.typography.titleSmall,
                color = NovaColors.Text,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { newFolderDialog = true }) { Text("New Folder") }
            TextButton(onClick = { viewModel.refreshAssets() }) { Text("Refresh") }
        }
        Spacer(Modifier.height(4.dp))

        if (entries.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("Empty folder", color = NovaColors.TextDim, style = MaterialTheme.typography.bodySmall)
            }
        } else {
            LazyColumn {
                items(entries, key = { it.relativePath }) { entry ->
                    AssetRow(
                        entry = entry,
                        onOpen = {
                            if (entry.isDirectory) viewModel.navigateAssets(entry.relativePath)
                        },
                        onAssign = {
                            if (entry.isTexture) viewModel.assignTextureToSelected(entry.relativePath)
                        },
                        onDelete = {
                            if (store.delete(entry.relativePath)) {
                                viewModel.log(LogLevel.INFO, "Deleted '${entry.name}'")
                                viewModel.refreshAssets()
                            } else {
                                viewModel.log(LogLevel.ERROR, "Could not delete '${entry.name}'")
                            }
                        },
                    )
                }
            }
        }
    }

    if (newFolderDialog) {
        AlertDialog(
            onDismissRequest = { newFolderDialog = false },
            title = { Text("New Folder") },
            text = {
                OutlinedTextField(
                    value = newFolderName,
                    onValueChange = { newFolderName = it },
                    singleLine = true,
                    placeholder = { Text("Folder name") },
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    if (store.createFolder(viewModel.assetDir, newFolderName)) {
                        viewModel.log(LogLevel.INFO, "Created folder '$newFolderName'")
                        viewModel.refreshAssets()
                    }
                    newFolderName = ""
                    newFolderDialog = false
                }) { Text("Create") }
            },
            dismissButton = { TextButton(onClick = { newFolderDialog = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun AssetRow(
    entry: AssetEntry,
    onOpen: () -> Unit,
    onAssign: () -> Unit,
    onDelete: () -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onOpen)
            .padding(vertical = 6.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            iconFor(entry),
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.padding(end = 8.dp),
        )
        Column(Modifier.weight(1f)) {
            Text(entry.name, style = MaterialTheme.typography.bodyMedium, color = NovaColors.Text)
            Text(
                subtitleFor(entry),
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
        }
        if (entry.isTexture) {
            TextButton(onClick = onAssign) { Text("Use", style = MaterialTheme.typography.labelSmall) }
        }
        Box {
            Text(
                "⋮",
                color = NovaColors.TextDim,
                modifier = Modifier
                    .clickable { menuExpanded = true }
                    .padding(horizontal = 8.dp, vertical = 4.dp),
            )
            DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                DropdownMenuItem(
                    text = { Text("Delete", color = NovaColors.Error) },
                    onClick = { onDelete(); menuExpanded = false },
                )
            }
        }
    }
}

private fun iconFor(entry: AssetEntry): String = when {
    entry.isDirectory -> "📁"
    entry.isTexture -> "🖼"
    entry.isAudio -> "🔊"
    entry.isScene -> "🎬"
    else -> "📄"
}

private fun subtitleFor(entry: AssetEntry): String = when {
    entry.isDirectory -> "Folder"
    else -> "${entry.extension.uppercase()} · ${formatSize(entry.sizeBytes)}"
}

private fun formatSize(bytes: Long): String = when {
    bytes < 1024 -> "$bytes B"
    bytes < 1024 * 1024 -> "${bytes / 1024} KB"
    else -> "%.1f MB".format(bytes / (1024f * 1024f))
}
