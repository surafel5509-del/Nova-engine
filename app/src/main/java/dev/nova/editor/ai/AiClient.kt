package dev.nova.editor.ai

import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL

/** Supported LLM providers (custom API keys; OpenAI-compatible covers many). */
enum class AiProvider(val label: String, val defaultBaseUrl: String, val defaultModel: String) {
    GEMINI("Google Gemini", "https://generativelanguage.googleapis.com", "gemini-1.5-flash"),
    OPENAI("OpenAI (ChatGPT)", "https://api.openai.com", "gpt-4o-mini"),
    CLAUDE("Anthropic Claude", "https://api.anthropic.com", "claude-3-5-sonnet-latest"),
    DEEPSEEK("DeepSeek", "https://api.deepseek.com", "deepseek-chat"),
    CUSTOM("OpenAI-compatible", "", ""),
}

data class AiSettings(
    val provider: AiProvider = AiProvider.GEMINI,
    val apiKey: String = "",
    val baseUrl: String = provider.defaultBaseUrl,
    val model: String = provider.defaultModel,
)

/** One HTTP request built by [AiClient.buildRequest]; kept separate for testability. */
data class HttpRequest(
    val url: String,
    val headers: Map<String, String>,
    val body: String,
)

/**
 * Multi-provider chat client. Request building + response parsing are pure
 * functions (unit-tested); only [chat] touches the network.
 */
object AiClient {

    fun buildRequest(settings: AiSettings, systemPrompt: String, userPrompt: String): HttpRequest =
        when (settings.provider) {
            AiProvider.GEMINI -> {
                val url = "${settings.baseUrl}/v1beta/models/${settings.model}:generateContent?key=${settings.apiKey}"
                val body = """{"contents":[{"role":"user","parts":[{"text":${quote(systemPrompt + "\n\n" + userPrompt)}}]}]}"""
                HttpRequest(url, mapOf("Content-Type" to "application/json"), body)
            }
            AiProvider.CLAUDE -> {
                val url = "${settings.baseUrl}/v1/messages"
                val body = """{"model":"${settings.model}","max_tokens":2048,"system":${quote(systemPrompt)},""" +
                    """"messages":[{"role":"user","content":${quote(userPrompt)}}]}"""
                HttpRequest(
                    url,
                    mapOf(
                        "Content-Type" to "application/json",
                        "x-api-key" to settings.apiKey,
                        "anthropic-version" to "2023-06-01",
                    ),
                    body,
                )
            }
            // OpenAI-compatible: ChatGPT, DeepSeek, CUSTOM (and local servers).
            else -> {
                val url = "${settings.baseUrl}/v1/chat/completions"
                val body = """{"model":"${settings.model}","messages":[""" +
                    """{"role":"system","content":${quote(systemPrompt)}},""" +
                    """{"role":"user","content":${quote(userPrompt)}}]}"""
                HttpRequest(
                    url,
                    mapOf(
                        "Content-Type" to "application/json",
                        "Authorization" to "Bearer ${settings.apiKey}",
                    ),
                    body,
                )
            }
        }

    /** Extracts the assistant text from each provider's response shape. */
    fun parseResponse(settings: AiSettings, json: String): String = when (settings.provider) {
        AiProvider.GEMINI -> extractJsonPath(json, "\"text\"") ?: throw apiError(json, "Gemini")
        AiProvider.CLAUDE -> extractJsonPath(json, "\"text\"") ?: throw apiError(json, "Claude")
        else -> {
            // OpenAI shape: choices[0].message.content
            extractOpenAiContent(json) ?: throw apiError(json, "OpenAI")
        }
    }

    private fun apiError(json: String, provider: String): IllegalStateException {
        val message = extractJsonPath(json, "\"message\"")
        return IllegalStateException(message?.let { "API error: $it" } ?: "No content in $provider response")
    }

    /** Minimal JSON string extraction for a flat key (handles escapes). */
    private fun extractJsonPath(json: String, key: String): String? {
        val idx = json.indexOf(key)
        if (idx < 0) return null
        val colon = json.indexOf(':', idx + key.length)
        if (colon < 0) return null
        var i = colon + 1
        while (i < json.length && json[i].isWhitespace()) i++
        if (i >= json.length || json[i] != '"') return null
        return readJsonString(json, i)
    }

    /** OpenAI responses nest content inside message: find "content":"...". */
    private fun extractOpenAiContent(json: String): String? = extractJsonPath(json, "\"content\"")

    private fun readJsonString(json: String, start: Int): String? {
        val sb = StringBuilder()
        var i = start + 1
        while (i < json.length) {
            val c = json[i]
            when {
                c == '\\' && i + 1 < json.length -> {
                    when (json[i + 1]) {
                        'n' -> sb.append('\n')
                        't' -> sb.append('\t')
                        'r' -> sb.append('\r')
                        '"' -> sb.append('"')
                        '\\' -> sb.append('\\')
                        '/' -> sb.append('/')
                        'u' -> {
                            if (i + 5 < json.length) {
                                sb.append(json.substring(i + 2, i + 6).toIntOrNull(16)?.toChar() ?: '?')
                                i += 4
                            }
                        }
                        else -> sb.append(json[i + 1])
                    }
                    i += 2
                }
                c == '"' -> return sb.toString()
                else -> {
                    sb.append(c)
                    i++
                }
            }
        }
        return null
    }

    private fun quote(text: String): String {
        val sb = StringBuilder("\"")
        for (c in text) {
            when (c) {
                '"' -> sb.append("\\\"")
                '\\' -> sb.append("\\\\")
                '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r")
                '\t' -> sb.append("\\t")
                else -> sb.append(c)
            }
        }
        sb.append('"')
        return sb.toString()
    }

    /** Blocking HTTP POST (call from a background thread). */
    fun chat(settings: AiSettings, systemPrompt: String, userPrompt: String): String {
        require(settings.apiKey.isNotBlank()) { "API key is empty — set it in AI Settings." }
        val request = buildRequest(settings, systemPrompt, userPrompt)
        val connection = URL(request.url).openConnection() as HttpURLConnection
        try {
            connection.requestMethod = "POST"
            connection.doOutput = true
            connection.connectTimeout = 30_000
            connection.readTimeout = 60_000
            request.headers.forEach { (k, v) -> connection.setRequestProperty(k, v) }
            OutputStreamWriter(connection.outputStream).use { it.write(request.body) }
            val code = connection.responseCode
            val stream = if (code in 200..299) connection.inputStream else connection.errorStream
            val text = BufferedReader(InputStreamReader(stream)).use { it.readText() }
            if (code !in 200..299) {
                throw IllegalStateException("HTTP $code: ${text.take(400)}")
            }
            return parseResponse(settings, text)
        } finally {
            connection.disconnect()
        }
    }
}
