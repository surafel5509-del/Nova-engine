package dev.nova.editor.scene

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.util.UUID

/** Current on-disk scene format version. Bump when the schema changes. */
const val SCENE_FORMAT_VERSION = 1

@Serializable
data class TransformComponent(
    val x: Float = 0f,
    val y: Float = 0f,
    val z: Float = 0f,               // used by 3D objects
    val rotation: Float = 0f,        // degrees, counter-clockwise (2D)
    val rotationX: Float = 0f,       // 3D Euler rotation (degrees)
    val rotationY: Float = 0f,
    val rotationZ: Float = 0f,
    val scaleX: Float = 1f,
    val scaleY: Float = 1f,
    val scaleZ: Float = 1f,          // 3D scale
)

@Serializable
data class SpriteComponent(
    val texturePath: String? = null,   // path relative to project root, null = untextured quad
    val width: Float = 1f,             // world units
    val height: Float = 1f,
    val r: Float = 1f,
    val g: Float = 1f,
    val b: Float = 1f,
    val a: Float = 1f,
    val flipX: Boolean = false,
    val flipY: Boolean = false,
    val sortingOrder: Int = 0,
    val parallaxFactor: Float = 1f,    // 1 = moves with world, 0 = fixed to camera
)

@Serializable
data class CameraComponent(
    val zoom: Float = 100f,            // pixels per world unit
    val frustumWidth: Float = 10f,     // preview frame extents (world units)
    val frustumHeight: Float = 6f,
    val backgroundR: Float = 0.09f,
    val backgroundG: Float = 0.10f,
    val backgroundB: Float = 0.13f,
    val followTargetName: String = "", // entity name the camera tracks (empty = static)
    val followLerp: Float = 4f,        // smoothing; higher = snappier
)

@Serializable
data class PhysicsBodyComponent(
    val bodyType: String = "static",   // static | dynamic | kinematic
    val mass: Float = 1f,
    val gravityScale: Float = 1f,
    val friction: Float = 0.5f,
    val restitution: Float = 0f,
    val colliderShape: String = "box", // box | circle (circle approximated as box)
    val colliderWidth: Float = 1f,     // world units; defaults match sprite size
    val colliderHeight: Float = 1f,
    val isSensor: Boolean = false,
)

/** Sprite-sheet animation over a grid of frames in the sprite's texture. */
@Serializable
data class AnimatorComponent(
    val frameCols: Int = 1,
    val frameRows: Int = 1,
    val framesPerSecond: Float = 8f,
    val loop: Boolean = true,
    val playing: Boolean = true,
)

/** CPU-simulated particle emitter (rendered as small sprites). */
@Serializable
data class ParticleEmitterComponent(
    val emissionRate: Float = 20f,     // particles per second
    val lifetime: Float = 1.5f,        // seconds
    val speed: Float = 3f,
    val spreadDegrees: Float = 360f,   // emission cone
    val gravity: Float = 0f,
    val startSize: Float = 0.2f,
    val endSize: Float = 0.05f,
    val startR: Float = 1f, val startG: Float = 0.8f, val startB: Float = 0.3f,
    val endR: Float = 1f, val endG: Float = 0.2f, val endB: Float = 0.1f,
    val maxParticles: Int = 100,
)

/** Grid of tile indices into a tileset atlas. -1 = empty cell. Row-major, row 0 = bottom. */
@Serializable
data class TilemapComponent(
    val tilesetPath: String? = null,   // atlas texture, project-relative
    val tileSize: Float = 1f,          // world units per cell
    val cols: Int = 16,
    val rows: Int = 9,
    val tilesetCols: Int = 8,          // atlas grid
    val tilesetRows: Int = 4,
    val tiles: List<Int> = List(cols * rows) { -1 },
) {
    fun tileAt(col: Int, row: Int): Int =
        if (col in 0 until cols && row in 0 until rows) tiles[row * cols + col] else -1

    fun withTile(col: Int, row: Int, value: Int): TilemapComponent {
        if (col !in 0 until cols || row !in 0 until rows) return this
        val next = tiles.toMutableList()
        // Tolerate shorter lists (older scenes).
        while (next.size < cols * rows) next.add(-1)
        next[row * cols + col] = value
        return copy(tiles = next)
    }

    fun resized(newCols: Int, newRows: Int): TilemapComponent {
        val next = MutableList(newCols * newRows) { -1 }
        for (r in 0 until minOf(rows, newRows)) {
            for (c in 0 until minOf(cols, newCols)) {
                next[r * newCols + c] = tileAt(c, r)
            }
        }
        return copy(cols = newCols, rows = newRows, tiles = next)
    }
}

