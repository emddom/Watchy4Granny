package com.watchy4granny.twin

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {

    private lateinit var bleManager: WatchyBleManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bleManager = WatchyBleManager(applicationContext)

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    background = Color(0xFF0F172A),
                    surface = Color(0xFF1E293B),
                    primary = Color(0xFF38BDF8),
                    secondary = Color(0xFF334155)
                )
            ) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = Color(0xFF0F172A)
                ) {
                    WatchyDigitalTwinApp(bleManager)
                }
            }
        }
    }
}

@Composable
fun WatchyDigitalTwinApp(bleManager: WatchyBleManager) {
    val state by bleManager.state.collectAsState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var cityQuery by remember { mutableStateOf("") }

    val requiredPermissions = remember {
        buildList {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(Manifest.permission.BLUETOOTH_SCAN)
                add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            add(Manifest.permission.ACCESS_FINE_LOCATION)
            add(Manifest.permission.ACCESS_COARSE_LOCATION)
        }.toTypedArray()
    }

    val permLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        bleManager.connect()
    }

    LaunchedEffect(Unit) {
        val btGranted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
        if (btGranted) {
            bleManager.connect()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "Watchy4Granny Twin",
                    color = Color(0xFFF8FAFC),
                    fontSize = 22.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Dual-Mode: BLE or WiFi AP (192.168.4.1)",
                    color = Color(0xFF94A3B8),
                    fontSize = 12.sp
                )
            }
            StatusPill(connected = state.connected, connecting = state.connecting, isWifi = state.isWifiMode)
        }

        // Card 1: Watchy Look-Alike 200x200 Bezel + Virtual SW1-SW4 Buttons
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E293B)),
            border = BorderStroke(1.dp, Color(0xFF334155)),
            shape = RoundedCornerShape(14.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Connection Buttons Row (BLE or WiFi)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            val allGranted = requiredPermissions.all {
                                ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
                            }
                            if (allGranted) bleManager.connect() else permLauncher.launch(requiredPermissions)
                        },
                        modifier = Modifier.weight(1f),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 8.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (state.connected && !state.isWifiMode) Color(0xFF22C55E) else Color(0xFF38BDF8)
                        )
                    ) {
                        Text(
                            text = if (state.connected && !state.isWifiMode) "ᛒ BLE Connected" else "ᛒ BLE Connect",
                            color = Color(0xFF0F172A),
                            fontWeight = FontWeight.Bold,
                            fontSize = 11.sp,
                            maxLines = 1
                        )
                    }

                    Button(
                        onClick = { bleManager.toggleWifiConnection() },
                        modifier = Modifier.weight(1f),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 8.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (state.connected && state.isWifiMode) Color(0xFF22C55E) else Color(0xFF0284C7)
                        )
                    ) {
                        Text(
                            text = if (state.connected && state.isWifiMode) "📶 WiFi Connected" else "📶 WiFi Connect",
                            color = Color(0xFFF8FAFC),
                            fontWeight = FontWeight.Bold,
                            fontSize = 11.sp,
                            maxLines = 1
                        )
                    }

                    OutlinedButton(
                        onClick = { bleManager.manualRefreshScreen() },
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 8.dp),
                        enabled = state.connected
                    ) {
                        Text("🔄 Capture", color = Color(0xFFF8FAFC), fontSize = 11.sp, maxLines = 1)
                    }
                }

                if (state.statusBanner.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                if (state.connected) Color(0xFF22C55E).copy(alpha = 0.15f)
                                else Color(0xFF0F172A),
                                RoundedCornerShape(8.dp)
                            )
                            .border(
                                1.dp,
                                if (state.connected) Color(0xFF22C55E).copy(alpha = 0.5f) else Color(0xFF334155),
                                RoundedCornerShape(8.dp)
                            )
                            .padding(horizontal = 10.dp, vertical = 6.dp)
                    ) {
                        Text(
                            text = state.statusBanner,
                            color = if (state.connected) Color(0xFF4ADE80) else Color(0xFFCBD5E1),
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Medium
                        )
                    }
                }

                // Mode Switch Row
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    OutlinedButton(
                        onClick = { bleManager.setWatchyMode("WATCH") },
                        enabled = state.connected,
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 8.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Watchface", color = Color(0xFFF8FAFC), fontSize = 11.sp, maxLines = 1)
                    }
                    OutlinedButton(
                        onClick = { bleManager.setWatchyMode("BLE") },
                        enabled = state.connected,
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 8.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Media Remote", color = Color(0xFFF8FAFC), fontSize = 11.sp, maxLines = 1)
                    }
                    Button(
                        onClick = { bleManager.setWatchyMode("OFF") },
                        enabled = state.connected,
                        contentPadding = PaddingValues(horizontal = 10.dp, vertical = 8.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFEF4444))
                    ) {
                        Text(
                            text = if (state.isWifiMode) "Stop WiFi" else "Stop BLE",
                            color = Color.White,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold,
                            maxLines = 1
                        )
                    }
                }

                // Watchy Physical Casing + 4 Corner Buttons (SW1-SW4) + 200x200 E-Paper Display
                WatchyBezelLookAlike(
                    framebuffer = state.framebuffer,
                    hasValidScreen = state.hasValidScreen,
                    watchTime = state.watchTime,
                    batteryText = state.batteryText,
                    stepsText = state.stepsText,
                    weatherText = state.weatherText,
                    progress = state.screenProgress,
                    enabled = state.connected,
                    onButtonPress = { btn -> bleManager.pressButton(btn) }
                )

                // Live Telemetry Grid
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    StatTile("WATCHY TIME", state.watchTime, Modifier.weight(1f))
                    StatTile("BATTERY", state.batteryText, Modifier.weight(1f))
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    StatTile("STEPS", state.stepsText, Modifier.weight(1f))
                    StatTile("WEATHER", state.weatherText, Modifier.weight(1f))
                }
            }
        }

        // Card 2: 1-Tap BLE Time & Timezone Sync
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E293B)),
            border = BorderStroke(1.dp, Color(0xFF334155)),
            shape = RoundedCornerShape(14.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = "1. BLE Time & Timezone Sync (No WiFi Needed)",
                    color = Color(0xFF38BDF8),
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp
                )
                Text(
                    text = "Syncs your phone's exact UTC time and timezone (${TimeZone.getDefault().id}) directly to Watchy over BLE.",
                    color = Color(0xFF94A3B8),
                    fontSize = 12.sp
                )
                Button(
                    onClick = { bleManager.syncPhoneTime() },
                    enabled = state.connected,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF38BDF8))
                ) {
                    Text(
                        text = "Sync Phone Time to Watchy Now",
                        color = Color(0xFF0F172A),
                        fontWeight = FontWeight.Bold
                    )
                }
            }
        }

        // Card 3: Weather Location & Phone-Assisted Open-Meteo Weather Push
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E293B)),
            border = BorderStroke(1.dp, Color(0xFF334155)),
            shape = RoundedCornerShape(14.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = "2. Weather Location & Phone Weather Push",
                    color = Color(0xFF38BDF8),
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    OutlinedTextField(
                        value = cityQuery,
                        onValueChange = { cityQuery = it },
                        label = { Text("City Name (e.g. Helsinki)", fontSize = 12.sp) },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )
                    Button(
                        onClick = {
                            if (cityQuery.isNotBlank()) {
                                scope.launch {
                                    searchCityOpenMeteo(cityQuery.trim(), bleManager)
                                }
                            }
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF334155))
                    ) {
                        Text("Find City", color = Color.White, fontSize = 12.sp)
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    OutlinedTextField(
                        value = state.latitude,
                        onValueChange = { bleManager.updateCoordinatesInputs(it, state.longitude) },
                        label = { Text("Latitude", fontSize = 12.sp) },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )
                    OutlinedTextField(
                        value = state.longitude,
                        onValueChange = { bleManager.updateCoordinatesInputs(state.latitude, it) },
                        label = { Text("Longitude", fontSize = 12.sp) },
                        singleLine = true,
                        modifier = Modifier.weight(1f)
                    )
                    Button(
                        onClick = { readPhoneGps(context, bleManager) },
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF334155))
                    ) {
                        Text("GPS", color = Color.White, fontSize = 12.sp)
                    }
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = { bleManager.saveLocationToWatchy() },
                        enabled = state.connected,
                        modifier = Modifier.weight(1f)
                    ) {
                        Text("Save Loc to Watchy", color = Color(0xFFF8FAFC), fontSize = 12.sp)
                    }
                    Button(
                        onClick = {
                            var lat = state.latitude.toDoubleOrNull()
                            var lon = state.longitude.toDoubleOrNull()
                            if (lat == null || lon == null || lat !in -90.0..90.0 || lon !in -180.0..180.0 || (lat == 0.0 && lon == 0.0) || lat == 720.0) {
                                lat = 59.446008
                                lon = 24.737912
                                bleManager.updateCoordinatesInputs("59.446008", "24.737912")
                            }
                            val finalLat = lat
                            val finalLon = lon
                            scope.launch {
                                fetchAndPushOpenMeteo(finalLat, finalLon, bleManager)
                            }
                        },
                        enabled = state.connected,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF38BDF8))
                    ) {
                        Text(
                            text = "Fetch & Push Weather",
                            color = Color(0xFF0F172A),
                            fontWeight = FontWeight.Bold,
                            fontSize = 12.sp
                        )
                    }
                }

                // Weather Inspection Section: Timestamps, Parsed Data & Strings
                if (state.weatherLastReceivedTime.isNotEmpty() || state.weatherLastPushedTime.isNotEmpty() || state.weatherPayloadString.isNotEmpty()) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(1.dp)
                            .background(Color(0xFF334155))
                    )

                    // Timestamps Box
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(Color(0xFF0F172A), RoundedCornerShape(8.dp))
                            .padding(10.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text("Received from Meteo:", color = Color(0xFF94A3B8), fontSize = 11.sp)
                            Text(
                                text = state.weatherLastReceivedTime.ifEmpty { "--" },
                                color = Color(0xFF38BDF8),
                                fontSize = 11.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text("Pushed to Watchy:", color = Color(0xFF94A3B8), fontSize = 11.sp)
                            Text(
                                text = state.weatherLastPushedTime.ifEmpty { "--" },
                                color = Color(0xFF4ADE80),
                                fontSize = 11.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                    }

                    // Parsed Weather Data Grid
                    Text(
                        text = "PARSED WEATHER DATA",
                        color = Color(0xFF94A3B8),
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        StatTile("TEMP (FEELS)", "${state.weatherParsedTemp} (${state.weatherParsedFeels})", Modifier.weight(1f))
                        StatTile("WEATHER CODE", state.weatherParsedCondition, Modifier.weight(1f))
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        StatTile("WIND / HUMIDITY", "${state.weatherParsedWind} | ${state.weatherParsedHumidity}", Modifier.weight(1f))
                        StatTile("PRESSURE / ELEV", "${state.weatherParsedPressure} | ${state.weatherParsedElevation}", Modifier.weight(1f))
                    }

                    // Weather Strings & Raw JSON
                    Text(
                        text = "WEATHER STRINGS",
                        color = Color(0xFF94A3B8),
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold
                    )
                    if (state.weatherPayloadString.isNotEmpty()) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(Color(0xFF0F172A), RoundedCornerShape(6.dp))
                                .padding(8.dp)
                        ) {
                            Text("Watchy Payload String:", color = Color(0xFF64748B), fontSize = 10.sp)
                            Text(
                                text = state.weatherPayloadString,
                                color = Color(0xFFF1F5F9),
                                fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace
                            )
                        }
                    }
                    if (state.weatherRawString.isNotEmpty()) {
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(Color(0xFF0F172A), RoundedCornerShape(6.dp))
                                .padding(8.dp)
                        ) {
                            Text("Open-Meteo Current JSON:", color = Color(0xFF64748B), fontSize = 10.sp)
                            Text(
                                text = state.weatherRawString,
                                color = Color(0xFFF1F5F9),
                                fontSize = 10.sp,
                                fontFamily = FontFamily.Monospace,
                                maxLines = 10,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }
            }
        }

        // Card 4: Activity Log
        Card(
            colors = CardDefaults.cardColors(containerColor = Color(0xFF1E293B)),
            border = BorderStroke(1.dp, Color(0xFF334155)),
            shape = RoundedCornerShape(14.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Text(
                    text = "Activity Log",
                    color = Color(0xFF38BDF8),
                    fontWeight = FontWeight.Bold,
                    fontSize = 14.sp
                )
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF090D16), RoundedCornerShape(8.dp))
                        .padding(10.dp),
                    verticalArrangement = Arrangement.spacedBy(3.dp)
                ) {
                    state.logs.take(12).forEach { line ->
                        Text(
                            text = line,
                            color = Color(0xFFCBD5E1),
                            fontFamily = FontFamily.Monospace,
                            fontSize = 11.sp
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun WatchyBezelLookAlike(
    framebuffer: ByteArray,
    hasValidScreen: Boolean,
    watchTime: String,
    batteryText: String,
    stepsText: String,
    weatherText: String,
    progress: Float,
    enabled: Boolean,
    onButtonPress: (Int) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Left Bezel Buttons: SW2 (Top-Left) & SW1 (Bottom-Left)
        Column(
            modifier = Modifier.height(216.dp),
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            BezelSwitchButton(
                swLabel = "SW2",
                actionLabel = "Back",
                enabled = enabled,
                onClick = { onButtonPress(2) }
            )
            BezelSwitchButton(
                swLabel = "SW1",
                actionLabel = "Menu",
                enabled = enabled,
                onClick = { onButtonPress(1) }
            )
        }

        Spacer(modifier = Modifier.width(8.dp))

        // Watchy PCB/Case + 200x200 E-Paper Screen
        Column(
            modifier = Modifier
                .background(Color(0xFF0B0F19), RoundedCornerShape(20.dp))
                .border(3.dp, Color(0xFF334155), RoundedCornerShape(20.dp))
                .padding(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = "WATCHY 200×200",
                color = Color(0xFF64748B),
                fontSize = 9.sp,
                fontWeight = FontWeight.Bold
            )
            Spacer(modifier = Modifier.height(4.dp))

            val bitmap = remember(framebuffer, hasValidScreen) {
                val bmp = Bitmap.createBitmap(200, 200, Bitmap.Config.ARGB_8888)
                val pixels = IntArray(200 * 200)
                val white = 0xFFECEFEA.toInt()
                val black = 0xFF111827.toInt()
                for (byteIdx in 0 until minOf(5000, framebuffer.size)) {
                    val b = framebuffer[byteIdx].toInt() and 0xFF
                    for (bit in 0 until 8) {
                        val isWhite = (b and (1 shl (7 - bit))) != 0
                        val pxIdx = byteIdx * 8 + bit
                        if (pxIdx < pixels.size) {
                            pixels[pxIdx] = if (isWhite) white else black
                        }
                    }
                }
                bmp.setPixels(pixels, 0, 200, 0, 0, 200, 200)
                bmp.asImageBitmap()
            }

            Box(
                modifier = Modifier
                    .size(190.dp)
                    .clip(RoundedCornerShape(4.dp))
                    .background(Color(0xFFECEFEA))
                    .border(1.dp, Color(0xFF1E293B), RoundedCornerShape(4.dp))
            ) {
                if (hasValidScreen) {
                    Canvas(modifier = Modifier.fillMaxSize()) {
                        drawImage(
                            image = bitmap,
                            dstOffset = IntOffset.Zero,
                            dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
                            filterQuality = FilterQuality.None
                        )
                    }
                } else {
                    // Local Watchy E-Paper Look-Alike Preview before first framebuffer capture
                    LocalWatchyPreviewScreen(watchTime, batteryText, stepsText, weatherText)
                }
            }

            Spacer(modifier = Modifier.height(6.dp))
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier
                    .width(190.dp)
                    .height(4.dp)
                    .clip(RoundedCornerShape(2.dp)),
                color = Color(0xFF38BDF8),
                trackColor = Color(0xFF1E293B)
            )
        }

        Spacer(modifier = Modifier.width(8.dp))

        // Right Bezel Buttons: SW3 (Top-Right) & SW4 (Bottom-Right)
        Column(
            modifier = Modifier.height(216.dp),
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            BezelSwitchButton(
                swLabel = "SW3",
                actionLabel = "Up/Weat",
                enabled = enabled,
                onClick = { onButtonPress(3) }
            )
            BezelSwitchButton(
                swLabel = "SW4",
                actionLabel = "Dn/BLE",
                enabled = enabled,
                onClick = { onButtonPress(4) }
            )
        }
    }
}

@Composable
fun LocalWatchyPreviewScreen(
    watchTime: String,
    batteryText: String,
    stepsText: String,
    weatherText: String
) {
    val nowTime = remember { SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date()) }
    val nowDay = remember { SimpleDateFormat("EEEE", Locale.getDefault()).format(Date()) }
    val nowDate = remember { SimpleDateFormat("yyyy.MM.dd", Locale.getDefault()).format(Date()) }

    val displayClock = if (watchTime != "--" && watchTime.length >= 16) {
        watchTime.substring(11, 16)
    } else nowTime

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(12.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.SpaceBetween
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(
                text = if (stepsText != "--") "👟 $stepsText" else "WATCHY GSR",
                color = Color(0xFF111827),
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 11.sp
            )
            Text(
                text = if (weatherText != "--") weatherText else "BLE TWIN",
                color = Color(0xFF111827),
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 10.sp
            )
        }

        Text(
            text = displayClock,
            color = Color(0xFF111827),
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.ExtraBold,
            fontSize = 44.sp
        )

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = nowDay,
                color = Color(0xFF111827),
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 16.sp
            )
            Text(
                text = nowDate,
                color = Color(0xFF111827),
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 14.sp
            )
        }

        Text(
            text = if (batteryText != "--") "BATT: $batteryText" else "Tap Connect Above",
            color = Color(0xFF334155),
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 9.sp,
            maxLines = 1
        )
    }
}

