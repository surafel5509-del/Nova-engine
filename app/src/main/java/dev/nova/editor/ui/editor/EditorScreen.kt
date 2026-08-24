package dev.nova.editor.ui.editor

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.VerticalDivider
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.isShiftPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.nova.editor.editor.EditorTool
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.editor.PlayState
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.ui.theme.NovaColors

private enum class BottomTab(val label: String) {
    HIERARCHY("Hierarchy"), INSPECTOR("Inspector"), ASSETS("Library"),
    FILES("Files"), SCRIPTS("Scripts"), TIMELINE("Timeline"), AI("AI"), CONSOLE("Console")
}

/**
 * Main editor shell: toolbar + hierarchy + viewport + inspector + console.
 * Layout adapts: wide/landscape = 3-pane; narrow/portrait = viewport + tabs.
 */
@Composable
fun EditorScreen(
    repository: ProjectRepository,
    projectPath: String,
    layoutMode: dev.nova.editor.settings.LayoutMode = dev.nova.editor.settings.LayoutMode.AUTO,
    onBack: () -> Unit,
) {
    val context = LocalContext.current

    val viewModel = remember(projectPath) {
        val (config, scene) = repository.openProject(projectPath)
        EditorViewModel(projectPath, config, scene, repository)
    }

    // Texture import: system picker -> bytes -> project assets -> sprite.
    val importLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        val targetId = viewModel.selectedId
        if (uri != null && targetId != null) {
            runCatching {
                val name = queryDisplayName(context, uri) ?: "texture.png"
                val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
                    ?: error("Could not read file")
                viewModel.importTextureForEntity(targetId, name, bytes)
            }.onFailure {
                viewModel.log(LogLevel.ERROR, "Import failed: ${it.message}")
            }
        }
    }
    val importTexture: () -> Unit = {
        if (viewModel.selectedId == null) {
            viewModel.log(LogLevel.WARNING, "Select an entity before importing a texture")
        } else {
            importLauncher.launch(arrayOf("image/png", "image/jpeg", "image/webp"))
        }
    }

    val configuration = LocalConfiguration.current
    val isWideLayout = when (layoutMode) {
        dev.nova.editor.settings.LayoutMode.DESKTOP -> true
        dev.nova.editor.settings.LayoutMode.MOBILE -> false
        dev.nova.editor.settings.LayoutMode.AUTO ->
            configuration.screenWidthDp >= 840 ||
                configuration.screenWidthDp > configuration.screenHeightDp
    }

    Surface(
        modifier = Modifier
            .fillMaxSize()
            .onKeyEvent { event -> handleShortcut(event, viewModel) },
        color = NovaColors.Background,
    ) {
        Column(Modifier.fillMaxSize()) {
            EditorToolbar(viewModel = viewModel, onBack = onBack)
            HorizontalDivider(color = NovaColors.PanelBorder)

            if (isWideLayout) {
                Row(Modifier.weight(1f)) {
                    HierarchyPanel(viewModel, Modifier.width(220.dp).fillMaxHeight())
                    VerticalDivider(color = NovaColors.PanelBorder)
                    Column(Modifier.weight(1f).fillMaxHeight()) {
                        ViewportWithOverlay(viewModel, Modifier.weight(1f).fillMaxWidth())
                        HorizontalDivider(color = NovaColors.PanelBorder)
                        ConsolePanel(viewModel, Modifier.height(120.dp).fillMaxWidth())
                    }
                    VerticalDivider(color = NovaColors.PanelBorder)
                    InspectorPanel(viewModel, importTexture, Modifier.width(280.dp).fillMaxHeight())
                }
            } else {
                // Compact layout: viewport on top, tabbed panels below.
                var bottomTab by remember { mutableStateOf(BottomTab.HIERARCHY) }
                ViewportWithOverlay(viewModel, Modifier.weight(1f).fillMaxWidth())
                HorizontalDivider(color = NovaColors.PanelBorder)
                TabRow(selectedTabIndex = bottomTab.ordinal) {
                    BottomTab.entries.forEach { tab ->
                        Tab(
                            selected = bottomTab == tab,
                            onClick = { bottomTab = tab },
                            text = { Text(tab.label) },
                        )
                    }
                }
                BoxWithConstraints(Modifier.fillMaxWidth().height(260.dp)) {
                    when (bottomTab) {
                        BottomTab.HIERARCHY -> HierarchyPanel(viewModel)
                        BottomTab.INSPECTOR -> InspectorPanel(viewModel, importTexture)
                        BottomTab.ASSETS -> AssetsLibraryPanel(viewModel)
                        BottomTab.FILES -> FileManagerPanel(viewModel)
                        BottomTab.SCRIPTS -> ScriptEditorPanel(viewModel)
                        BottomTab.TIMELINE -> TimelinePanel(viewModel)
                        BottomTab.AI -> AiAssistantPanel(viewModel)
                        BottomTab.CONSOLE -> ConsolePanel(viewModel)
                    }
                }
            }

            // Status bar: scene, entity count, tool, play state, dirty marker.
            StatusBar(viewModel)
        }
    }
}