/** Audio source: SFX (preloaded) or music (streamed). Playback runs in Play/runtime. */
@Serializable
data class AudioSourceComponent(
    val audioPath: String? = null,     // project-relative wav/ogg/mp3
    val volume: Float = 1f,
    val pitch: Float = 1f,
    val loop: Boolean = false,
    val autoplay: Boolean = false,
    val music: Boolean = false,
)

/** Lua script bound to this entity. Source lives in the project's scripts/ dir. */
@Serializable
data class ScriptComponent(
    val scriptPath: String = "scripts/main.lua",  // project-relative .lua file
)

/**
 * Game UI element anchored to the camera center (screen-space).
 * Labels/buttons render their text via a bitmap texture generated by the editor.
 */
@Serializable
data class UiComponent(
    val kind: String = "label",      // label | button | panel
    val text: String = "Label",
    val fontSizeSp: Float = 16f,
    val offsetX: Float = 0f,         // world-unit offset from camera center
    val offsetY: Float = 2.5f,
    val width: Float = 3f,
    val height: Float = 0.8f,
    val r: Float = 0.16f,
    val g: Float = 0.18f,
    val b: Float = 0.24f,
    val a: Float = 0.92f,
    val textR: Float = 1f,
    val textG: Float = 1f,
    val textB: Float = 1f,
    val pressAction: String = "",    // optional: lua function name called on tap
)

enum class EntityKind {
    EMPTY, SPRITE, CAMERA, PHYSICS_BODY, ANIMATED_SPRITE, PARTICLE_SYSTEM,
    TILEMAP, AUDIO_SOURCE, UI_ELEMENT, MESH3D, LIGHT3D, PHYSICS_BODY_3D,
}

/** 3D rigid body (sphere collider). */
@Serializable
data class PhysicsBody3DComponent(
    val bodyType: String = "dynamic",  // static | dynamic | kinematic
    val radius: Float = 0.5f,
    val mass: Float = 1f,
    val gravityScale: Float = 1f,
    val friction: Float = 0.5f,
    val restitution: Float = 0f,
)

/** World/environment settings (sky, fog, ambient) — usually on one entity. */
@Serializable
data class WorldEnvironmentComponent(
    val skyR: Float = 0.08f,
    val skyG: Float = 0.09f,
    val skyB: Float = 0.12f,
    val horizonR: Float = 0.12f,
    val horizonG: Float = 0.13f,
    val horizonB: Float = 0.18f,
    val fogDensity: Float = 0f,
    val fogR: Float = 0.5f,
    val fogG: Float = 0.55f,
    val fogB: Float = 0.65f,
    val ambientIntensity: Float = 1f,
)

/** A 3D primitive object (cube, cylinder, ground plane). */
@Serializable
data class MeshComponent(
    val shape: String = "cube",      // cube | cylinder | ground | plane
    val r: Float = 0.7f,
    val g: Float = 0.7f,
    val b: Float = 0.75f,
    val a: Float = 1f,
    val texturePath: String? = null, // reserved
)

/** Directional light + ambient for 3D scenes (one per scene is typical). */
@Serializable
data class LightComponent(
    val dirX: Float = -0.4f,
    val dirY: Float = -1f,
    val dirZ: Float = -0.3f,
    val r: Float = 0.95f,
    val g: Float = 0.93f,
    val b: Float = 0.85f,
    val intensity: Float = 1f,
    val ambientR: Float = 0.18f,
    val ambientG: Float = 0.18f,
    val ambientB: Float = 0.20f,
    val type: String = "directional",   // directional | point | spot | sun | ambient
)

