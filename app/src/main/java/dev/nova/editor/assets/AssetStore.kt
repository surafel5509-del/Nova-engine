package dev.nova.editor.assets

import java.io.File

/** One entry in the asset browser (file or directory). */
data class AssetEntry(
    val name: String,
    val relativePath: String,   // relative to project root
    val isDirectory: Boolean,
    val sizeBytes: Long,
) {
    val extension: String get() = name.substringAfterLast('.', "").lowercase()
    val isTexture: Boolean get() = extension in setOf("png", "jpg", "jpeg", "webp")
    val isAudio: Boolean get() = extension in setOf("wav", "ogg", "mp3")
    val isScene: Boolean get() = extension in setOf("json") && relativePath.contains("scenes")
}

/**
 * Browses and mutates the on-disk project asset tree. Kept separate from
 * ProjectRepository so the browser logic is independently testable.
 */
class AssetStore(private val projectPath: String) {

    fun list(relativeDir: String): List<AssetEntry> {
        val dir = File(projectPath, relativeDir)
        if (!dir.exists() || !dir.isDirectory) return emptyList()
        return dir.listFiles()
            ?.map { f ->
                AssetEntry(
                    name = f.name,
                    relativePath = if (relativeDir.isEmpty()) f.name else "$relativeDir/${f.name}",
                    isDirectory = f.isDirectory,
                    sizeBytes = if (f.isFile) f.length() else 0L,
                )
            }
            ?.sortedWith(compareBy({ !it.isDirectory }, { it.name.lowercase() }))
            ?: emptyList()
    }

    fun createFolder(relativeDir: String, name: String): Boolean {
        val safe = name.trim().replace(Regex("[^A-Za-z0-9_\\- ]"), "")
        if (safe.isBlank()) return false
        return File(projectPath, "$relativeDir/$safe").mkdirs()
    }

    fun delete(relativePath: String): Boolean {
        val f = File(projectPath, relativePath)
        return if (f.isDirectory) f.deleteRecursively() else f.delete()
    }

    fun rename(relativePath: String, newName: String): String? {
        val src = File(projectPath, relativePath)
        if (!src.exists()) return null
        val safe = newName.trim()
        if (safe.isBlank() || safe.contains('/')) return null
        val dest = File(src.parentFile, safe)
        if (dest.exists()) return null
        return if (src.renameTo(dest)) {
            val parent = src.parentFile?.absolutePath ?: projectPath
            val relParent = File(projectPath).toURI().relativize(File(parent).toURI()).path.trimEnd('/')
            if (relParent.isEmpty()) safe else "$relParent/$safe"
        } else null
    }

    fun readBytes(relativePath: String): ByteArray? {
        val f = File(projectPath, relativePath)
        return if (f.isFile) f.readBytes() else null
    }
}
