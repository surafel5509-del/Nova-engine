package dev.nova.editor.ai

import dev.nova.editor.scene.Entity
import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.PhysicsBodyComponent
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.scene.ScriptComponent
import dev.nova.editor.scene.TransformComponent
import dev.nova.editor.scene.ParticleEmitterComponent
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.floatOrNull
import kotlinx.serialization.json.jsonPrimitive
import java.io.File

/** System prompt teaching the model the Nova action protocol + Lua API. */
const val AI_SYSTEM_PROMPT = """You are Nova, an AI game-builder inside a 2D Android game engine.
You can modify the scene by returning a JSON object with an "actions" array. Respond with ONLY the JSON object.

Action types:
- {"type":"create_entity","kind":"SPRITE|PHYSICS_BODY|CAMERA|ANIMATED_SPRITE|PARTICLE_SYSTEM|TILEMAP|AUDIO_SOURCE|EMPTY","name":"...","x":0,"y":0,"width":1,"height":1,"r":1,"g":1,"b":1,"bodyType":"static|dynamic"}
- {"type":"set_physics","name":"entity name","bodyType":"dynamic","mass":1,"gravityScale":1,"friction":0.5,"restitution":0,"colliderWidth":1,"colliderHeight":1}
- {"type":"add_script","entityName":"...","path":"scripts/x.lua","source":"-- lua code"}
- {"type":"set_transform","name":"...","x":0,"y":0,"rotation":0,"scaleX":1,"scaleY":1}
- {"type":"add_particles","entityName":"...","emissionRate":30,"lifetime":1,"speed":3,"gravity":0}

Lua scripts run during Play. Define on_update(id, dt) and optionally on_start(id). Engine API:
nova.get_position(id) -> x,y | nova.set_position(id,x,y) | nova.get_velocity(id) -> vx,vy |
nova.set_velocity(id,vx,vy) | nova.is_grounded(id) | nova.input_axis() -> x,y | nova.input_jump() |
nova.play_sound(path) | nova.set_animation_frame(id,frame) | nova.set_ui_text(id,text) |
nova.ui_pressed(id) | nova.raycast(x1,y1,x2,y2) | nova.log(msg).

World: y is up, gravity is -9.8. Ground usually sits at y=-2.5. Keep scenes small (under 15 entities)."""

/** Applies AI-returned actions to the scene (one undoable step). */
object AiActionApplier {

    private val json = Json { ignoreUnknownKeys = true }

    data class Result(
        val scene: Scene,
        val actions: Int,
        val summary: String,
        val scripts: Map<String, String>,
    )

    /** Extracts the JSON object from a possibly markdown-wrapped reply. */
    fun extractJson(reply: String): String? {
        val start = reply.indexOf('{')
        val end = reply.lastIndexOf('}')
        if (start < 0 || end <= start) return null
        return reply.substring(start, end + 1)
    }

