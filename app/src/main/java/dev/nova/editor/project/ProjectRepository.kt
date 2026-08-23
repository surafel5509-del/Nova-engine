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
    RPG("RPG", false),        // Phase 2+
    ARCADE("Arcade", false),  // Phase 2+
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
                ),
            )
            scene = SceneOps.add(
                scene,
                SceneOps.createEntity(EntityKind.PHYSICS_BODY, "Player").copy(
                    transform = TransformComponent(x = 0f, y = 0f),
                    sprite = SpriteComponent(width = 1f, height = 1f, r = 0.3f, g = 0.7f, b = 0.95f),
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

    companion object {
        const val PROJECT_FILE = "project.json"
    }
}
