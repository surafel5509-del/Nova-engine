package dev.nova.editor.ai

import dev.nova.editor.scene.Scene
import dev.nova.editor.scene.SceneOps
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AiClientTest {

    private val settings = AiSettings(AiProvider.OPENAI, "sk-test", "https://api.openai.com", "gpt-4o-mini")

    @Test
    fun `openai request shape`() {
        val req = AiClient.buildRequest(settings, "SYS", "USER")
        assertTrue(req.url.endsWith("/v1/chat/completions"))
        assertEquals("Bearer sk-test", req.headers["Authorization"])
        assertTrue(req.body.contains("\"role\":\"system\""))
        assertTrue(req.body.contains("gpt-4o-mini"))
    }

    @Test
    fun `gemini request embeds key in url`() {
        val g = AiSettings(AiProvider.GEMINI, "gk-123", "https://generativelanguage.googleapis.com", "gemini-1.5-flash")
        val req = AiClient.buildRequest(g, "SYS", "USER")
        assertTrue(req.url.contains("gemini-1.5-flash:generateContent?key=gk-123"))
        assertTrue(req.body.contains("\"contents\""))
    }

    @Test
    fun `claude request has anthropic headers`() {
        val c = AiSettings(AiProvider.CLAUDE, "ak-1", "https://api.anthropic.com", "claude-3-5-sonnet-latest")
        val req = AiClient.buildRequest(c, "SYS", "USER")
        assertEquals("ak-1", req.headers["x-api-key"])
        assertEquals("2023-06-01", req.headers["anthropic-version"])
        assertTrue(req.body.contains("\"max_tokens\""))
    }

    @Test
    fun `parse openai content`() {
        val json = """{"choices":[{"message":{"role":"assistant","content":"{\"actions\":[]}"}}]}"""
        val text = AiClient.parseResponse(settings, json)
        assertEquals("{\"actions\":[]}", text)
    }

    @Test
    fun `parse gemini text`() {
        val g = AiSettings(AiProvider.GEMINI, "k", "", "")
        val json = """{"candidates":[{"content":{"parts":[{"text":"hello world"}]}}]}"""
        assertEquals("hello world", AiClient.parseResponse(g, json))
    }

    @Test
    fun `parse escapes in content`() {
        val json = """{"choices":[{"message":{"content":"line1\nline2 \"quoted\""}}]}"""
        val text = AiClient.parseResponse(settings, json)
        assertTrue(text.contains("line1\nline2"))
        assertTrue(text.contains("\"quoted\""))
    }

    @Test
    fun `api error surfaces`() {
        val json = """{"error":{"message":"Invalid API key"}}"""
        try {
            AiClient.parseResponse(settings, json)
            throw AssertionError("expected error")
        } catch (e: IllegalStateException) {
            assertTrue(e.message!!.contains("Invalid API key") || e.message!!.contains("No content"))
        }
    }
}
