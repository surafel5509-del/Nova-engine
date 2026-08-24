package dev.nova.editor.editor

import dev.nova.editor.scene.Entity
import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps

/**
 * Centralized undo/redo. Every editor mutation goes through an [EditorCommand]
 * so history is uniform (scene edits, asset ops, etc.).
 */
interface EditorCommand {
    val description: String
    fun execute(scene: Scene): Scene
    fun undo(scene: Scene): Scene
}

class UndoStack(private val capacity: Int = 100) {
    private val undoStack = ArrayDeque<EditorCommand>()
    private val redoStack = ArrayDeque<EditorCommand>()

    val canUndo: Boolean get() = undoStack.isNotEmpty()
    val canRedo: Boolean get() = redoStack.isNotEmpty()
    val undoDescription: String? get() = undoStack.lastOrNull()?.description
    val redoDescription: String? get() = redoStack.lastOrNull()?.description

    /** Executes [command] against [scene], pushes it, clears redo. Returns new scene. */
    fun push(scene: Scene, command: EditorCommand): Scene {
        val next = command.execute(scene)
        undoStack.addLast(command)
        if (undoStack.size > capacity) undoStack.removeFirst()
        redoStack.clear()
        return next
    }

    /**
     * Pushes a command whose effect has already been applied to the scene
     * (e.g. a continuous drag). Only undo/redo will run later.
     */
    fun pushPreApplied(scene: Scene, command: EditorCommand): Scene {
        undoStack.addLast(command)
        if (undoStack.size > capacity) undoStack.removeFirst()
        redoStack.clear()
        return scene
    }

    fun undo(scene: Scene): Scene {
        val command = undoStack.removeLastOrNull() ?: return scene
        redoStack.addLast(command)
        return command.undo(scene)
    }

    fun redo(scene: Scene): Scene {
        val command = redoStack.removeLastOrNull() ?: return scene
        undoStack.addLast(command)
        return command.execute(scene)
    }

    fun clear() {
        undoStack.clear()
        redoStack.clear()
    }
}

class AddEntityCommand(
    private val kind: EntityKind,
    private val parentId: String? = null,
) : EditorCommand {
    private var created: Entity? = null

    override val description: String get() = "Create ${SceneOps.defaultName(kind)}"

    override fun execute(scene: Scene): Scene {
        val entity = created ?: SceneOps.createEntity(kind, parentId = parentId).also { created = it }
        return SceneOps.add(scene, entity)
    }

    override fun undo(scene: Scene): Scene {
        val id = created?.id ?: return scene
        return SceneOps.remove(scene, id)
    }

    fun createdId(): String? = created?.id
}

/** Removes an entity and its descendants; undo restores the full snapshot. */
class DeleteEntityCommand(private val entityId: String) : EditorCommand {
    private var snapshot: List<Entity>? = null

    override val description: String get() = "Delete entity"

    override fun execute(scene: Scene): Scene {
        val ids = SceneOps.collectWithDescendants(scene, entityId)
        snapshot = scene.entities.filter { it.id in ids }
        return SceneOps.remove(scene, entityId)
    }

    override fun undo(scene: Scene): Scene {
        val restored = snapshot ?: return scene
        val existing = scene.entities.map { it.id }.toSet()
        return scene.copy(entities = scene.entities + restored.filter { it.id !in existing })
    }
}

/**
 * Generic entity-mutation command. Stores before/after snapshots so any
 * property change (rename, transform edit, component toggle...) is undoable.
 */
class UpdateEntityCommand(
    private val entityId: String,
    override val description: String,
    private val mutation: (Entity) -> Entity,
) : EditorCommand {
    private var before: Entity? = null
    private var after: Entity? = null

    override fun execute(scene: Scene): Scene {
        val current = SceneOps.find(scene, entityId) ?: return scene
        if (before == null) before = current
        val target = after ?: mutation(current).also { after = it }
        return SceneOps.update(scene, entityId) { target }
    }

    override fun undo(scene: Scene): Scene {
        val original = before ?: return scene
        return SceneOps.update(scene, entityId) { original }
    }
}

class ReparentEntityCommand(
    private val entityId: String,
    private val newParentId: String?,
) : EditorCommand {
    private var oldParentId: String? = null
    private var captured = false

    override val description: String get() = "Reparent entity"

    override fun execute(scene: Scene): Scene {
        if (!captured) {
            oldParentId = SceneOps.find(scene, entityId)?.parentId
            captured = true
        }
        return SceneOps.reparent(scene, entityId, newParentId)
    }

    override fun undo(scene: Scene): Scene = SceneOps.reparent(scene, entityId, oldParentId)
}

/**
 * Command with explicit before/after snapshots, for continuous edits
 * (e.g. dragging an entity) where the mutation already happened outside
 * the undo stack and only the endpoints should be recorded.
 */
class SnapshotEntityCommand(
    private val entityId: String,
    override val description: String,
    private val before: Entity,
    private val after: Entity,
) : EditorCommand {
    override fun execute(scene: Scene): Scene = SceneOps.update(scene, entityId) { after }
    override fun undo(scene: Scene): Scene = SceneOps.update(scene, entityId) { before }
}

class DuplicateEntityCommand(private val entityId: String) : EditorCommand {
    private var copyRootId: String? = null
    private var snapshot: List<Entity>? = null

    override val description: String get() = "Duplicate entity"

    override fun execute(scene: Scene): Scene {
        val existing = snapshot
        return if (existing == null) {
            val (next, newId) = SceneOps.duplicate(scene, entityId)
            copyRootId = newId
            snapshot = newId?.let { root -> next.entities.filter { it.id !in scene.entities.map { e -> e.id } } }
            next
        } else {
            val existingIds = scene.entities.map { it.id }.toSet()
            scene.copy(entities = scene.entities + existing.filter { it.id !in existingIds })
        }
    }

    override fun undo(scene: Scene): Scene {
        val root = copyRootId ?: return scene
        return SceneOps.remove(scene, root)
    }

    fun duplicatedId(): String? = copyRootId
}

/** Whole-scene replacement (used by AI agent edits; single undo entry). */
class ReplaceSceneCommand(
    override val description: String,
    private val before: Scene,
    private val after: Scene,
) : EditorCommand {
    override fun execute(scene: Scene): Scene = after
    override fun undo(scene: Scene): Scene = before
}
