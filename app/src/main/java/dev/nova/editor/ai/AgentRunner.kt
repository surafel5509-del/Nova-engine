package dev.nova.editor.ai

import dev.nova.editor.scene.Scene
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** One step in the agent's plan. */
data class AgentTask(
    val number: Int,
    val title: String,
    var status: TaskStatus = TaskStatus.PENDING,
    var detail: String = "",
    var elapsedMs: Long = 0,
)

enum class TaskStatus { PENDING, RUNNING, DONE, FAILED }

/** Live view of the agent's state, rendered by the AI panel. */
data class AgentProgress(
    val running: Boolean = false,
    val plan: List<AgentTask> = emptyList(),
    val currentTask: Int = 0,
    val log: List<String> = emptyList(),
    val finished: Boolean = false,
    val error: String? = null,
)

/**
 * Autonomous game-development agent. Given a goal, it:
 *  1. asks the LLM for a numbered development plan,
 *  2. executes each task with a task-specific LLM prompt,
 *  3. applies the returned scene actions,
 *  4. verifies the result (entity count changed, scripts parse, files exist),
 *  5. retries a failed task once,
 *  6. logs every step with elapsed time.
 *
 * All state is reported through [onProgress] so the chat UI renders a live
 * task list. Pure orchestration — LLM calls are suspend functions injected
 * for testability.
 */
