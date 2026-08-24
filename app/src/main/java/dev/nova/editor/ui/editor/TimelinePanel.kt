package dev.nova.editor.ui.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.scene.AnimationClipComponent
import dev.nova.editor.scene.AnimationKey
import dev.nova.editor.scene.AnimationTrackData
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.ui.theme.NovaColors

private val ANIMATED_PROPERTIES = listOf("x", "y", "rotation", "scaleX", "scaleY")
private const val TIMELINE_SECONDS = 10f
private val PX_PER_SECOND = 30.dp

/**
 * Professional animation timeline: playback controls, playhead, time ruler,
 * per-track keyframe markers, add/delete/move/copy/paste keyframes, loop.
 * Tracks sample natively during Play.
 */
@Composable
fun TimelinePanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val selected = viewModel.selectedId?.let { SceneOps.find(viewModel.scene, it) }
    var playing by remember { mutableStateOf(false) }
    var loop by remember { mutableStateOf(true) }
    var playheadSec by remember { mutableStateOf(0f) }
    var clipboard by remember { mutableStateOf<AnimationKey?>(null) }

    Column(modifier.fillMaxWidth().padding(8.dp)) {
        // Header + playback controls.
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "Timeline — ${selected?.name ?: "no selection"}",
                style = MaterialTheme.typography.titleSmall,
                color = NovaColors.Text,
                modifier = Modifier.weight(1f),
            )
            if (selected != null) {
                var addMenu by remember { mutableStateOf(false) }
                Box {
                    OutlinedButton(onClick = { addMenu = true }) { Text("+ Track") }
                    DropdownMenu(expanded = addMenu, onDismissRequest = { addMenu = false }) {
                        ANIMATED_PROPERTIES.forEach { prop ->
                            DropdownMenuItem(
                                text = { Text(prop) },
                                onClick = { addTrack(viewModel, selected.id, prop); addMenu = false },
                            )
                        }
                    }
                }
            }
        }

        // Playback row.
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { playheadSec = 0f; playing = false }) { Text("⏮") }
            TextButton(onClick = { playheadSec = (playheadSec - 0.1f).coerceAtLeast(0f) }) { Text("⏴") }
            TextButton(onClick = { playing = !playing }) { Text(if (playing) "⏸" else "▶") }
            TextButton(onClick = { playing = false }) { Text("⏹") }
            TextButton(onClick = { playheadSec = (playheadSec + 0.1f).coerceAtMost(TIMELINE_SECONDS) }) { Text("⏵") }
            TextButton(onClick = { loop = !loop }) {
                Text(if (loop) "🔁" else "🔂", color = if (loop) NovaColors.Accent else NovaColors.TextDim)
            }
            Text(
                "%.1fs".format(playheadSec),
                style = MaterialTheme.typography.labelSmall,
                color = NovaColors.TextDim,
            )
        }
        Spacer(Modifier.height(4.dp))

        if (selected == null) {
            Text(
                "Select an entity to edit its animation tracks.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            return@Column
        }

        val clip = selected.animation
        if (clip == null || clip.tracks.isEmpty()) {
            Text(
                "No animation tracks. Tap + Track to animate this entity.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
            return@Column
        }

        // Time ruler with playhead.
        Row(Modifier.horizontalScroll(rememberScrollState())) {
            Spacer(Modifier.width(80.dp))
            for (sec in 0..TIMELINE_SECONDS.toInt()) {
                Text(
                    "${sec}s",
                    style = MaterialTheme.typography.labelSmall,
                    color = NovaColors.TextDim,
                    modifier = Modifier.width(PX_PER_SECOND),
                )
            }
        }

        clip.tracks.forEachIndexed { index, track ->
            TrackRow(
                track = track,
                playheadSec = playheadSec,
                onAddKey = { addKeyAtPlayhead(viewModel, selected.id, index, playheadSec) },
                onDeleteKey = { keyIndex -> deleteKey(viewModel, selected.id, index, keyIndex) },
                onMoveKey = { keyIndex, delta -> moveKey(viewModel, selected.id, index, keyIndex, delta) },
                onCopyKey = { keyIndex -> clipboard = track.keys.getOrNull(keyIndex) },
                onPasteKey = { clipboard?.let { pasteKey(viewModel, selected.id, index, it) } },
                onRemoveTrack = { removeTrack(viewModel, selected.id, index) },
            )
        }
        Text(
            if (playing) "Playing preview…" else "Animations play during Play mode (loops by default).",
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.TextDim,
        )
    }
}

