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

    /** Moves a file or folder to a new relative directory. Returns the new path or null. */
    fun move(relativePath: String, newDirRelative: String): String? {
        val src = File(projectPath, relativePath)
        if (!src.exists()) return null
        val destDir = File(projectPath, newDirRelative).apply { mkdirs() }
        val dest = File(destDir, src.name)
        if (dest.exists()) return null
        return if (src.renameTo(dest)) {
            val rel = File(projectPath).toURI().relativize(dest.toURI()).path.trimEnd('/')
            rel
        } else null
    }

    /** Creates an empty (or text) file. Returns its relative path or null. */
    fun createFile(relativeDir: String, name: String, content: String = ""): String? {
        val safe = name.trim().replace('/', '_')
        if (safe.isBlank()) return null
        val file = File(projectPath, "$relativeDir/$safe")
        if (file.exists()) return null
        file.parentFile?.mkdirs()
        file.writeText(content)
        return "$relativeDir/$safe"
    }

    /**
     * Imports a game-source ZIP into [destDirRelative], extracting entries
     * safely (no path traversal). Returns the number of files extracted.
     */
    fun importZip(zipBytes: ByteArray, destDirRelative: String): Int {
        var count = 0
        val destRoot = File(projectPath, destDirRelative).apply { mkdirs() }
        val destRootPath = destRoot.canonicalPath + File.separator
        java.util.zip.ZipInputStream(zipBytes.inputStream().buffered()).use { zip ->
            var entry = zip.nextEntry
            while (entry != null) {
                if (!entry.isDirectory) {
                    val entryName = entry.name
                    // Path-traversal guard: reject entries that escape the dest root
                    // both lexically ("..") and after canonicalization.
                    val outFile = File(destRoot, entryName)
                    if (!entryName.contains("..") && outFile.canonicalPath.startsWith(destRootPath)) {
                        outFile.parentFile?.mkdirs()
                        outFile.outputStream().use { zip.copyTo(it) }
                        count++
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return count
    }

    /** Duplicates a file next to the original (adds "_copy"). Returns the new path. */
    fun duplicate(relativePath: String): String? {
        val src = File(projectPath, relativePath)
        if (!src.isFile) return null
        val dot = src.name.lastIndexOf('.')
        val newName = if (dot > 0) "${src.name.substring(0, dot)}_copy${src.name.substring(dot)}" else "${src.name}_copy"
        val dest = File(src.parentFile, newName)
        var target = dest
        var n = 2
        while (target.exists()) {
            target = File(src.parentFile, if (dot > 0) "${src.name.substring(0, dot)}_copy$n${src.name.substring(dot)}" else "${src.name}_copy$n")
            n++
        }
        src.copyTo(target)
        val rel = File(projectPath).toURI().relativize(target.toURI()).path.trimEnd('/')
        return rel
    }

    /** Copies a file into another directory (paste). Returns the new path or null. */
    fun copyTo(relativePath: String, destDirRelative: String): String? {
        val src = File(projectPath, relativePath)
        if (!src.isFile) return null
        val destDir = File(projectPath, destDirRelative).apply { mkdirs() }
        var target = File(destDir, src.name)
        var n = 2
        while (target.exists()) {
            val dot = src.name.lastIndexOf('.')
            target = File(destDir, if (dot > 0) "${src.name.substring(0, dot)}_$n${src.name.substring(dot)}" else "${src.name}_$n")
            n++
        }
        src.copyTo(target)
        return File(projectPath).toURI().relativize(target.toURI()).path.trimEnd('/')
    }

    /** Exports a directory as a ZIP. Returns the zip file. */
    fun exportZip(relativeDir: String, outFile: File): Int {
        val root = File(projectPath, relativeDir)
        require(root.isDirectory) { "Not a directory: $relativeDir" }
        outFile.parentFile?.mkdirs()
        var count = 0
        java.util.zip.ZipOutputStream(outFile.outputStream().buffered()).use { zip ->
            val base = root.toPath()
            root.walkTopDown().filter { it.isFile }.forEach { file ->
                val rel = base.relativize(file.toPath()).toString().replace(File.separatorChar, '/')
                zip.putNextEntry(java.util.zip.ZipEntry(rel))
                file.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
                count++
            }
        }
        return count
    }
}