/** One keyframe on an animation track. */
@Serializable
data class AnimationKey(
    val t: Float,                    // seconds
    val value: Float,
)

/** One track: animates one property of this entity. */
@Serializable
data class AnimationTrackData(
    val property: String,            // x | y | rotation | scaleX | scaleY
    val keys: List<AnimationKey> = emptyList(),
    val loop: Boolean = true,
)

/** Keyframe animation clip: a set of property tracks on this entity. */
@Serializable
data class AnimationClipComponent(
    val tracks: List<AnimationTrackData> = emptyList(),
)

/**
 * Immutable editor entity. Components are nullable; presence = attached.
 * Hierarchy is expressed via [parentId]; sibling order is the order of
 * [Scene.entities] among entities sharing the same parent.
 */
@Serializable
data class Entity(
    val id: String = UUID.randomUUID().toString(),
    val name: String = "Entity",
    val enabled: Boolean = true,
    val parentId: String? = null,
    val transform: TransformComponent = TransformComponent(),
    val sprite: SpriteComponent? = null,
    val camera: CameraComponent? = null,
    val physicsBody: PhysicsBodyComponent? = null,
    val animator: AnimatorComponent? = null,
    val particles: ParticleEmitterComponent? = null,
    val tilemap: TilemapComponent? = null,
    val audioSource: AudioSourceComponent? = null,
    val script: ScriptComponent? = null,
    val ui: UiComponent? = null,
    val mesh: MeshComponent? = null,
    val light: LightComponent? = null,
    val animation: AnimationClipComponent? = null,
    val physicsBody3d: PhysicsBody3DComponent? = null,
    val world: WorldEnvironmentComponent? = null,
) {
    val kind: EntityKind
        get() = when {
            camera != null -> EntityKind.CAMERA
            mesh != null -> EntityKind.MESH3D
            physicsBody3d != null -> EntityKind.PHYSICS_BODY_3D
            light != null -> EntityKind.LIGHT3D
            ui != null -> EntityKind.UI_ELEMENT
            tilemap != null -> EntityKind.TILEMAP
            audioSource != null && sprite == null -> EntityKind.AUDIO_SOURCE
            particles != null -> EntityKind.PARTICLE_SYSTEM
            animator != null -> EntityKind.ANIMATED_SPRITE
            physicsBody != null -> EntityKind.PHYSICS_BODY
            sprite != null -> EntityKind.SPRITE
            else -> EntityKind.EMPTY
        }
}

@Serializable
data class Scene(
    val version: Int = SCENE_FORMAT_VERSION,
    val name: String = "Main",
    val entities: List<Entity> = emptyList(),
)

/** Pure functions over [Scene]; every mutation returns a new Scene. */
object SceneOps {

    fun createEntity(kind: EntityKind, name: String? = null, parentId: String? = null): Entity {
        val base = Entity(name = name ?: defaultName(kind), parentId = parentId)
        return when (kind) {
            EntityKind.EMPTY -> base
            EntityKind.SPRITE -> base.copy(sprite = SpriteComponent())
            EntityKind.CAMERA -> base.copy(camera = CameraComponent())
            EntityKind.PHYSICS_BODY -> base.copy(
                sprite = SpriteComponent(r = 0.4f, g = 0.8f, b = 0.4f),
                physicsBody = PhysicsBodyComponent(bodyType = "dynamic"),
            )
            EntityKind.ANIMATED_SPRITE -> base.copy(
                sprite = SpriteComponent(r = 0.9f, g = 0.6f, b = 0.95f),
                animator = AnimatorComponent(frameCols = 4, frameRows = 1),
            )
            EntityKind.PARTICLE_SYSTEM -> base.copy(
                sprite = SpriteComponent(width = 0.3f, height = 0.3f, r = 1f, g = 0.7f, b = 0.3f),
                particles = ParticleEmitterComponent(),
            )
            EntityKind.TILEMAP -> base.copy(tilemap = TilemapComponent())
            EntityKind.AUDIO_SOURCE -> base.copy(audioSource = AudioSourceComponent())
            EntityKind.UI_ELEMENT -> base.copy(ui = UiComponent())
            EntityKind.MESH3D -> base.copy(mesh = MeshComponent())
            EntityKind.LIGHT3D -> base.copy(light = LightComponent())
            EntityKind.PHYSICS_BODY_3D -> base.copy(
                mesh = MeshComponent(),
                physicsBody3d = PhysicsBody3DComponent(),
            )
        }
    }

