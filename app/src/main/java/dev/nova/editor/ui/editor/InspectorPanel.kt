package dev.nova.editor.ui.editor

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.scene.Entity
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.ui.theme.NovaColors

/**
 * Inspector foundation: edits the selected entity's components.
 * Every edit goes through [EditorViewModel.updateEntity] -> undoable.
 */
@Composable
fun InspectorPanel(
    viewModel: EditorViewModel,
    onImportTexture: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val selected = viewModel.selectedId?.let { SceneOps.find(viewModel.scene, it) }

    Column(
        modifier
            .fillMaxSize()
            .padding(8.dp)
            .verticalScroll(rememberScrollState()),
    ) {
        Text("Inspector", style = MaterialTheme.typography.titleSmall, color = NovaColors.Text)
        Spacer(Modifier.height(8.dp))

        if (selected == null) {
            Text(
                "Select an entity to edit its components.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            return@Column
        }

        // --- Entity header ---
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(selected.name, style = MaterialTheme.typography.titleMedium, color = NovaColors.Text, modifier = Modifier.weight(1f))
            Text("Enabled", style = MaterialTheme.typography.bodySmall, color = NovaColors.TextDim)
            Spacer(Modifier.padding(2.dp))
            Switch(checked = selected.enabled, onCheckedChange = { viewModel.setEntityEnabled(selected.id, it) })
        }
        Text(
            "Kind: ${selected.kind.name.lowercase().replaceFirstChar { it.uppercase() }}",
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.TextDim,
        )
        Spacer(Modifier.height(8.dp))
        HorizontalDivider(color = NovaColors.PanelBorder)

        // --- Transform (always present) ---
        ComponentHeader(title = "Transform", onReset = {
            viewModel.updateEntity(selected.id, "Reset Transform") {
                it.copy(transform = dev.nova.editor.scene.TransformComponent())
            }
        })
        val t = selected.transform
        FloatField("X", t.x) { v -> viewModel.updateEntity(selected.id, "Edit position X") { e -> e.copy(transform = e.transform.copy(x = v)) } }
        FloatField("Y", t.y) { v -> viewModel.updateEntity(selected.id, "Edit position Y") { e -> e.copy(transform = e.transform.copy(y = v)) } }
        FloatField("Rotation", t.rotation) { v -> viewModel.updateEntity(selected.id, "Edit rotation") { e -> e.copy(transform = e.transform.copy(rotation = v)) } }
        FloatField("Scale X", t.scaleX) { v -> viewModel.updateEntity(selected.id, "Edit scale X") { e -> e.copy(transform = e.transform.copy(scaleX = v)) } }
        FloatField("Scale Y", t.scaleY) { v -> viewModel.updateEntity(selected.id, "Edit scale Y") { e -> e.copy(transform = e.transform.copy(scaleY = v)) } }
        Spacer(Modifier.height(8.dp))
        HorizontalDivider(color = NovaColors.PanelBorder)

        // --- Sprite ---
        selected.sprite?.let { sprite ->
            ComponentHeader(
                title = "Sprite",
                onReset = { viewModel.updateEntity(selected.id, "Reset Sprite") { it.copy(sprite = dev.nova.editor.scene.SpriteComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Sprite component") { it.copy(sprite = null) } },
            )
            FloatField("Width", sprite.width) { v -> viewModel.updateEntity(selected.id, "Edit sprite width") { e -> e.copy(sprite = e.sprite?.copy(width = v)) } }
            FloatField("Height", sprite.height) { v -> viewModel.updateEntity(selected.id, "Edit sprite height") { e -> e.copy(sprite = e.sprite?.copy(height = v)) } }

            Text("Color (RGBA)", style = MaterialTheme.typography.bodySmall, color = NovaColors.TextDim)
            ColorSlider("R", sprite.r) { v -> viewModel.updateEntity(selected.id, "Edit sprite color") { e -> e.copy(sprite = e.sprite?.copy(r = v)) } }
            ColorSlider("G", sprite.g) { v -> viewModel.updateEntity(selected.id, "Edit sprite color") { e -> e.copy(sprite = e.sprite?.copy(g = v)) } }
            ColorSlider("B", sprite.b) { v -> viewModel.updateEntity(selected.id, "Edit sprite color") { e -> e.copy(sprite = e.sprite?.copy(b = v)) } }
            ColorSlider("A", sprite.a) { v -> viewModel.updateEntity(selected.id, "Edit sprite color") { e -> e.copy(sprite = e.sprite?.copy(a = v)) } }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Flip X", style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
                Switch(checked = sprite.flipX, onCheckedChange = { v -> viewModel.updateEntity(selected.id, "Toggle flip X") { e -> e.copy(sprite = e.sprite?.copy(flipX = v)) } })
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Flip Y", style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
                Switch(checked = sprite.flipY, onCheckedChange = { v -> viewModel.updateEntity(selected.id, "Toggle flip Y") { e -> e.copy(sprite = e.sprite?.copy(flipY = v)) } })
            }
            FloatField("Sorting order", sprite.sortingOrder.toFloat()) { v ->
                viewModel.updateEntity(selected.id, "Edit sorting order") { e -> e.copy(sprite = e.sprite?.copy(sortingOrder = v.toInt())) }
            }

            Text(
                "Texture: ${sprite.texturePath ?: "(none — colored quad)"}",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = onImportTexture) { Text("Import Texture…") }
                if (sprite.texturePath != null) {
                    TextButton(onClick = {
                        viewModel.updateEntity(selected.id, "Clear texture") { e -> e.copy(sprite = e.sprite?.copy(texturePath = null)) }
                    }) { Text("Clear", color = NovaColors.Warning) }
                }
            }
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Camera ---
        selected.camera?.let { camera ->
            ComponentHeader(
                title = "Camera",
                onReset = { viewModel.updateEntity(selected.id, "Reset Camera") { it.copy(camera = dev.nova.editor.scene.CameraComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Camera component") { it.copy(camera = null) } },
            )
            FloatField("Zoom (px/unit)", camera.zoom) { v -> viewModel.updateEntity(selected.id, "Edit camera zoom") { e -> e.copy(camera = e.camera?.copy(zoom = v)) } }
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Physics body ---
        selected.physicsBody?.let { body ->
            ComponentHeader(
                title = "Physics Body",
                onReset = { viewModel.updateEntity(selected.id, "Reset Physics Body") { it.copy(physicsBody = dev.nova.editor.scene.PhysicsBodyComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Physics Body component") { it.copy(physicsBody = null) } },
            )
            BodyTypeDropdown(body.bodyType) { v ->
                viewModel.updateEntity(selected.id, "Edit body type") { e -> e.copy(physicsBody = e.physicsBody?.copy(bodyType = v)) }
            }
            FloatField("Mass", body.mass) { v -> viewModel.updateEntity(selected.id, "Edit mass") { e -> e.copy(physicsBody = e.physicsBody?.copy(mass = v)) } }
            FloatField("Gravity scale", body.gravityScale) { v -> viewModel.updateEntity(selected.id, "Edit gravity scale") { e -> e.copy(physicsBody = e.physicsBody?.copy(gravityScale = v)) } }
            FloatField("Friction", body.friction) { v -> viewModel.updateEntity(selected.id, "Edit friction") { e -> e.copy(physicsBody = e.physicsBody?.copy(friction = v)) } }
            FloatField("Restitution", body.restitution) { v -> viewModel.updateEntity(selected.id, "Edit restitution") { e -> e.copy(physicsBody = e.physicsBody?.copy(restitution = v)) } }
            Text(
                "Simulation runs in Play mode (Phase 4).",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Add component ---
        Spacer(Modifier.height(8.dp))
        Text("Add Component", style = MaterialTheme.typography.titleSmall, color = NovaColors.Text)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (selected.sprite == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Sprite component") { it.copy(sprite = dev.nova.editor.scene.SpriteComponent()) }
                }) { Text("Sprite") }
            }
            if (selected.camera == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Camera component") { it.copy(camera = dev.nova.editor.scene.CameraComponent()) }
                }) { Text("Camera") }
            }
            if (selected.physicsBody == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Physics Body component") { it.copy(physicsBody = dev.nova.editor.scene.PhysicsBodyComponent()) }
                }) { Text("Physics") }
            }
        }
    }
}

@Composable
private fun ComponentHeader(title: String, onReset: () -> Unit, onRemove: (() -> Unit)? = null) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
        Text(title, style = MaterialTheme.typography.titleSmall, color = NovaColors.Primary, modifier = Modifier.weight(1f))
        TextButton(onClick = onReset) { Text("Reset", style = MaterialTheme.typography.labelSmall) }
        if (onRemove != null) {
            TextButton(onClick = onRemove) { Text("Remove", style = MaterialTheme.typography.labelSmall, color = NovaColors.Error) }
        }
    }
}

/**
 * Numeric field that commits on IME-done or when leaving with a valid value.
 * Keeps local text state so typing isn't disturbed by upstream updates.
 */
@Composable
private fun FloatField(label: String, value: Float, onCommit: (Float) -> Unit) {
    var text by remember(value) { mutableStateOf(formatFloat(value)) }
    OutlinedTextField(
        value = text,
        onValueChange = { new ->
            text = new
            new.toFloatOrNull()?.let(onCommit)
        },
        label = { Text(label, style = MaterialTheme.typography.labelSmall) },
        singleLine = true,
        textStyle = MaterialTheme.typography.bodySmall,
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
    )
}

@Composable
private fun ColorSlider(label: String, value: Float, onChange: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.padding(end = 8.dp))
        Slider(
            value = value.coerceIn(0f, 1f),
            onValueChange = onChange,
            valueRange = 0f..1f,
            modifier = Modifier.weight(1f),
        )
        Text(
            formatFloat(value),
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.TextDim,
            modifier = Modifier.padding(start = 8.dp),
        )
    }
}

@Composable
private fun BodyTypeDropdown(current: String, onSelected: (String) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val types = listOf("static", "dynamic", "kinematic")
    Box {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
            Text("Body type: $current")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            types.forEach { type ->
                DropdownMenuItem(text = { Text(type) }, onClick = { onSelected(type); expanded = false })
            }
        }
    }
}

internal fun formatFloat(v: Float): String =
    if (v == v.toLong().toFloat()) v.toLong().toString() else "%.3f".format(v).trimEnd('0').trimEnd('.')