    fun apply(scene: Scene, reply: String, projectPath: String): Result {
        val jsonText = extractJson(reply)
            ?: return Result(scene, 0, "No JSON actions found in reply.", emptyMap())
        val root = runCatching { json.parseToJsonElement(jsonText) as? JsonObject }.getOrNull()
            ?: return Result(scene, 0, "Reply is not valid JSON.", emptyMap())
        val array = root["actions"] as? JsonArray
            ?: return Result(scene, 0, "Reply has no 'actions' array.", emptyMap())

        var current = scene
        val scripts = LinkedHashMap<String, String>()
        val summaries = mutableListOf<String>()

        for (element in array) {
            val action = element as? JsonObject ?: continue
            when (action.str("type")) {
                "create_entity" -> {
                    val kind = runCatching {
                        EntityKind.valueOf((action.str("kind") ?: "SPRITE").uppercase())
                    }.getOrDefault(EntityKind.SPRITE)
                    val name = action.str("name") ?: SceneOps.defaultName(kind)
                    var entity = SceneOps.createEntity(kind, name).copy(
                        transform = TransformComponent(
                            x = action.num("x") ?: 0f,
                            y = action.num("y") ?: 0f,
                        ),
                    )
                    entity.sprite?.let { sprite ->
                        entity = entity.copy(sprite = sprite.copy(
                            width = action.num("width") ?: 1f,
                            height = action.num("height") ?: 1f,
                            r = action.num("r") ?: 1f,
                            g = action.num("g") ?: 1f,
                            b = action.num("b") ?: 1f,
                        ))
                    }
                    val bodyType = action.str("bodyType")
                    if (!bodyType.isNullOrBlank() && entity.physicsBody != null) {
                        entity = entity.copy(physicsBody = entity.physicsBody!!.copy(bodyType = bodyType))
                    }
                    current = SceneOps.add(current, entity)
                    summaries += "created '$name'"
                }
                "set_physics" -> {
                    val name = action.str("name") ?: continue
                    current = updateByName(current, name, summaries) { e ->
                        val p = e.physicsBody ?: PhysicsBodyComponent()
                        e.copy(physicsBody = p.copy(
                            bodyType = action.str("bodyType") ?: p.bodyType,
                            mass = action.num("mass") ?: p.mass,
                            gravityScale = action.num("gravityScale") ?: p.gravityScale,
                            friction = action.num("friction") ?: p.friction,
                            restitution = action.num("restitution") ?: p.restitution,
                            colliderWidth = action.num("colliderWidth") ?: p.colliderWidth,
                            colliderHeight = action.num("colliderHeight") ?: p.colliderHeight,
                        ))
                    }
                }
                "set_transform" -> {
                    val name = action.str("name") ?: continue
                    current = updateByName(current, name, summaries) { e ->
                        e.copy(transform = e.transform.copy(
                            x = action.num("x") ?: e.transform.x,
                            y = action.num("y") ?: e.transform.y,
                            rotation = action.num("rotation") ?: e.transform.rotation,
                            scaleX = action.num("scaleX") ?: e.transform.scaleX,
                            scaleY = action.num("scaleY") ?: e.transform.scaleY,
                        ))
                    }
                }
                "add_script" -> {
                    val path = action.str("path") ?: "scripts/ai_script.lua"
                    val source = action.str("source") ?: ""
                    val entityName = action.str("entityName") ?: ""
                    if (source.isNotBlank()) {
                        scripts[path] = source
                        val file = File(projectPath, path)
                        file.parentFile?.mkdirs()
                        file.writeText(source)
                    }
                    if (entityName.isNotBlank()) {
                        current = updateByName(current, entityName, summaries) { e ->
                            e.copy(script = ScriptComponent(scriptPath = path))
                        }
                    }
                    summaries += "script '$path'"
                }
                "add_particles" -> {
                    val name = action.str("entityName") ?: action.str("name") ?: continue
                    current = updateByName(current, name, summaries) { e ->
                        val p = e.particles ?: ParticleEmitterComponent()
                        e.copy(particles = p.copy(
                            emissionRate = action.num("emissionRate") ?: p.emissionRate,
                            lifetime = action.num("lifetime") ?: p.lifetime,
                            speed = action.num("speed") ?: p.speed,
                            gravity = action.num("gravity") ?: p.gravity,
                        ))
                    }
                }
            }
        }

        val summary = if (summaries.isEmpty()) "No recognizable actions." else summaries.joinToString("; ")
        return Result(current, array.size, summary, scripts)
    }

    private fun JsonObject.str(key: String): String? =
        (this[key] as? JsonPrimitive)?.takeIf { it.isString }?.content

    private fun JsonObject.num(key: String): Float? =
        this[key]?.jsonPrimitive?.floatOrNull

    private fun updateByName(
        scene: Scene,
        name: String,
        summaries: MutableList<String>,
        update: (Entity) -> Entity,
    ): Scene {
        val target = scene.entities.firstOrNull { it.name.equals(name, ignoreCase = true) } ?: return scene
        summaries += "updated '$name'"
        return SceneOps.update(scene, target.id) { update(it) }
    }

    /** Compact scene summary sent as context with the user's prompt. */
    fun sceneSummary(scene: Scene): String {
        if (scene.entities.isEmpty()) return "Scene is empty."
        return "Current scene entities: " + scene.entities.joinToString(", ") { e ->
            "${e.name}(${e.kind})@(${"%.1f".format(e.transform.x)},${"%.1f".format(e.transform.y)})"
        }
    }
}