    fun defaultName(kind: EntityKind): String = when (kind) {
        EntityKind.EMPTY -> "Empty"
        EntityKind.SPRITE -> "Sprite"
        EntityKind.CAMERA -> "Camera"
        EntityKind.PHYSICS_BODY -> "Body"
        EntityKind.ANIMATED_SPRITE -> "Animated Sprite"
        EntityKind.PARTICLE_SYSTEM -> "Particles"
        EntityKind.TILEMAP -> "Tilemap"
        EntityKind.AUDIO_SOURCE -> "Audio Source"
        EntityKind.UI_ELEMENT -> "UI Element"
        EntityKind.MESH3D -> "3D Object"
        EntityKind.LIGHT3D -> "Light"
        EntityKind.PHYSICS_BODY_3D -> "3D Body"
    }

    fun add(scene: Scene, entity: Entity): Scene {
        val baseName = entity.name
        var finalName = baseName
        var n = 2
        val names = scene.entities.map { it.name }.toSet()
        while (finalName in names) {
            finalName = "$baseName $n"
            n++
        }
        return scene.copy(entities = scene.entities + entity.copy(name = finalName))
    }

    fun find(scene: Scene, id: String): Entity? = scene.entities.firstOrNull { it.id == id }

    fun update(scene: Scene, id: String, transform: (Entity) -> Entity): Scene =
        scene.copy(entities = scene.entities.map { if (it.id == id) transform(it) else it })

    fun remove(scene: Scene, id: String): Scene {
        val toRemove = collectWithDescendants(scene, id)
        return scene.copy(entities = scene.entities.filterNot { it.id in toRemove })
    }

    /** Ids of [id] plus all transitive descendants. */
    fun collectWithDescendants(scene: Scene, id: String): Set<String> {
        val result = linkedSetOf(id)
        var changed = true
        while (changed) {
            changed = false
            for (e in scene.entities) {
                if (e.parentId in result && e.id !in result) {
                    result.add(e.id)
                    changed = true
                }
            }
        }
        return result
    }

    fun duplicate(scene: Scene, id: String): Pair<Scene, String?> {
        val original = find(scene, id) ?: return scene to null
        val ids = collectWithDescendants(scene, id)
        val idMap = HashMap<String, String>()
        for (e in scene.entities) if (e.id in ids) idMap[e.id] = UUID.randomUUID().toString()
        val copies = scene.entities.filter { it.id in ids }.map { e ->
            e.copy(
                id = idMap.getValue(e.id),
                parentId = if (e.id == id) e.parentId else e.parentId?.let { idMap[it] },
            )
        }
        var next = scene.copy(entities = scene.entities + copies)
        val rootId = idMap.getValue(id)
        val names = scene.entities.map { it.name }.toSet()
        if (original.name in names) {
            var n = 2
            var candidate = "${original.name} $n"
            while (candidate in names) { n++; candidate = "${original.name} $n" }
            next = update(next, rootId) { it.copy(name = candidate) }
        }
        return next to rootId
    }

    /** Re-parent [id] under [newParentId] (null = scene root). Guards against cycles. */
    fun reparent(scene: Scene, id: String, newParentId: String?): Scene {
        if (id == newParentId) return scene
        if (newParentId != null && newParentId in collectWithDescendants(scene, id)) return scene
        if (newParentId != null && find(scene, newParentId) == null) return scene
        return update(scene, id) { it.copy(parentId = newParentId) }
    }

    fun childrenOf(scene: Scene, parentId: String?): List<Entity> =
        scene.entities.filter { it.parentId == parentId }

