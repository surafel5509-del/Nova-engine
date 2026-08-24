package dev.nova.editor.ui.editor

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import dev.nova.editor.ai.AiProvider
import dev.nova.editor.ai.AiSettings
import dev.nova.editor.ai.AiSettingsStore
import dev.nova.editor.editor.EditorViewModel
import dev.nova.editor.ui.theme.NovaColors

/**
 * AI game-builder panel: configure a provider (Gemini / ChatGPT / Claude /
 * DeepSeek / custom OpenAI-compatible), type a prompt, and the returned
 * actions are applied to the scene as one undoable edit.
 */
@Composable
fun AiAssistantPanel(
    viewModel: EditorViewModel,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val store = remember { AiSettingsStore(context) }
    var settings by remember { mutableStateOf(store.load()) }
    var prompt by remember { mutableStateOf("") }
    var showSettings by remember { mutableStateOf(false) }

    Column(modifier.fillMaxSize().padding(8.dp).verticalScroll(rememberScrollState())) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "AI Game Builder — ${settings.provider.label}",
                style = MaterialTheme.typography.titleSmall,
                color = NovaColors.Text,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = { showSettings = true }) { Text("Settings") }
        }

        if (settings.apiKey.isBlank()) {
            Text(
                "No API key set for ${settings.provider.label}. Open Settings and paste your key.",
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.Warning,
            )
            Spacer(Modifier.height(4.dp))
        }

        OutlinedTextField(
            value = prompt,
            onValueChange = { prompt = it },
            modifier = Modifier.fillMaxWidth(),
            placeholder = {
                Text(
                    "e.g. Create a platformer level with a player, ground, and jump script",
                    style = MaterialTheme.typography.bodySmall,
                )
            },
            textStyle = MaterialTheme.typography.bodySmall,
            minLines = 3,
        )
        Spacer(Modifier.height(6.dp))

        Row(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Button(
                onClick = { viewModel.sendAiPrompt(settings, prompt) },
                enabled = !viewModel.aiBusy && settings.apiKey.isNotBlank() && prompt.isNotBlank(),
            ) {
                if (viewModel.aiBusy) {
                    CircularProgressIndicator(
                        modifier = Modifier.height(16.dp),
                        strokeWidth = 2.dp,
                        color = NovaColors.OnPrimary,
                    )
                } else {
                    Text("Build with AI")
                }
            }
            TextButton(onClick = { prompt = QUICK_PROMPTS.random() }, enabled = !viewModel.aiBusy) {
                Text("Idea")
            }
        }

        viewModel.aiLastReply?.let { reply ->
            Spacer(Modifier.height(8.dp))
            Text(
                reply.take(1500),
                style = MaterialTheme.typography.bodySmall,
                color = NovaColors.TextDim,
            )
        }
    }

    if (showSettings) {
        AiSettingsDialog(
            initial = settings,
            onDismiss = { showSettings = false },
            onSave = { newSettings ->
                settings = newSettings
                store.save(newSettings)
                showSettings = false
            },
        )
    }
}

private val QUICK_PROMPTS = listOf(
    "Create a platformer level: ground, dynamic player with a Lua jump script, and a coin to collect.",
    "Add 5 static platform blocks in a staircase pattern on the right side.",
    "Create a space shooter: player ship at bottom, 3 asteroids falling from above with scripts.",
    "Add a particle torch with orange fire at position (3, 0).",
    "Create a brick breaker: paddle, bouncing ball, and a row of 6 bricks.",
)

@Composable
private fun AiSettingsDialog(
    initial: AiSettings,
    onDismiss: () -> Unit,
    onSave: (AiSettings) -> Unit,
) {
    var provider by remember { mutableStateOf(initial.provider) }
    var apiKey by remember(initial) { mutableStateOf(initial.apiKey) }
    var baseUrl by remember(initial) { mutableStateOf(initial.baseUrl) }
    var model by remember(initial) { mutableStateOf(initial.model) }
    var providerMenu by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("AI Provider Settings") },
        text = {
            Column {
                OutlinedButton(onClick = { providerMenu = true }, modifier = Modifier.fillMaxWidth()) {
                    Text("Provider: ${provider.label}")
                }
                DropdownMenu(expanded = providerMenu, onDismissRequest = { providerMenu = false }) {
                    AiProvider.entries.forEach { p ->
                        DropdownMenuItem(
                            text = { Text(p.label) },
                            onClick = {
                                provider = p
                                if (baseUrl.isBlank() || AiProvider.entries.any { it.defaultBaseUrl == baseUrl }) {
                                    baseUrl = p.defaultBaseUrl
                                }
                                if (model.isBlank() || AiProvider.entries.any { it.defaultModel == model }) {
                                    model = p.defaultModel
                                }
                                providerMenu = false
                            },
                        )
                    }
                }
                Spacer(Modifier.height(6.dp))
                OutlinedTextField(
                    value = apiKey,
                    onValueChange = { apiKey = it },
                    label = { Text("API key", style = MaterialTheme.typography.labelSmall) },
                    visualTransformation = PasswordVisualTransformation(),
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = baseUrl,
                    onValueChange = { baseUrl = it },
                    label = { Text("Base URL", style = MaterialTheme.typography.labelSmall) },
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = model,
                    onValueChange = { model = it },
                    label = { Text("Model", style = MaterialTheme.typography.labelSmall) },
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    "Keys stay on this device (SharedPreferences).",
                    style = MaterialTheme.typography.labelSmall,
                    color = NovaColors.TextDim,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onSave(AiSettings(provider, apiKey.trim(), baseUrl.trim(), model.trim())) }) {
                Text("Save")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
