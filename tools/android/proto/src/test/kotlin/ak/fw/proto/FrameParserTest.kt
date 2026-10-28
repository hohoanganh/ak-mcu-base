package ak.fw.proto

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/** The cases of tests/test_ak_fw.py, on the Kotlin port of the frame reader. */
class FrameParserTest {
	private fun resp(cmd: Int, seq: Int, payload: ByteArray = ByteArray(0)) =
		FwProto.pack(cmd or FwProto.RESP, seq, payload)

	private fun b(s: String) = s.toByteArray(Charsets.ISO_8859_1)

	/** Feeds the pieces; returns (frames, text). */
	private fun feed(vararg pieces: ByteArray): Pair<List<Frame>, ByteArray> {
		val frames = ArrayList<Frame>()
		var text = ByteArray(0)
		val p = FrameParser({ text += it }, { frames.add(it) })
		for (x in pieces) p.feed(x)
		return frames to text
	}

	private val p1 = byteArrayOf(0) + ByteArray(32) { it.toByte() }
	private val f1 = resp(FwProto.CMD_INFO, 1, p1)
	private val f2 = resp(FwProto.CMD_DATA, 2, byteArrayOf(0))

	@Test
	fun crc16MatchesCcittFalse() {
		assertEquals(0x29B1, FwProto.crc16(b("123456789")))
	}

	@Test
	fun packLayout() {
		val f = FwProto.pack(FwProto.CMD_BEGIN, 0x105, byteArrayOf(1, 2, 3))
		assertEquals(10, f.size)
		assertEquals(0xA5, f[0].toInt() and 0xFF)
		assertEquals(FwProto.CMD_BEGIN, f[1].toInt())
		assertEquals(0x05, f[2].toInt())
		assertEquals(3, FwProto.u16(f, 3))
		assertEquals(FwProto.crc16(f, 1, 7), FwProto.u16(f, 8))
	}

	@Test
	fun oneFrameNothingElse() {
		val (frames, text) = feed(f1)
		assertEquals(1, frames.size)
		assertEquals(FwProto.CMD_INFO, frames[0].cmd)
		assertEquals(1, frames[0].seq)
		assertContentEquals(p1, frames[0].payload)
		assertEquals(0, text.size)
	}

	@Test
	fun framesBetweenLogLines() {
		val (frames, text) = feed(b("[I] APP: start\r\n> "), f1, b("more text\r\n"), f2)
		assertEquals(listOf(1, 2), frames.map { it.seq })
		assertContentEquals(b("[I] APP: start\r\n> more text\r\n"), text)
	}

	@Test
	fun framesArrivingInPieces() {
		val (frames, text) = feed(f1.copyOfRange(0, 3), f1.copyOfRange(3, 9), f1.copyOfRange(9, f1.size),
			f2.copyOfRange(0, 1), f2.copyOfRange(1, f2.size))
		assertEquals(listOf(1, 2), frames.map { it.seq })
		assertEquals(0, text.size)
	}

	@Test
	fun straySofRightBeforeFrame() {
		val (frames, text) = feed(b("log\r\n¥\u0081"), f1)
		assertEquals(listOf(1), frames.map { it.seq })
		assertContentEquals(b("log\r\n¥\u0081"), text)
	}

	@Test
	fun singleStraySofBytes() {
		val (frames, _) = feed(b("¥"), f2, b("¥¥"), f1)
		assertEquals(listOf(2, 1), frames.map { it.seq })
	}

	@Test
	fun falseStartThenTextThenFrame() {
		val start = b("¥\u0081\u0001ÿ\u0001")
		val (frames, text) = feed(start, b("x".repeat(100)), f2)
		assertEquals(listOf(2), frames.map { it.seq })
		assertContentEquals(start + b("x".repeat(100)), text)
	}

	@Test
	fun damagedFrameDropped() {
		val bad = f1.copyOf()
		bad[10] = (bad[10].toInt() xor 1).toByte()
		val (frames, _) = feed(bad, f2)
		assertEquals(listOf(2), frames.map { it.seq })
	}

	@Test
	fun echoedRequestIsNotResponse() {
		val (frames, _) = feed(FwProto.pack(FwProto.CMD_INFO, 7), f2)
		assertEquals(listOf(2), frames.map { it.seq })
	}

	@Test
	fun endlessFalseStartDoesNotHoldText() {
		val pieces = arrayOf(b("¥\u0081\u0001ÿ\u0001")) + Array(8) { b("y".repeat(500)) }
		val (_, text) = feed(*pieces)
		assertTrue(text.size >= 3000, "text after an endless false start: ${text.size} B")
	}
}
