package dev.nova.editor.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Tests for UI elements and camera follow in the render-scene projection. */
class UiAndFollowTest {

    @Test
    fun `ui entity produces a render ui element with text key`() {
        val ui = Entity(name = "Score", ui = UiComponent(kind = "label", text = "Score: 0"))
        val render = buildRenderScene(Scene(entities = listOf(ui)), null)
        assertEquals(1, render.uiElements.size)
        val el = render.uiElements[0]
        assertEquals("label", el.kind)
        assertEquals("ui://text/${ui.id}", el.textKey)
        assertEquals(3f, el.width, 1e-4f)
    }

    @Test
    fun `ui with blank text has no text key`() {
        val panel = Entity(name = "P", ui = UiComponent(kind = "panel", text = ""))
        val render = buildRenderScene(Scene(entities = listOf(panel)), null)
        assertEquals("", render.uiElements[0].textKey)
    }

    @Test
    fun `camera follow resolves target name to id`() {
        val player = Entity(name = "Player", sprite = SpriteComponent())
        val camera = Entity(
            name = "Cam",
            camera = CameraComponent(followTargetName = "player", followLerp = 6f),
        )
        val render = buildRenderScene(Scene(entities = listOf(player, camera)), null)
        assertNotNull(render.gameCamera)
        assertEquals(player.id, render.gameCamera!!.followId)
        assertEquals(6f, render.gameCamera!!.followLerp, 1e-4f)
    }

    @Test
    fun `camera with no follow target has empty followId`() {
        val camera = Entity(name = "Cam", camera = CameraComponent())
        val render = buildRenderScene(Scene(entities = listOf(camera)), null)
        assertEquals("", render.gameCamera!!.followId)
    }

    @Test
    fun `sprite flip is forwarded to the render sprite`() {
        val sprite = Entity(name = "S", sprite = SpriteComponent(flipX = true, flipY = true))
        val render = buildRenderScene(Scene(entities = listOf(sprite)), null)
        assertTrue(render.sprites[0].flipX)
        assertTrue(render.sprites[0].flipY)
    }

    @Test
    fun `disabled ui is excluded`() {
        val ui = Entity(name = "L", enabled = false, ui = UiComponent())
        val render = buildRenderScene(Scene(entities = listOf(ui)), null)
        assertTrue(render.uiElements.isEmpty())
    }
}