@Composable
private fun StatusBar(viewModel: EditorViewModel) {
    val playLabel = when (viewModel.playState) {
        PlayState.PLAYING -> "▶ Playing"
        PlayState.PAUSED -> "❚❚ Paused"
        PlayState.STOPPED -> "■ Stopped"
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(NovaColors.Surface)
            .padding(horizontal = 10.dp, vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            "${viewModel.scene.name}  ·  ${viewModel.scene.entities.size} entities",
            style = MaterialTheme.typography.labelSmall,
            color = NovaColors.TextDim,
        )
        Spacer(Modifier.weight(1f))
        Text(
            "${viewModel.activeTool.label} tool",
            style = MaterialTheme.typography.labelSmall,
            color = NovaColors.TextDim,
        )
        Spacer(Modifier.padding(4.dp))
        Text(
            playLabel,
            style = MaterialTheme.typography.labelSmall,
            color = if (viewModel.playState == PlayState.PLAYING) NovaColors.Accent else NovaColors.TextDim,
        )
        Spacer(Modifier.padding(4.dp))
        Text(
            if (viewModel.dirty) "● Unsaved" else "✓ Saved",
            style = MaterialTheme.typography.labelSmall,
            color = if (viewModel.dirty) NovaColors.Warning else NovaColors.Accent,
        )
    }
}

/** Viewport plus the profiler HUD overlay. */
@Composable
private fun ViewportWithOverlay(viewModel: EditorViewModel, modifier: Modifier = Modifier) {
    Box(modifier) {
        Viewport(viewModel, Modifier.fillMaxSize())
        if (viewModel.profilerVisible) {
            ProfilerOverlay(viewModel, Modifier.align(androidx.compose.ui.Alignment.TopStart))
        }
    }
}

