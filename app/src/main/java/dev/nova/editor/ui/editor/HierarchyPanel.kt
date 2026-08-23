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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.scene.Entity
import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.ui.theme.NovaColors

/** Scene hierarchy tree with search, enable toggles, and entity ops menu. */
@Composable
fun HierarchyPanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    var addMenuExpanded by remember { mutableStateOf(false) }
    var collapsedIds by remember { mutableStateOf(setOf<String>()) }

    Column(modifier.fillMaxSize().padding(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "Hierarchy",
                style = MaterialTheme.typography.titleSmall,
                color = NovaColors.Text,
                modifier = Modifier.weight(1f),
            )
            Box {
                IconButton(onClick = { addMenuExpanded = true }) {
                    Icon(Icons.Default.Add, contentDescription = "Add entity", tint = NovaColors.Primary)
                }
                DropdownMenu(expanded = addMenuExpanded, onDismissRequest = { addMenuExpanded = false }) {
                    EntityKind.entries.forEach { kind ->
                        DropdownMenuItem(
                            text = { Text(SceneOps.defaultName(kind)) },
                            onClick = {
                                viewModel.addEntity(kind)
                                addMenuExpanded = false
                            },
                        )
                    }
                }
            }
        }

        OutlinedTextField(
            value = viewModel.hierarchyFilter,
            onValueChange = viewModel::applyHierarchyFilter,
            modifier = Modifier.fillMaxWidth(),
            placeholder = { Text("Search entities", style = MaterialTheme.typography.bodySmall) },
            singleLine = true,
            textStyle = MaterialTheme.typography.bodySmall,
        )
        Spacer(Modifier.height(4.dp))

        val ordered = SceneOps.hierarchyOrder(viewModel.scene)
        val filter = viewModel.hierarchyFilter.trim().lowercase()
        val visible = ordered.filter { entity ->
            (filter.isEmpty() || entity.name.lowercase().contains(filter)) &&
                !isHiddenByCollapse(viewModel.scene, entity, collapsedIds)
        }

        if (visible.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("No entities", color = NovaColors.TextDim, style = MaterialTheme.typography.bodySmall)
            }
        } else {
            LazyColumn {
                items(visible, key = { it.id }) { entity ->
                    EntityRow(
                        entity = entity,
                        depth = SceneOps.depthOf(viewModel.scene, entity.id),
                        selected = entity.id == viewModel.selectedId,
                        hasChildren = SceneOps.childrenOf(viewModel.scene, entity.id).isNotEmpty(),
                        collapsed = entity.id in collapsedIds,
                        onToggleCollapse = {
                            collapsedIds = if (entity.id in collapsedIds) {
                                collapsedIds - entity.id
                            } else {
                                collapsedIds + entity.id
                            }
                        },
                        onSelect = { viewModel.select(entity.id) },
                        onToggleEnabled = { viewModel.setEntityEnabled(entity.id, !entity.enabled) },
                        viewModel = viewModel,
                    )
                }
            }
        }
    }
}

private fun isHiddenByCollapse(
    scene: dev.nova.editor.scene.Scene,
    entity: Entity,
    collapsedIds: Set<String>,
): Boolean {
    var current = entity.parentId
    var guard = 0
    while (current != null && guard < 1000) {
        if (current in collapsedIds) return true
        current = SceneOps.find(scene, current)?.parentId
        guard++
    }
    return false
}

@Composable
private fun EntityRow(
    entity: Entity,
    depth: Int,
    selected: Boolean,
    hasChildren: Boolean,
    collapsed: Boolean,
    onToggleCollapse: () -> Unit,
    onSelect: () -> Unit,
    onToggleEnabled: () -> Unit,
    viewModel: EditorViewModel,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    var renameDialog by remember { mutableStateOf(false) }
    var reparentDialog by remember { mutableStateOf(false) }

    val background = if (selected) NovaColors.SurfaceVariant else androidx.compose.ui.graphics.Color.Transparent
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(background)
            .clickable(onClick = onSelect)
            .padding(start = (depth * 14).dp, top = 2.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (hasChildren) {
            Icon(
                imageVector = if (collapsed) Icons.Default.KeyboardArrowRight else Icons.Default.KeyboardArrowDown,
                contentDescription = if (collapsed) "Expand" else "Collapse",
                tint = NovaColors.TextDim,
                modifier = Modifier
                    .width(20.dp)
                    .clickable(onClick = onToggleCollapse),
            )
        } else {
            Spacer(Modifier.width(20.dp))
        }
        Text(
            entity.name,
            style = MaterialTheme.typography.bodyMedium,
            color = if (entity.enabled) NovaColors.Text else NovaColors.TextDim,
            modifier = Modifier.weight(1f),
        )
        Switch(
            checked = entity.enabled,
            onCheckedChange = { onToggleEnabled() },
        )
        Box {
            Text(
                "⋮",
                color = NovaColors.TextDim,
                modifier = Modifier
                    .clickable { menuExpanded = true }
                    .padding(horizontal = 8.dp, vertical = 4.dp),
            )
            DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                DropdownMenuItem(text = { Text("Rename") }, onClick = { renameDialog = true; menuExpanded = false })
                DropdownMenuItem(text = { Text("Duplicate") }, onClick = { viewModel.duplicateEntity(entity.id); menuExpanded = false })
                DropdownMenuItem(text = { Text("Reparent…") }, onClick = { reparentDialog = true; menuExpanded = false })
                DropdownMenuItem(
                    text = { Text("Add child Sprite") },
                    onClick = { viewModel.addEntity(EntityKind.SPRITE, parentId = entity.id); menuExpanded = false },
                )
                DropdownMenuItem(
                    text = { Text("Delete", color = NovaColors.Error) },
                    onClick = { viewModel.deleteEntity(entity.id); menuExpanded = false },
                )
            }
        }
    }

    if (renameDialog) {
        RenameEntityDialog(
            initialName = entity.name,
            onDismiss = { renameDialog = false },
            onConfirm = { viewModel.renameEntity(entity.id, it); renameDialog = false },
        )
    }
    if (reparentDialog) {
        ReparentDialog(
            viewModel = viewModel,
            entityId = entity.id,
            onDismiss = { reparentDialog = false },
        )
    }
}