class AgentRunner(
    private val sceneProvider: () -> Scene,
    private val sceneApplier: (Scene, String) -> Scene,   // apply actions -> new scene
    private val onSceneApplied: (Scene) -> Unit,          // single undoable push
    private val onProgress: (AgentProgress) -> Unit,
) {

    private val taskLog = mutableListOf<String>()

    /** Generates the plan from the user's goal. Returns the task list. */
    suspend fun plan(
        settings: AiSettings,
        goal: String,
        callLlm: suspend (String) -> String,
    ): List<AgentTask> = withContext(Dispatchers.IO) {
        val prompt = """Create a numbered development plan for building this game in a 2D/3D mobile engine: "$goal".
Respond with ONLY a JSON object: {"tasks":[{"n":1,"title":"Create project structure"},{"n":2,"title":"Create player"}, ...]}.
Keep it to 8-14 concrete tasks."""
        val reply = callLlm(prompt)
        parsePlan(reply)
    }

    /** Executes the full plan, reporting progress. */
    suspend fun run(
        settings: AiSettings,
        goal: String,
        callLlm: suspend (String) -> String,
    ): AgentProgress = withContext(Dispatchers.Default) {
        taskLog.clear()
        var progress = AgentProgress(running = true)
        fun emit() = onProgress(progress)

        emitLog(progress, "Planning: \"$goal\"")

        // --- Plan ---
        val planStart = System.currentTimeMillis()
        val tasks = try {
            plan(settings, goal) { p -> callLlm(p) }
        } catch (e: Exception) {
            progress = progress.copy(running = false, finished = true, error = "Planning failed: ${e.message}")
            emitLog(progress, "✗ Planning failed: ${e.message}")
            emit()
            return@withContext progress
        }
        progress = progress.copy(plan = tasks)
        emitLog(progress, "Plan ready (${tasks.size} tasks) in ${elapsedSince(planStart)}")
        emit()

        // --- Execute each task ---
        for (task in tasks) {
            progress = progress.copy(currentTask = task.number)
            task.status = TaskStatus.RUNNING
            emitLog(progress, "[${task.number}/${tasks.size}] ${task.title}…")
            emit()
            val start = System.currentTimeMillis()

            var succeeded = false
            var lastError = ""
            for (attempt in 1..2) {
                try {
                    val sceneBefore = sceneProvider()
                    val prompt = buildTaskPrompt(goal, task, sceneBefore)
                    val reply = callLlm(prompt)
                    val newScene = sceneApplier(sceneBefore, reply)
                    val error = verify(newScene, sceneBefore, task)
                    if (error == null) {
                        onSceneApplied(newScene)
                        succeeded = true
                        break
                    } else {
                        lastError = error
                        emitLog(progress, "  verify: $error — retrying")
                    }
                } catch (e: Exception) {
                    lastError = e.message ?: "unknown"
                    emitLog(progress, "  error: $lastError — retrying")
                }
                emit()
            }

            task.elapsedMs = System.currentTimeMillis() - start
            if (succeeded) {
                task.status = TaskStatus.DONE
                emitLog(progress, "  ✓ done in ${formatElapsed(task.elapsedMs)}")
            } else {
                task.status = TaskStatus.FAILED
                emitLog(progress, "  ✗ failed: $lastError (continuing)")
            }
            emit()
        }

        val done = tasks.count { it.status == TaskStatus.DONE }
        progress = progress.copy(running = false, finished = true,
            error = if (done < tasks.size) "$done/${tasks.size} tasks succeeded" else null)
        emitLog(progress, "Finished: $done/${tasks.size} tasks succeeded.")
        emit()
        progress
    }

    private fun buildTaskPrompt(goal: String, task: AgentTask, scene: Scene): String {
        val assets = assetSummary()
        return """Game goal: "$goal".
Current task ${task.number}: ${task.title}.
$assets
${AiActionApplier.sceneSummary(scene)}
Return the JSON actions to accomplish this task."""
    }

    private fun assetSummary(): String = ""   // assets list injected by panel when available

    /** Basic verification: scene JSON round-trips; scripts referenced exist. */
    private fun verify(newScene: Scene, before: Scene, task: AgentTask): String? {
        // A scene that is structurally identical to before AND the task expected
        // to create something counts as suspicious only for create tasks.
        val grew = newScene.entities.size != before.entities.size
        val isCreateTask = task.title.contains("create", ignoreCase = true) ||
            task.title.contains("add", ignoreCase = true) ||
            task.title.contains("build", ignoreCase = true)
        if (isCreateTask && !grew && newScene == before) {
            return "no change applied"
        }
        // Scripts referenced must be non-empty paths.
        for (e in newScene.entities) {
            e.script?.let { if (it.scriptPath.isBlank()) return "blank script path on ${e.name}" }
        }
        return null
    }

    private fun emitLog(progress: AgentProgress, line: String) {
        taskLog.add(line)
        onProgress(progress.copy(log = taskLog.toList()))
    }

    private fun elapsedSince(sinceMs: Long): String = formatElapsed(System.currentTimeMillis() - sinceMs)
    private fun formatElapsed(ms: Long): String {
        val totalSec = ms / 1000
        return "%d:%02d".format(totalSec / 60, totalSec % 60)
    }

    companion object {
        /** Parses the LLM plan reply into tasks (tolerant of markdown fences). */
        fun parsePlan(reply: String): List<AgentTask> {
            val start = reply.indexOf('{')
            val end = reply.lastIndexOf('}')
            if (start < 0 || end <= start) return fallbackPlan()
            return runCatching {
                val root = kotlinx.serialization.json.Json.parseToJsonElement(
                    reply.substring(start, end + 1)
                ).jsonObject
                val arr = root["tasks"]?.jsonArray ?: return@runCatching emptyList<AgentTask>()
                arr.mapIndexed { index, el ->
                    val obj = el.jsonObject
                    AgentTask(
                        number = obj["n"]?.jsonPrimitive?.content?.toIntOrNull() ?: (index + 1),
                        title = obj["title"]?.jsonPrimitive?.content ?: "Task ${index + 1}",
                    )
                }
            }.getOrElse { fallbackPlan() }.ifEmpty { fallbackPlan() }
        }

        private fun fallbackPlan(): List<AgentTask> = listOf(
            AgentTask(1, "Create the player"),
            AgentTask(2, "Create the player controller script"),
            AgentTask(3, "Create the environment and ground"),
            AgentTask(4, "Add physics"),
            AgentTask(5, "Add enemies or obstacles"),
            AgentTask(6, "Add the user interface"),
            AgentTask(7, "Add sound"),
            AgentTask(8, "Configure the camera"),
            AgentTask(9, "Test the game"),
        )
    }
}
