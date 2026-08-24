package dev.nova.editor.ai

import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class AiActionApplierTest {

    @get:Rule
    val tmp = TemporaryFolder()

    @Test
    fun `extract json from markdown-wrapped reply`() {
        val reply = "Here is your level:\n```json\n{\"actions\":[]}\n```\nDone!"
        assertEquals("{\"actions\":[]}", AiActionApplier.extractJson(reply))
        assertEquals(null, AiActionApplier.extractJson("no json here"))
    }

    @Test
    fun `create entity action adds to scene`() {
        val reply = """{"actions":[{"type":"create_entity","kind":"SPRITE","name":"Enemy","x":2,"y":3,"r":1,"g":0,"b":0}]}"""
        val result = AiActionApplier.apply(Scene(), reply, tmp.root.absolutePath)
        assertEquals(1, result.actions)
        assertEquals(1, result.scene.entities.size)
        val e = result.scene.entities[0]
        assertEquals("Enemy", e.name)
        assertEquals(2f, e.transform.x, 1e-4f)
        assertEquals(0f, e.sprite!!.g, 1e-4f)
    }

    @Test
    fun `script action writes file and attaches component`() {
        val reply = """{"actions":[
            {"type":"create_entity","kind":"PHYSICS_BODY","name":"Player"},
            {"type":"add_script","entityName":"Player","path":"scripts/p.lua","source":"-- code"}
        ]}""".trimIndent()
        val root = tmp.newFolder("proj")
        val result = AiActionApplier.apply(Scene(), reply, root.absolutePath)
        assertTrue(result.scripts.containsKey("scripts/p.lua"))
        val player = result.scene.entities.first { it.name == "Player" }
        assertNotNull(player.script)
        assertEquals("scripts/p.lua", player.script!!.scriptPath)
        assertTrue(java.io.File(root, "scripts/p.lua").exists())
    }

    @Test
    fun `set physics updates named entity`() {
        var scene = Scene()
        scene = SceneOps.add(scene, SceneOps.createEntity(dev.nova.editor.scene.EntityKind.SPRITE, "Box"))
        val reply = """{"actions":[{"type":"set_physics","name":"Box","bodyType":"dynamic","restitution":0.8}]}"""
        val result = AiActionApplier.apply(scene, reply, tmp.root.absolutePath)
        val box = result.scene.entities[0]
        assertEquals("dynamic", box.physicsBody!!.bodyType)
        assertEquals(0.8f, box.physicsBody!!.restitution, 1e-4f)
    }

    @Test
    fun `malformed reply returns zero actions`() {
        val result = AiActionApplier.apply(Scene(), "Just some prose, no JSON.", tmp.root.absolutePath)
        assertEquals(0, result.actions)
    }

    @Test
    fun `scene summary lists entities`() {
        var scene = Scene()
        scene = SceneOps.add(scene, SceneOps.createEntity(dev.nova.editor.scene.EntityKind.SPRITE, "Hero"))
        val summary = AiActionApplier.sceneSummary(scene)
        assertTrue(summary.contains("Hero"))
    }
}
