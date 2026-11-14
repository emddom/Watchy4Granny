package com.watchy4granny.twin

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.*
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume

data class WatchyTwinUiState(
    val connected: Boolean = false,
    val connecting: Boolean = false,
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
    val logs: List<String> = listOf("Ready. Press SW4 on Watchy (BLE Remote) then tap Connect.")
)

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
                    log("OS bond cache locked old GATT table — resetting bond for ${dev.address} & reconnecting...")
                    _state.value = _state.value.copy(
                        statusBanner = "Refreshing cached Bluetooth pairing with Watchy..."
                    )
                    try {
                        gatt.disconnect()
                        gatt.close()
                    } catch (_: Exception) {}
                    bluetoothGatt = null
                    try {
                        dev.javaClass.getMethod("removeBond").invoke(dev)
                    } catch (e: Exception) {
                        log("removeBond error: ${e.message}")
                    }
                    mainHandler.postDelayed({
                        connectToDevice(dev)
                    }, 1400L)
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
                    writeCharString(charTime, "MODE:WATCH")
                    delay(280L)
                    readTelemetryUnlocked()
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
                _state.value = _state.value.copy(
                    batteryText = "$pct% (${volt}V)" + if (chg) " ⚡" else "",
                    stepsText = steps,
                    frameSeq = seq
                )
            }
        }

        readCharBytes(charLoc)?.let { bytes ->
            val lStr = String(bytes, StandardCharsets.UTF_8)
            val parts = lStr.split(",")
            if (parts.size >= 2 && _state.value.latitude.isEmpty()) {
                _state.value = _state.value.copy(
                    latitude = parts[0].trim(),
                    longitude = parts[1].trim()
                )
            }
        }

        readCharBytes(charWeat)?.let { bytes ->
            val wStr = String(bytes, StandardCharsets.UTF_8)
            val parts = wStr.split(",")
            if (parts.size >= 7) {
                val ready = parts[6].trim() == "1"
                val weatherDisplay = if (ready) "${parts[0]}°C, ${parts[4]} m/s" else "No Data"
                _state.value = _state.value.copy(weatherText = weatherDisplay)
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
                            val oldSeq = _state.value.frameSeq
                            _state.value = _state.value.copy(
                                batteryText = "$pct% (${volt}V)" + if (chg) " ⚡" else "",
                                stepsText = steps,
                                frameSeq = seq
                            )
                            if (oldSeq.isNotEmpty() && seq != oldSeq) {
                                readScreenUnlocked()
                            }
                        }
                    }
                }
            }
        }
    }

    fun manualRefreshScreen() {
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

    fun syncPhoneTime() {
        scope.launch {
            bleMutex.withLock {
                val epochSec = System.currentTimeMillis() / 1000L
                val tzId = TimeZone.getDefault().id ?: ""
                val payload = if (tzId.isNotEmpty()) "$epochSec,$tzId" else "$epochSec"
                writeCharString(charTime, payload)
                log("Synced phone time & timezone: $payload")
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

    fun pushWeatherPayload(payload: String, locPayload: String) {
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
}