@Composable
fun BezelSwitchButton(
    swLabel: String,
    actionLabel: String,
    enabled: Boolean,
    onClick: () -> Unit
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 10.dp),
        shape = RoundedCornerShape(10.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = Color(0xFF0F172A),
            disabledContainerColor = Color(0xFF0F172A).copy(alpha = 0.5f)
        ),
        border = BorderStroke(1.5.dp, if (enabled) Color(0xFF38BDF8) else Color(0xFF334155)),
        modifier = Modifier.width(68.dp)
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = swLabel,
                color = Color(0xFF38BDF8),
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )
            Text(
                text = actionLabel,
                color = Color(0xFFF8FAFC),
                fontSize = 10.sp,
                fontWeight = FontWeight.SemiBold
            )
        }
    }
}

@Composable
fun StatTile(label: String, value: String, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .background(Color(0xFF0F172A), RoundedCornerShape(8.dp))
            .border(1.dp, Color(0xFF334155), RoundedCornerShape(8.dp))
            .padding(10.dp)
    ) {
        Text(
            text = label,
            color = Color(0xFF94A3B8),
            fontSize = 10.sp,
            fontWeight = FontWeight.SemiBold
        )
        Spacer(modifier = Modifier.height(2.dp))
        Text(
            text = value,
            color = Color(0xFFF8FAFC),
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 13.sp
        )
    }
}

