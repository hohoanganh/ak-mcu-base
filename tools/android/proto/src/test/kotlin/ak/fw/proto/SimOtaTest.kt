package ak.fw.proto

import java.io.File
import java.nio.file.Files
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.jupiter.api.Assumptions.assumeTrue

/**
 * The Kotlin client against ak_sim (simulated bootloader + app) over pipes,
 * with images from tools/mkimage.py: the same path the app takes over USB.
 *
 * Needs AK_SIM=<path to ak_sim> (cmake --build build/host --target ak_sim)
 * and python3; skipped without them.
 */
class SimOtaTest {
	private val sim = System.getenv("AK_SIM")?.let(::File)
	private val tools = File(System.getProperty("ak.tools", "../.."))

	private class Sim(cmd: List<String>) : AutoCloseable {
		val p: Process = ProcessBuilder(cmd).redirectError(ProcessBuilder.Redirect.DISCARD).start()
		val text = StringBuffer()
		val fw = AkFw({ p.outputStream.write(it); p.outputStream.flush() }, { text.append(String(it)) })
		private val reader = thread(isDaemon = true) {
			val buf = ByteArray(4096)
			while (true) {
				val n = try { p.inputStream.read(buf) } catch (_: Exception) { -1 }
				if (n < 0) break
				fw.receive(buf, n)
			}
		}

		fun waitText(needle: String, ms: Long = 5000): Boolean {
			val end = System.currentTimeMillis() + ms
			while (System.currentTimeMillis() < end) {
				if (text.contains(needle)) return true
				Thread.sleep(10)
			}
			return false
		}

		override fun close() {
			p.outputStream.close()
			if (!p.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) p.destroyForcibly()
			reader.join(1000)
		}
	}

	private fun mkimage(dir: File, name: String, vararg args: String): ByteArray {
		val out = File(dir, name)
		val cmd = listOf("python3", File(tools, "mkimage.py").path, "synthetic", "-o", out.path) + args
		val p = ProcessBuilder(cmd).redirectErrorStream(true).start()
		val log = p.inputStream.readBytes()
		assertEquals(0, p.waitFor(), "mkimage: ${String(log)}")
		return out.readBytes()
	}

	@Test
	fun flashUpdateConfirmRollback() {
		assumeTrue(sim != null && sim.canExecute(), "AK_SIM not set")
		val dir = Files.createTempDirectory("akfw").toFile()
		try {
			val img1 = mkimage(dir, "v1.img", "--version", "1.0.0", "--size", "9000", "--seed", "1")
			val img2 = mkimage(dir, "v2.img", "--version", "2.1.0", "--size", "20000", "--seed", "2")
			val bad = mkimage(dir, "bad.img", "--version", "9.9.9", "--board", "other", "--seed", "3")
			val flash = File(dir, "flash.bin").path

			Sim(listOf(sim!!.path, "--flash", flash, "--fresh")).use { s ->
				val fw = s.fw
				val i = fw.waitReady(5000)
				assertEquals("bootloader", i.role, "blank flash -> loader")
				assertEquals("ak-host", i.board)

				var r = fw.flash(img1)
				assertEquals("app" to "1.0.0", r.role to r.version, "v1 via loader")

				var progressed = 0
				r = fw.flash(img2, { done, _ -> progressed = done })
				assertEquals("app" to "2.1.0", r.role to r.version, "OTA v2 via app")
				assertEquals(img2.size, progressed)

				val e = assertFailsWith<FwException> { fw.upload(bad) }
				assertTrue(e.message!!.contains("wrong board"), e.message)

				fw.sendText("ver\r")
				assertTrue(s.waitText("app v2.1.0"), "shell 'ver' through the log text")

				assertTrue(fw.confirm(), "v2 on trial, CONFIRM ends it")
				assertFalse(fw.confirm(), "nothing left to confirm")

				r = fw.rollback()
				assertEquals("app" to "1.0.0", r.role to r.version, "ROLLBACK -> v1")

				assertEquals("bootloader", fw.loader().role, "LOADER -> bootloader")
				fw.run()
				assertEquals("1.0.0", fw.waitApp(10_000).version, "RUN -> back to app v1")
			}
		} finally {
			dir.deleteRecursively()
		}
	}
}
