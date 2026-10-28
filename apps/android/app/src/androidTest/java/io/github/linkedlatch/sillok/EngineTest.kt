package io.github.linkedlatch.sillok

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import io.github.linkedlatch.sillok.engine.Engine
import io.github.linkedlatch.sillok.engine.Sillok
import io.github.linkedlatch.sillok.engine.SillokException
import io.github.linkedlatch.sillok.engine.Transcript
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

/** The JNI bridge and the transcript model, without the models (CI has none). */
@RunWith(AndroidJUnit4::class)
class EngineTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    private val document = """
        {"version": 1, "duration_ms": 9000,
         "speakers": [{"id": 0, "name": null}, {"id": 1, "name": null}],
         "segments": [
           {"start_ms": 0, "end_ms": 2000, "text": "안녕하세요 😀", "language": "ko", "speaker": 0,
            "words": [{"text": "안녕하세요", "start_ms": 0, "end_ms": 900, "confidence": 0.9, "speaker": 0}]},
           {"start_ms": 2500, "end_ms": 4000, "text": "Hello", "language": "en", "speaker": 1, "words": []},
           {"start_ms": 5000, "end_ms": 9000, "text": "다시", "language": "ko", "speaker": 0, "words": []}],
         "models": {}}
    """.trimIndent()

    @Test
    fun catalogNamesTheModels() {
        val roles = Sillok.catalog().map { it.role }
        assertTrue(roles.toString(), roles.containsAll(listOf("voice_detection", "transcription", "diarization")))
        assertTrue(Sillok.version.startsWith("sillok "))
    }

    @Test
    fun devicesIncludeTheCpu() {
        assertTrue(Sillok.devices().any { it.type == "cpu" })
    }

    @Test
    fun engineReportsMissingModels() {
        val empty = File(context.cacheDir, "no-models").apply { mkdirs() }
        try {
            Engine(empty, context.cacheDir, null, 1).close()
            fail("engine created without models")
        } catch (e: SillokException) {
            assertEquals(SillokException.MODEL_ERROR, e.status)
            assertTrue(e.message!!, e.message!!.contains("missing"))
        }
    }

    @Test
    fun exportKeepsUnicodeAndNamesSpeakers() {
        val named = Transcript(document).withSpeakerNames(mapOf(0 to "민지 "))
        val text = Sillok.export(named.json, "txt", "화자 {n}")
        assertTrue(text, text.contains("민지: 안녕하세요 😀"))
        assertTrue(text, text.contains("화자 2: Hello"))
        val srt = Sillok.export(named.json, "srt", "Speaker {n}")
        assertTrue(srt, srt.contains("00:00:02,500 --> 00:00:04,000"))
    }

    @Test
    fun transcriptFindsThePlayingSegment() {
        val transcript = Transcript(document)
        assertEquals(9000L, transcript.durationMs)
        assertEquals(0, transcript.segmentAt(1000))
        assertEquals(-1, transcript.segmentAt(2200))  // between segments
        assertEquals(1, transcript.segmentAt(2500))
        assertEquals(2, transcript.segmentAt(8999))
        assertEquals(-1, transcript.segmentAt(9000))
    }

    @Test
    fun clearedSpeakerNameBecomesNull() {
        val named = Transcript(document).withSpeakerNames(mapOf(1 to "Alex"))
        assertEquals("Alex", named.speakerName(1))
        val cleared = named.withSpeakerNames(mapOf(1 to "  "))
        assertEquals(null, cleared.speakerName(1))
        // Fields the app does not show survive the edit.
        assertTrue(cleared.json.contains("\"confidence\""))
    }
}