@Composable
fun StatusPill(connected: Boolean, connecting: Boolean, isWifi: Boolean = false) {
    val bg = when {
        connected && isWifi -> Color(0xFF38BDF8).copy(alpha = 0.2f)
        connected -> Color(0xFF22C55E).copy(alpha = 0.2f)
        connecting -> Color(0xFFF59E0B).copy(alpha = 0.2f)
        else -> Color(0xFF334155)
    }
    val fg = when {
        connected && isWifi -> Color(0xFF38BDF8)
        connected -> Color(0xFF22C55E)
        connecting -> Color(0xFFF59E0B)
        else -> Color(0xFF94A3B8)
    }
    val text = when {
        connected && isWifi -> "WIFI AP"
        connected -> "BLE CONNECTED"
        connecting -> "CONNECTING"
        else -> "DISCONNECTED"
    }
    Box(
        modifier = Modifier
            .background(bg, CircleShape)
            .padding(horizontal = 10.dp, vertical = 5.dp)
    ) {
        Text(text = text, color = fg, fontSize = 11.sp, fontWeight = FontWeight.Bold)
    }
}

@SuppressLint("MissingPermission")
fun readPhoneGps(context: Context, bleManager: WatchyBleManager) {
    try {
        val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        val loc = lm?.getLastKnownLocation(LocationManager.GPS_PROVIDER)
            ?: lm?.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
            ?: lm?.getLastKnownLocation(LocationManager.PASSIVE_PROVIDER)
        if (loc != null) {
            val lat = String.format(Locale.US, "%.6f", loc.latitude)
            val lon = String.format(Locale.US, "%.6f", loc.longitude)
            bleManager.updateCoordinatesInputs(lat, lon)
            bleManager.log("Got phone GPS: $lat, $lon")
        } else {
            bleManager.log("No cached GPS fix found — use City Search or enable Location.")
        }
    } catch (e: Exception) {
        bleManager.log("GPS error: ${e.message}")
    }
}