    /** Depth-first hierarchy order starting at the roots. */
    fun hierarchyOrder(scene: Scene): List<Entity> {
        val out = ArrayList<Entity>(scene.entities.size)
        fun walk(parentId: String?) {
            for (e in childrenOf(scene, parentId)) {
                out.add(e)
                walk(e.id)
            }
        }
        walk(null)
        return out
    }

    fun depthOf(scene: Scene, id: String): Int {
        var depth = 0
        var current = find(scene, id)?.parentId
        var guard = 0
        while (current != null && guard < 1000) {
            depth++
            current = find(scene, current)?.parentId
            guard++
        }
        return depth
    }

    /** World transform of an entity: ancestor transforms composed parent-first. */
    fun worldTransform(scene: Scene, id: String): TransformComponent {
        val chain = ArrayList<Entity>()
        var current = find(scene, id)
        var guard = 0
        while (current != null && guard < 1000) {
            chain.add(current)
            current = current.parentId?.let { find(scene, it) }
            guard++
        }
        var acc = TransformComponent()
        for (e in chain.asReversed()) {
            acc = compose(acc, e.transform)
        }
        return acc
    }

    /** Compose parent [p] with local child [c] (2D TRS, no shear). */
    fun compose(p: TransformComponent, c: TransformComponent): TransformComponent {
        val rad = Math.toRadians(p.rotation.toDouble())
        val cos = Math.cos(rad).toFloat()
        val sin = Math.sin(rad).toFloat()
        val sx = c.x * p.scaleX
        val sy = c.y * p.scaleY
        return TransformComponent(
            x = p.x + sx * cos - sy * sin,
            y = p.y + sx * sin + sy * cos,
            z = p.z + c.z * p.scaleZ,
            rotation = p.rotation + c.rotation,
            rotationX = p.rotationX + c.rotationX,
            rotationY = p.rotationY + c.rotationY,
            rotationZ = p.rotationZ + c.rotationZ,
            scaleX = p.scaleX * c.scaleX,
            scaleY = p.scaleY * c.scaleY,
            scaleZ = p.scaleZ * c.scaleZ,
        )
    }
}

/** Flat world-space sprite record consumed by the native renderer. */
@Serializable
data class RenderSprite(
    val id: String,
    val x: Float,
    val y: Float,
    val rotation: Float,
    val scaleX: Float,
    val scaleY: Float,
    val width: Float,
    val height: Float,
    val r: Float,
    val g: Float,
    val b: Float,
    val a: Float,
    val texture: String? = null,
    val selected: Boolean = false,
    val flipX: Boolean = false,
    val flipY: Boolean = false,
    val sortingOrder: Int = 0,
    val parallaxFactor: Float = 1f,
    val frameCols: Int = 1,
    val frameRows: Int = 1,
    val frameIndex: Int = 0,
)

@Serializable
data class RenderBody(
    val id: String,
    val bodyType: Int,                 // 0 static, 1 dynamic, 2 kinematic
    val x: Float,
    val y: Float,
    val halfW: Float,
    val halfH: Float,
    val mass: Float,
    val gravityScale: Float,
    val friction: Float,
    val restitution: Float,
)

@Serializable
data class RenderGameCamera(
    val x: Float,
    val y: Float,
    val zoom: Float,
    val width: Float,
    val height: Float,
    val bgR: Float,
    val bgG: Float,
    val bgB: Float,
    val followId: String = "",
    val followLerp: Float = 4f,
)

@Serializable
data class RenderUiElement(
    val id: String,
    val kind: String,
    val offsetX: Float,
    val offsetY: Float,
    val width: Float,
    val height: Float,
    val r: Float,
    val g: Float,
    val b: Float,
    val a: Float,
    val textKey: String = "",
)

@Serializable
data class RenderEmitter(
    val id: String,
    val x: Float,
    val y: Float,
    val emissionRate: Float,
    val lifetime: Float,
    val speed: Float,
    val gravity: Float,
    val startSize: Float,
    val endSize: Float,
    val spread: Float,               // radians
    val direction: Float,            // radians
    val r: Float,
    val g: Float,
    val b: Float,
)

