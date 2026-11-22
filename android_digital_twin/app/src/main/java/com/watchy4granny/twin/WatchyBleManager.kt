package com.watchy4granny.twin

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.location.LocationManager
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.*
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume
import kotlin.math.roundToInt

data class WatchyTwinUiState(
    val connected: Boolean = false,
    val connecting: Boolean = false,
    val isWifiMode: Boolean = false,
    val wifiBaseUrl: String = "http://192.168.4.1",
    val statusBanner: String = "",
    val watchTime: String = "--",
    val batteryText: String = "--",
    val stepsText: String = "--",
    val weatherText: String = "--",
    val latitude: String = "",
    val longitude: String = "",
    val framebuffer: ByteArray = ByteArray(5000) { 0xFF.toByte() },
    val hasValidScreen: Boolean = false,
    val screenProgress: Float = 0f,
    val frameSeq: String = "",
    val logs: List<String> = listOf("Ready. Connect via BLE (SW4 on Watchy) or WiFi (Watchy Connect)."),
    val weatherRawString: String = "",
    val weatherPayloadString: String = "",
    val weatherParsedTemp: String = "",
    val weatherParsedFeels: String = "",
    val weatherParsedCondition: String = "",
    val weatherParsedHumidity: String = "",
    val weatherParsedWind: String = "",
    val weatherParsedPressure: String = "",
    val weatherParsedElevation: String = "",
    val weatherLastReceivedTime: String = "",
    val weatherLastPushedTime: String = ""
)

fun parseWeatherCodeDescription(code: Int): String = when (code) {
    0 -> "Clear sky"
    1 -> "Mainly clear"
    2 -> "Partly cloudy"
    3 -> "Overcast"
    45, 48 -> "Fog"
    51 -> "Light drizzle"
    53 -> "Moderate drizzle"
    55 -> "Dense drizzle"
    56, 57 -> "Freezing drizzle"
    61 -> "Slight rain"
    63 -> "Moderate rain"
    65 -> "Heavy rain"
    66, 67 -> "Freezing rain"
    71 -> "Slight snowfall"
    73 -> "Moderate snowfall"
    75 -> "Heavy snowfall"
    77 -> "Snow grains"
    80, 81, 82 -> "Rain showers"
    85, 86 -> "Snow showers"
    95 -> "Thunderstorm"
    96, 99 -> "Thunderstorm with hail"
    else -> "Code $code"
}

fun formatCurrentJson(
    time: String,
    interval: Int,
    temp: Any,
    feels: Any,
    weatherCodeName: String,
    hum: Any,
    wind: Any,
    pres: Any,
    elevation: Any
): String {
    return buildString {
        append("{\n")
        if (time.isNotEmpty()) append("  \"time\": \"$time\",\n")
        append("  \"interval\": $interval,\n")
        append("  \"temperature_2m\": $temp,\n")
        append("  \"apparent_temperature\": $feels,\n")
        append("  \"weather_code\": \"$weatherCodeName\",\n")
        append("  \"relative_humidity_2m\": $hum,\n")
        append("  \"wind_speed_10m\": $wind,\n")
        append("  \"surface_pressure\": $pres,\n")
        append("  \"elevation\": $elevation\n")
        append("}")
    }
}

@SuppressLint("MissingPermission")
class WatchyBleManager(private val context: Context) {

