package dev.nova.editor.ui.editor

import android.graphics.BitmapFactory
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.Card
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
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.nova.editor.assets.AssetEntry
import dev.nova.editor.assets.AssetStore
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.ui.theme.NovaColors

/**
 * Assets Library: a central, searchable repository of project resources
 * (textures, audio, scripts) under assets/library/. Supports browsing,
 * search, preview, import (incl. ZIP), and assigning to the selection.
 */
@Composable
fun AssetsLibraryPanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val store = remember(viewModel.projectPath) { AssetStore(viewModel.projectPath) }
    var revision by remember { mutableStateOf(0) }
    var search by remember { mutableStateOf("") }

    // Flatten the library recursively.
    val allAssets = remember(revision) { flattenLibrary(store, "assets/library") }
    val filtered = remember(search, allAssets) {
        if (search.isBlank()) allAssets
        else allAssets.filter { it.name.lowercase().contains(search.lowercase()) }
    }

    val zipLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        uri?.let {
            val bytes = context.contentResolver.openInputStream(it)?.readBytes()
            if (bytes != null) {
                val count = store.importZip(bytes, "assets/library")
                viewModel.log(LogLevel.INFO, "Assets Library: imported $count files from ZIP")
                revision++
                viewModel.refreshAssets()
            }
        }
    }

    Column(modifier.fillMaxSize().padding(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = search,
                onValueChange = { search = it },
                modifier = Modifier.weight(1f),
                placeholder = { Text("Search assets…", style = MaterialTheme.typography.bodySmall) },
                singleLine = true,
                textStyle = MaterialTheme.typography.bodySmall,
            )
            TextButton(onClick = { zipLauncher.launch("application/zip") }) { Text("Import ZIP") }
            TextButton(onClick = { revision++ }) { Text("Refresh") }
        }
        Spacer(Modifier.height(4.dp))

        if (filtered.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text(
                    if (allAssets.isEmpty()) "Assets Library is empty — import a ZIP or textures."
                    else "No assets match \"$search\"",
                    color = NovaColors.TextDim,
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        } else {
            LazyVerticalGrid(columns = GridCells.Adaptive(minSize = 96.dp)) {
                items(filtered, key = { it.relativePath }) { entry ->
                    AssetCard(
                        entry = entry,
                        store = store,
                        onUse = {
                            if (entry.isTexture) {
                                viewModel.assignTextureToSelected(entry.relativePath)
                            }
                        },
                    )
                }
            }
        }
    }
}

/** Recursively collects files under the library folder. */
private fun flattenLibrary(store: AssetStore, dir: String): List<AssetEntry> {
    val result = mutableListOf<AssetEntry>()
    fun walk(d: String) {
        for (entry in store.list(d)) {
            if (entry.isDirectory) walk(entry.relativePath) else result.add(entry)
        }
    }
    walk(dir)
    return result
}

@Composable
private fun AssetCard(entry: AssetEntry, store: AssetStore, onUse: () -> Unit) {
    var menuExpanded by remember { mutableStateOf(false) }
    Card(
        modifier = Modifier
            .padding(4.dp)
            .clickable { menuExpanded = true },
    ) {
        Column(
            modifier = Modifier.padding(4.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (entry.isTexture) {
                val bitmap = remember(entry.relativePath) {
                    store.readBytes(entry.relativePath)?.let { BitmapFactory.decodeByteArray(it, 0, it.size) }
                }
                if (bitmap != null) {
                    Image(
                        bitmap = bitmap.asImageBitmap(),
                        contentDescription = entry.name,
                        modifier = Modifier
                            .fillMaxWidth()
                            .aspectRatio(1f),
                        contentScale = ContentScale.Crop,
                    )
                } else {
                    Text("🖼", style = MaterialTheme.typography.headlineSmall)
                }
            } else {
                Text(
                    when {
                        entry.isAudio -> "🔊"
                        entry.extension == "lua" -> "📜"
                        else -> "📄"
                    },
                    style = MaterialTheme.typography.headlineSmall,
                )
            }
            Text(
                entry.name,
                style = MaterialTheme.typography.labelSmall,
                color = NovaColors.Text,
                maxLines = 1,
            )
            if (entry.isTexture) {
                TextButton(onClick = onUse) { Text("Use", style = MaterialTheme.typography.labelSmall) }
            }
        }
    }
}
