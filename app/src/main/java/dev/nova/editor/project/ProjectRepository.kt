package dev.nova.editor.project

import dev.nova.editor.scene.EntityKind
import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import dev.nova.editor.scene.SceneJson
import dev.nova.editor.scene.TransformComponent
import dev.nova.editor.scene.SpriteComponent
import dev.nova.editor.scene.serializeScene
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import java.io.File

const val PROJECT_FORMAT_VERSION = 1

enum class ProjectTemplate(val label: String, val implemented: Boolean) {
    EMPTY("Empty 2D", true),
    PLATFORMER("Platformer", true),
    RPG("RPG", true),
    ARCADE("Arcade", true),
}

enum class ProjectOrientation(val label: String) { PORTRAIT("Portrait"), LANDSCAPE("Landscape") }
enum class ProjectDimension(val label: String) { TWO_D("2D"), TWO_D_PLUS_3D("2D + 3D"), THREE_D("3D") }

@Serializable
data class ProjectConfig(
    val version: Int = PROJECT_FORMAT_VERSION,
    val name: String,
    val packageName: String = "com.example.game",
    val projectVersion: String = "1.0.0",
    val orientation: String = ProjectOrientation.LANDSCAPE.name,
    val dimension: String = ProjectDimension.TWO_D.name,
    val template: String = ProjectTemplate.EMPTY.name,
    var lastOpenedEpochMs: Long = 0L,
)

@Serializable
data class RecentProject(
    val name: String,
    val path: String,
    val lastOpenedEpochMs: Long,
)

/**
 * File-backed project store. [rootDir] is the directory that contains one
 * sub-directory per project (e.g. <app files>/projects). Injected so the
 * repository is unit-testable on the JVM without Android.
 */
class ProjectRepository(private val rootDir: File) {

    fun listProjects(): List<RecentProject> {
        val dir = rootDir
        if (!dir.exists()) return emptyList()
        return dir.listFiles { f -> f.isDirectory }
            ?.mapNotNull { projectDir ->
                val configFile = File(projectDir, PROJECT_FILE)
                if (!configFile.exists()) return@mapNotNull null
                runCatching {
                    val config = SceneJson.decodeFromString<ProjectConfig>(configFile.readText())
                    RecentProject(config.name, projectDir.absolutePath, config.lastOpenedEpochMs)
                }.getOrNull()
            }
            ?.sortedByDescending { it.lastOpenedEpochMs }
            ?: emptyList()
    }

    fun createProject(
        name: String,
        packageName: String,
        projectVersion: String,
        orientation: ProjectOrientation,
        dimension: ProjectDimension,
        template: ProjectTemplate,
        nowEpochMs: Long = System.currentTimeMillis(),
    ): File {
        require(name.isNotBlank()) { "Project name must not be blank" }
        require(template.implemented) { "Template ${template.label} is not available yet" }
        val dir = uniqueProjectDir(name)
        File(dir, "scenes").mkdirs()
        File(dir, "assets/textures").mkdirs()
        File(dir, "assets/audio").mkdirs()
        File(dir, "prefabs").mkdirs()
        File(dir, "scripts").mkdirs()
        File(dir, "shaders").mkdirs()
        File(dir, "materials").mkdirs()

        val config = ProjectConfig(
            name = name.trim(),
            packageName = packageName.trim(),
            projectVersion = projectVersion.trim(),
            orientation = orientation.name,
            dimension = dimension.name,
            template = template.name,
            lastOpenedEpochMs = nowEpochMs,
        )
        File(dir, PROJECT_FILE).writeText(SceneJson.encodeToString(config))
        File(dir, "scenes/main.scene.json").writeText(serializeScene(templateScene(template)))
        templateScript(template)?.let { script ->
            val scriptFile = File(dir, "scripts/player.lua")
            scriptFile.parentFile?.mkdirs()
            scriptFile.writeText(script)
        }
        return dir
    }

    fun openProject(path: String, nowEpochMs: Long = System.currentTimeMillis()): Pair<ProjectConfig, Scene> {
        val dir = File(path)
        val configFile = File(dir, PROJECT_FILE)
        require(configFile.exists()) { "Not a Nova project: $path" }
        val config = SceneJson.decodeFromString<ProjectConfig>(configFile.readText())
        config.lastOpenedEpochMs = nowEpochMs
        configFile.writeText(SceneJson.encodeToString(config))

        val sceneFile = File(dir, "scenes/main.scene.json")
        val scene = if (sceneFile.exists()) {
            dev.nova.editor.scene.deserializeScene(sceneFile.readText())
        } else {
            templateScene(ProjectTemplate.EMPTY).also {
                sceneFile.parentFile?.mkdirs()
                sceneFile.writeText(serializeScene(it))
            }
        }
        return config to scene
    }

    fun saveScene(projectPath: String, scene: Scene) {
        val file = File(projectPath, "scenes/main.scene.json")
        file.parentFile?.mkdirs()
        file.writeText(serializeScene(scene))
    }

    fun deleteProject(path: String): Boolean = File(path).deleteRecursively()

    fun importTexture(projectPath: String, fileName: String, bytes: ByteArray): String {
        val safeName = fileName.substringAfterLast('/').ifBlank { "texture.png" }
        val dir = File(projectPath, "assets/textures").apply { mkdirs() }
        var target = File(dir, safeName)
        var n = 2
        while (target.exists()) {
            val dot = safeName.lastIndexOf('.')
            target = if (dot > 0) {
                File(dir, "${safeName.substring(0, dot)}_$n${safeName.substring(dot)}")
            } else {
                File(dir, "${safeName}_$n")
            }
            n++
        }
        target.writeBytes(bytes)
        return "assets/textures/${target.name}"
    }