@Composable
private fun EditorToolbar(
    viewModel: EditorViewModel,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var showBuildDialog by remember { mutableStateOf(false) }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 4.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = {
            if (viewModel.dirty) viewModel.save()
            onBack()
        }) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back to projects", tint = NovaColors.Text)
        }

        TextButton(onClick = { viewModel.save() }, enabled = viewModel.dirty) {
            Text(if (viewModel.dirty) "Save *" else "Save")
        }
        TextButton(onClick = { viewModel.undo() }, enabled = viewModel.undoStack.canUndo) {
            Text("Undo")
        }
        TextButton(onClick = { viewModel.redo() }, enabled = viewModel.undoStack.canRedo) {
            Text("Redo")
        }

        VerticalDivider(modifier = Modifier.height(24.dp), color = NovaColors.PanelBorder)

        EditorTool.entries.forEach { tool ->
            FilterChip(
                selected = viewModel.activeTool == tool,
                onClick = { viewModel.setTool(tool) },
                label = { Text(tool.label, style = MaterialTheme.typography.labelSmall) },
                modifier = Modifier.padding(horizontal = 2.dp),
            )
        }

        VerticalDivider(modifier = Modifier.height(24.dp), color = NovaColors.PanelBorder)

        FilterChip(
            selected = viewModel.gridVisible,
            onClick = { viewModel.showGrid(!viewModel.gridVisible) },
            label = { Text("Grid", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.padding(horizontal = 2.dp),
        )
        FilterChip(
            selected = viewModel.snapEnabled,
            onClick = { viewModel.enableSnapping(!viewModel.snapEnabled) },
            label = { Text("Snap", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.padding(horizontal = 2.dp),
        )
        FilterChip(
            selected = viewModel.autoTile,
            onClick = { viewModel.toggleAutoTile() },
            label = { Text("AutoTile", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.padding(horizontal = 2.dp),
        )

        VerticalDivider(modifier = Modifier.height(24.dp), color = NovaColors.PanelBorder)

        // Play mode controls (functional).
        TextButton(
            onClick = { viewModel.play() },
            enabled = viewModel.playState != PlayState.PLAYING,
        ) { Text(if (viewModel.playState == PlayState.PAUSED) "Resume" else "Play") }
        TextButton(
            onClick = { viewModel.pause() },
            enabled = viewModel.playState == PlayState.PLAYING,
        ) { Text("Pause") }
        TextButton(
            onClick = { viewModel.stop() },
            enabled = viewModel.playState != PlayState.STOPPED,
        ) { Text("Stop") }

        VerticalDivider(modifier = Modifier.height(24.dp), color = NovaColors.PanelBorder)

        FilterChip(
            selected = viewModel.gameView,
            onClick = { viewModel.toggleGameView() },
            label = { Text("Game", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.padding(horizontal = 2.dp),
        )
        FilterChip(
            selected = viewModel.physicsDebug,
            onClick = { viewModel.togglePhysicsDebug() },
            label = { Text("Physics", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.padding(horizontal = 2.dp),
        )
        FilterChip(
            selected = viewModel.profilerVisible,
            onClick = { viewModel.toggleProfiler() },
            label = { Text("Stats", style = MaterialTheme.typography.labelSmall) },
            modifier = Modifier.padding(horizontal = 2.dp),
        )

        VerticalDivider(modifier = Modifier.height(24.dp), color = NovaColors.PanelBorder)
        TextButton(onClick = {
            // Save first so the runtime loads the latest scene.
            viewModel.save()
            val intent = android.content.Intent(context, dev.nova.editor.runtime.GameActivity::class.java)
                .putExtra(dev.nova.editor.runtime.GameActivity.EXTRA_PROJECT_PATH, viewModel.projectPath)
            context.startActivity(intent)
        }) { Text("Run ▶") }
        TextButton(onClick = { showBuildDialog = true }) { Text("Build") }
    }

    if (showBuildDialog) {
        BuildExportDialog(viewModel, onDismiss = { showBuildDialog = false })
    }
}

private fun handleShortcut(
    event: androidx.compose.ui.input.key.KeyEvent,
    viewModel: EditorViewModel,
): Boolean {
    if (event.type != KeyEventType.KeyDown) return false
    val ctrl = event.isCtrlPressed
    val shift = event.isShiftPressed
    when {
        ctrl && event.key == Key.S -> { viewModel.save(); return true }
        ctrl && shift && event.key == Key.Z -> { viewModel.redo(); return true }
        ctrl && event.key == Key.Z -> { viewModel.undo(); return true }
        ctrl && event.key == Key.D -> { viewModel.selectedId?.let(viewModel::duplicateEntity); return true }
        event.key == Key.Delete -> { viewModel.selectedId?.let(viewModel::deleteEntity); return true }
        event.key == Key.W -> { viewModel.setTool(EditorTool.MOVE); return true }
        event.key == Key.Q -> { viewModel.setTool(EditorTool.SELECT); return true }
        event.key == Key.G -> { viewModel.toggleGameView(); return true }
        event.key == Key.Spacebar -> {
            when (viewModel.playState) {
                PlayState.PLAYING -> viewModel.pause()
                else -> viewModel.play()
            }
            return true
        }
    }
    return false
}

private fun queryDisplayName(context: android.content.Context, uri: android.net.Uri): String? =
    runCatching {
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        }
    }.getOrNull()
