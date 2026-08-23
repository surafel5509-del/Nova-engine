package dev.nova.editor.audio

import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.SoundPool
import java.io.File

/**
 * Real audio playback for the editor's Play mode and the game runtime.
 * SFX are preloaded into a SoundPool (low latency); music streams through
 * MediaPlayer. Keyed by project-relative path.
 */
class AudioEngine(private val projectPath: String) {

    private val soundPool: SoundPool = SoundPool.Builder()
        .setMaxStreams(16)
        .setAudioAttributes(
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build(),
        )
        .build()

    private val loaded = HashMap<String, Int>()          // path -> soundId
    private val streams = HashMap<String, Int>()         // path -> active loop streamId
    private var musicPlayer: MediaPlayer? = null
    private var musicPath: String? = null

    /** Preloads an SFX file. Safe to call repeatedly. */
    fun preload(path: String) {
        if (loaded.containsKey(path)) return
        val file = File(projectPath, path)
        if (!file.exists()) return
        val id = soundPool.load(file.absolutePath, 1)
        if (id != 0) loaded[path] = id
    }

    /** Plays an SFX once (or loops if [loop]); volume/pitch applied. */
    fun play(path: String, volume: Float = 1f, pitch: Float = 1f, loop: Boolean = false) {
        preload(path)
        val soundId = loaded[path] ?: return
        if (streams.containsKey(path)) return   // already playing (loops)
        val streamId = soundPool.play(
            soundId,
            volume.coerceIn(0f, 1f), volume.coerceIn(0f, 1f),
            1, if (loop) -1 else 0, pitch.coerceIn(0.5f, 2f),
        )
        if (streamId != 0 && loop) streams[path] = streamId
    }

    /** Stops a looping SFX. */
    fun stopLoop(path: String) {
        streams.remove(path)?.let { soundPool.stop(it) }
    }

    /** Streams music (one track at a time). */
    fun playMusic(path: String, volume: Float = 1f, loop: Boolean = true) {
        val file = File(projectPath, path)
        if (!file.exists()) return
        if (musicPath == path && musicPlayer?.isPlaying == true) return
        stopMusic()
        runCatching {
            musicPlayer = MediaPlayer().apply {
                setDataSource(file.absolutePath)
                isLooping = loop
                setVolume(volume.coerceIn(0f, 1f), volume.coerceIn(0f, 1f))
                prepare()
                start()
            }
            musicPath = path
        }
    }

    fun stopMusic() {
        musicPlayer?.let { player ->
            runCatching { if (player.isPlaying) player.stop() }
            player.release()
        }
        musicPlayer = null
        musicPath = null
    }

    /** Stops everything (Play mode stop / runtime exit). */
    fun stopAll() {
        for ((_, streamId) in streams) soundPool.stop(streamId)
        streams.clear()
        stopMusic()
    }

    fun release() {
        stopAll()
        soundPool.release()
    }
}