    fun readTexture(projectPath: String, relativePath: String): ByteArray? {
        val file = File(projectPath, relativePath)
        return if (file.exists()) file.readBytes() else null
    }

    /** Loads config + scene from an explicit project directory (no recents update). */
    fun loadProjectScene(projectPath: String): Pair<ProjectConfig, Scene> {
        val dir = File(projectPath)
        val config = SceneJson.decodeFromString<ProjectConfig>(File(dir, PROJECT_FILE).readText())
        val sceneFile = File(dir, "scenes/main.scene.json")
        val scene = dev.nova.editor.scene.deserializeScene(sceneFile.readText())
        return config to scene
    }

    private fun uniqueProjectDir(name: String): File {
        val slug = name.trim().lowercase()
            .replace(Regex("[^a-z0-9]+"), "-")
            .trim('-')
            .ifBlank { "project" }
        var dir = File(rootDir, slug)
        var n = 2
        while (dir.exists()) {
            dir = File(rootDir, "$slug-$n")
            n++
        }
        dir.mkdirs()
        return dir
    }

    private fun templateScene(template: ProjectTemplate): Scene = when (template) {
        ProjectTemplate.PLATFORMER -> {
            var scene = Scene(name = "Main")
            scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.CAMERA, "Main Camera"))
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.SPRITE, "Ground").copy(
                    transform = TransformComponent(x = 0f, y = -2.5f),
                    sprite = SpriteComponent(width = 12f, height = 1f, r = 0.35f, g = 0.45f, b = 0.38f),
                    physicsBody = dev.nova.editor.scene.PhysicsBodyComponent(
                        bodyType = "static", colliderWidth = 12f, colliderHeight = 1f,
                    ),
                ),
            )
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.PHYSICS_BODY, "Player").copy(
                    transform = TransformComponent(x = 0f, y = 0f),
                    sprite = SpriteComponent(width = 1f, height = 1f, r = 0.3f, g = 0.7f, b = 0.95f),
                    physicsBody = dev.nova.editor.scene.PhysicsBodyComponent(
                        bodyType = "dynamic", colliderWidth = 1f, colliderHeight = 1f,
                    ),
                    script = dev.nova.editor.scene.ScriptComponent(scriptPath = "scripts/player.lua"),
                ),
            )
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.PARTICLE_SYSTEM, "Dust").copy(
                    transform = TransformComponent(x = 0f, y = -1.8f),
                ),
            )
            scene
        }
        ProjectTemplate.RPG -> {
            var scene = Scene(name = "Main")
            scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.CAMERA, "Main Camera"))
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.SPRITE, "Hero").copy(
                    transform = TransformComponent(x = 0f, y = 0f),
                    sprite = SpriteComponent(width = 1f, height = 1f, r = 0.85f, g = 0.75f, b = 0.4f),
                    animator = dev.nova.editor.scene.AnimatorComponent(frameCols = 4, frameRows = 1),
                ),
            )
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.PARTICLE_SYSTEM, "Torch").copy(
                    transform = TransformComponent(x = 2f, y = 1f),
                ),
            )
            scene
        }
        ProjectTemplate.ARCADE -> {
            var scene = Scene(name = "Main")
            scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.CAMERA, "Main Camera"))
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.PHYSICS_BODY, "Ball").copy(
                    transform = TransformComponent(x = 0f, y = 2f),
                    sprite = SpriteComponent(width = 0.6f, height = 0.6f, r = 0.95f, g = 0.4f, b = 0.4f),
                    physicsBody = dev.nova.editor.scene.PhysicsBodyComponent(
                        bodyType = "dynamic", restitution = 0.8f, colliderWidth = 0.6f, colliderHeight = 0.6f,
                    ),
                ),
            )
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.SPRITE, "Floor").copy(
                    transform = TransformComponent(x = 0f, y = -2f),
                    sprite = SpriteComponent(width = 10f, height = 0.5f, r = 0.4f, g = 0.4f, b = 0.5f),
                    physicsBody = dev.nova.editor.scene.PhysicsBodyComponent(
                        bodyType = "static", colliderWidth = 10f, colliderHeight = 0.5f,
                    ),
                ),
            )
            scene
        }
        else -> {
            var scene = Scene(name = "Main")
            scene = SceneOps.add(scene, SceneOps.createEntity(EntityKind.CAMERA, "Main Camera"))
            scene
        }
    }

    /** Starter Lua script for templates that need one (real gameplay logic). */
    private fun templateScript(template: ProjectTemplate): String? = when (template) {
        ProjectTemplate.PLATFORMER -> """-- Player controller (Lua 5.4) — generated by the Platformer template.
local speed = 5.0
local jumpVelocity = 9.0

function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local vx, vy = nova.get_velocity(id)
    nova.set_velocity(id, ax * speed, vy)

    if nova.input_jump() and nova.is_grounded(id) then
        nova.set_velocity(id, ax * speed, jumpVelocity)
        -- Hook a jump SFX here once imported:
        -- nova.play_sound("assets/audio/jump.wav")
    end
end
"""
        else -> null
    }

    companion object {
        const val PROJECT_FILE = "project.json"
    }
}