@Composable
private fun TrackRow(
    track: AnimationTrackData,
    playheadSec: Float,
    onAddKey: () -> Unit,
    onDeleteKey: (Int) -> Unit,
    onMoveKey: (Int, Float) -> Unit,
    onCopyKey: (Int) -> Unit,
    onPasteKey: () -> Unit,
    onRemoveTrack: () -> Unit,
) {
    var keyMenuIndex by remember { mutableStateOf<Int?>(null) }
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
    ) {
        Text(
            track.property,
            style = MaterialTheme.typography.labelSmall,
            color = NovaColors.Primary,
            modifier = Modifier.width(80.dp),
        )
        Row(Modifier.horizontalScroll(rememberScrollState()).weight(1f)) {
            Box(
                Modifier
                    .height(26.dp)
                    .width((TIMELINE_SECONDS * PX_PER_SECOND.value).dp)
                    .background(NovaColors.SurfaceVariant),
            ) {
                track.keys.forEachIndexed { keyIndex, key ->
                    TextButton(
                        onClick = { keyMenuIndex = keyIndex },
                        modifier = Modifier
                            .padding(start = (key.t * PX_PER_SECOND.value).dp)
                            .width(10.dp)
                            .height(26.dp),
                    ) {
                        Box(Modifier.width(8.dp).height(26.dp).background(NovaColors.Accent, CircleShape))
                    }
                }
            }
        }
        TextButton(onClick = onAddKey) { Text("+Key", style = MaterialTheme.typography.labelSmall) }
        TextButton(onClick = onRemoveTrack) { Text("✕", color = NovaColors.Error) }

        keyMenuIndex?.let { keyIndex ->
            DropdownMenu(
                expanded = true,
                onDismissRequest = { keyMenuIndex = null },
            ) {
                DropdownMenuItem(text = { Text("Move +0.5s") }, onClick = { onMoveKey(keyIndex, 0.5f); keyMenuIndex = null })
                DropdownMenuItem(text = { Text("Move −0.5s") }, onClick = { onMoveKey(keyIndex, -0.5f); keyMenuIndex = null })
                DropdownMenuItem(text = { Text("Copy") }, onClick = { onCopyKey(keyIndex); keyMenuIndex = null })
                DropdownMenuItem(text = { Text("Paste") }, onClick = { onPasteKey(); keyMenuIndex = null })
                DropdownMenuItem(
                    text = { Text("Delete", color = NovaColors.Error) },
                    onClick = { onDeleteKey(keyIndex); keyMenuIndex = null },
                )
            }
        }
    }
}

private fun addTrack(viewModel: EditorViewModel, entityId: String, property: String) {
    viewModel.updateEntity(entityId, "Add animation track") { e ->
        val clip = e.animation ?: AnimationClipComponent()
        if (clip.tracks.any { it.property == property }) return@updateEntity e
        val current = propertyValue(e.transform, property)
        val track = AnimationTrackData(
            property = property,
            keys = listOf(AnimationKey(0f, current), AnimationKey(2f, current)),
        )
        e.copy(animation = clip.copy(tracks = clip.tracks + track))
    }
}

private fun addKeyAtPlayhead(viewModel: EditorViewModel, entityId: String, trackIndex: Int, timeSec: Float) {
    viewModel.updateEntity(entityId, "Add keyframe") { e ->
        val clip = e.animation ?: return@updateEntity e
        val tracks = clip.tracks.toMutableList()
        val track = tracks.getOrNull(trackIndex) ?: return@updateEntity e
        val value = propertyValue(e.transform, track.property)
        val newKey = AnimationKey(timeSec, value)
        tracks[trackIndex] = track.copy(keys = (track.keys + newKey).sortedBy { it.t })
        e.copy(animation = clip.copy(tracks = tracks))
    }
}

private fun deleteKey(viewModel: EditorViewModel, entityId: String, trackIndex: Int, keyIndex: Int) {
    viewModel.updateEntity(entityId, "Delete keyframe") { e ->
        val clip = e.animation ?: return@updateEntity e
        val tracks = clip.tracks.toMutableList()
        val track = tracks.getOrNull(trackIndex) ?: return@updateEntity e
        val keys = track.keys.toMutableList()
        if (keyIndex in keys.indices) keys.removeAt(keyIndex)
        tracks[trackIndex] = track.copy(keys = keys)
        e.copy(animation = clip.copy(tracks = tracks))
    }
}

private fun moveKey(viewModel: EditorViewModel, entityId: String, trackIndex: Int, keyIndex: Int, delta: Float) {
    viewModel.updateEntity(entityId, "Move keyframe") { e ->
        val clip = e.animation ?: return@updateEntity e
        val tracks = clip.tracks.toMutableList()
        val track = tracks.getOrNull(trackIndex) ?: return@updateEntity e
        val keys = track.keys.toMutableList()
        val key = keys.getOrNull(keyIndex) ?: return@updateEntity e
        keys[keyIndex] = key.copy(t = (key.t + delta).coerceAtLeast(0f))
        tracks[trackIndex] = track.copy(keys = keys.sortedBy { it.t })
        e.copy(animation = clip.copy(tracks = tracks))
    }
}

private fun pasteKey(viewModel: EditorViewModel, entityId: String, trackIndex: Int, key: AnimationKey) {
    viewModel.updateEntity(entityId, "Paste keyframe") { e ->
        val clip = e.animation ?: return@updateEntity e
        val tracks = clip.tracks.toMutableList()
        val track = tracks.getOrNull(trackIndex) ?: return@updateEntity e
        tracks[trackIndex] = track.copy(keys = (track.keys + key).sortedBy { it.t })
        e.copy(animation = clip.copy(tracks = tracks))
    }
}

private fun removeTrack(viewModel: EditorViewModel, entityId: String, trackIndex: Int) {
    viewModel.updateEntity(entityId, "Remove animation track") { e ->
        val clip = e.animation ?: return@updateEntity e
        val tracks = clip.tracks.toMutableList()
        if (trackIndex in tracks.indices) tracks.removeAt(trackIndex)
        e.copy(animation = clip.copy(tracks = tracks))
    }
}

private fun propertyValue(t: dev.nova.editor.scene.TransformComponent, property: String): Float = when (property) {
    "x" -> t.x
    "y" -> t.y
    "rotation" -> t.rotation
    "scaleX" -> t.scaleX
    "scaleY" -> t.scaleY
    else -> 0f
}
