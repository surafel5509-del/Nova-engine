package dev.nova.editor.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import dev.nova.editor.project.ProjectConfig
import dev.nova.editor.project.ProjectRepository
import dev.nova.editor.scene.Entity
import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneJson
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.scene.buildRenderScene
import kotlinx.serialization.encodeToString

enum class EditorTool(val label: String) { SELECT("Select"), MOVE("Move"), TILE("Tile"), PAN("Pan"), ZOOM("Zoom") }

enum class LogLevel { INFO, WARNING, ERROR }

enum class PlayState { STOPPED, PLAYING, PAUSED }

data class LogEntry(val timestampMs: Long, val level: LogLevel, val message: String)

/** Profiler snapshot mirrored from native stats. */
data class EngineStats(
    val fps: Float = 0f,
    val frameMs: Float = 0f,
    val drawCalls: Int = 0,
    val sprites: Int = 0,
    val bodies: Int = 0,
    val particles: Int = 0,
    val scripts: Int = 0,
)

class EditorViewModel(
    val projectPath: String,
    val config: ProjectConfig,
    initialScene: Scene,
    private val repository: ProjectRepository,
) : ViewModel() {

    var scene by mutableStateOf(initialScene)
        private set

    var selectedId by mutableStateOf<String?>(null)
        private set

    var activeTool by mutableStateOf(EditorTool.SELECT)
        private set

    var camera by mutableStateOf(Camera2D())
        private set

    var gridVisible by mutableStateOf(true)
        private set

    var snapEnabled by mutableStateOf(true)
        private set

    val snapStep = 0.5f

    var hierarchyFilter by mutableStateOf("")

    var console by mutableStateOf<List<LogEntry>>(emptyList())
        private set

    var dirty by mutableStateOf(false)
        private set

    // ---- Play mode / game view ----
    var playState by mutableStateOf(PlayState.STOPPED)
        private set

    var gameView by mutableStateOf(false)
        private set

    var physicsDebug by mutableStateOf(false)
        private set

    /** Snapshot of the scene taken when Play was pressed; restored on Stop. */
    private var prePlayScene: Scene? = null

    // ---- Tile editing ----
    /** Currently selected tile index painted by the TILE tool. */
    var tileBrush by mutableIntStateOf(0)

    // ---- Profiler ----
    var stats by mutableStateOf(EngineStats())
        private set
    var profilerVisible by mutableStateOf(false)

    // ---- Asset browser ----
    var assetDir by mutableStateOf("assets")
        private set
    var assetRevision by mutableIntStateOf(0)
        private set

    /** Bumped whenever [scene] or [selectedId] changes; triggers native scene push. */
    var renderRevision by mutableIntStateOf(0)
        private set

    /** Bumped when the imported texture set changes; triggers native texture upload. */
    var textureRevision by mutableIntStateOf(0)
        private set

    val undoStack = UndoStack()

    /** Texture keys (project-relative paths) currently needed by the scene. */
    val requiredTextures: Set<String>
        get() = scene.entities.mapNotNull { it.sprite?.texturePath }.toSet()

    init {
        log(LogLevel.INFO, "Opened project '${config.name}'")
    }

    // ---- Logging ----

    fun log(level: LogLevel, message: String) {
        val entry = LogEntry(System.currentTimeMillis(), level, message)
        console = (console + entry).takeLast(500)
    }

    fun clearConsole() {
        console = emptyList()
    }

    // ---- Selection / tools / camera ----

    fun select(id: String?) {
        if (selectedId != id) {
            selectedId = id
            bumpRender()
        }
    }

    fun setTool(tool: EditorTool) {
        activeTool = tool
    }

    fun showGrid(visible: Boolean) {
        gridVisible = visible
    }

    fun enableSnapping(enabled: Boolean) {
        snapEnabled = enabled
    }

    fun applyHierarchyFilter(filter: String) {
        hierarchyFilter = filter
    }

    fun updateCamera(next: Camera2D) {
        camera = next
    }

    // ---- Entity operations (all undoable) ----

    fun addEntity(kind: EntityKind, parentId: String? = null) {
        val command = AddEntityCommand(kind, parentId)
        scene = undoStack.push(scene, command)
        select(command.createdId())
        markChanged("Created ${SceneOps.defaultName(kind)}")
    }

    fun deleteEntity(id: String) {
        val name = SceneOps.find(scene, id)?.name ?: return
        scene = undoStack.push(scene, DeleteEntityCommand(id))
        if (selectedId != null && SceneOps.find(scene, selectedId!!) == null) selectedId = null
        markChanged("Deleted '$name'")
    }

    fun duplicateEntity(id: String) {
        val command = DuplicateEntityCommand(id)
        scene = undoStack.push(scene, command)
        command.duplicatedId()?.let { select(it) }
        markChanged("Duplicated entity")
    }

    fun renameEntity(id: String, newName: String) {
        if (newName.isBlank()) return
        val current = SceneOps.find(scene, id) ?: return
        if (current.name == newName) return
        scene = undoStack.push(scene, UpdateEntityCommand(id, "Rename entity") { it.copy(name = newName.trim()) })
        markChanged("Renamed '${current.name}' to '$newName'")
    }

    fun setEntityEnabled(id: String, enabled: Boolean) {
        scene = undoStack.push(scene, UpdateEntityCommand(id, if (enabled) "Enable entity" else "Disable entity") {
            it.copy(enabled = enabled)
        })
        markChanged(if (enabled) "Enabled entity" else "Disabled entity")
    }

    fun reparentEntity(id: String, newParentId: String?) {
        if (SceneOps.find(scene, id)?.parentId == newParentId) return
        val before = scene
        scene = undoStack.push(scene, ReparentEntityCommand(id, newParentId))
        if (scene !== before) markChanged("Reparented entity")
    }

    /** Generic component/property edit through the inspector. */
    fun updateEntity(id: String, description: String, mutation: (Entity) -> Entity) {
        if (SceneOps.find(scene, id) == null) return
        scene = undoStack.push(scene, UpdateEntityCommand(id, description, mutation))
        markChanged(description)
    }

    // ---- Drag-move (single undo entry per drag) ----

    private var dragOriginal: Entity? = null

    fun beginEntityDrag(id: String) {
        dragOriginal = SceneOps.find(scene, id)
    }

    fun moveEntityBy(id: String, dxWorld: Float, dyWorld: Float) {
        val entity = SceneOps.find(scene, id) ?: return
        val t = entity.transform
        val newX = Camera2D.snap(t.x + dxWorld, snapStep, snapEnabled)
        val newY = Camera2D.snap(t.y + dyWorld, snapStep, snapEnabled)
        scene = SceneOps.update(scene, id) { it.copy(transform = t.copy(x = newX, y = newY)) }
        bumpRender()
    }

    fun endEntityDrag(id: String) {
        val original = dragOriginal ?: return
        dragOriginal = null
        val current = SceneOps.find(scene, id) ?: return
        if (current.transform != original.transform) {
            undoStack.pushPreApplied(scene, SnapshotEntityCommand(id, "Move entity", original, current))
            markChanged("Moved '${original.name}'")
        }
    }

    // ---- Undo / redo / save ----

    fun undo() {
        if (!undoStack.canUndo) return
        scene = undoStack.undo(scene)
        if (selectedId != null && SceneOps.find(scene, selectedId!!) == null) selectedId = null
        markChanged("Undo: ${undoStack.redoDescription ?: ""}")
    }

    fun redo() {
        if (!undoStack.canRedo) return
        scene = undoStack.redo(scene)
        markChanged("Redo: ${undoStack.undoDescription ?: ""}")
    }

    fun save() {
        runCatching {
            repository.saveScene(projectPath, scene)
        }.onSuccess {
            dirty = false
            log(LogLevel.INFO, "Scene saved (${scene.entities.size} entities)")
        }.onFailure {
            log(LogLevel.ERROR, "Save failed: ${it.message}")
        }
    }

    // ---- Textures ----

    fun importTextureForEntity(id: String, fileName: String, bytes: ByteArray) {
        runCatching {
            repository.importTexture(projectPath, fileName, bytes)
        }.onSuccess { relativePath ->
            updateEntity(id, "Assign texture") { e ->
                e.copy(sprite = e.sprite?.copy(texturePath = relativePath))
            }
            textureRevision++
            log(LogLevel.INFO, "Imported texture '$relativePath'")
        }.onFailure {
            log(LogLevel.ERROR, "Texture import failed: ${it.message}")
        }
    }

    fun readTextureBytes(relativePath: String): ByteArray? = repository.readTexture(projectPath, relativePath)

    // ---- Play mode ----

    fun play() {
        if (playState == PlayState.PLAYING) return
        if (playState == PlayState.STOPPED) {
            prePlayScene = scene
            // Seed the game camera into the editor camera so Game View looks right.
            scene.entities.firstOrNull { it.enabled && it.camera != null }?.let { camEntity ->
                val wt = SceneOps.worldTransform(scene, camEntity.id)
                camera = camera.copy(centerX = wt.x, centerY = wt.y)
            }
            log(LogLevel.INFO, "Play: simulation started (${scene.entities.count { it.physicsBody != null }} bodies)")
        } else {
            log(LogLevel.INFO, "Resumed")
        }
        playState = PlayState.PLAYING
    }

    fun pause() {
        if (playState == PlayState.PLAYING) {
            playState = PlayState.PAUSED
            log(LogLevel.INFO, "Paused")
        }
    }

    fun stop() {
        if (playState == PlayState.STOPPED) return
        playState = PlayState.STOPPED
        prePlayScene?.let { scene = it }
        prePlayScene = null
        bumpRender()
        log(LogLevel.INFO, "Stop: scene restored")
    }

    /** Called by the viewport when simulation positions are read back. */
    fun applySimulatedPositions(positions: Map<String, Pair<Float, Float>>) {
        if (positions.isEmpty()) return
        // Guard against positions from a stale simulation run mutating a restored scene.
        if (playState == PlayState.STOPPED) return
        scene = scene.copy(entities = scene.entities.map { e ->
            positions[e.id]?.let { (x, y) ->
                e.copy(transform = e.transform.copy(x = x, y = y))
            } ?: e
        })
        bumpRender()
    }

    fun toggleGameView() {
        gameView = !gameView
        log(LogLevel.INFO, if (gameView) "Game View" else "Scene View")
    }

    fun togglePhysicsDebug() {
        physicsDebug = !physicsDebug
    }

    fun toggleProfiler() {
        profilerVisible = !profilerVisible
    }

    /** Called by the viewport with fresh native stats. */
    fun updateStats(newStats: EngineStats) {
        stats = newStats
    }

    // ---- Tile painting ----

    /**
     * Paints [value] into the tilemap of [entityId] at the world position.
     * Returns true if a cell was hit. Undoable; repeated paints of the same
     * cell during one drag are coalesced by the caller.
     */
    fun paintTileAt(entityId: String, worldX: Float, worldY: Float, value: Int): Boolean {
        val entity = SceneOps.find(scene, entityId) ?: return false
        val map = entity.tilemap ?: return false
        val wt = SceneOps.worldTransform(scene, entityId)
        val col = kotlin.math.floor((worldX - wt.x) / map.tileSize).toInt()
        val row = kotlin.math.floor((worldY - wt.y) / map.tileSize).toInt()
        if (col !in 0 until map.cols || row !in 0 until map.rows) return false
        if (map.tileAt(col, row) == value) return true   // nothing to do
        updateEntity(entityId, if (value >= 0) "Paint tile" else "Erase tile") { e ->
            e.copy(tilemap = e.tilemap?.withTile(col, row, value))
        }
        return true
    }

    /** First tilemap entity in the scene (the TILE tool's target), if any. */
    fun activeTilemapId(): String? =
        scene.entities.firstOrNull { it.enabled && it.tilemap != null }?.id

    // ---- Scripts ----

    /** Reads every script referenced by the scene: path -> source. */
    fun loadScriptSources(): Map<String, String> {
        val result = LinkedHashMap<String, String>()
        for (e in scene.entities) {
            val path = e.script?.scriptPath ?: continue
            val file = java.io.File(projectPath, path)
            if (file.isFile) result[path] = file.readText()
        }
        return result
    }

    // ---- Asset browser ----

    fun navigateAssets(dir: String) {
        assetDir = dir
    }

    fun refreshAssets() {
        assetRevision++
    }

    fun assignTextureToSelected(relativePath: String) {
        val id = selectedId ?: run {
            log(LogLevel.WARNING, "Select an entity before assigning a texture")
            return
        }
        updateEntity(id, "Assign texture") { e ->
            e.copy(sprite = e.sprite?.copy(texturePath = relativePath))
        }
        textureRevision++
        log(LogLevel.INFO, "Assigned texture '$relativePath'")
    }

    // ---- Picking ----

    /** Returns the top-most sprite entity at world position, or null. */
    fun pickAt(worldX: Float, worldY: Float): String? {
        val candidates = scene.entities
            .filter { it.enabled && it.sprite != null }
            .sortedBy { it.sprite!!.sortingOrder }
        for (e in candidates.asReversed()) {
            val s = e.sprite!!
            val wt = SceneOps.worldTransform(scene, e.id)
            val halfW = s.width * wt.scaleX / 2f
            val halfH = s.height * wt.scaleY / 2f
            if (hitTestSprite(worldX, worldY, wt.x, wt.y, wt.rotation, halfW, halfH)) {
                return e.id
            }
        }
        return null
    }

    // ---- Render push ----

    fun renderSceneJson(): String =
        SceneJson.encodeToString(buildRenderScene(scene, selectedId))

    private fun markChanged(message: String) {
        dirty = true
        bumpRender()
        log(LogLevel.INFO, message)
    }

    private fun bumpRender() {
        renderRevision++
    }
}
