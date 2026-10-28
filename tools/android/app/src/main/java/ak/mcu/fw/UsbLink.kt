package ak.mcu.fw

import android.hardware.usb.UsbManager
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.util.SerialInputOutputManager

/**
 * One USB serial port (CH340, CP210x, FTDI, PL2303, CDC ACM such as the
 * ST-LINK/V2-1 virtual COM port). Received bytes go to [onData] from the
 * reader thread; [onError] fires once when the port dies (unplugged).
 */
class UsbLink(
	usb: UsbManager,
	val driver: UsbSerialDriver,
	baud: Int,
	private val onData: (ByteArray) -> Unit,
	private val onError: (Exception) -> Unit,
) {
	private val port: UsbSerialPort = driver.ports[0]
	private val io: SerialInputOutputManager

	init {
		val conn = usb.openDevice(driver.device) ?: throw IllegalStateException("cannot open USB device")
		port.open(conn)
		try {
			port.setParameters(baud, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
			// DTR and RTS on, as pyserial opens the port for tools/ak_fw.py
			try {
				port.dtr = true
				port.rts = true
			} catch (_: UnsupportedOperationException) {
			}
		} catch (e: Exception) {
			port.close()
			throw e
		}
		io = SerialInputOutputManager(port, object : SerialInputOutputManager.Listener {
			override fun onNewData(data: ByteArray) = onData(data)
			override fun onRunError(e: Exception) = onError(e)
		})
		io.start()
	}

	val name: String
		get() = driver.javaClass.simpleName.removeSuffix("SerialDriver")

	fun write(data: ByteArray) = port.write(data, WRITE_TIMEOUT_MS)

	fun close() {
		io.listener = null
		io.stop()
		try {
			port.close()
		} catch (_: Exception) {
		}
	}

	companion object {
		private const val WRITE_TIMEOUT_MS = 1000
	}
}
