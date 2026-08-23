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
    val rotation: Float = 0f,          // degrees, counter-clockwise
    val scaleX: Float = 1f,
    val scaleY: Float = 1f,
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

enum class EntityKind { EMPTY, SPRITE, CAMERA, PHYSICS_BODY, ANIMATED_SPRITE, PARTICLE_SYSTEM }

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
) {
    val kind: EntityKind
        get() = when {
            camera != null -> EntityKind.CAMERA
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
        }
    }

    fun defaultName(kind: EntityKind): String = when (kind) {
        EntityKind.EMPTY -> "Empty"
        EntityKind.SPRITE -> "Sprite"
        EntityKind.CAMERA -> "Camera"
        EntityKind.PHYSICS_BODY -> "Body"
        EntityKind.ANIMATED_SPRITE -> "Animated Sprite"
        EntityKind.PARTICLE_SYSTEM -> "Particles"
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
            rotation = p.rotation + c.rotation,
            scaleX = p.scaleX * c.scaleX,
            scaleY = p.scaleY * c.scaleY,
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
)

@Serializable
data class RenderScene(
    val version: Int = SCENE_FORMAT_VERSION,
    val sprites: List<RenderSprite>,
    val bodies: List<RenderBody> = emptyList(),
    val gameCamera: RenderGameCamera? = null,
)

private fun bodyTypeToInt(type: String): Int = when (type) {
    "dynamic" -> 1
    "kinematic" -> 2
    else -> 0
}

/** Build the flat render scene: only enabled entities with sprites, sorted. */
fun buildRenderScene(scene: Scene, selectedId: String?): RenderScene {
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
        RenderGameCamera(
            x = wt.x, y = wt.y, zoom = c.zoom,
            width = c.frustumWidth, height = c.frustumHeight,
            bgR = c.backgroundR, bgG = c.backgroundG, bgB = c.backgroundB,
        )
    }

    return RenderScene(sprites = sprites, bodies = bodies, gameCamera = gameCamera)
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
