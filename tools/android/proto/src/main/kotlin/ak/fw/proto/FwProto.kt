package ak.fw.proto

/**
 * fw_proto framing, the same as tools/ak_fw.py and services/fw/fw_proto.c:
 *
 *   0xA5 | cmd | seq | len (u16 LE) | payload | crc16 (u16 LE, over cmd..payload)
 *
 * Responses carry cmd | 0x80. Frames share the UART with the console log text.
 */
object FwProto {
	const val SOF = 0xA5
	const val RESP = 0x80
	const val MAX_CHUNK = 128
	const val MAX_PAYLOAD = 512

	const val CMD_INFO = 1
	const val CMD_BEGIN = 2
	const val CMD_DATA = 3
	const val CMD_END = 4
	const val CMD_INSTALL = 5
	const val CMD_LOADER = 6
	const val CMD_RESET = 7
	const val CMD_RUN = 8
	const val CMD_CONFIRM = 9
	const val CMD_ROLLBACK = 10

	const val ERR_BAD_OFFSET = 10

	// keep in sync with services/fw/fw_types.h
	val ERRORS = mapOf(
		0 to "ok", 1 to "bad arg", 2 to "bad state", 3 to "too big", 4 to "flash error", 5 to "no image",
		6 to "header crc", 7 to "wrong board", 8 to "wrong load addr", 9 to "image crc", 10 to "bad offset",
		11 to "unknown cmd", 12 to "bad size", 13 to "bad vector table", 14 to "bad signature",
	)
	val ROLES = mapOf(0 to "bootloader", 1 to "app")

	/** CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF): binascii.crc_hqx(data, 0xFFFF). */
	fun crc16(data: ByteArray, off: Int = 0, len: Int = data.size - off): Int {
		var crc = 0xFFFF
		for (i in off until off + len) {
			crc = crc xor ((data[i].toInt() and 0xFF) shl 8)
			for (b in 0 until 8) {
				crc = if (crc and 0x8000 != 0) (crc shl 1) xor 0x1021 else crc shl 1
			}
			crc = crc and 0xFFFF
		}
		return crc
	}

	fun pack(cmd: Int, seq: Int, payload: ByteArray = ByteArray(0)): ByteArray {
		val n = payload.size
		val fr = ByteArray(7 + n)
		fr[0] = SOF.toByte()
		fr[1] = cmd.toByte()
		fr[2] = seq.toByte()
		fr[3] = n.toByte()
		fr[4] = (n shr 8).toByte()
		payload.copyInto(fr, 5)
		val crc = crc16(fr, 1, 4 + n)
		fr[5 + n] = crc.toByte()
		fr[6 + n] = (crc shr 8).toByte()
		return fr
	}

	fun u16(b: ByteArray, off: Int): Int = (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

	fun u32(b: ByteArray, off: Int): Long = u16(b, off).toLong() or (u16(b, off + 2).toLong() shl 16)

	fun le32(v: Long): ByteArray = byteArrayOf(v.toByte(), (v shr 8).toByte(), (v shr 16).toByte(), (v shr 24).toByte())
}

class Frame(val cmd: Int, val seq: Int, val payload: ByteArray)

/**
 * Splits the received byte stream into response frames and log text, as
 * AkFw._parse in tools/ak_fw.py.
 *
 * EVERY 0xA5 in the buffer is tried as a start of frame, not only the first
 * one: a stray 0xA5 in the log text right before a real frame "declares" a
 * length made of bytes of that real frame, and waiting for that many bytes
 * would hold the real frame back for ever.
 */
class FrameParser(private val onText: (ByteArray) -> Unit, private val onFrame: (Frame) -> Unit) {
	private var rx = ByteArray(0)

	fun feed(data: ByteArray, len: Int = data.size) {
		rx += data.copyOf(len)
		parse()
	}

	private fun find(from: Int): Int {
		for (i in from until rx.size) {
			if ((rx[i].toInt() and 0xFF) == FwProto.SOF) return i
		}
		return -1
	}

	private fun text(n: Int) {
		if (n > 0) onText(rx.copyOf(n))
		rx = rx.copyOfRange(n, rx.size)
	}

	private fun parse() {
		while (rx.isNotEmpty()) {
			val i = find(0)
			if (i < 0) {
				text(rx.size)
				return
			}
			text(i)

			var hold = -1		// first SOF whose frame may still be arriving
			var found = -1
			var foundLen = 0
			var pos = 0
			while (pos >= 0) {
				val left = rx.size - pos
				if (left < 7) {
					if (hold < 0) hold = pos
					break
				}
				val ln = FwProto.u16(rx, pos + 3)
				if (ln <= FwProto.MAX_PAYLOAD) {
					if (left < 7 + ln) {
						if (hold < 0) hold = pos
					} else if (FwProto.crc16(rx, pos + 1, 4 + ln) == FwProto.u16(rx, pos + 5 + ln) &&
						(rx[pos + 1].toInt() and FwProto.RESP) != 0) {
						found = pos
						foundLen = 7 + ln
						break
					}
				}
				pos = find(pos + 1)
			}

			if (found >= 0) {
				text(found)		// what stood before it was text
				val ln = foundLen - 7
				onFrame(Frame(rx[1].toInt() and 0x7F, rx[2].toInt() and 0xFF, rx.copyOfRange(5, 5 + ln)))
				rx = rx.copyOfRange(foundLen, rx.size)
				continue
			}
			if (hold < 0) {		// no frame can start in here
				text(rx.size)
			} else if (hold > 0) {
				text(hold)
			}
			if (rx.size > 2048) {	// a false start must not hold text back for ever
				text(rx.size - 1024)
			}
			return
		}
	}
}