@Serializable
data class RenderTilemap(
    val id: String,
    val x: Float,
    val y: Float,
    val tileSize: Float,
    val cols: Int,
    val rows: Int,
    val tileset: String? = null,
    val tilesetCols: Int = 1,
    val tilesetRows: Int = 1,
    val tiles: List<Int>,
)

@Serializable
data class RenderAudioSource(
    val id: String,
    val path: String,
    val volume: Float,
    val pitch: Float,
    val loop: Boolean,
    val autoplay: Boolean,
    val music: Boolean,
)

@Serializable
data class RenderScript(
    val id: String,
    val script: String,
)

@Serializable
data class RenderObject3D(
    val id: String,
    val shape: String,
    val x: Float, val y: Float, val z: Float,
    val rx: Float, val ry: Float, val rz: Float,
    val sx: Float, val sy: Float, val sz: Float,
    val r: Float, val g: Float, val b: Float, val a: Float,
    val texture: String? = null,
    val selected: Boolean = false,
)

@Serializable
data class RenderLight(
    val dirX: Float, val dirY: Float, val dirZ: Float,
    val r: Float, val g: Float, val b: Float,
    val intensity: Float,
    val ambientR: Float, val ambientG: Float, val ambientB: Float,
    val type: String = "directional",
)

@Serializable
data class RenderBody3D(
    val id: String,
    val bodyType: Int,
    val x: Float, val y: Float, val z: Float,
    val radius: Float,
    val mass: Float,
    val gravityScale: Float,
    val friction: Float,
    val restitution: Float,
)

@Serializable
data class RenderWorld(
    val skyR: Float, val skyG: Float, val skyB: Float,
    val horizonR: Float, val horizonG: Float, val horizonB: Float,
    val fogDensity: Float,
    val fogR: Float, val fogG: Float, val fogB: Float,
    val ambientIntensity: Float,
)

@Serializable
data class RenderAnimationKey(val t: Float, val value: Float)

@Serializable
data class RenderAnimationTrack(
    val entityId: String,
    val property: String,
    val keys: List<RenderAnimationKey>,
    val loop: Boolean = true,
)

@Serializable
data class RenderScene(
    val version: Int = SCENE_FORMAT_VERSION,
    val mode3d: Boolean = false,
    val sprites: List<RenderSprite>,
    val bodies: List<RenderBody> = emptyList(),
    val gameCamera: RenderGameCamera? = null,
    val emitters: List<RenderEmitter> = emptyList(),
    val tilemaps: List<RenderTilemap> = emptyList(),
    val audioSources: List<RenderAudioSource> = emptyList(),
    val scripts: List<RenderScript> = emptyList(),
    val uiElements: List<RenderUiElement> = emptyList(),
    val objects3d: List<RenderObject3D> = emptyList(),
    val bodies3d: List<RenderBody3D> = emptyList(),
    val light: RenderLight? = null,
    val world: RenderWorld? = null,
    val animations: List<RenderAnimationTrack> = emptyList(),
)

private fun bodyTypeToInt(type: String): Int = when (type) {
    "dynamic" -> 1
    "kinematic" -> 2
    else -> 0
}

