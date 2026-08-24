package dev.nova.editor.ui.editor

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
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
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.nova.editor.assets.AssetEntry
import dev.nova.editor.assets.AssetStore
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.ui.theme.NovaColors

/**
 * Full project file manager: navigate, create folders/files, rename, move,
 * delete, and import complete game-source ZIP files.
 */
@Composable
fun FileManagerPanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val store = remember(viewModel.projectPath) { AssetStore(viewModel.projectPath) }
    var currentDir by remember { mutableStateOf("") }
    var revision by remember { mutableStateOf(0) }
    var searchQuery by remember { mutableStateOf("") }
    var sortBySize by remember { mutableStateOf(false) }
    val entries = remember(currentDir, revision, searchQuery, sortBySize) {
        var list = store.list(currentDir)
        if (searchQuery.isNotBlank()) {
            list = list.filter { it.name.lowercase().contains(searchQuery.lowercase()) }
        }
        if (sortBySize) list.sortedWith(compareBy({ !it.isDirectory }, { -it.sizeBytes })) else list
    }

    var dialog by remember { mutableStateOf<FileDialog?>(null) }
    var dialogText by remember { mutableStateOf("") }
    var moveSource by remember { mutableStateOf<AssetEntry?>(null) }
    var clipboardPath by remember { mutableStateOf<String?>(null) }
    var previewEntry by remember { mutableStateOf<AssetEntry?>(null) }

    val zipLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let {
            val bytes = context.contentResolver.openInputStream(it)?.readBytes()
            if (bytes != null) {
                val dest = currentDir.ifBlank { "assets" }
                val count = store.importZip(bytes, dest)
                viewModel.log(LogLevel.INFO, "Imported ZIP: $count files into '$dest'")
                viewModel.refreshAssets()
                revision++
            }
        }
    }

    Column(modifier.fillMaxSize().padding(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (currentDir.isNotEmpty()) {
                IconButton(onClick = { currentDir = currentDir.substringBeforeLast('/', "") }) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Up", tint = NovaColors.Text)
                }
            }
            Text(
                if (currentDir.isEmpty()) "Project files" else currentDir,
                style = MaterialTheme.typography.titleSmall,
                color = NovaColors.Text,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { dialog = FileDialog.NEW_FOLDER }) { Text("+ Folder") }
            TextButton(onClick = { dialog = FileDialog.NEW_FILE }) { Text("+ File") }
            TextButton(onClick = { zipLauncher.launch("application/zip") }) { Text("ZIP") }
        }
        OutlinedTextField(
            value = searchQuery,
            onValueChange = { searchQuery = it },
            modifier = Modifier.fillMaxWidth(),
            placeholder = { Text("Search / filter…", style = MaterialTheme.typography.bodySmall) },
            singleLine = true,
            textStyle = MaterialTheme.typography.bodySmall,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { sortBySize = !sortBySize }) {
                Text(if (sortBySize) "Sort: size" else "Sort: name", style = MaterialTheme.typography.labelSmall)
            }
            clipboardPath?.let {
                TextButton(onClick = {
                    if (store.copyTo(it, currentDir) != null) {
                        viewModel.log(LogLevel.INFO, "Pasted into '$currentDir'")
                        revision++
                    }
                }) { Text("Paste", style = MaterialTheme.typography.labelSmall) }
            }
            TextButton(onClick = {
                val outFile = java.io.File(viewModel.projectPath, "export.zip")
                val count = store.exportZip(currentDir.ifBlank { "assets" }, outFile)
                viewModel.log(LogLevel.INFO, "Exported $count files to export.zip")
            }) { Text("Export ZIP", style = MaterialTheme.typography.labelSmall) }
        }
        Spacer(Modifier.height(4.dp))

        if (entries.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("Empty folder", color = NovaColors.TextDim, style = MaterialTheme.typography.bodySmall)
            }
        } else {
            LazyColumn {
                items(entries, key = { it.relativePath }) { entry ->
                    FileRow(
                        entry = entry,
                        onOpen = {
                            if (entry.isDirectory) currentDir = entry.relativePath
                            else previewEntry = entry
                        },
                        onRename = { dialog = FileDialog.RENAME; dialogText = entry.name; moveSource = entry },
                        onMove = {
                            // Move up one level (simple, reliable on mobile).
                            val parent = entry.relativePath.substringBeforeLast('/', "")
                            val grand = parent.substringBeforeLast('/', "")
                            if (store.move(entry.relativePath, grand) != null) {
                                viewModel.log(LogLevel.INFO, "Moved '${entry.name}' up")
                                revision++
                            }
                        },
                        onCopy = { clipboardPath = entry.relativePath },
                        onDuplicate = {
                            store.duplicate(entry.relativePath)?.let {
                                viewModel.log(LogLevel.INFO, "Duplicated '${entry.name}'")
                                revision++
                            }
                        },
                        onDelete = {
                            if (store.delete(entry.relativePath)) {
                                viewModel.log(LogLevel.INFO, "Deleted '${entry.name}'")
                                revision++
                            }
                        },
                    )
                }
            }
        }
    }

    // File preview dialog (image + text).
    previewEntry?.let { entry ->
        AlertDialog(
            onDismissRequest = { previewEntry = null },
            title = { Text(entry.name) },
            text = {
                if (entry.isTexture) {
                    val bitmap = remember(entry.relativePath) {
                        store.readBytes(entry.relativePath)?.let {
                            android.graphics.BitmapFactory.decodeByteArray(it, 0, it.size)
                        }
                    }
                    if (bitmap != null) {
                        androidx.compose.foundation.Image(
                            bitmap = bitmap.asImageBitmap(),
                            contentDescription = entry.name,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    } else {
                        Text("Could not decode image.", color = NovaColors.Warning)
                    }
                } else {
                    val text = remember(entry.relativePath) {
                        store.readBytes(entry.relativePath)?.decodeToString()?.take(2000) ?: "(binary)"
                    }
                    Text(text, style = MaterialTheme.typography.bodySmall, color = NovaColors.TextDim)
                }
            },
            confirmButton = { TextButton(onClick = { previewEntry = null }) { Text("Close") } },
        )
    }

    if (dialog == FileDialog.NEW_FOLDER || dialog == FileDialog.NEW_FILE || dialog == FileDialog.RENAME) {
        val title = when (dialog) {
            FileDialog.NEW_FOLDER -> "New folder"
            FileDialog.NEW_FILE -> "New file"
            FileDialog.RENAME -> "Rename"
            else -> ""
        }
        AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text(title) },
            text = {
                OutlinedTextField(
                    value = dialogText,
                    onValueChange = { dialogText = it },
                    singleLine = true,
                    placeholder = { Text("Name") },
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    when (dialog) {
                        FileDialog.NEW_FOLDER -> store.createFolder(currentDir, dialogText)
                        FileDialog.NEW_FILE -> store.createFile(currentDir, dialogText)
                        FileDialog.RENAME -> moveSource?.let { store.rename(it.relativePath, dialogText) }
                        else -> false
                    }
                    revision++
                    dialogText = ""
                    dialog = null
                }) { Text("OK") }
            },
            dismissButton = { TextButton(onClick = { dialog = null; dialogText = "" }) { Text("Cancel") } },
        )
    }
}

