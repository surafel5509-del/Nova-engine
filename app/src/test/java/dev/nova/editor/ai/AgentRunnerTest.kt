package dev.nova.editor.ai

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AgentRunnerTest {

    @Test
    fun `parses a numbered plan`() {
        val reply = """Here is the plan:
```json
{"tasks":[{"n":1,"title":"Create player"},{"n":2,"title":"Add physics"},{"n":3,"title":"Add UI"}]}
```
"""
        val plan = AgentRunner.parsePlan(reply)
        assertEquals(3, plan.size)
        assertEquals("Create player", plan[0].title)
        assertEquals(3, plan[2].number)
    }

    @Test
    fun `falls back to a default plan on bad json`() {
        val plan = AgentRunner.parsePlan("I cannot help with that.")
        assertTrue(plan.size >= 5)
        assertEquals(1, plan[0].number)
    }

    @Test
    fun `handles tasks without numbers`() {
        val plan = AgentRunner.parsePlan("""{"tasks":[{"title":"Do A"},{"title":"Do B"}]}""")
        assertEquals(2, plan.size)
        assertEquals(1, plan[0].number)
        assertEquals(2, plan[1].number)
    }
}
