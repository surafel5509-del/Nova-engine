package dev.nova.editor.build

import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Exports a project as a portable `.novapkg` zip (project.json + scenes +
 * assets + scripts). Pure JVM — fully unit-testable. The zip is what the
 * `:game` Gradle module consumes to produce a standalone APK.
 */
object BuildExporter {

    private val INCLUDED_TOP_LEVEL = setOf(
        "project.json", "scenes", "assets", "scripts", "textures", "audio",
    )

    /** Zips the project's game content into [outFile]. Returns entry count. */
    fun exportPackage(projectPath: String, outFile: File): Int {
        val root = File(projectPath)
        require(root.isDirectory) { "Not a project directory: $projectPath" }
        require(File(root, "project.json").isFile) { "Missing project.json in $projectPath" }

        outFile.parentFile?.mkdirs()
        var entries = 0
        ZipOutputStream(BufferedOutputStream(FileOutputStream(outFile))).use { zip ->
            for (name in INCLUDED_TOP_LEVEL) {
                val file = File(root, name)
                if (!file.exists()) continue
                entries += addToZip(zip, file, name)
            }
        }
        return entries
    }

    private fun addToZip(zip: ZipOutputStream, file: File, entryName: String): Int {
        if (file.isDirectory) {
            var count = 0
            file.listFiles()?.sortedBy { it.name }?.forEach { child ->
                count += addToZip(zip, child, "$entryName/${child.name}")
            }
            return count
        }
        zip.putNextEntry(ZipEntry(entryName))
        file.inputStream().use { it.copyTo(zip) }
        zip.closeEntry()
        return 1
    }

    /** Gradle command producing a standalone game APK from this project. */
    fun apkBuildCommand(projectPath: String): String =
        "./gradlew :game:assembleDebug -PnovaProjectPath=\"$projectPath\""
}
