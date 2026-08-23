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
    data class Editor(val projectPath: String) : AppDestination
}

@Composable
fun NovaApp() {
    val context = LocalContext.current
    val repository = remember {
        ProjectRepository(File(context.filesDir, "projects"))
    }
    var destination by remember { mutableStateOf<AppDestination>(AppDestination.ProjectManager) }

    when (val dest = destination) {
        AppDestination.ProjectManager -> ProjectManagerScreen(
            repository = repository,
            onOpenProject = { path -> destination = AppDestination.Editor(path) },
        )
        is AppDestination.Editor -> EditorScreen(
            repository = repository,
            projectPath = dest.projectPath,
            onBack = { destination = AppDestination.ProjectManager },
        )
    }
}
