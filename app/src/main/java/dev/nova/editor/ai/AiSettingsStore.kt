package dev.nova.editor.ai

import android.content.Context

/** Persists AI provider settings (API key stays on-device in SharedPreferences). */
class AiSettingsStore(context: Context) {

    private val prefs = context.getSharedPreferences("nova_ai_settings", Context.MODE_PRIVATE)

    fun load(): AiSettings {
        val providerName = prefs.getString(KEY_PROVIDER, AiProvider.GEMINI.name) ?: AiProvider.GEMINI.name
        val provider = runCatching { AiProvider.valueOf(providerName) }.getOrDefault(AiProvider.GEMINI)
        return AiSettings(
            provider = provider,
            apiKey = prefs.getString(keyFor(provider, KEY_API_KEY_SUFFIX), "") ?: "",
            baseUrl = prefs.getString(keyFor(provider, KEY_BASE_URL_SUFFIX), provider.defaultBaseUrl)
                ?: provider.defaultBaseUrl,
            model = prefs.getString(keyFor(provider, KEY_MODEL_SUFFIX), provider.defaultModel)
                ?: provider.defaultModel,
        )
    }

    fun save(settings: AiSettings) {
        prefs.edit().apply {
            putString(KEY_PROVIDER, settings.provider.name)
            putString(keyFor(settings.provider, KEY_API_KEY_SUFFIX), settings.apiKey)
            putString(keyFor(settings.provider, KEY_BASE_URL_SUFFIX), settings.baseUrl)
            putString(keyFor(settings.provider, KEY_MODEL_SUFFIX), settings.model)
            apply()
        }
    }

    private fun keyFor(provider: AiProvider, suffix: String) = "${provider.name.lowercase()}_$suffix"

    private companion object {
        const val KEY_PROVIDER = "provider"
        const val KEY_API_KEY_SUFFIX = "api_key"
        const val KEY_BASE_URL_SUFFIX = "base_url"
        const val KEY_MODEL_SUFFIX = "model"
    }
}
