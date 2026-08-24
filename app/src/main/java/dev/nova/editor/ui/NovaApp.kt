package dev.nova.editor.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.ui.editor.EditorScreen
import dev.nova.editor.ui.project.ProjectManagerScreen
import java.io.File

/** Minimal state-based navigation (avoids the navigation-compose dependency). */
sealed interface AppDestination {
    data object ProjectManager : AppDestination
    data object Settings : AppDestination
    data class Editor(val projectPath: String) : AppDestination
}

@Composable
fun NovaApp() {
    val context = LocalContext.current
    val repository = remember {
        ProjectRepository(File(context.filesDir, "projects"))
    }
    val settingsStore = remember { dev.nova.editor.settings.SettingsStore(context) }
    var settings by remember { mutableStateOf(settingsStore.load()) }
    var destination by remember { mutableStateOf<AppDestination>(AppDestination.ProjectManager) }

    when (val dest = destination) {
        AppDestination.ProjectManager -> ProjectManagerScreen(
            repository = repository,
            onOpenProject = { path -> destination = AppDestination.Editor(path) },
            onOpenSettings = { destination = AppDestination.Settings },
        )
        AppDestination.Settings -> dev.nova.editor.ui.settings.SettingsScreen(
            onBack = { destination = AppDestination.ProjectManager },
            onSettingsChanged = { settings = it },
        )
        is AppDestination.Editor -> EditorScreen(
            repository = repository,
            projectPath = dest.projectPath,
            layoutMode = settings.layoutMode,
            onBack = { destination = AppDestination.ProjectManager },
        )
    }
}
