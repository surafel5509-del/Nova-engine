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
private val PX_PER_SECOND = 28.dp

/**
 * Animation timeline: tracks (x, y, rotation, scaleX, scaleY) with keyframe
 * markers on a time ruler. Add/remove tracks and keyframes; the engine
 * samples them during Play.
 */
@Composable
fun TimelinePanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val selected = viewModel.selectedId?.let { SceneOps.find(viewModel.scene, it) }

    Column(modifier.fillMaxWidth().padding(8.dp)) {
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
                                onClick = {
                                    addTrack(viewModel, selected.id, prop)
                                    addMenu = false
                                },
                            )
                        }
                    }
                }
            }
        }
        Spacer(Modifier.height(6.dp))

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

        // Time ruler.
        Row(Modifier.horizontalScroll(rememberScrollState())) {
            Spacer(Modifier.width(70.dp))
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
                onAddKey = { addKeyAtHalf(viewModel, selected.id, index) },
                onRemoveTrack = { removeTrack(viewModel, selected.id, index) },
            )
        }
        Text(
            "Animations play during Play mode (loops by default).",
            style = MaterialTheme.typography.bodySmall,
            color = NovaColors.TextDim,
        )
    }
}

@Composable
private fun TrackRow(
    track: AnimationTrackData,
    onAddKey: () -> Unit,
    onRemoveTrack: () -> Unit,
) {
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
            modifier = Modifier.width(70.dp),
        )
        Row(Modifier.horizontalScroll(rememberScrollState()).weight(1f)) {
            Box(
                Modifier
                    .height(22.dp)
                    .width((TIMELINE_SECONDS * PX_PER_SECOND.value).dp)
                    .background(NovaColors.SurfaceVariant),
            ) {
                track.keys.forEach { key ->
                    Box(
                        Modifier
                            .padding(start = (key.t * PX_PER_SECOND.value).dp)
                            .width(8.dp)
                            .height(22.dp)
                            .background(NovaColors.Accent, CircleShape),
                    )
                }
            }
        }
        TextButton(onClick = onAddKey) { Text("+Key", style = MaterialTheme.typography.labelSmall) }
        TextButton(onClick = onRemoveTrack) { Text("✕", color = NovaColors.Error) }
    }
}

private fun addTrack(viewModel: EditorViewModel, entityId: String, property: String) {
    viewModel.updateEntity(entityId, "Add animation track") { e ->
        val clip = e.animation ?: AnimationClipComponent()
        if (clip.tracks.any { it.property == property }) return@updateEntity e
        // Seed with two keys: current value at t=0 and same at t=2.
        val current = propertyValue(e.transform, property)
        val track = AnimationTrackData(
            property = property,
            keys = listOf(AnimationKey(0f, current), AnimationKey(2f, current)),
        )
        e.copy(animation = clip.copy(tracks = clip.tracks + track))
    }
}

private fun addKeyAtHalf(viewModel: EditorViewModel, entityId: String, trackIndex: Int) {
    viewModel.updateEntity(entityId, "Add keyframe") { e ->
        val clip = e.animation ?: return@updateEntity e
        val tracks = clip.tracks.toMutableList()
        val track = tracks.getOrNull(trackIndex) ?: return@updateEntity e
        val last = track.keys.maxByOrNull { it.t } ?: return@updateEntity e
        val newKey = AnimationKey(last.t + 1f, propertyValue(e.transform, track.property))
        tracks[trackIndex] = track.copy(keys = (track.keys + newKey).sortedBy { it.t })
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