/** Build the flat render scene: only enabled entities with sprites, sorted. */
fun buildRenderScene(scene: Scene, selectedId: String?, mode3d: Boolean = false): RenderScene {
    val sprites = scene.entities
        .filter { it.enabled && it.sprite != null && isChainEnabled(scene, it) }
        .sortedBy { it.sprite!!.sortingOrder }
        .map { e ->
            val wt = SceneOps.worldTransform(scene, e.id)
            val s = e.sprite!!
            val anim = e.animator
            RenderSprite(
                id = e.id,
                x = wt.x, y = wt.y, rotation = wt.rotation,
                scaleX = wt.scaleX, scaleY = wt.scaleY,
                width = s.width, height = s.height,
                r = s.r, g = s.g, b = s.b, a = s.a,
                texture = s.texturePath,
                selected = e.id == selectedId,
                flipX = s.flipX,
                flipY = s.flipY,
                sortingOrder = s.sortingOrder,
                parallaxFactor = s.parallaxFactor,
                frameCols = anim?.frameCols ?: 1,
                frameRows = anim?.frameRows ?: 1,
                frameIndex = 0,
            )
        }

    val bodies = scene.entities
        .filter { it.enabled && it.physicsBody != null && isChainEnabled(scene, it) }
        .map { e ->
            val wt = SceneOps.worldTransform(scene, e.id)
            val p = e.physicsBody!!
            val s = e.sprite
            val hw = (if (p.colliderWidth > 0f) p.colliderWidth else s?.width ?: 1f) * wt.scaleX / 2f
            val hh = (if (p.colliderHeight > 0f) p.colliderHeight else s?.height ?: 1f) * wt.scaleY / 2f
            RenderBody(
                id = e.id,
                bodyType = bodyTypeToInt(p.bodyType),
                x = wt.x, y = wt.y,
                halfW = hw, halfH = hh,
                mass = p.mass,
                gravityScale = p.gravityScale,
                friction = p.friction,
                restitution = p.restitution,
            )
        }

    val cameraEntity = scene.entities.firstOrNull { it.enabled && it.camera != null }
    val gameCamera = cameraEntity?.let { e ->
        val wt = SceneOps.worldTransform(scene, e.id)
        val c = e.camera!!
        val followId = if (c.followTargetName.isNotBlank()) {
            scene.entities.firstOrNull { it.name.equals(c.followTargetName, ignoreCase = true) }?.id ?: ""
        } else ""
        RenderGameCamera(
            x = wt.x, y = wt.y, zoom = c.zoom,
            width = c.frustumWidth, height = c.frustumHeight,
            bgR = c.backgroundR, bgG = c.backgroundG, bgB = c.backgroundB,
            followId = followId,
            followLerp = c.followLerp,
        )
    }

    val uiElements = scene.entities
        .filter { it.enabled && it.ui != null && isChainEnabled(scene, it) }
        .map { e ->
            val u = e.ui!!
            RenderUiElement(
                id = e.id,
                kind = u.kind,
                offsetX = u.offsetX,
                offsetY = u.offsetY,
                width = u.width,
                height = u.height,
                r = u.r, g = u.g, b = u.b, a = u.a,
                textKey = if (u.text.isNotBlank()) "ui://text/${e.id}" else "",
            )
        }

    val emitters = scene.entities
        .filter { it.enabled && it.particles != null && isChainEnabled(scene, it) }
        .map { e ->
            val wt = SceneOps.worldTransform(scene, e.id)
            val p = e.particles!!
            RenderEmitter(
                id = e.id,
                x = wt.x, y = wt.y,
                emissionRate = p.emissionRate,
                lifetime = p.lifetime,
                speed = p.speed,
                gravity = p.gravity,
                startSize = p.startSize,
                endSize = p.endSize,
                spread = Math.toRadians(p.spreadDegrees.toDouble()).toFloat() / 2f,
                direction = (Math.PI / 2).toFloat(),
                r = p.startR, g = p.startG, b = p.startB,
            )
        }

    val tilemaps = scene.entities
        .filter { it.enabled && it.tilemap != null && isChainEnabled(scene, it) }
        .map { e ->
            val wt = SceneOps.worldTransform(scene, e.id)
            val t = e.tilemap!!
            RenderTilemap(
                id = e.id,
                x = wt.x, y = wt.y,
                tileSize = t.tileSize,
                cols = t.cols, rows = t.rows,
                tileset = t.tilesetPath,
                tilesetCols = t.tilesetCols,
                tilesetRows = t.tilesetRows,
                tiles = t.tiles,
            )
        }

    val audioSources = scene.entities
        .filter { it.enabled && it.audioSource != null && it.audioSource!!.audioPath != null && isChainEnabled(scene, it) }
        .map { e ->
            val a = e.audioSource!!
            RenderAudioSource(
                id = e.id,
                path = a.audioPath!!,
                volume = a.volume,
                pitch = a.pitch,
                loop = a.loop,
                autoplay = a.autoplay,
                music = a.music,
            )
        }

    val scripts = scene.entities
        .filter { it.enabled && it.script != null && isChainEnabled(scene, it) }
        .map { e -> RenderScript(id = e.id, script = e.script!!.scriptPath) }

    val objects3d = scene.entities
        .filter { it.enabled && it.mesh != null && isChainEnabled(scene, it) }
        .map { e ->
            val wt = SceneOps.worldTransform(scene, e.id)
            val m = e.mesh!!
            RenderObject3D(
                id = e.id,
                shape = m.shape,
                x = wt.x, y = wt.y, z = wt.z,
                rx = wt.rotationX, ry = wt.rotationY, rz = wt.rotationZ,
                sx = wt.scaleX, sy = wt.scaleY, sz = wt.scaleZ,
                r = m.r, g = m.g, b = m.b, a = m.a,
                texture = m.texturePath,
                selected = e.id == selectedId,
            )
        }

    val lightEntity = scene.entities.firstOrNull { it.enabled && it.light != null }
    val light = lightEntity?.light?.let { l ->
        RenderLight(
            dirX = l.dirX, dirY = l.dirY, dirZ = l.dirZ,
            r = l.r, g = l.g, b = l.b,
            intensity = l.intensity,
            ambientR = l.ambientR, ambientG = l.ambientG, ambientB = l.ambientB,
            type = l.type,
        )
    }

    val bodies3d = scene.entities
        .filter { it.enabled && it.physicsBody3d != null && isChainEnabled(scene, it) }
        .map { e ->
            val wt = SceneOps.worldTransform(scene, e.id)
            val p = e.physicsBody3d!!
            RenderBody3D(
                id = e.id,
                bodyType = bodyTypeToInt(p.bodyType),
                x = wt.x, y = wt.y, z = wt.z,
                radius = p.radius,
                mass = p.mass,
                gravityScale = p.gravityScale,
                friction = p.friction,
                restitution = p.restitution,
            )
        }

    val worldEntity = scene.entities.firstOrNull { it.enabled && it.world != null }
    val world = worldEntity?.world?.let { w ->
        RenderWorld(
            skyR = w.skyR, skyG = w.skyG, skyB = w.skyB,
            horizonR = w.horizonR, horizonG = w.horizonG, horizonB = w.horizonB,
            fogDensity = w.fogDensity,
            fogR = w.fogR, fogG = w.fogG, fogB = w.fogB,
            ambientIntensity = w.ambientIntensity,
        )
    }

    val animations = scene.entities
        .filter { it.enabled && it.animation != null && isChainEnabled(scene, it) }
        .flatMap { e ->
            e.animation!!.tracks.map { track ->
                RenderAnimationTrack(
                    entityId = e.id,
                    property = track.property,
                    keys = track.keys.map { RenderAnimationKey(it.t, it.value) },
                    loop = track.loop,
                )
            }
        }

    return RenderScene(
        mode3d = mode3d,
        sprites = sprites,
        bodies = bodies,
        gameCamera = gameCamera,
        emitters = emitters,
        tilemaps = tilemaps,
        audioSources = audioSources,
        scripts = scripts,
        uiElements = uiElements,
        objects3d = objects3d,
        bodies3d = bodies3d,
        light = light,
        world = world,
        animations = animations,
    )
}

private fun isChainEnabled(scene: Scene, entity: Entity): Boolean {
    var current: Entity? = entity
    var guard = 0
    while (current != null && guard < 1000) {
        if (!current.enabled) return false
        current = current.parentId?.let { SceneOps.find(scene, it) }
        guard++
    }
    return true
}

val SceneJson: Json = Json {
    prettyPrint = true
    ignoreUnknownKeys = true   // forward compatibility
    encodeDefaults = true
}

fun serializeScene(scene: Scene): String = SceneJson.encodeToString(Scene.serializer(), scene)

fun deserializeScene(text: String): Scene {
    val scene = SceneJson.decodeFromString(Scene.serializer(), text)
    require(scene.version <= SCENE_FORMAT_VERSION) {
        "Scene format v${scene.version} is newer than supported v$SCENE_FORMAT_VERSION"
    }
    return scene
}