    companion object {
        val SERVICE_UUID: UUID = UUID.fromString("19b10000-e8f2-537e-4f6c-d104768a1214")
        val CHAR_TIME_UUID: UUID = UUID.fromString("19b10001-e8f2-537e-4f6c-d104768a1214")
        val CHAR_LOC_UUID: UUID = UUID.fromString("19b10002-e8f2-537e-4f6c-d104768a1214")
        val CHAR_WEAT_UUID: UUID = UUID.fromString("19b10003-e8f2-537e-4f6c-d104768a1214")
        val CHAR_STATUS_UUID: UUID = UUID.fromString("19b10004-e8f2-537e-4f6c-d104768a1214")
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val bleMutex = Mutex()
    private val mainHandler = Handler(Looper.getMainLooper())

    private val _state = MutableStateFlow(WatchyTwinUiState())
    val state: StateFlow<WatchyTwinUiState> = _state

    private var bluetoothGatt: BluetoothGatt? = null
    private var charTime: BluetoothGattCharacteristic? = null
    private var charLoc: BluetoothGattCharacteristic? = null
    private var charWeat: BluetoothGattCharacteristic? = null
    private var charStatus: BluetoothGattCharacteristic? = null

    private var pendingReadCont: Continuation<ByteArray?>? = null
    private var pendingWriteCont: Continuation<Boolean>? = null
    private var pollJob: Job? = null
    private var scanning = false
    private var serviceRefreshRetries = 0
    private var watchMode = "0"
    private var lastWeatherReqSeq = "0"
    private var autoReconnectEnabled = true

    init {
        startAutoReconnectLoop()
    }

    private fun startAutoReconnectLoop() {
        scope.launch {
            while (isActive) {
                delay(5000L)
                if (!_state.value.connected && !_state.value.connecting && !scanning && autoReconnectEnabled) {
                    val btManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
                    val adapter = btManager?.adapter
                    if (adapter != null && adapter.isEnabled) {
                        val activeConnected = try {
                            btManager.getConnectedDevices(BluetoothProfile.GATT)
                                .firstOrNull { it.name == "Watchy4Granny" }
                        } catch (_: Exception) {
                            null
                        }
                        if (activeConnected != null) {
                            log("Watchy4Granny detected via BLE — auto-connecting...")
                            connect()
                        } else {
                            // Brief background scan in case Watchy just turned on BLE (e.g. SW3 Weather)
                            val scanner = try { adapter.bluetoothLeScanner } catch (_: Exception) { null }
                            if (scanner != null) {
                                scanning = true
                                val settings = ScanSettings.Builder()
                                    .setScanMode(ScanSettings.SCAN_MODE_BALANCED)
                                    .build()
                                try {
                                    scanner.startScan(null, settings, scanCallback)
                                    mainHandler.postDelayed({
                                        if (scanning) {
                                            scanning = false
                                            try { scanner.stopScan(scanCallback) } catch (_: Exception) {}
                                        }
                                    }, 2200L)
                                } catch (_: Exception) {
                                    scanning = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    fun log(msg: String) {
        val ts = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())
        val line = "[$ts] $msg"
        val current = _state.value.logs
        _state.value = _state.value.copy(logs = (listOf(line) + current).take(40))
    }

    /**
     * Forces Android's internal Bluetooth stack to clear its cached GATT service table
     * (critical when Watchy4Granny was previously paired as a BLE HID Media Remote before
     * the Digital Twin GATT service 19b10000-... was added).
     */
    private fun refreshGattCache(gatt: BluetoothGatt): Boolean {
        return try {
            val refreshMethod = gatt.javaClass.getMethod("refresh")
            val ok = (refreshMethod.invoke(gatt) as? Boolean) ?: false
            log("Refreshed Android GATT service cache: $ok")
            ok
        } catch (e: Exception) {
            log("GATT cache refresh unavailable: ${e.message}")
            false
        }
    }

    fun connect() {
        val btManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
        val adapter = btManager?.adapter
        if (adapter == null || !adapter.isEnabled) {
            log("Bluetooth is disabled. Please enable Bluetooth.")
            _state.value = _state.value.copy(statusBanner = "Bluetooth is turned off on phone.")
            return
        }

        _state.value = _state.value.copy(connecting = true, statusBanner = "Connecting to Watchy4Granny...")
        serviceRefreshRetries = 0

        // 1. Check already-connected GATT/HID devices first
        val activeConnected = try {
            btManager.getConnectedDevices(BluetoothProfile.GATT)
                .firstOrNull { it.name == "Watchy4Granny" }
        } catch (_: Exception) {
            null
        }
        if (activeConnected != null) {
            log("Watchy4Granny is actively connected to phone (${activeConnected.address}), opening GATT...")
            connectToDevice(activeConnected)
            return
        }

        // 2. Also scan via BLE Scanner AND fall back to bonded device if advertisement isn't seen
        // (When Watchy is already connected to the phone's OS HID profile, it may not be advertising,
        // so we try the bonded device right away if found, or scan otherwise.)
        val bonded = adapter.bondedDevices?.firstOrNull { it.name == "Watchy4Granny" }
        if (bonded != null) {
            log("Found paired Watchy4Granny (${bonded.address}), connecting GATT...")
            connectToDevice(bonded)
            return
        }

        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            log("BLE Scanner unavailable.")
            _state.value = _state.value.copy(connecting = false, statusBanner = "BLE Scanner unavailable.")
            return
        }

        log("Scanning for Watchy4Granny BLE advertisements...")
        scanning = true
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(null, settings, scanCallback)

        mainHandler.postDelayed({
            if (scanning) {
                scanning = false
                scanner.stopScan(scanCallback)
                if (!_state.value.connected) {
                    _state.value = _state.value.copy(
                        connecting = false,
                        statusBanner = "Scan timed out. Press SW4 on Watchy (BLE Remote) and try again."
                    )
                    log("Scan timed out. Make sure Watchy is on BLE Remote screen (press SW4).")
                }
            }
        }, 8000L)
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val dev = result.device ?: return
            val name = dev.name ?: result.scanRecord?.deviceName ?: ""
            val hasService = result.scanRecord?.serviceUuids?.any { it.uuid == SERVICE_UUID } == true
            if (name == "Watchy4Granny" || hasService) {
                if (scanning) {
                    scanning = false
                    val btManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
                    btManager?.adapter?.bluetoothLeScanner?.stopScan(this)
                    log("Discovered Watchy4Granny (${dev.address}), connecting...")
                    connectToDevice(dev)
                }
            }
        }
    }

    private fun connectToDevice(device: BluetoothDevice) {
        try {
            bluetoothGatt?.close()
        } catch (_: Exception) {}
        bluetoothGatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                log("GATT Connected! Refreshing cache & requesting MTU 517...")
                refreshGattCache(gatt)
                val mtuRequested = gatt.requestMtu(517)
                if (!mtuRequested) {
                    log("MTU request skipped by OS, discovering services directly...")
                    gatt.discoverServices()
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                log("Disconnected from Watchy (status=$status).")
                pollJob?.cancel()
                pendingReadCont?.resume(null)
                pendingReadCont = null
                pendingWriteCont?.resume(false)
                pendingWriteCont = null
                if (serviceRefreshRetries == 2 && _state.value.connecting) {
                    return
                }
                _state.value = _state.value.copy(
                    connected = false,
                    connecting = false,
                    screenProgress = 0f,
                    statusBanner = "Disconnected. Press SW4 on Watchy & tap Connect."
                )
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            log("MTU negotiated: $mtu bytes. Discovering services...")
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val discoveredUuids = gatt.services.map { it.uuid.toString().substring(0, 8) }
            log("Services found (${gatt.services.size}): ${discoveredUuids.joinToString(",")}")

            val srv = gatt.getService(SERVICE_UUID)
            if (srv == null) {
                if (serviceRefreshRetries == 0) {
                    serviceRefreshRetries = 1
                    log("Digital Twin service not in cached list (attempt 1) — clearing GATT cache & retrying...")
                    refreshGattCache(gatt)
                    mainHandler.postDelayed({
                        gatt.discoverServices()
                    }, 500L)
                    return
                } else if (serviceRefreshRetries == 1) {
                    serviceRefreshRetries = 2
                    val dev = gatt.device
                    log("Retrying GATT service discovery with cache refresh for ${dev.address}...")
                    _state.value = _state.value.copy(
                        statusBanner = "Refreshing cached Bluetooth services with Watchy..."
                    )
                    refreshGattCache(gatt)
                    mainHandler.postDelayed({
                        gatt.discoverServices()
                    }, 800L)
                    return
                }
                val msg = "Phone cached old Bluetooth pairing. Open Android Settings → Bluetooth → Watchy4Granny → Forget/Unpair, then tap Connect."
                log("ERROR: $msg")
                _state.value = _state.value.copy(
                    connecting = false,
                    connected = false,
                    statusBanner = msg
                )
                return
            }

            charTime = srv.getCharacteristic(CHAR_TIME_UUID)
            charLoc = srv.getCharacteristic(CHAR_LOC_UUID)
            charWeat = srv.getCharacteristic(CHAR_WEAT_UUID)
            charStatus = srv.getCharacteristic(CHAR_STATUS_UUID)

            _state.value = _state.value.copy(
                connected = true,
                connecting = false,
                statusBanner = "Connected to Watchy4Granny!"
            )
            log("Digital Twin ready! Switching to Live Watchface & reading 200x200 screen...")

            scope.launch {
                bleMutex.withLock {
                    readTelemetryUnlocked()
                    syncPhoneTimeUnlocked()
                    // If Watchy is in BLE remote mode (mode == 1), switch it to WATCH mode
                    if (watchMode == "1") {
                        writeCharString(charTime, "MODE:WATCH")
                        delay(280L)
                    }
                    readScreenUnlocked()
                }
                startPolling()
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            @Suppress("DEPRECATION")
            val bytes = if (status == BluetoothGatt.GATT_SUCCESS) characteristic.value else null
            val cont = pendingReadCont
            pendingReadCont = null
            cont?.resume(bytes)
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int
        ) {
            val bytes = if (status == BluetoothGatt.GATT_SUCCESS) value else null
            val cont = pendingReadCont
            pendingReadCont = null
            cont?.resume(bytes)
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            val cont = pendingWriteCont
            pendingWriteCont = null
            cont?.resume(status == BluetoothGatt.GATT_SUCCESS)
        }
    }

    private suspend fun readCharBytes(c: BluetoothGattCharacteristic?): ByteArray? {
        val gatt = bluetoothGatt ?: return null
        val ch = c ?: return null
        return withTimeoutOrNull(4000L) {
            suspendCancellableCoroutine { cont ->
                pendingReadCont = cont
                if (!gatt.readCharacteristic(ch)) {
                    pendingReadCont = null
                    cont.resume(null)
                }
            }
        }
    }

    @Suppress("DEPRECATION")
    private suspend fun writeCharString(c: BluetoothGattCharacteristic?, text: String): Boolean {
        val gatt = bluetoothGatt ?: return false
        val ch = c ?: return false
        val data = text.toByteArray(StandardCharsets.UTF_8)
        return withTimeoutOrNull(4000L) {
            suspendCancellableCoroutine { cont ->
                pendingWriteCont = cont
                ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                ch.value = data
                if (!gatt.writeCharacteristic(ch)) {
                    pendingWriteCont = null
                    cont.resume(false)
                }
            }
        } ?: false
    }

    private suspend fun readTelemetryUnlocked() {
        writeCharString(charTime, "SCR:END")
        readCharBytes(charTime)?.let { bytes ->
            val tStr = String(bytes, StandardCharsets.UTF_8)
            val dtPart = tStr.split(",").firstOrNull() ?: tStr
            _state.value = _state.value.copy(watchTime = dtPart)
        }

        readCharBytes(charStatus)?.let { bytes ->
            val sStr = String(bytes, StandardCharsets.UTF_8)
            val parts = sStr.split(",")
            if (parts.size >= 4) {
                val pct = parts[0]
                val volt = parts[1]
                val steps = parts[2]
                val chg = parts[3] == "1"
                val seq = if (parts.size >= 5) parts[4] else _state.value.frameSeq
                val mode = if (parts.size >= 6) parts[5] else "0"
                val reqSeq = if (parts.size >= 7) parts[6] else "0"
                watchMode = mode
                _state.value = _state.value.copy(
                    batteryText = "$pct% (${volt}V)" + if (chg) " ⚡" else "",
                    stepsText = steps,
                    frameSeq = seq
                )
                if (reqSeq != "0" && reqSeq != lastWeatherReqSeq) {
                    lastWeatherReqSeq = reqSeq
                    autoFetchAndPushWeatherUnlocked()
                }
            }
        }

        readCharBytes(charLoc)?.let { bytes ->
            val lStr = String(bytes, StandardCharsets.UTF_8)
            val parts = lStr.split(",")
            if (parts.size >= 2) {
                val lt = parts[0].trim().toDoubleOrNull()
                val ln = parts[1].trim().toDoubleOrNull()
                if (lt != null && ln != null && lt in -90.0..90.0 && ln in -180.0..180.0 && (lt != 0.0 || ln != 0.0) && lt != 720.0) {
                    if (_state.value.latitude.isEmpty() || _state.value.latitude == "720.000000" || _state.value.latitude == "0.000000") {
                        _state.value = _state.value.copy(
                            latitude = parts[0].trim(),
                            longitude = parts[1].trim()
                        )
                    }
                }
            }
        }

        readCharBytes(charWeat)?.let { bytes ->
            val wStr = String(bytes, StandardCharsets.UTF_8).trim()
            val parts = wStr.split(",")
            if (parts.size >= 7) {
                val ready = parts[6].trim() == "1"
                val weatherDisplay = if (ready) "${parts[0]}°C, ${parts[4]} m/s" else "No Data"
                val code = parts[2].toIntOrNull() ?: 0
                val pres = parts[5]
                _state.value = _state.value.copy(
                    weatherText = weatherDisplay,
                    weatherPayloadString = if (_state.value.weatherPayloadString.isEmpty() && ready) wStr else _state.value.weatherPayloadString,
                    weatherRawString = if (_state.value.weatherRawString.isEmpty() && ready) {
                        formatCurrentJson(
                            time = "",
                            interval = 900,
                            temp = parts[0],
                            feels = if (parts.size >= 2) parts[1] else 0,
                            weatherCodeName = parseWeatherCodeDescription(code),
                            hum = if (parts.size >= 4) parts[3] else 0,
                            wind = parts[4],
                            pres = pres,
                            elevation = if (parts.size >= 7) parts[6] else 0
                        )
                    } else _state.value.weatherRawString,
                    weatherParsedTemp = if (_state.value.weatherParsedTemp.isEmpty() && ready) "${parts[0]}°C" else _state.value.weatherParsedTemp,
                    weatherParsedFeels = if (_state.value.weatherParsedFeels.isEmpty() && ready && parts.size >= 2) "${parts[1]}°C" else _state.value.weatherParsedFeels,
                    weatherParsedCondition = if (_state.value.weatherParsedCondition.isEmpty() && ready) "${parseWeatherCodeDescription(code)} (Code $code)" else _state.value.weatherParsedCondition,
                    weatherParsedHumidity = if (_state.value.weatherParsedHumidity.isEmpty() && ready && parts.size >= 4) "${parts[3]}%" else _state.value.weatherParsedHumidity,
                    weatherParsedWind = if (_state.value.weatherParsedWind.isEmpty() && ready) "${parts[4]} m/s" else _state.value.weatherParsedWind,
                    weatherParsedPressure = if (_state.value.weatherParsedPressure.isEmpty() && ready) "$pres hPa" else _state.value.weatherParsedPressure
                )
            }
        }
    }

    private suspend fun readScreenUnlocked() {
        val fb = _state.value.framebuffer.copyOf()
        var offset = 0
        _state.value = _state.value.copy(screenProgress = 0.02f)

        while (offset < 5000) {
            val reqLen = minOf(500, 5000 - offset)
            if (!writeCharString(charTime, "SCR:$offset,$reqLen")) break
            val chunk = readCharBytes(charTime) ?: break
            if (chunk.isEmpty()) break
            val copyLen = minOf(chunk.size, 5000 - offset)
            System.arraycopy(chunk, 0, fb, offset, copyLen)
            offset += copyLen
            _state.value = _state.value.copy(
                framebuffer = fb.copyOf(),
                hasValidScreen = true,
                screenProgress = (offset.toFloat() / 5000f).coerceIn(0f, 1f)
            )
        }
        writeCharString(charTime, "SCR:END")
        _state.value = _state.value.copy(screenProgress = 0f)
    }

    private fun startPolling() {
        pollJob?.cancel()
        pollJob = scope.launch {
            while (isActive && _state.value.connected) {
                delay(1500L)
                if (!bleMutex.isLocked) {
                    bleMutex.withLock {
                        val bytes = readCharBytes(charStatus) ?: return@withLock
                        val parts = String(bytes, StandardCharsets.UTF_8).split(",")
                        if (parts.size >= 5) {
                            val pct = parts[0]
                            val volt = parts[1]
                            val steps = parts[2]
                            val chg = parts[3] == "1"
                            val seq = parts[4]
                            val mode = if (parts.size >= 6) parts[5] else "0"
                            val reqSeq = if (parts.size >= 7) parts[6] else "0"
                            watchMode = mode
                            val oldSeq = _state.value.frameSeq
                            _state.value = _state.value.copy(
                                batteryText = "$pct% (${volt}V)" + if (chg) " ⚡" else "",
                                stepsText = steps,
                                frameSeq = seq
                            )
                            if (reqSeq != "0" && reqSeq != lastWeatherReqSeq) {
                                lastWeatherReqSeq = reqSeq
                                autoFetchAndPushWeatherUnlocked()
                            }
                            if (oldSeq.isNotEmpty() && seq != oldSeq) {
                                readScreenUnlocked()
                            }
                        }
                    }
                }
            }
        }
    }

    fun setWifiBaseUrl(url: String) {
        _state.value = _state.value.copy(wifiBaseUrl = url.trim())
    }

    fun toggleWifiConnection() {
        if (_state.value.isWifiMode) {
            disconnectWifi()
            return
        }
        connectWifi()
    }

    private fun connectWifi() {
        val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
        _state.value = _state.value.copy(connecting = true, statusBanner = "Connecting to Watchy WiFi ($baseUrl)...")
        log("Connecting to Watchy WiFi AP at $baseUrl...")

        scope.launch(Dispatchers.IO) {
            try {
                val url = URL("$baseUrl/api/status")
                val conn = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = 3500
                    readTimeout = 3500
                }
                if (conn.responseCode != 200) {
                    throw Exception("HTTP ${conn.responseCode}")
                }
                val body = conn.inputStream.bufferedReader().use { it.readText() }
                val json = JSONObject(body)

                _state.value = _state.value.copy(
                    connected = true,
                    connecting = false,
                    isWifiMode = true,
                    statusBanner = "Connected to Watchy via WiFi AP!"
                )
                log("WiFi AP Connected! Battery: ${json.optInt("battery")}%, Steps: ${json.optLong("steps")}")

                updateTelemetryFromWifiJson(json)
                readScreenWifi()
                startWifiPolling()
            } catch (e: Exception) {
                _state.value = _state.value.copy(
                    connected = false,
                    connecting = false,
                    isWifiMode = false,
                    statusBanner = "WiFi connect failed. Connect to 'Watchy Connect' WiFi AP first."
                )
                log("WiFi connect error: ${e.message}. Connect phone to 'Watchy Connect' WiFi.")
            }
        }
    }

    fun disconnectWifi() {
        pollJob?.cancel()
        _state.value = _state.value.copy(
            connected = false,
            connecting = false,
            isWifiMode = false,
            statusBanner = "Disconnected from Watchy WiFi."
        )
        log("Disconnected from Watchy WiFi.")
    }

    private fun updateTelemetryFromWifiJson(json: JSONObject) {
        val pct = json.optInt("battery")
        val volt = json.optDouble("voltage")
        val chg = json.optBoolean("charging")
        val steps = json.optLong("steps")
        val timeStr = json.optString("time", "--").split(",").firstOrNull() ?: "--"
        val seq = json.optLong("frameSeq").toString()
        val reqSeq = json.optLong("weatherReqSeq").toString()

        _state.value = _state.value.copy(
            batteryText = "$pct% (${String.format(Locale.US, "%.2f", volt)}V)" + if (chg) " ⚡" else "",
            stepsText = steps.toString(),
            watchTime = timeStr,
            frameSeq = seq
        )

        if (reqSeq != "0" && reqSeq != lastWeatherReqSeq) {
            lastWeatherReqSeq = reqSeq
            log("Watchy requested weather via WiFi (seq: $reqSeq)! Auto-fetching...")
            autoFetchAndPushWeatherUnlocked()
        }
    }

    private fun startWifiPolling() {
        pollJob?.cancel()
        pollJob = scope.launch(Dispatchers.IO) {
            val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
            while (isActive && _state.value.connected && _state.value.isWifiMode) {
                delay(1200L)
                try {
                    val url = URL("$baseUrl/api/status")
                    val conn = (url.openConnection() as HttpURLConnection).apply {
                        connectTimeout = 2000
                        readTimeout = 2000
                    }
                    if (conn.responseCode == 200) {
                        val body = conn.inputStream.bufferedReader().use { it.readText() }
                        val json = JSONObject(body)
                        val oldSeq = _state.value.frameSeq
                        updateTelemetryFromWifiJson(json)
                        if (oldSeq.isNotEmpty() && _state.value.frameSeq != oldSeq) {
                            readScreenWifi()
                        }
                    }
                } catch (_: Exception) {}
            }
        }
    }

    private suspend fun readScreenWifi() {
        withContext(Dispatchers.IO) {
            try {
                val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
                _state.value = _state.value.copy(screenProgress = 0.3f)
                val url = URL("$baseUrl/api/screen")
                val conn = (url.openConnection() as HttpURLConnection).apply {
                    connectTimeout = 4000
                    readTimeout = 4000
                }
                if (conn.responseCode == 200) {
                    val bytes = conn.inputStream.use { it.readBytes() }
                    if (bytes.size == 5000) {
                        _state.value = _state.value.copy(
                            framebuffer = bytes,
                            hasValidScreen = true,
                            screenProgress = 1f
                        )
                    }
                }
            } catch (e: Exception) {
                log("WiFi screen read error: ${e.message}")
            } finally {
                delay(200L)
                _state.value = _state.value.copy(screenProgress = 0f)
            }
        }
    }

    fun manualRefreshScreen() {
        if (_state.value.isWifiMode) {
            scope.launch {
                log("Capturing 200x200 screen over WiFi AP...")
                readScreenWifi()
                log("Screen capture complete.")
            }
            return
        }
        scope.launch {
            bleMutex.withLock {
                log("Capturing 200x200 e-paper screen from Watchy...")
                readTelemetryUnlocked()
                readScreenUnlocked()
                log("Screen capture complete.")
            }
        }
    }

    fun setWatchyMode(mode: String) {
        if (_state.value.isWifiMode) {
            if (mode == "OFF") {
                scope.launch(Dispatchers.IO) {
                    try {
                        val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
                        URL("$baseUrl/exit").readText()
                    } catch (_: Exception) {}
                    disconnectWifi()
                }
            }
            return
        }
        scope.launch {
            bleMutex.withLock {
                writeCharString(charTime, "MODE:$mode")
                if (mode == "OFF") {
                    log("Stopped BLE on Watchy.")
                    pollJob?.cancel()
                    bluetoothGatt?.disconnect()
                    _state.value = _state.value.copy(connected = false, statusBanner = "BLE stopped on Watchy.")
                    return@withLock
                }
                log(if (mode == "WATCH") "Switched to Live Watchface & Menu Mode." else "Switched to BLE Media Remote Mode.")
                delay(280L)
                readTelemetryUnlocked()
                readScreenUnlocked()
            }
        }
    }

    fun pressButton(btn: Int) {
        if (_state.value.isWifiMode) {
            scope.launch(Dispatchers.IO) {
                try {
                    val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
                    val conn = (URL("$baseUrl/api/button?btn=$btn").openConnection() as HttpURLConnection).apply {
                        requestMethod = "POST"
                        connectTimeout = 3000
                    }
                    conn.responseCode
                    log("Pressed SW$btn on Watchy (WiFi)")
                    delay(300L)
                    readScreenWifi()
                } catch (e: Exception) {
                    log("Button SW$btn WiFi error: ${e.message}")
                }
            }
            return
        }
        scope.launch {
            bleMutex.withLock {
                writeCharString(charTime, "BTN:$btn")
                log("Pressed SW$btn on Watchy")
                delay(340L)
                readTelemetryUnlocked()
                readScreenUnlocked()
            }
        }
    }

    private suspend fun syncPhoneTimeUnlocked() {
        val ch = charTime ?: return
        val epochSec = System.currentTimeMillis() / 1000L
        val tzId = TimeZone.getDefault().id ?: ""
        val payload = if (tzId.isNotEmpty()) "$epochSec,$tzId" else "$epochSec"
        writeCharString(ch, payload)
        log("Synced phone time & timezone via BLE: $payload")
    }

    fun syncPhoneTime() {
        val epochSec = System.currentTimeMillis() / 1000L
        val tzId = TimeZone.getDefault().id ?: ""
        val payload = if (tzId.isNotEmpty()) "$epochSec,$tzId" else "$epochSec"

        if (_state.value.isWifiMode) {
            scope.launch(Dispatchers.IO) {
                try {
                    val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
                    val conn = (URL("$baseUrl/api/time").openConnection() as HttpURLConnection).apply {
                        requestMethod = "POST"
                        doOutput = true
                        connectTimeout = 3000
                        outputStream.use { it.write(payload.toByteArray(StandardCharsets.UTF_8)) }
                    }
                    conn.responseCode
                    log("Synced phone time over WiFi: $payload")
                    delay(200L)
                    readScreenWifi()
                } catch (e: Exception) {
                    log("WiFi time sync error: ${e.message}")
                }
            }
            return
        }

        scope.launch {
            bleMutex.withLock {
                syncPhoneTimeUnlocked()
                delay(280L)
                readTelemetryUnlocked()
                readScreenUnlocked()
            }
        }
    }

    fun updateCoordinatesInputs(lat: String, lon: String) {
        _state.value = _state.value.copy(latitude = lat, longitude = lon)
    }

    fun saveLocationToWatchy() {
        val lat = _state.value.latitude.toDoubleOrNull()
        val lon = _state.value.longitude.toDoubleOrNull()
        if (lat == null || lon == null) {
            log("Enter valid Latitude and Longitude first.")
            return
        }
        scope.launch {
            bleMutex.withLock {
                val payload = String.format(Locale.US, "%.6f,%.6f", lat, lon)
                writeCharString(charLoc, payload)
                log("Saved Location to Watchy NVS: $payload")
                delay(220L)
                readTelemetryUnlocked()
                readScreenUnlocked()
            }
        }
    }

    fun pushWeatherPayload(payload: String, locPayload: String, rawJson: String = "") {
        val pushTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
        val parts = payload.split(",")
        if (parts.size >= 5) {
            val code = parts[2].toIntOrNull() ?: 0
            val elev = if (parts.size >= 7) parts[6] else "--"
            val pres = if (parts.size >= 6) parts[5] else "--"
            val raw = if (rawJson.isNotEmpty()) {
                rawJson
            } else if (_state.value.weatherRawString.isNotEmpty()) {
                _state.value.weatherRawString
            } else {
                formatCurrentJson(
                    time = "",
                    interval = 900,
                    temp = parts[0],
                    feels = if (parts.size >= 2) parts[1] else 0,
                    weatherCodeName = parseWeatherCodeDescription(code),
                    hum = if (parts.size >= 4) parts[3] else 0,
                    wind = parts[4],
                    pres = pres,
                    elevation = elev
                )
            }
            _state.value = _state.value.copy(
                weatherPayloadString = payload,
                weatherRawString = raw,
                weatherParsedTemp = "${parts[0]}°C",
                weatherParsedFeels = if (parts.size >= 2) "${parts[1]}°C" else _state.value.weatherParsedFeels,
                weatherParsedCondition = "${parseWeatherCodeDescription(code)} (Code $code)",
                weatherParsedHumidity = if (parts.size >= 4) "${parts[3]}%" else _state.value.weatherParsedHumidity,
                weatherParsedWind = "${parts[4]} m/s",
                weatherParsedPressure = "$pres hPa",
                weatherParsedElevation = "$elev m",
                weatherLastPushedTime = pushTime,
                weatherText = "${parts[0]}°C, ${parts[4]} m/s"
            )
        }
        if (_state.value.isWifiMode) {
            scope.launch(Dispatchers.IO) {
                try {
                    val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
                    val conn = (URL("$baseUrl/api/weather").openConnection() as HttpURLConnection).apply {
                        requestMethod = "POST"
                        doOutput = true
                        connectTimeout = 3000
                        outputStream.use { it.write(payload.toByteArray(StandardCharsets.UTF_8)) }
                    }
                    conn.responseCode
                    log("Pushed Weather over WiFi: $payload")
                    delay(200L)
                    readScreenWifi()
                } catch (e: Exception) {
                    log("WiFi weather push error: ${e.message}")
                }
            }
            return
        }

        scope.launch {
            bleMutex.withLock {
                writeCharString(charLoc, locPayload)
                writeCharString(charWeat, payload)
                log("Pushed Open-Meteo Weather to Watchy: $payload")
                delay(280L)
                readTelemetryUnlocked()
                readScreenUnlocked()
            }
        }
    }

    fun fetchAndPushWeather(lat: Double, lon: Double, auto: Boolean = false) {
        scope.launch(Dispatchers.IO) {
            try {
                val recvTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
                log("Fetching Open-Meteo weather on phone for $lat, $lon...")
                val urlStr = String.format(
                    Locale.US,
                    "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,surface_pressure,wind_speed_10m&wind_speed_unit=ms",
                    lat, lon
                )
                val conn = (URL(urlStr).openConnection() as HttpURLConnection).apply {
                    connectTimeout = 7000
                    readTimeout = 7000
                }
                val body = conn.inputStream.bufferedReader().use { it.readText() }
                val root = JSONObject(body)
                val elev = root.optDouble("elevation", 0.0).roundToInt()
                val current = root.getJSONObject("current")
                val timeStr = current.optString("time", "")
                val intervalVal = current.optInt("interval", 900)
                val tempVal = current.opt("temperature_2m") ?: 0
                val feelsVal = current.opt("apparent_temperature") ?: 0
                val humVal = current.opt("relative_humidity_2m") ?: 0
                val windVal = current.opt("wind_speed_10m") ?: 0.0
                val presVal = current.opt("surface_pressure") ?: 1013

                val tempC = current.getDouble("temperature_2m").roundToInt()
                val feelsC = current.getDouble("apparent_temperature").roundToInt()
                val code = current.getInt("weather_code")
                val codeDesc = parseWeatherCodeDescription(code)
                val hum = current.getDouble("relative_humidity_2m").roundToInt()
                val windMs = String.format(Locale.US, "%.1f", current.getDouble("wind_speed_10m"))
                val pres = current.getDouble("surface_pressure").roundToInt()

                val rawJson = formatCurrentJson(
                    time = timeStr,
                    interval = intervalVal,
                    temp = tempVal,
                    feels = feelsVal,
                    weatherCodeName = codeDesc,
                    hum = humVal,
                    wind = windVal,
                    pres = presVal,
                    elevation = elev
                )

                val weatherPayload = "$tempC,$feelsC,$code,$hum,$windMs,$pres,$elev"
                val locPayload = String.format(Locale.US, "%.6f,%.6f", lat, lon)

                _state.value = _state.value.copy(
                    weatherRawString = rawJson,
                    weatherPayloadString = weatherPayload,
                    weatherParsedTemp = "$tempC°C",
                    weatherParsedFeels = "$feelsC°C",
                    weatherParsedCondition = "$codeDesc (Code $code)",
                    weatherParsedHumidity = "$hum%",
                    weatherParsedWind = "$windMs m/s",
                    weatherParsedPressure = "$pres hPa",
                    weatherParsedElevation = "$elev m",
                    weatherLastReceivedTime = recvTime,
                    weatherText = "$tempC°C, $windMs m/s"
                )

                if (_state.value.isWifiMode) {
                    val baseUrl = _state.value.wifiBaseUrl.ifEmpty { "http://192.168.4.1" }
                    val pConn = (URL("$baseUrl/api/weather").openConnection() as HttpURLConnection).apply {
                        requestMethod = "POST"
                        doOutput = true
                        connectTimeout = 3000
                        outputStream.use { it.write(weatherPayload.toByteArray(StandardCharsets.UTF_8)) }
                    }
                    pConn.responseCode
                    val pushTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
                    _state.value = _state.value.copy(weatherLastPushedTime = pushTime)
                    log("Pushed Weather over WiFi: $weatherPayload ($codeDesc)")
                    delay(200L)
                    readScreenWifi()
                } else {
                    bleMutex.withLock {
                        writeCharString(charLoc, locPayload)
                        writeCharString(charWeat, weatherPayload)
                        if (auto) syncPhoneTimeUnlocked()
                        val pushTime = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
                        _state.value = _state.value.copy(weatherLastPushedTime = pushTime)
                        log("Pushed weather to Watchy: $weatherPayload ($codeDesc)")
                        delay(280L)
                        readTelemetryUnlocked()
                        readScreenUnlocked()
                    }
                }
            } catch (e: Exception) {
                log("Weather fetch/push failed: ${e.message}")
            }
        }
    }

    private fun autoFetchAndPushWeatherUnlocked() {
        var lat = _state.value.latitude.toDoubleOrNull()
        var lon = _state.value.longitude.toDoubleOrNull()
        if (lat == null || lon == null || lat !in -90.0..90.0 || lon !in -180.0..180.0 || (lat == 0.0 && lon == 0.0) || lat == 720.0) {
            try {
                val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
                val loc = lm?.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                    ?: lm?.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
                    ?: lm?.getLastKnownLocation(LocationManager.PASSIVE_PROVIDER)
                if (loc != null) {
                    lat = loc.latitude
                    lon = loc.longitude
                }
            } catch (_: Exception) {}
        }
        if (lat == null || lon == null || lat !in -90.0..90.0 || lon !in -180.0..180.0 || (lat == 0.0 && lon == 0.0) || lat == 720.0) {
            lat = 59.446008
            lon = 24.737912
        }
        fetchAndPushWeather(lat, lon, auto = true)
    }
}