private enum class FileDialog { NEW_FOLDER, NEW_FILE, RENAME }

@Composable
private fun FileRow(
    entry: AssetEntry,
    onOpen: () -> Unit,
    onRename: () -> Unit,
    onMove: () -> Unit,
    onCopy: () -> Unit,
    onDuplicate: () -> Unit,
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
            if (entry.isDirectory) "📁" else fileIcon(entry),
            modifier = Modifier.padding(end = 8.dp),
        )
        Column(Modifier.weight(1f)) {
            Text(entry.name, style = MaterialTheme.typography.bodyMedium, color = NovaColors.Text)
            if (!entry.isDirectory) {
                Text(
                    "${entry.extension.uppercase()} · ${entry.sizeBytes} B",
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.TextDim,
                )
            }
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
                DropdownMenuItem(text = { Text("Rename") }, onClick = { onRename(); menuExpanded = false })
                DropdownMenuItem(text = { Text("Move up") }, onClick = { onMove(); menuExpanded = false })
                DropdownMenuItem(text = { Text("Copy") }, onClick = { onCopy(); menuExpanded = false })
                DropdownMenuItem(text = { Text("Duplicate") }, onClick = { onDuplicate(); menuExpanded = false })
                DropdownMenuItem(
                    text = { Text("Delete", color = NovaColors.Error) },
                    onClick = { onDelete(); menuExpanded = false },
                )
            }
        }
    }
}

private fun fileIcon(entry: AssetEntry): String = when {
    entry.isTexture -> "🖼"
    entry.isAudio -> "🔊"
    entry.extension == "lua" -> "📜"
    entry.extension == "json" -> "🧾"
    entry.extension == "zip" -> "🗜"
    else -> "📄"
}
