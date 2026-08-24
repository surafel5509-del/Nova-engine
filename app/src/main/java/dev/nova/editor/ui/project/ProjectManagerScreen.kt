package dev.nova.editor.ui.project

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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import dev.nova.editor.project.ProjectDimension
import dev.nova.editor.project.ProjectOrientation
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.project.ProjectTemplate
import dev.nova.editor.project.RecentProject
import dev.nova.editor.ui.theme.NovaColors
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun ProjectManagerScreen(
    repository: ProjectRepository,
    onOpenProject: (String) -> Unit,
    onOpenSettings: () -> Unit = {},
) {
    var projects by remember { mutableStateOf(repository.listProjects()) }
    var showCreateDialog by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    text = "Nova Engine",
                    style = MaterialTheme.typography.headlineLarge,
                    color = NovaColors.Primary,
                    fontWeight = FontWeight.Bold,
                )
                Text(
                    text = "Project Manager",
                    style = MaterialTheme.typography.titleMedium,
                    color = NovaColors.TextDim,
                )
            }
            OutlinedButton(onClick = onOpenSettings) { Text("⚙ Settings") }
        }
        Spacer(Modifier.height(24.dp))

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(onClick = { showCreateDialog = true }) { Text("Create Project") }
            OutlinedButton(
                onClick = {
                    errorMessage = "Import from external storage is planned for Phase 5 (Build/Export)."
                },
            ) { Text("Import Project") }
        }

        errorMessage?.let {
            Spacer(Modifier.height(8.dp))
            Text(it, color = NovaColors.Warning, style = MaterialTheme.typography.bodySmall)
        }

        Spacer(Modifier.height(24.dp))
        Text("Recent Projects", style = MaterialTheme.typography.titleSmall, color = NovaColors.Text)
        Spacer(Modifier.height(8.dp))

        if (projects.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("No projects yet. Create one to get started.", color = NovaColors.TextDim)
            }
        } else {
            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(projects, key = { it.path }) { project ->
                    RecentProjectRow(
                        project = project,
                        onOpen = { onOpenProject(project.path) },
                        onDelete = {
                            repository.deleteProject(project.path)
                            projects = repository.listProjects()
                        },
                    )
                }
            }
        }
    }

    if (showCreateDialog) {
        CreateProjectDialog(
            onDismiss = { showCreateDialog = false },
            onCreate = { name, pkg, version, orientation, dimension, template ->
                val result = runCatching {
                    repository.createProject(name, pkg, version, orientation, dimension, template)
                }
                result.onSuccess { dir ->
                    showCreateDialog = false
                    onOpenProject(dir.absolutePath)
                }.onFailure {
                    errorMessage = it.message
                }
            },
        )
    }
}

@Composable
private fun RecentProjectRow(
    project: RecentProject,
    onOpen: () -> Unit,
    onDelete: () -> Unit,
) {
    var confirmDelete by remember { mutableStateOf(false) }
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onOpen),
        colors = CardDefaults.cardColors(containerColor = NovaColors.Surface),
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(project.name, style = MaterialTheme.typography.titleMedium, color = NovaColors.Text)
                Text(
                    formatEpoch(project.lastOpenedEpochMs),
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.TextDim,
                )
            }
            TextButton(onClick = { confirmDelete = true }) {
                Text("Delete", color = NovaColors.Error)
            }
        }
    }
    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete project?") },
            text = { Text("'${project.name}' will be permanently deleted.") },
            confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete() }) {
                    Text("Delete", color = NovaColors.Error)
                }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun CreateProjectDialog(
    onDismiss: () -> Unit,
    onCreate: (String, String, String, ProjectOrientation, ProjectDimension, ProjectTemplate) -> Unit,
) {
    var name by remember { mutableStateOf("MyGame") }
    var packageName by remember { mutableStateOf("com.example.mygame") }
    var version by remember { mutableStateOf("1.0.0") }
    var orientation by remember { mutableStateOf(ProjectOrientation.LANDSCAPE) }
    var dimension by remember { mutableStateOf(ProjectDimension.TWO_D) }
    var template by remember { mutableStateOf(ProjectTemplate.EMPTY) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Create Project") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(value = name, onValueChange = { name = it }, label = { Text("Name") }, singleLine = true)
                OutlinedTextField(value = packageName, onValueChange = { packageName = it }, label = { Text("Package name") }, singleLine = true)
                OutlinedTextField(value = version, onValueChange = { version = it }, label = { Text("Version") }, singleLine = true)

                Text("Orientation", style = MaterialTheme.typography.labelMedium, color = NovaColors.TextDim)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ProjectOrientation.entries.forEach { option ->
                        FilterChip(
                            selected = orientation == option,
                            onClick = { orientation = option },
                            label = { Text(option.label) },
                        )
                    }
                }

                Text("Dimension", style = MaterialTheme.typography.labelMedium, color = NovaColors.TextDim)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    // 2D and 3D are both supported; 2D+3D is a future mixed mode.
                    listOf(ProjectDimension.TWO_D, ProjectDimension.THREE_D).forEach { option ->
                        FilterChip(
                            selected = dimension == option,
                            onClick = { dimension = option },
                            label = { Text(if (option == ProjectDimension.TWO_D) "2D" else "3D") },
                        )
                    }
                }
                Text(
                    if (dimension == ProjectDimension.THREE_D)
                        "3D project: meshes, lighting, orbit camera, 3D editor."
                    else "2D project: sprites, tilemaps, physics, scripts.",
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.TextDim,
                )

                Text("Template", style = MaterialTheme.typography.labelMedium, color = NovaColors.TextDim)
                TemplateDropdown(template) { template = it }
                Text(
                    templateDescription(template),
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.TextDim,
                )
            }
        },
        confirmButton = {
            Button(
                onClick = { onCreate(name, packageName, version, orientation, dimension, template) },
                enabled = name.isNotBlank() && packageName.isNotBlank(),
            ) { Text("Create") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

@Composable
private fun TemplateDropdown(selected: ProjectTemplate, onSelected: (ProjectTemplate) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
            Text(selected.label)
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            ProjectTemplate.entries.forEach { option ->
                DropdownMenuItem(
                    text = { Text(if (option.implemented) option.label else "${option.label} (Phase 2+)") },
                    enabled = option.implemented,
                    onClick = {
                        onSelected(option)
                        expanded = false
                    },
                )
            }
        }
    }
}

private fun templateDescription(template: ProjectTemplate): String = when (template) {
    ProjectTemplate.EMPTY -> "Blank scene with a camera."
    ProjectTemplate.PLATFORMER -> "Static ground + dynamic player. Press Play to watch it fall and land."
    ProjectTemplate.RPG -> "Animated hero (sprite-sheet) + particle torch. Press Play to animate."
    ProjectTemplate.ARCADE -> "Bouncy ball + floor. Press Play to watch it bounce."
}

private fun formatEpoch(epochMs: Long): String =
    if (epochMs <= 0L) "Never opened"
    else SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault()).format(Date(epochMs))
