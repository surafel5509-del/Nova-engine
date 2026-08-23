package dev.nova.editor.ui.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.editor.LogLevel
import dev.nova.editor.scene.ScriptComponent
import dev.nova.editor.ui.theme.NovaColors
import java.io.File

private const val NEW_SCRIPT_TEMPLATE = """-- Nova game script (Lua 5.4)
-- Lifecycle: on_start(entityId) once, on_update(entityId, dt) every frame.
-- API: nova.get_position/set_position, nova.get_velocity/set_velocity,
--      nova.is_grounded, nova.input_axis, nova.input_jump,
--      nova.play_sound, nova.set_animation_frame, nova.log

local speed = 5.0
local jumpVelocity = 9.0

function on_start(id)
    nova.log("script started for " .. id)
end

function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local vx, vy = nova.get_velocity(id)
    nova.set_velocity(id, ax * speed, vy)

    if nova.input_jump() and nova.is_grounded(id) then
        local _, vy2 = nova.get_velocity(id)
        nova.set_velocity(id, ax * speed, jumpVelocity)
        -- nova.play_sound("assets/audio/jump.wav")
    end
end
"""

/**
 * Mobile-friendly Lua script editor: file list, create-from-template,
 * text editing with monospace font, save to the project's scripts/ dir.
 */
@Composable
fun ScriptEditorPanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val scriptsDir = remember(viewModel.projectPath) { File(viewModel.projectPath, "scripts") }
    var revision by remember { mutableStateOf(0) }
    var currentPath by remember { mutableStateOf<String?>(null) }
    var editorText by remember { mutableStateOf("") }
    var dirty by remember { mutableStateOf(false) }

    val files = remember(revision) {
        scriptsDir.mkdirs()
        scriptsDir.listFiles { f -> f.extension == "lua" }
            ?.sortedBy { it.name.lowercase() }
            ?: emptyList()
    }

    Row(modifier.fillMaxSize().padding(8.dp)) {
        // File list.
        Column(Modifier.width(120.dp)) {
            Text("Scripts", style = MaterialTheme.typography.titleSmall, color = NovaColors.Text)
            TextButton(onClick = {
                var index = 1
                var candidate: File
                do {
                    candidate = File(scriptsDir, "script$index.lua")
                    index++
                } while (candidate.exists())
                candidate.writeText(NEW_SCRIPT_TEMPLATE)
                currentPath = "scripts/${candidate.name}"
                editorText = NEW_SCRIPT_TEMPLATE
                dirty = false
                revision++
                viewModel.log(LogLevel.INFO, "Created script '${candidate.name}'")
            }) { Text("+ New") }
            LazyColumn {
                items(files, key = { it.name }) { file ->
                    val rel = "scripts/${file.name}"
                    Text(
                        file.name,
                        style = MaterialTheme.typography.bodySmall,
                        color = if (currentPath == rel) NovaColors.Primary else NovaColors.Text,
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                if (currentPath == rel) NovaColors.SurfaceVariant
                                else androidx.compose.ui.graphics.Color.Transparent,
                            )
                            .clickable {
                                currentPath = rel
                                editorText = file.readText()
                                dirty = false
                            }
                            .padding(4.dp),
                    )
                }
            }
        }

        Spacer(Modifier.width(8.dp))

        // Editor.
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    currentPath ?: "No script open",
                    style = MaterialTheme.typography.bodySmall,
                    color = NovaColors.TextDim,
                    modifier = Modifier.weight(1f),
                )
                TextButton(
                    onClick = {
                        currentPath?.let { path ->
                            File(viewModel.projectPath, path).writeText(editorText)
                            dirty = false
                            viewModel.log(LogLevel.INFO, "Saved '$path'")
                        }
                    },
                    enabled = dirty && currentPath != null,
                ) { Text("Save") }
                TextButton(
                    onClick = {
                        val id = viewModel.selectedId
                        val path = currentPath
                        if (id != null && path != null) {
                            viewModel.updateEntity(id, "Add Script component") { e ->
                                e.copy(script = ScriptComponent(scriptPath = path))
                            }
                            viewModel.log(LogLevel.INFO, "Attached '$path' to entity")
                        } else {
                            viewModel.log(LogLevel.WARNING, "Select an entity and open a script first")
                        }
                    },
                ) { Text("Attach") }
            }
            if (currentPath == null) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text(
                        "Open or create a script to edit it.",
                        style = MaterialTheme.typography.bodySmall,
                        color = NovaColors.TextDim,
                    )
                }
            } else {
                val lineCount = editorText.count { it == '\n' } + 1
                Row(Modifier.fillMaxSize()) {
                    // Line numbers.
                    Column(
                        Modifier
                            .verticalScroll(rememberScrollState())
                            .padding(end = 6.dp),
                    ) {
                        for (i in 1..lineCount) {
                            Text(
                                "$i",
                                style = TextStyle(
                                    fontFamily = FontFamily.Monospace,
                                    fontSize = 11.sp,
                                    color = NovaColors.TextDim,
                                ),
                            )
                        }
                    }
                    BasicTextField(
                        value = editorText,
                        onValueChange = {
                            editorText = it
                            dirty = true
                        },
                        textStyle = TextStyle(
                            fontFamily = FontFamily.Monospace,
                            fontSize = 11.sp,
                            color = NovaColors.Text,
                            lineHeight = 14.sp,
                        ),
                        cursorBrush = SolidColor(NovaColors.Primary),
                        modifier = Modifier
                            .weight(1f)
                            .horizontalScroll(rememberScrollState()),
                    )
                }
            }
        }
    }
}
