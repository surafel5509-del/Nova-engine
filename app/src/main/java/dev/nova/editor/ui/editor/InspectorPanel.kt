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
            FloatField("Parallax factor", sprite.parallaxFactor) { v ->
                viewModel.updateEntity(selected.id, "Edit parallax") { e -> e.copy(sprite = e.sprite?.copy(parallaxFactor = v.coerceIn(0f, 1f))) }
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
            FloatField("Frustum width", camera.frustumWidth) { v -> viewModel.updateEntity(selected.id, "Edit frustum width") { e -> e.copy(camera = e.camera?.copy(frustumWidth = v)) } }
            FloatField("Frustum height", camera.frustumHeight) { v -> viewModel.updateEntity(selected.id, "Edit frustum height") { e -> e.copy(camera = e.camera?.copy(frustumHeight = v)) } }
            Text("Background", style = MaterialTheme.typography.bodySmall, color = NovaColors.TextDim)
            ColorSlider("R", camera.backgroundR) { v -> viewModel.updateEntity(selected.id, "Edit camera bg") { e -> e.copy(camera = e.camera?.copy(backgroundR = v)) } }
            ColorSlider("G", camera.backgroundG) { v -> viewModel.updateEntity(selected.id, "Edit camera bg") { e -> e.copy(camera = e.camera?.copy(backgroundG = v)) } }
            ColorSlider("B", camera.backgroundB) { v -> viewModel.updateEntity(selected.id, "Edit camera bg") { e -> e.copy(camera = e.camera?.copy(backgroundB = v)) } }
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
            FloatField("Collider width", body.colliderWidth) { v -> viewModel.updateEntity(selected.id, "Edit collider width") { e -> e.copy(physicsBody = e.physicsBody?.copy(colliderWidth = v)) } }
            FloatField("Collider height", body.colliderHeight) { v -> viewModel.updateEntity(selected.id, "Edit collider height") { e -> e.copy(physicsBody = e.physicsBody?.copy(colliderHeight = v)) } }
            Text(
                "Simulation runs in Play mode.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Animator ---
        selected.animator?.let { anim ->
            ComponentHeader(
                title = "Animator",
                onReset = { viewModel.updateEntity(selected.id, "Reset Animator") { it.copy(animator = dev.nova.editor.scene.AnimatorComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Animator component") { it.copy(animator = null) } },
            )
            FloatField("Frame columns", anim.frameCols.toFloat()) { v -> viewModel.updateEntity(selected.id, "Edit frame cols") { e -> e.copy(animator = e.animator?.copy(frameCols = v.toInt().coerceAtLeast(1))) } }
            FloatField("Frame rows", anim.frameRows.toFloat()) { v -> viewModel.updateEntity(selected.id, "Edit frame rows") { e -> e.copy(animator = e.animator?.copy(frameRows = v.toInt().coerceAtLeast(1))) } }
            FloatField("Frames / sec", anim.framesPerSecond) { v -> viewModel.updateEntity(selected.id, "Edit fps") { e -> e.copy(animator = e.animator?.copy(framesPerSecond = v.coerceAtLeast(0.1f))) } }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Loop", style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
                Switch(checked = anim.loop, onCheckedChange = { v -> viewModel.updateEntity(selected.id, "Toggle loop") { e -> e.copy(animator = e.animator?.copy(loop = v)) } })
            }
            Text(
                "Animation plays in Play mode (sprite-sheet UV frames).",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Particle emitter ---
        selected.particles?.let { p ->
            ComponentHeader(
                title = "Particle Emitter",
                onReset = { viewModel.updateEntity(selected.id, "Reset Particles") { it.copy(particles = dev.nova.editor.scene.ParticleEmitterComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Particles component") { it.copy(particles = null) } },
            )
            FloatField("Emission rate", p.emissionRate) { v -> viewModel.updateEntity(selected.id, "Edit emission") { e -> e.copy(particles = e.particles?.copy(emissionRate = v)) } }
            FloatField("Lifetime (s)", p.lifetime) { v -> viewModel.updateEntity(selected.id, "Edit lifetime") { e -> e.copy(particles = e.particles?.copy(lifetime = v)) } }
            FloatField("Speed", p.speed) { v -> viewModel.updateEntity(selected.id, "Edit speed") { e -> e.copy(particles = e.particles?.copy(speed = v)) } }
            FloatField("Gravity", p.gravity) { v -> viewModel.updateEntity(selected.id, "Edit particle gravity") { e -> e.copy(particles = e.particles?.copy(gravity = v)) } }
            FloatField("Start size", p.startSize) { v -> viewModel.updateEntity(selected.id, "Edit start size") { e -> e.copy(particles = e.particles?.copy(startSize = v)) } }
            FloatField("End size", p.endSize) { v -> viewModel.updateEntity(selected.id, "Edit end size") { e -> e.copy(particles = e.particles?.copy(endSize = v)) } }
            Text(
                "Particles emit in Play mode.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Tilemap ---
        selected.tilemap?.let { map ->
            ComponentHeader(
                title = "Tilemap",
                onReset = { viewModel.updateEntity(selected.id, "Reset Tilemap") { it.copy(tilemap = dev.nova.editor.scene.TilemapComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Tilemap component") { it.copy(tilemap = null) } },
            )
            FloatField("Tile size", map.tileSize) { v -> viewModel.updateEntity(selected.id, "Edit tile size") { e -> e.copy(tilemap = e.tilemap?.copy(tileSize = v.coerceAtLeast(0.1f))) } }
            FloatField("Columns", map.cols.toFloat()) { v -> viewModel.updateEntity(selected.id, "Resize tilemap") { e -> e.copy(tilemap = e.tilemap?.resized(v.toInt().coerceAtLeast(1), e.tilemap.rows)) } }
            FloatField("Rows", map.rows.toFloat()) { v -> viewModel.updateEntity(selected.id, "Resize tilemap") { e -> e.copy(tilemap = e.tilemap?.resized(e.tilemap.cols, v.toInt().coerceAtLeast(1))) } }
            FloatField("Tileset columns", map.tilesetCols.toFloat()) { v -> viewModel.updateEntity(selected.id, "Edit tileset grid") { e -> e.copy(tilemap = e.tilemap?.copy(tilesetCols = v.toInt().coerceAtLeast(1))) } }
            FloatField("Tileset rows", map.tilesetRows.toFloat()) { v -> viewModel.updateEntity(selected.id, "Edit tileset grid") { e -> e.copy(tilemap = e.tilemap?.copy(tilesetRows = v.toInt().coerceAtLeast(1))) } }
            Text(
                "Tileset: ${map.tilesetPath ?: "(none — colored selection)"}",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Text(
                "Paint with the Tile tool. Brush index: ${viewModel.tileBrush}",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            FloatField("Brush tile index", viewModel.tileBrush.toFloat()) { v ->
                viewModel.tileBrush = v.toInt().coerceAtLeast(0)
            }
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Audio source ---
        selected.audioSource?.let { audio ->
            ComponentHeader(
                title = "Audio Source",
                onReset = { viewModel.updateEntity(selected.id, "Reset Audio Source") { it.copy(audioSource = dev.nova.editor.scene.AudioSourceComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Audio Source component") { it.copy(audioSource = null) } },
            )
            Text(
                "Clip: ${audio.audioPath ?: "(none — pick from Assets)"}",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            ColorSlider("Volume", audio.volume) { v -> viewModel.updateEntity(selected.id, "Edit volume") { e -> e.copy(audioSource = e.audioSource?.copy(volume = v)) } }
            FloatField("Pitch", audio.pitch) { v -> viewModel.updateEntity(selected.id, "Edit pitch") { e -> e.copy(audioSource = e.audioSource?.copy(pitch = v.coerceIn(0.5f, 2f))) } }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Loop", style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
                Switch(checked = audio.loop, onCheckedChange = { v -> viewModel.updateEntity(selected.id, "Toggle loop") { e -> e.copy(audioSource = e.audioSource?.copy(loop = v)) } })
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Autoplay", style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
                Switch(checked = audio.autoplay, onCheckedChange = { v -> viewModel.updateEntity(selected.id, "Toggle autoplay") { e -> e.copy(audioSource = e.audioSource?.copy(autoplay = v)) } })
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Music (streamed)", style = MaterialTheme.typography.bodySmall, color = NovaColors.Text, modifier = Modifier.weight(1f))
                Switch(checked = audio.music, onCheckedChange = { v -> viewModel.updateEntity(selected.id, "Toggle music") { e -> e.copy(audioSource = e.audioSource?.copy(music = v)) } })
            }
            Text(
                "Assign a clip from Assets → file menu 'Use as clip'.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- Script ---
        selected.script?.let { script ->
            ComponentHeader(
                title = "Script",
                onReset = { viewModel.updateEntity(selected.id, "Reset Script") { it.copy(script = dev.nova.editor.scene.ScriptComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove Script component") { it.copy(script = null) } },
            )
            Text(
                "File: ${script.scriptPath}",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Text(
                "Edit it in the Scripts tab; it runs during Play.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            Spacer(Modifier.height(8.dp))
            HorizontalDivider(color = NovaColors.PanelBorder)
        }

        // --- UI element ---
        selected.ui?.let { ui ->
            ComponentHeader(
                title = "UI Element",
                onReset = { viewModel.updateEntity(selected.id, "Reset UI") { it.copy(ui = dev.nova.editor.scene.UiComponent()) } },
                onRemove = { viewModel.updateEntity(selected.id, "Remove UI component") { it.copy(ui = null) } },
            )
            UiKindDropdown(ui.kind) { v ->
                viewModel.updateEntity(selected.id, "Edit UI kind") { e -> e.copy(ui = e.ui?.copy(kind = v)) }
            }
            var text by remember(ui.text) { mutableStateOf(ui.text) }
            OutlinedTextField(
                value = text,
                onValueChange = { new ->
                    text = new
                    viewModel.updateEntity(selected.id, "Edit UI text") { e -> e.copy(ui = e.ui?.copy(text = new)) }
                },
                label = { Text("Text", style = MaterialTheme.typography.labelSmall) },
                singleLine = true,
                textStyle = MaterialTheme.typography.bodySmall,
                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
            )
            FloatField("Font size (sp)", ui.fontSizeSp) { v -> viewModel.updateEntity(selected.id, "Edit font size") { e -> e.copy(ui = e.ui?.copy(fontSizeSp = v.coerceIn(8f, 64f))) } }
            FloatField("Offset X", ui.offsetX) { v -> viewModel.updateEntity(selected.id, "Edit UI offset") { e -> e.copy(ui = e.ui?.copy(offsetX = v)) } }
            FloatField("Offset Y", ui.offsetY) { v -> viewModel.updateEntity(selected.id, "Edit UI offset") { e -> e.copy(ui = e.ui?.copy(offsetY = v)) } }
            FloatField("Width", ui.width) { v -> viewModel.updateEntity(selected.id, "Edit UI width") { e -> e.copy(ui = e.ui?.copy(width = v.coerceAtLeast(0.2f))) } }
            FloatField("Height", ui.height) { v -> viewModel.updateEntity(selected.id, "Edit UI height") { e -> e.copy(ui = e.ui?.copy(height = v.coerceAtLeast(0.2f))) } }
            Text("Background", style = MaterialTheme.typography.bodySmall, color = NovaColors.TextDim)
            ColorSlider("R", ui.r) { v -> viewModel.updateEntity(selected.id, "Edit UI bg") { e -> e.copy(ui = e.ui?.copy(r = v)) } }
            ColorSlider("G", ui.g) { v -> viewModel.updateEntity(selected.id, "Edit UI bg") { e -> e.copy(ui = e.ui?.copy(g = v)) } }
            ColorSlider("B", ui.b) { v -> viewModel.updateEntity(selected.id, "Edit UI bg") { e -> e.copy(ui = e.ui?.copy(b = v)) } }
            Text(
                if (ui.kind == "button") "Taps reach scripts via nova.ui_pressed(id)."
                else "Anchored to the camera center (screen-space).",
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
            if (selected.animator == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Animator component") { it.copy(animator = dev.nova.editor.scene.AnimatorComponent()) }
                }) { Text("Animator") }
            }
            if (selected.particles == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Particles component") { it.copy(particles = dev.nova.editor.scene.ParticleEmitterComponent()) }
                }) { Text("Particles") }
            }
            if (selected.tilemap == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Tilemap component") { it.copy(tilemap = dev.nova.editor.scene.TilemapComponent()) }
                }) { Text("Tilemap") }
            }
            if (selected.audioSource == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Audio Source component") { it.copy(audioSource = dev.nova.editor.scene.AudioSourceComponent()) }
                }) { Text("Audio") }
            }
            if (selected.script == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add Script component") { it.copy(script = dev.nova.editor.scene.ScriptComponent()) }
                }) { Text("Script") }
            }
            if (selected.ui == null) {
                OutlinedButton(onClick = {
                    viewModel.updateEntity(selected.id, "Add UI component") { it.copy(ui = dev.nova.editor.scene.UiComponent()) }
                }) { Text("UI") }
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

@Composable
private fun UiKindDropdown(current: String, onSelected: (String) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val kinds = listOf("label", "button", "panel")
    Box {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth()) {
            Text("UI kind: $current")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            kinds.forEach { kind ->
                DropdownMenuItem(text = { Text(kind) }, onClick = { onSelected(kind); expanded = false })
            }
        }
    }
}

internal fun formatFloat(v: Float): String =
    if (v == v.toLong().toFloat()) v.toLong().toString() else "%.3f".format(v).trimEnd('0').trimEnd('.')
