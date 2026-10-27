package ak.mcu.fw

import ak.fw.proto.AkFw
import ak.fw.proto.BoardInfo
import android.app.AlertDialog
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.util.concurrent.Executors

/**
 * One screen: connect to a USB-UART adapter, run the fw_proto commands of
 * tools/ak_fw.py (info, flash, confirm, rollback, loader, run, reset) and use
 * the board's console, which shares the same UART.
 */
class MainActivity : ComponentActivity() {
	private lateinit var usb: UsbManager
	private lateinit var status: TextView
	private lateinit var baud: EditText
	private lateinit var connectBtn: Button
	private lateinit var actions: LinearLayout
	private lateinit var progress: ProgressBar
	private lateinit var log: TextView
	private lateinit var logScroll: ScrollView
	private lateinit var cmd: EditText

	private var link: UsbLink? = null
	private var fw: AkFw? = null
	private val worker = Executors.newSingleThreadExecutor()
	@Volatile private var busy = false

	private val pickImage = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
		if (uri != null) askFlash(uri)
	}

	private val usbReceiver = object : BroadcastReceiver() {
		override fun onReceive(ctx: Context, intent: Intent) {
			when (intent.action) {
				ACTION_USB_PERMISSION ->
					if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) connect()
					else toast(getString(R.string.no_permission))
				UsbManager.ACTION_USB_DEVICE_DETACHED -> if (link != null) disconnect("USB adapter unplugged")
			}
		}
	}

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		setContentView(R.layout.activity_main)
		usb = getSystemService(Context.USB_SERVICE) as UsbManager

		val root = findViewById<View>(R.id.root)
		ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
			val b = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.ime())
			val pad = (8 * resources.displayMetrics.density).toInt()
			v.setPadding(b.left + pad, b.top + pad, b.right + pad, b.bottom + pad)
			WindowInsetsCompat.CONSUMED
		}

		status = findViewById(R.id.status)
		baud = findViewById(R.id.baud)
		connectBtn = findViewById(R.id.connect)
		actions = findViewById(R.id.actions)
		progress = findViewById(R.id.progress)
		log = findViewById(R.id.log)
		logScroll = findViewById(R.id.logScroll)
		cmd = findViewById(R.id.cmd)

		connectBtn.setOnClickListener { if (link == null) connect() else disconnect(null) }
		findViewById<Button>(R.id.info).setOnClickListener {
			job("info") { f -> show(f.info()) }
		}
		findViewById<Button>(R.id.flash).setOnClickListener { pickImage.launch(arrayOf("*/*")) }
		findViewById<Button>(R.id.confirm).setOnClickListener {
			job("confirm") { f -> say(if (f.confirm()) "confirmed" else "nothing to confirm (image not on trial)") }
		}
		findViewById<Button>(R.id.rollback).setOnClickListener {
			AlertDialog.Builder(this)
				.setMessage(R.string.rollback_message)
				.setPositiveButton(R.string.ok) { _, _ ->
					job("rollback") { f -> say("rolling back..."); show(f.rollback()) }
				}
				.setNegativeButton(R.string.cancel, null)
				.show()
		}
		findViewById<Button>(R.id.loader).setOnClickListener {
			job("loader") { f -> say("now in ${f.loader().role}") }
		}
		findViewById<Button>(R.id.run).setOnClickListener {
			job("run") { f -> f.run(); show(f.waitApp(10_000)) }
		}
		findViewById<Button>(R.id.reset).setOnClickListener {
			job("reset") { f -> f.reset() }
		}
		findViewById<Button>(R.id.send).setOnClickListener { sendCmd() }
		findViewById<Button>(R.id.clear).setOnClickListener { log.text = "" }
		cmd.setOnEditorActionListener { _, id, _ ->
			if (id == EditorInfo.IME_ACTION_SEND) {
				sendCmd()
				true
			} else false
		}

		val filter = IntentFilter(ACTION_USB_PERMISSION).apply { addAction(UsbManager.ACTION_USB_DEVICE_DETACHED) }
		ContextCompat.registerReceiver(this, usbReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

		refresh()
		if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) connect()
	}

	override fun onNewIntent(intent: Intent) {
		super.onNewIntent(intent)
		if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED && link == null) connect()
	}

	override fun onDestroy() {
		unregisterReceiver(usbReceiver)
		link?.close()
		worker.shutdownNow()
		super.onDestroy()
	}

	// --------------------------------------------------------------------
	// connection
	// --------------------------------------------------------------------
	private fun connect() {
		val driver = UsbSerialProber.getDefaultProber().findAllDrivers(usb).firstOrNull()
		if (driver == null) {
			toast(getString(R.string.no_device))
			return
		}
		if (!usb.hasPermission(driver.device)) {
			// mutable: the system adds EXTRA_PERMISSION_GRANTED; explicit package for Android 14+
			val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
			val pi = PendingIntent.getBroadcast(this, 0, Intent(ACTION_USB_PERMISSION).setPackage(packageName), flags)
			usb.requestPermission(driver.device, pi)
			return
		}
		val rate = baud.text.toString().toIntOrNull() ?: DEFAULT_BAUD
		try {
			// the client exists before the port starts reading; it writes only once l is set
			var port: UsbLink? = null
			val client = AkFw(write = { port!!.write(it) }, onText = { appendLog(String(it, Charsets.UTF_8)) })
			val l = UsbLink(usb, driver, rate,
				onData = { client.receive(it) },
				onError = { e -> runOnUiThread { if (link != null) disconnect("port closed: ${e.message}") } })
			port = l
			link = l
			fw = client
			status.text = getString(R.string.connected, l.name, rate)
			say("connected: ${l.name} @ $rate")
			job("info") { f -> show(f.waitReady(3000)) }
		} catch (e: Exception) {
			say("error: cannot open port: ${e.message}")
		}
		refresh()
	}

	private fun disconnect(why: String?) {
		link?.close()
		link = null
		fw = null
		status.setText(R.string.not_connected)
		if (why != null) say(why)
		refresh()
	}

	// --------------------------------------------------------------------
	// commands (fw_proto requests block: they run on the worker thread)
	// --------------------------------------------------------------------
	private fun job(name: String, body: (AkFw) -> Unit) {
		val f = fw ?: return
		if (busy) return
		busy = true
		refresh()
		worker.execute {
			try {
				body(f)
			} catch (e: Exception) {
				say("error: $name: ${e.message}")
			} finally {
				busy = false
				runOnUiThread {
					progress.visibility = View.GONE
					refresh()
				}
			}
		}
	}

	private fun askFlash(uri: Uri) {
		val image = try {
			contentResolver.openInputStream(uri)?.use { it.readBytes() }
		} catch (e: Exception) {
			null
		}
		if (image == null || image.isEmpty()) {
			say("error: cannot read $uri")
			return
		}
		val name = displayName(uri) ?: uri.lastPathSegment ?: "image"
		AlertDialog.Builder(this)
			.setTitle(R.string.flash_title)
			.setMessage(getString(R.string.flash_message, name, image.size))
			.setPositiveButton(R.string.ok) { _, _ -> flash(name, image) }
			.setNegativeButton(R.string.cancel, null)
			.show()
	}

	private fun flash(name: String, image: ByteArray) {
		progress.max = image.size
		progress.progress = 0
		progress.visibility = View.VISIBLE
		job("flash") { f ->
			say("flashing $name (${image.size} B)...")
			val t0 = System.currentTimeMillis()
			val info = f.flash(image,
				progress = { done, _ -> runOnUiThread { progress.progress = done } },
				status = { say(it) })
			say("running ${info.role} v${info.version} (%.1f s); confirm once it works"
				.format((System.currentTimeMillis() - t0) / 1000.0))
		}
	}

	private fun sendCmd() {
		val f = fw ?: return
		val line = cmd.text.toString()
		cmd.setText("")
		worker.execute {
			try {
				f.sendText(line + "\r")
			} catch (e: Exception) {
				say("error: send: ${e.message}")
			}
		}
	}

	// --------------------------------------------------------------------
	// UI helpers (callable from any thread)
	// --------------------------------------------------------------------
	private fun refresh() {
		val on = link != null
		connectBtn.setText(if (on) R.string.disconnect else R.string.connect)
		baud.isEnabled = !on
		for (i in 0 until actions.childCount) actions.getChildAt(i).isEnabled = on && !busy
	}

	private fun show(i: BoardInfo) =
		say("board ${i.board}, ${i.role} v${i.version} build ${i.build}, staging ${i.stagingSize} B")

	private fun say(s: String) = appendLog("\n>> $s\n")

	private fun appendLog(s: String) {
		val text = s.replace("\r", "")
		runOnUiThread {
			log.append(text)
			val t = log.text
			if (t.length > MAX_LOG) log.text = t.subSequence(t.length - MAX_LOG / 2, t.length)
			logScroll.post { logScroll.fullScroll(View.FOCUS_DOWN) }
		}
	}

	private fun toast(s: String) = runOnUiThread { Toast.makeText(this, s, Toast.LENGTH_SHORT).show() }

	private fun displayName(uri: Uri): String? =
		contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
			if (c.moveToFirst()) c.getString(0) else null
		}

	companion object {
		private const val ACTION_USB_PERMISSION = "ak.mcu.fw.USB_PERMISSION"
		private const val DEFAULT_BAUD = 115200
		private const val MAX_LOG = 60_000
	}
}