suspend fun searchCityOpenMeteo(query: String, bleManager: WatchyBleManager) {
    withContext(Dispatchers.IO) {
        try {
            bleManager.log("Searching city \"$query\"...")
            val encoded = URLEncoder.encode(query, "UTF-8")
            val url = URL("https://geocoding-api.open-meteo.com/v1/search?name=$encoded&count=1&language=en&format=json")
            val conn = (url.openConnection() as HttpURLConnection).apply {
                connectTimeout = 6000
                readTimeout = 6000
            }
            val body = conn.inputStream.bufferedReader().use { it.readText() }
            val json = JSONObject(body)
            val results = json.optJSONArray("results")
            if (results == null || results.length() == 0) {
                bleManager.log("No city found for \"$query\".")
                return@withContext
            }
            val first = results.getJSONObject(0)
            val name = first.optString("name", query)
            val country = first.optString("country", "")
            val lat = String.format(Locale.US, "%.6f", first.getDouble("latitude"))
            val lon = String.format(Locale.US, "%.6f", first.getDouble("longitude"))
            bleManager.updateCoordinatesInputs(lat, lon)
            bleManager.log("Found: $name, $country ($lat, $lon)")
        } catch (e: Exception) {
            bleManager.log("City search error: ${e.message}")
        }
    }
}

fun fetchAndPushOpenMeteo(lat: Double, lon: Double, bleManager: WatchyBleManager) {
    bleManager.fetchAndPushWeather(lat, lon, auto = false)
}
