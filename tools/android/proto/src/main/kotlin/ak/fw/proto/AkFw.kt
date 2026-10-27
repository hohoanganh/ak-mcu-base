package ak.fw.proto

class FwException(msg: String) : Exception(msg)

data class BoardInfo(
	val proto: Int,
	val role: String,
	val version: String,
	val build: Long,
	val board: String,
	val stagingSize: Long,
	val maxChunk: Int,
)

/**
 * fw_proto client, a port of AkFw in tools/ak_fw.py.
 *
 * The transport is not part of it: [write] sends bytes to the board, and
 * whatever reads the port hands every received byte to [receive] (from any
 * thread). Log text that is not a frame goes to [onText].
 *
 * Every request blocks: call it from a worker thread, never from the UI thread.
 */
class AkFw(private val write: (ByteArray) -> Unit, private val onText: (ByteArray) -> Unit = {}) {
	private val lock = Object()
	private val frames = ArrayList<Frame>()
	private val parser = FrameParser(onText) { frames.add(it) }
	private var seq = 0

	fun receive(data: ByteArray, len: Int = data.size) {
		synchronized(lock) {
			parser.feed(data, len)
			lock.notifyAll()
		}
	}

	fun sendText(s: String) = write(s.toByteArray())

	fun request(cmd: Int, payload: ByteArray = ByteArray(0), timeoutMs: Long = 1000, retries: Int = 3): ByteArray {
		repeat(retries) {
			seq = (seq + 1) and 0xFF
			val s = seq
			write(FwProto.pack(cmd, s, payload))
			val end = System.currentTimeMillis() + timeoutMs
			synchronized(lock) {
				while (true) {
					val k = frames.indexOfFirst { it.cmd == cmd && it.seq == s }
					if (k >= 0) {
						val p = frames[k].payload
						frames.subList(0, k + 1).clear()
						return p
					}
					val left = end - System.currentTimeMillis()
					if (left <= 0) break
					lock.wait(left)
				}
			}
		}
		throw FwException("no response to command 0x%02X".format(cmd))
	}

	private fun check(p: ByteArray, what: String) {
		if (p.isEmpty() || p[0].toInt() != 0) {
			val code = if (p.isEmpty()) -1 else p[0].toInt() and 0xFF
			throw FwException("$what: ${FwProto.ERRORS[code] ?: "error $code"}")
		}
	}

	private fun ver(p: ByteArray, off: Int) =
		"${p[off].toInt() and 0xFF}.${p[off + 1].toInt() and 0xFF}.${p[off + 2].toInt() and 0xFF}"

	fun info(timeoutMs: Long = 1000, retries: Int = 3): BoardInfo {
		val p = request(FwProto.CMD_INFO, timeoutMs = timeoutMs, retries = retries)
		check(p, "INFO")
		if (p.size < 33) throw FwException("INFO: short answer (${p.size} B)")
		val board = p.copyOfRange(11, 27)
		val n = board.indexOf(0).let { if (it < 0) board.size else it }
		return BoardInfo(
			proto = p[1].toInt() and 0xFF,
			role = FwProto.ROLES[p[2].toInt() and 0xFF] ?: (p[2].toInt() and 0xFF).toString(),
			version = ver(p, 3),
			build = FwProto.u32(p, 7),
			board = String(board, 0, n),
			stagingSize = FwProto.u32(p, 27),
			maxChunk = FwProto.u16(p, 31),
		)
	}

	/** Wait until the board answers INFO (after reset / boot <-> app switch). */
	fun waitReady(timeoutMs: Long = 10_000): BoardInfo {
		val end = System.currentTimeMillis() + timeoutMs
		while (System.currentTimeMillis() < end) {
			try {
				return info(timeoutMs = 300, retries = 1)
			} catch (_: FwException) {
			}
		}
		throw FwException("no answer from board after ${timeoutMs / 1000} s")
	}

	/** BEGIN + DATA + END. Returns the version of the image the board verified. */
	fun upload(image: ByteArray, progress: (Int, Int) -> Unit = { _, _ -> }): String {
		val info = info()
		val chunk = minOf(FwProto.MAX_CHUNK, if (info.maxChunk > 0) info.maxChunk else FwProto.MAX_CHUNK)
		if (image.size > info.stagingSize) {
			throw FwException("image ${image.size} B larger than staging ${info.stagingSize} B")
		}
		check(request(FwProto.CMD_BEGIN, FwProto.le32(image.size.toLong())), "BEGIN")
		var off = 0
		while (off < image.size) {
			val part = image.copyOfRange(off, minOf(off + chunk, image.size))
			val p = request(FwProto.CMD_DATA, FwProto.le32(off.toLong()) + part, timeoutMs = 2000)
			if (p.size >= 5 && p[0].toInt() == FwProto.ERR_BAD_OFFSET) {	// resync to the board
				off = FwProto.u32(p, 1).toInt()
				continue
			}
			check(p, "DATA @$off")
			off += part.size
			progress(off, image.size)
		}
		val p = request(FwProto.CMD_END, timeoutMs = 5000)
		check(p, "END")
		return ver(p, 1)
	}

	fun install() = check(request(FwProto.CMD_INSTALL), "INSTALL")

	fun simple(cmd: Int, name: String) = check(request(cmd), name)

	/** End the trial of the running image. Returns true if it was on trial. */
	fun confirm(): Boolean {
		val p = request(FwProto.CMD_CONFIRM)
		check(p, "CONFIRM")
		return p.size > 1 && p[1].toInt() == 1
	}

	/**
	 * Upload, install and wait for the new app, as `ak_fw.py flash`.
	 * Returns what runs afterwards.
	 */
	fun flash(image: ByteArray, progress: (Int, Int) -> Unit = { _, _ -> }, status: (String) -> Unit = {}): BoardInfo {
		val ver = upload(image, progress)
		status("uploaded v$ver, verified on board; installing...")
		install()
		Thread.sleep(300)
		return waitApp(30_000, ver)
	}

	/** App -> bootloader. Returns the role that answers afterwards. */
	fun loader(): BoardInfo {
		simple(FwProto.CMD_LOADER, "LOADER")
		Thread.sleep(300)
		return waitReady(10_000)
	}

	fun reset() = simple(FwProto.CMD_RESET, "RESET")

	fun run() = simple(FwProto.CMD_RUN, "RUN")

	/** Put the image before the last update back. Returns the app that runs afterwards. */
	fun rollback(): BoardInfo {
		simple(FwProto.CMD_ROLLBACK, "ROLLBACK")
		Thread.sleep(300)
		return waitApp(30_000)
	}

	/** Wait until the app (not the bootloader) answers, [version] if given. */
	fun waitApp(timeoutMs: Long, version: String? = null): BoardInfo {
		val deadline = System.currentTimeMillis() + timeoutMs
		var info = waitReady(timeoutMs)
		while (info.role != "app" || (version != null && info.version != version)) {
			if (System.currentTimeMillis() > deadline) {
				throw FwException("still ${info.role} v${info.version} (see the log)")
			}
			Thread.sleep(200)
			info = waitReady(timeoutMs)
		}
		return info
	}
}
