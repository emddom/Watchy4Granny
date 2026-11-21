#include "OverrideGSR.h"
#include "UiStateMachine.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

struct NTPUse final {
    uint8_t State;
    uint8_t Wait;
    uint8_t Pause;
    unsigned long tWait;
    time_t Last;
    bool TimeZone;
    bool UpdateUTC;
    bool AutoSync;
    uint8_t SyncHour;
    uint8_t SyncMins;
    uint8_t SyncDays;
    time_t TravelTemp;
    char fromWeb[64];
};
extern RTC_DATA_ATTR struct NTPUse NTPData;

RTC_DATA_ATTR bool callingForHelp = false;
RTC_DATA_ATTR bool helpRequested = false;
RTC_DATA_ATTR bool wifiFailed = false;
RTC_DATA_ATTR bool showingMedInfo = false;
RTC_DATA_ATTR bool isSuspended = false;
RTC_DATA_ATTR bool showingButtons = false;
RTC_DATA_ATTR bool showingWeather = false;
RTC_DATA_ATTR int8_t lastDrawnMinute = -1;
bool showingBleRemote = false;
bool bleMirrorMode = false;
volatile uint8_t pendingBleMode = 0;
RTC_DATA_ATTR time_t suspendTime = 0;

using GxDisplayType = GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT>;
using GxBufType = uint8_t(GxDisplayType::*)[5000];
template<GxBufType M>
struct GxBufferAccess {
    friend const uint8_t* getGxDisplayBuffer(const GxDisplayType& d) {
        return d.*M;
    }
};
template struct GxBufferAccess<&GxDisplayType::_buffer>;
const uint8_t* getGxDisplayBuffer(const GxDisplayType& d);

static bool bleInitialized = false;
static bool bleConnected = false;
static bool screenReadMode = false;
static bool bleWeatherPending = false;
static unsigned long weatherCompleteTime = 0;
static unsigned long bleKeepAliveUntil = 0;
static uint32_t screenFrameSeq = 1;
static uint32_t bleWeatherReqSeq = 0;
static unsigned long bleStartTime = 0;
static String bleLastAction = "Ready";
static NimBLEServer* bleServer = nullptr;
static NimBLEHIDDevice* bleHid = nullptr;
static NimBLECharacteristic* bleInputMedia = nullptr;
static NimBLECharacteristic* bleTwinTimeChar = nullptr;
static NimBLECharacteristic* bleTwinLocChar = nullptr;
static NimBLECharacteristic* bleTwinWeatChar = nullptr;
static NimBLECharacteristic* bleTwinStatusChar = nullptr;

// Watchy4Granny Digital Twin GATT Service & Characteristic UUIDs
static constexpr const char* TWIN_SERVICE_UUID     = "19b10000-e8f2-537e-4f6c-d104768a1214";
static constexpr const char* TWIN_CHAR_TIME_UUID   = "19b10001-e8f2-537e-4f6c-d104768a1214";
static constexpr const char* TWIN_CHAR_LOC_UUID    = "19b10002-e8f2-537e-4f6c-d104768a1214";
static constexpr const char* TWIN_CHAR_WEAT_UUID   = "19b10003-e8f2-537e-4f6c-d104768a1214";
static constexpr const char* TWIN_CHAR_STATUS_UUID = "19b10004-e8f2-537e-4f6c-d104768a1214";

static String formatTwinTime() {
    char buf[48];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d,%ld",
             WatchTime.Local.Year + 1900,
             WatchTime.Local.Month + 1,
             WatchTime.Local.Day,
             WatchTime.Local.Hour,
             WatchTime.Local.Minute,
             WatchTime.Local.Second,
             (long)WatchTime.UTC_RAW);
    return String(buf);
}

static String formatTwinLocation() {
    if (!uiSM.watchy) return "0.000000,0.000000";
    double lat = uiSM.watchy->getWeatherLatitude(true);
    double lon = uiSM.watchy->getWeatherLongitude(true);
    char buf[40];
    snprintf(buf, sizeof(buf), "%.6f,%.6f", lat, lon);
    return String(buf);
}

static String formatTwinWeather() {
    if (!uiSM.watchy) return "0,0,0,0,0.0,0,0";
    bool ready = uiSM.watchy->IsWeatherReady();
    int temp = ready ? uiSM.watchy->GetWeatherTemperature() : 0;
    int feels = ready ? uiSM.watchy->GetWeatherTemperatureFeelsLike() : 0;
    uint16_t cond = ready ? uiSM.watchy->GetWeatherID() : 0;
    uint8_t hum = ready ? uiSM.watchy->GetWeatherHumidity() : 0;
    float windMs = ready ? (uiSM.watchy->GetWeatherWindSpeed() / 3.6f) : 0.0f;
    uint16_t pres = ready ? uiSM.watchy->GetWeatherPressure() : 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d,%d,%u,%u,%.1f,%u,%d",
             temp, feels, cond, hum, windMs, pres, ready ? 1 : 0);
    return String(buf);
}

static String formatTwinStatus() {
    if (!uiSM.watchy) return "0,0.00,0,0,0,0,0";
    float v = WatchyGSR::getBatteryVoltage();
    int pct = 0;
    if (v >= 4.20f) pct = 100;
    else if (v <= 3.30f) pct = 0;
    else pct = (int)((v - 3.30f) / (4.20f - 3.30f) * 100.0f);
    uint32_t steps = uiSM.watchy->CurrentStepCount();
    int chg = (uiSM.watchy->GetBatteryState() == 3) ? 1 : 0;
    int mode = showingBleRemote ? 1 : (showingWeather ? 2 : 0);
    char buf[72];
    snprintf(buf, sizeof(buf), "%d,%.2f,%lu,%d,%lu,%d,%lu",
             pct, v, (unsigned long)steps, chg, (unsigned long)screenFrameSeq, mode,
             (unsigned long)(bleWeatherPending ? bleWeatherReqSeq : 0));
    return String(buf);
}

static void setCharString(NimBLECharacteristic* pChar, const String& s) {
    if (pChar) {
        pChar->setValue((const uint8_t*)s.c_str(), s.length());
    }
}

class WatchyTwinCharCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pChar) {
        if (uiSM.watchy) uiSM.watchy->refreshBleActivity();
        std::string uuid = pChar->getUUID().toString();
        if (uuid == TWIN_CHAR_TIME_UUID) {
            if (!screenReadMode) {
                setCharString(pChar, formatTwinTime());
            }
        } else if (uuid == TWIN_CHAR_LOC_UUID) {
            setCharString(pChar, formatTwinLocation());
        } else if (uuid == TWIN_CHAR_WEAT_UUID) {
            setCharString(pChar, formatTwinWeather());
        } else if (uuid == TWIN_CHAR_STATUS_UUID) {
            setCharString(pChar, formatTwinStatus());
        }
    }

    void onWrite(NimBLECharacteristic* pChar) {
        if (!uiSM.watchy) return;
        uiSM.watchy->refreshBleActivity();
        std::string uuid = pChar->getUUID().toString();
        std::string raw = std::string(pChar->getValue());
        String val = String(raw.c_str());
        val.trim();
        if (val.length() == 0) return;

        if (uuid == TWIN_CHAR_TIME_UUID) {
            if (val.startsWith("SCR:")) {
                if (val == "SCR:END") {
                    screenReadMode = false;
                    setCharString(pChar, formatTwinTime());
                } else {
                    int offset = 0, len = 500;
                    if (sscanf(val.c_str() + 4, "%d,%d", &offset, &len) >= 1) {
                        if (offset >= 0 && offset < 5000) {
                            if (len <= 0 || len > 500) len = 500;
                            if (offset + len > 5000) len = 5000 - offset;
                            const uint8_t* fb = getGxDisplayBuffer(WatchyGSR::display);
                            screenReadMode = true;
                            pChar->setValue(fb + offset, len);
                        }
                    }
                }
                return;
            }
            screenReadMode = false;
            if (val == "MODE:WATCH") {
                pendingBleMode = 1;
                return;
            } else if (val == "MODE:BLE") {
                pendingBleMode = 2;
                return;
            } else if (val == "MODE:OFF") {
                pendingBleMode = 3;
                return;
            } else if (val.startsWith("BTN:")) {
                int btn = atoi(val.c_str() + 4);
                if (btn >= 1 && btn <= 4) {
                    if (showingBleRemote && btn == 2) {
                        pendingBleMode = 1;
                    } else {
                        Button = (uint8_t)btn;
                    }
                }
                return;
            }

            int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
            if (val.indexOf('-') > 0 && sscanf(val.c_str(), "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) >= 5) {
                uiSM.watchy->SetLocalTimeFromBLE(Y, M, D, h, m, s);
            } else {
                int commaIdx = val.indexOf(',');
                String epochStr = (commaIdx > 0) ? val.substring(0, commaIdx) : val;
                String tzStr = (commaIdx > 0) ? val.substring(commaIdx + 1) : "";
                tzStr.trim();
                long long rawEpoch = atoll(epochStr.c_str());
                if (rawEpoch > 100000000000LL) {
                    rawEpoch /= 1000LL; // Convert milliseconds to seconds
                }
                if (rawEpoch > 1700000000LL) {
                    uiSM.watchy->SetTimeFromBLE((time_t)rawEpoch, tzStr);
                }
            }
            setCharString(pChar, formatTwinTime());
            bleLastAction = "Time Synced";
            uiSM.watchy->UpdateScreen();
        } else if (uuid == TWIN_CHAR_LOC_UUID) {
            int commaIdx = val.indexOf(',');
            if (commaIdx > 0) {
                double lat = val.substring(0, commaIdx).toDouble();
                double lon = val.substring(commaIdx + 1).toDouble();
                uiSM.watchy->SetWeatherLocationFromBLE(lat, lon);
                setCharString(pChar, formatTwinLocation());
                bleLastAction = "Loc Updated";
                uiSM.watchy->UpdateScreen();
            }
        } else if (uuid == TWIN_CHAR_WEAT_UUID) {
            float tempC = 0, feelsC = 0, windMs = 0;
            unsigned int cond = 0, hum = 0, pres = 1013;
            float elev = 0;
            if (sscanf(val.c_str(), "%f,%f,%u,%u,%f,%u,%f", &tempC, &feelsC, &cond, &hum, &windMs, &pres, &elev) >= 5) {
                bleWeatherPending = false;
                uiSM.watchy->SetWeatherFromBLE(tempC, feelsC, (uint16_t)cond, (uint8_t)hum, windMs * 3.6f, (uint16_t)pres, elev);
                setCharString(pChar, formatTwinWeather());
                if (bleTwinStatusChar) {
                    setCharString(bleTwinStatusChar, formatTwinStatus());
                }
                bleLastAction = "Weather Sync";
                uiSM.watchy->notifyWeatherComplete();
                uiSM.watchy->UpdateScreen();
            }
        }
    }
};

// Standard USB HID Consumer Control Report Descriptor (Report ID 1: 4 media keys + 4 padding bits)
static const uint8_t hidMediaReportDescriptor[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x04,        //   Report Count (4)
    0x09, 0xE9,        //   Usage (Volume Increment - bit 0 = 0x01)
    0x09, 0xEA,        //   Usage (Volume Decrement - bit 1 = 0x02)
    0x09, 0xCD,        //   Usage (Play/Pause       - bit 2 = 0x04)
    0x09, 0xE2,        //   Usage (Mute             - bit 3 = 0x08)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x03,        //   Input (Const,Var,Abs)
    0xC0               // End Collection
};

class WatchyBleServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        bleConnected = true;
        bleLastAction = "Connected";
        if (uiSM.watchy) uiSM.watchy->refreshBleActivity();
        if ((showingBleRemote || bleMirrorMode || showingWeather) && pServer->getConnectedCount() < 3) {
            NimBLEDevice::startAdvertising();
        }
        if (uiSM.watchy) uiSM.watchy->UpdateScreen();
    }
    void onDisconnect(NimBLEServer* pServer) {
        bleConnected = (pServer->getConnectedCount() > 0);
        bleLastAction = bleConnected ? "Connected" : "Disconnected";
        if (!bleConnected) {
            if (bleMirrorMode) bleMirrorMode = false;
            bleKeepAliveUntil = 0;
            screenReadMode = false;
            if (uiSM.watchy) uiSM.watchy->refreshBleActivity();
        }
        if (showingBleRemote || showingWeather) {
            NimBLEDevice::startAdvertising();
        }
        if (uiSM.watchy) uiSM.watchy->UpdateScreen();
    }
};

void OverrideGSR::InsertDefaults() {
    SetWeatherScale(true); // Default to Metric (Celsius, m/s)
    NTPData.AutoSync = true; // Default auto-sync enabled (using BLE with phone app)
    SetWeatherInterval(0);
    CancelWeather();
}

void OverrideGSR::InsertPost() {
    SetWeatherInterval(0); // Disable automatic background WiFi weather polling
    CancelWeather();
}

void OverrideGSR::InsertInitWatchStyle(uint8_t StyleID) {
    WantWeather(true);
    Design.Status.BATTy = 193;
}

void OverrideGSR::InsertWiFi() {
  if (callingForHelp) {
    // Send a webhook request here
    WiFiClientSecure client;
    client.setInsecure(); // Disable SSL certificate verification for simplicity
    HTTPClient http;
    String url = "https://maker.ifttt.com/trigger/find_phone/with/key/dwVClfXDdWL9FfUEWEvdlw";
    
    http.begin(client, url);
    int httpResponseCode = http.GET();
    http.end();
    
    callingForHelp = false;
    helpRequested = true;
    UpdateScreen(); // Force screen redraw to show the next message
    endWiFi(); // Release WiFi
  }
}

void OverrideGSR::InsertWiFiEnding() {
  if (callingForHelp) {
    // This is called if WiFi fails to connect
    callingForHelp = false;
    wifiFailed = true;
    UpdateScreen(); // Force screen redraw to show error
  }
}

static bool isBleSyncToday() {
    return !WatchyGSR::SRTC.checkingDrift(WatchTime.ESPRTC) && NTPData.AutoSync && (NTPData.SyncDays & (1 << WatchTime.Local.Wday));
}

void OverrideGSR::InsertOnMinute() {
  float voltage = WatchyGSR::getBatteryVoltage();
  int percentage = 0;
  if (voltage >= 4.20) percentage = 100;
  else if (voltage <= 3.30) percentage = 0;
  else percentage = (int)((voltage - 3.30) / (4.20 - 3.30) * 100.0);

  if (percentage <= GetSuspendLevel() && GetBatteryState() != 3 && !isSuspended) {
      suspendTime = WatchTime.UTC_RAW;
      uiSM.setState(&suspendState);
  }

  // Daily Auto-Sync via BLE with Companion Phone App (replaces WiFi NTP)
  if (isBleSyncToday() && WatchTime.Local.Hour == NTPData.SyncHour && WatchTime.Local.Minute == NTPData.SyncMins && WatchTime.UTC_RAW - NTPData.Last > 59) {
      NTPData.State = 0;
      NTPData.Last = WatchTime.UTC_RAW;
      endWiFi();
      startBleHid();
      extendBleKeepAlive(45000); // 45-second advertising window for phone app
      requestBleWeather();       // Request both fresh time & weather push
      UpdateScreen();
  }

  // Clear the messages when the minute changes so the normal clock returns
  if (helpRequested || wifiFailed || showingMedInfo || showingButtons || (showingWeather && !IsWeatherLoading() && !bleWeatherPending)) {
     helpRequested = false;
     wifiFailed = false;
     showingMedInfo = false;
     showingButtons = false;
     showingWeather = false;
     if (uiSM.getCurrentState() != &watchfaceState && !isSuspended) {
         uiSM.setState(&watchfaceState);
     }
     UpdateScreen();
  }
}

bool OverrideGSR::InsertNeedAwake(bool GoingAsleep) {
    if (showingButtons) return true;
    if (showingBleRemote || bleMirrorMode || showingWeather) {
        KeepDisplayAwake();
        return true;
    }
    if (screenReadMode) {
        KeepDisplayAwake();
        return true;
    }
    if (millis() < bleKeepAliveUntil) {
        KeepDisplayAwake();
        return true;
    }
    // Intercept manual NTP sync requests from menu (Menu -> Options -> Sync Watchy)
    if (NTPData.State > 0 && !callingForHelp) {
        NTPData.State = 0;
        NTPData.Last = WatchTime.UTC_RAW;
        endWiFi();
        startBleHid();
        extendBleKeepAlive(45000);
        requestBleWeather();
        UpdateScreen();
        return true;
    }
    // Intercept WiFi weather polling - disable if Twin is not connected
    if (IsWeatherLoading() && !callingForHelp) {
        if (!isBleConnected()) {
            CancelWeather();
            SetWeatherInterval(0);
        } else {
            CancelWeather();
            requestBleWeather();
        }
        UpdateScreen();
        return false;
    }
    if (isSuspended && GetBatteryState() == 3) {
        return true; // Keep awake when charging to animate blinking character
    }
    // Ensure display is drawn if the minute rolled over before sleeping
    if (NoMenu() && !isSuspended && (lastDrawnMinute < 0 || WatchTime.Local.Minute != lastDrawnMinute)) {
        return true;
    }
    return false;
}

void OverrideGSR::drawWatchFace() {
    if (isSuspended) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona16pt7b);
        display.setTextColor(ForeColor());
        
        if (GetBatteryState() == 3) {
            // Charging
            display.setCursor(5, 30);
            display.print("CHARGING...");
            display.setFont(&aAntiCorona10pt7b);
            display.setCursor(5, 60);
            struct tm * t = localtime(&suspendTime);
            char buf[32];
            strftime(buf, sizeof(buf), "Suspended at %H:%M", t);
            display.print(buf);
            
            display.setCursor(5, 90);
            display.print("Steps: " + String(CurrentStepCount()));
            display.setCursor(5, 120);
            display.print(CountdownTimerActive() ? "Timers: Active" : "Timers: Saved");
            
            display.setFont(&aAntiCorona16pt7b);
            display.setCursor(5, 160);
            display.print("WAKE UP?");
            
            // Blinking character next to SW3 (Top Right button is button 3)
            if (WatchTime.Local.Second % 2 == 0) {
                display.setCursor(180, 50); // Near top right button
                display.print("*");
            }
        } else {
            // Suspended, not charging
            display.setCursor(20, 100);
            display.print("SUSPENDED");
            display.setFont(&aAntiCorona10pt7b);
            display.setCursor(10, 130);
            display.print("Battery critically low");
        }
        screenFrameSeq++;
        return;
    }

    // First, call the base drawing logic so the watch renders normally underneath
    WatchyGSR::drawWatchFace();
    
    // Overlay our custom screens based on state
    if (showingButtons) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona10pt7b);
        display.setTextColor(ForeColor());

        auto getLogicalName = [](uint8_t logicalId) -> String {
            if (logicalId == 1) return "Menu";
            if (logicalId == 2) return "Back";
            if (logicalId == 3) return "Weather";
            if (logicalId == 4) return "BLE";
            return "";
        };

        auto printTruncated = [&](String text, int16_t x, int16_t y, int16_t maxWidth) {
            int16_t x1, y1;
            uint16_t w, h;
            display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
            if (w > maxWidth) {
                while (text.length() > 0) {
                    text.remove(text.length() - 1);
                    String temp = text + "..";
                    display.getTextBounds(temp, 0, 0, &x1, &y1, &w, &h);
                    if (w <= maxWidth) {
                        text = temp;
                        break;
                    }
                }
            }
            display.setCursor(x, y);
            display.print(text);
        };

        // Bottom Left corner (Physical SW1)
        printTruncated("SW1: " + getLogicalName(WatchyGSR::getSWValue(1, 0, 0, 0)), 5, 180, 95);
        // Top Left corner (Physical SW2)
        printTruncated("SW2: " + getLogicalName(WatchyGSR::getSWValue(0, 1, 0, 0)), 5, 40, 95);
        // Top Right corner (Physical SW3)
        printTruncated("SW3: " + getLogicalName(WatchyGSR::getSWValue(0, 0, 1, 0)), 105, 40, 95);
        // Bottom Right corner (Physical SW4)
        printTruncated("SW4: " + getLogicalName(WatchyGSR::getSWValue(0, 0, 0, 1)), 105, 180, 95);

    } else if (showingMedInfo) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona16pt7b);
        display.setTextColor(ForeColor());
        
        String title = "MEDICAL INFO";
        int16_t x1, y1;
        uint16_t w, h;
        
        // Center the title
        display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((200 - w) / 2, 30);
        display.print(title);
        
        String line1 = "Name: Endre Domiczi";
        String line2 = "ID: 35307050015";
        String line3 = "Allergies: None";
        String line4 = "Birth: 1953.07.05";

        // Detect if the longest string fits, otherwise adjust to a smaller font
        display.setFont(&aAntiCorona11pt7b);
        display.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
        if (w > 180) {
            display.setFont(&aAntiCorona10pt7b); // Downgrade font size
        }
        
        display.setCursor(5, 70);
        display.print(line1);
        display.setCursor(5, 100);
        display.print(line2);
        display.setCursor(5, 130);
        display.print(line3);
        display.setCursor(5, 160);
        display.print(line4);
    } else if (showingWeather) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona15pt7b);
        display.setTextColor(ForeColor());

        String title = (IsWeatherLoading() || bleWeatherPending) ? "WEATHER *" : "WEATHER";
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((200 - w) / 2, 24);
        display.print(title);

        if (!IsWeatherReady()) {
            display.setFont(&aAntiCorona12pt7b);
            if (IsWeatherLoading() || bleWeatherPending) {
                display.setCursor(15, 85);
                display.print(bleConnected ? "SYNCING BLE..." : "BLE WAITING...");
                display.setFont(&aAntiCorona10pt7b);
                display.setCursor(10, 120);
                display.print(bleConnected ? "Fetching on Twin" : "Open Digital Twin");
                display.setCursor(10, 148);
                display.print("or Press UP (SW3)");
            } else {
                display.setCursor(20, 85);
                display.print("NO DATA YET");
                display.setFont(&aAntiCorona10pt7b);
                display.setCursor(15, 120);
                display.print("Open Twin App or");
                display.setCursor(15, 148);
                display.print("Press UP (SW3)");
            }
        } else {
            uint16_t cond = GetWeatherID();
            const unsigned char* icon = getWeatherIcon(cond, false);
            if (icon != nullptr) {
                display.drawBitmap(8, 34, icon, 48, 32, ForeColor());
            }

            display.setFont(&aAntiCorona16pt7b);
            String tempStr = String(GetWeatherTemperature()) + (IsMetric() ? " C" : " F");
            display.setCursor(60, 60);
            display.print(tempStr);

            display.setFont(&aAntiCorona10pt7b);
            display.setCursor(display.getCursorX() + 4, 60);
            String feelsStr = "(" + String(GetWeatherTemperatureFeelsLike()) + (IsMetric() ? " C)" : " F)");
            display.print(feelsStr);

            double lat = getWeatherLatitude(true);
            if (lat == NOLOC || lat == 0.0) lat = getWeatherLatitude(false);
            double lon = getWeatherLongitude(true);
            if (lon == NOLOC || lon == 0.0) lon = getWeatherLongitude(false);

            display.setFont(&aAntiCorona10pt7b);
            display.setCursor(5, 90);
            String coordStr = "Lat/Lng: ";
            if (filterGeo(lat, true) && filterGeo(lon, false) && lat != NOLOC && lon != NOLOC) {
                coordStr += String(lat, 1) + " / " + String(lon, 1);
            } else {
                coordStr += "-- / --";
            }
            display.print(coordStr);

            display.setCursor(5, 115);
            display.print("Elevation: " + String(GetWeatherElevation()) + (IsMetric() ? " m" : " ft"));

            display.setCursor(5, 140);
            display.print("Humidity: " + String(GetWeatherHumidity()) + "%");

            display.setCursor(5, 165);
            if (IsMetric()) {
                display.print("Wind: " + String(GetWeatherWindSpeed() / 3.6f, 1) + " m/s");
            } else {
                display.print("Wind: " + String((int)GetWeatherWindSpeed()) + " mph");
            }

            display.setCursor(5, 190);
            display.print("Pressure: " + String(GetWeatherPressure()) + " hPa");
        }
    } else if (showingBleRemote) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona15pt7b);
        display.setTextColor(ForeColor());

        String title = "BLE REMOTE";
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((200 - w) / 2, 24);
        display.print(title);

        display.setFont(&aAntiCorona10pt7b);
        display.setCursor(5, 52);
        display.print("ID: Watchy4Granny");

        display.setCursor(5, 78);
        display.print(String("BLE: ") + (bleConnected ? "CONNECTED" : "PAIR / WAIT"));

        char timeBuf[24];
        snprintf(timeBuf, sizeof(timeBuf), "Time: %02d:%02d:%02d",
                 WatchTime.Local.Hour, WatchTime.Local.Minute, WatchTime.Local.Second);
        display.setCursor(5, 104);
        display.print(timeBuf);

        display.setCursor(5, 130);
        display.print("Act: " + bleLastAction);

        // Top-Left (SW2) & Top-Right (SW3)
        display.setCursor(5, 162);
        display.print("SW2: Exit");
        display.setCursor(105, 162);
        display.print("SW3: Vol+");
        // Bottom-Left (SW1) & Bottom-Right (SW4)
        display.setCursor(5, 188);
        display.print("SW1: Play");
        display.setCursor(105, 188);
        display.print("SW4: Vol-");
    } else if (callingForHelp) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona16pt7b);
        display.setTextColor(ForeColor());
        display.setCursor(5, 100);
        display.print("FINDING PHONE");
        display.setCursor(20, 130);
        display.print("PLEASE WAIT...");
    } else if (helpRequested) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona16pt7b);
        display.setTextColor(ForeColor());
        display.setCursor(20, 100);
        display.print("PHONE PINGED!");
        display.setCursor(40, 130);
        display.print("(Check it)");
    } else if (wifiFailed) {
        display.fillScreen(BackColor());
        display.setFont(&aAntiCorona16pt7b);
        display.setTextColor(ForeColor());
        display.setCursor(20, 100);
        display.print("WIFI FAILED!");
        display.setCursor(10, 130);
        display.print("(Check settings)");
    } else if (NoMenu()) {
        // Top-level Watch Face: draw BLE connection indicator in lower-left corner (x=5, y=180, w=26, h=15)
        if (bleConnected) {
            // Connected: Solid filled pill with Bluetooth rune + checkmark
            display.fillRoundRect(5, 180, 26, 15, 3, ForeColor());
            uint16_t fg = BackColor();
            display.drawLine(12, 182, 12, 192, fg);
            display.drawLine(12, 182, 16, 185, fg);
            display.drawLine(16, 185, 9, 190, fg);
            display.drawLine(12, 192, 16, 189, fg);
            display.drawLine(16, 189, 9, 184, fg);
            // Checkmark
            display.drawLine(19, 188, 22, 191, fg);
            display.drawLine(22, 191, 27, 184, fg);
            display.drawLine(19, 187, 22, 190, fg);
            display.drawLine(22, 190, 27, 183, fg);
        } else {
            // Disconnected: Outlined pill with Bluetooth rune + 'x'
            display.fillRoundRect(5, 180, 26, 15, 3, BackColor());
            display.drawRoundRect(5, 180, 26, 15, 3, ForeColor());
            uint16_t fg = ForeColor();
            display.drawLine(12, 182, 12, 192, fg);
            display.drawLine(12, 182, 16, 185, fg);
            display.drawLine(16, 185, 9, 190, fg);
            display.drawLine(12, 192, 16, 189, fg);
            display.drawLine(16, 189, 9, 184, fg);
            // Small 'x'
            display.drawLine(20, 184, 26, 190, fg);
            display.drawLine(26, 184, 20, 190, fg);
        }
    }
}

void OverrideGSR::InsertPostDraw() {
    screenFrameSeq++;
    lastDrawnMinute = WatchTime.Local.Minute;
}

extern bool UpdateDisp;

void OverrideGSR::syncMinuteRollover() {
    ClockSeconds();
    if (!isBleConnected()) {
        SuppressWifiWeather();
    }
    if (lastDrawnMinute < 0 || WatchTime.Local.Minute != lastDrawnMinute) {
        WatchTime.NewMinute = true;
        InsertOnMinute();
        UpdateScreen(true);
        showWatchFace();
        WatchTime.NewMinute = false;
        UpdateDisp = false;
    }
}

bool OverrideGSR::InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh) {
  if (isSuspended) {
      if (GetBatteryState() == 3 && SwitchNumber == 3) { // SW3 (Up)
          uiSM.setState(&watchfaceState);
          Haptic = true;
          Refresh = true;
      }
      return true; // Block other interactions while suspended
  }

  if (showingMedInfo || showingWeather || callingForHelp || helpRequested || wifiFailed) {
      if (SwitchNumber == 2) { // Back / SW2
          showingMedInfo = false;
          showingWeather = false;
          bleWeatherPending = false;
          if (callingForHelp) {
              endWiFi();
          }
          callingForHelp = false;
          helpRequested = false;
          wifiFailed = false;
          Haptic = true;
          Refresh = true;
          return true; // We handled the button press
      }
      if (showingWeather && SwitchNumber == 3) { // Up / SW3 -> Refresh weather via BLE Twin
          requestBleWeather();
          Haptic = true;
          Refresh = true;
          return true;
      }
  }
  
  return false;
}

void OverrideGSR::InsertHandleShowButtons() {
    uiSM.setState(&showButtonsState);
}

void OverrideGSR::InsertHandleFindPhone() {
    uiSM.setState(&findFoneState);
}

void OverrideGSR::InsertHandleMedInfo() {
    uiSM.setState(&medInfoState);
}

void OverrideGSR::InsertHandleWeather() {
    uiSM.setState(&weatherState);
}

void OverrideGSR::InsertHandleBleRemote() {
    uiSM.setState(&bleRemoteState);
}

void OverrideGSR::startBleHid() {
    endWiFi();
    bleLastAction = "Ready";
    bleStartTime = millis();
    if (!bleInitialized) {
        NimBLEDevice::init("Watchy4Granny");
        NimBLEDevice::setMTU(517);
        NimBLEDevice::setSecurityAuth(true, false, true);

        bleServer = NimBLEDevice::createServer();
        bleServer->setCallbacks(new WatchyBleServerCallbacks());

        bleHid = new NimBLEHIDDevice(bleServer);
        bleInputMedia = bleHid->inputReport(1);

        bleHid->manufacturer()->setValue("Watchy4Granny");
        bleHid->pnp(0x02, 0xe502, 0xa111, 0x0210);
        bleHid->hidInfo(0x00, 0x01);
        bleHid->reportMap((uint8_t*)hidMediaReportDescriptor, sizeof(hidMediaReportDescriptor));
        bleHid->startServices();

        // Digital Twin GATT Service (Time, Location, Weather, Status)
        NimBLEService* twinService = bleServer->createService(TWIN_SERVICE_UUID);
        WatchyTwinCharCallbacks* twinCallbacks = new WatchyTwinCharCallbacks();

        bleTwinTimeChar = twinService->createCharacteristic(
            TWIN_CHAR_TIME_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
        );
        bleTwinTimeChar->setCallbacks(twinCallbacks);
        setCharString(bleTwinTimeChar, formatTwinTime());

        bleTwinLocChar = twinService->createCharacteristic(
            TWIN_CHAR_LOC_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
        );
        bleTwinLocChar->setCallbacks(twinCallbacks);
        setCharString(bleTwinLocChar, formatTwinLocation());

        bleTwinWeatChar = twinService->createCharacteristic(
            TWIN_CHAR_WEAT_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
        );
        bleTwinWeatChar->setCallbacks(twinCallbacks);
        setCharString(bleTwinWeatChar, formatTwinWeather());

        bleTwinStatusChar = twinService->createCharacteristic(
            TWIN_CHAR_STATUS_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
        );
        bleTwinStatusChar->setCallbacks(twinCallbacks);
        setCharString(bleTwinStatusChar, formatTwinStatus());

        twinService->start();

        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->setAppearance(HID_KEYBOARD);
        pAdvertising->addServiceUUID(bleHid->hidService()->getUUID());
        pAdvertising->addServiceUUID(TWIN_SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->start();

        bleInitialized = true;
    } else {
        NimBLEDevice::startAdvertising();
    }
}

void OverrideGSR::stopBleHid() {
    bleMirrorMode = false;
    bleWeatherPending = false;
    screenReadMode = false;
    bleStartTime = 0;
    bleKeepAliveUntil = 0;
    weatherCompleteTime = 0;
    if (bleInitialized) {
        NimBLEDevice::stopAdvertising();
        if (bleServer && bleServer->getConnectedCount() > 0) {
            std::vector<uint16_t> peers = bleServer->getPeerDevices();
            for (uint16_t connId : peers) {
                bleServer->disconnect(connId);
            }
        }
        bleConnected = false;
    }
}

bool OverrideGSR::isBleRunning() {
    return bleStartTime > 0 && (showingBleRemote || bleMirrorMode || showingWeather || bleConnected);
}

bool OverrideGSR::isBleConnected() const {
    return bleConnected;
}

void OverrideGSR::refreshBleActivity() {
    bleStartTime = millis();
}

void OverrideGSR::tickBleTimeout() {
    if (bleStartTime == 0) return;

    if (bleConnected) {
        // When connected, disconnect only after 120 seconds of complete idle
        if (millis() - bleStartTime >= 120000) {
            stopBleHid();
            UpdateScreen();
        }
    } else {
        // Not connected (advertising): timeout after 30 seconds of no connection
        if (millis() - bleStartTime >= 30000) {
            stopBleHid();
            UpdateScreen();
        }
    }
}

void OverrideGSR::requestBleWeather() {
    refreshBleActivity();
    bleWeatherPending = true;
    bleWeatherReqSeq++;
    startBleHid();
    if (bleTwinStatusChar) {
        setCharString(bleTwinStatusChar, formatTwinStatus());
    }
}

void OverrideGSR::sendBleMediaKey(uint8_t mask, const char* label) {
    refreshBleActivity();
    if (!bleConnected || bleInputMedia == nullptr) {
        bleLastAction = "Not Connected";
        UpdateScreen();
        return;
    }
    uint8_t report = mask;
    bleInputMedia->setValue(&report, 1);
    bleInputMedia->notify();
    delay(30);
    report = 0x00;
    bleInputMedia->setValue(&report, 1);
    bleInputMedia->notify();

    bleLastAction = String(label);
    UpdateScreen();
}

static unsigned long lastWifiActivityTime = 0;

void OverrideGSR::recordWifiActivity() {
    lastWifiActivityTime = millis();
}

unsigned long OverrideGSR::getWifiIdleSeconds() const {
    if (lastWifiActivityTime == 0) return 0;
    return (millis() - lastWifiActivityTime) / 1000;
}

bool OverrideGSR::isBleWeatherPending() const {
    return bleWeatherPending;
}

void OverrideGSR::notifyWeatherComplete() {
    weatherCompleteTime = millis();
}

unsigned long OverrideGSR::getWeatherCompleteTime() const {
    return weatherCompleteTime;
}

void OverrideGSR::clearWeatherCompleteTime() {
    weatherCompleteTime = 0;
}

void OverrideGSR::extendBleKeepAlive(unsigned long ms) {
    unsigned long target = millis() + ms;
    if (target > bleKeepAliveUntil) {
        bleKeepAliveUntil = target;
    }
}

extern WebServer server;

void OverrideGSR::InsertAddWeb() {
    recordWifiActivity();

    // 1. GET /api/status - JSON Telemetry (battery, voltage, charging, steps, time, mode)
    server.on("/api/status", HTTP_GET, [this]() {
        recordWifiActivity();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        server.sendHeader("Connection", "close");
        ResetOTA();

        float voltage = getBatteryVoltage();
        int percentage = 0;
        if (voltage >= 4.20) percentage = 100;
        else if (voltage <= 3.30) percentage = 0;
        else percentage = (int)((voltage - 3.30) / (4.20 - 3.30) * 100.0);

        uint8_t currentMode = 0;
        if (showingBleRemote) currentMode = 1;
        else if (showingWeather) currentMode = 2;

        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"battery\":%d,\"voltage\":%.2f,\"charging\":%s,\"steps\":%lu,\"time\":\"%s\",\"frameSeq\":%lu,\"mode\":%u,\"weatherReqSeq\":%lu}",
            percentage,
            voltage,
            GetBatteryState() == 3 ? "true" : "false",
            (unsigned long)CurrentStepCount(),
            formatTwinTime().c_str(),
            (unsigned long)screenFrameSeq,
            currentMode,
            (unsigned long)bleWeatherReqSeq
        );
        server.send(200, "application/json", buf);
    });

    // 2. GET /api/screen - Raw 5000-byte 200x200 1bpp framebuffer
    server.on("/api/screen", HTTP_GET, [this]() {
        recordWifiActivity();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        server.sendHeader("Connection", "close");
        ResetOTA();

        const uint8_t* fb = getGxDisplayBuffer(display);
        server.send_P(200, "application/octet-stream", (const char*)fb, 5000);
    });

    // 3. POST /api/button?btn=1..4 (or body btn=X) - Virtual SW1-SW4 Button Press
    server.on("/api/button", HTTP_ANY, [this]() {
        recordWifiActivity();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        if (server.method() == HTTP_OPTIONS) {
            server.send(204);
            return;
        }
        server.sendHeader("Connection", "close");
        ResetOTA();

        int btn = 0;
        if (server.hasArg("btn")) {
            btn = server.arg("btn").toInt();
        } else if (server.hasArg("plain")) {
            String p = server.arg("plain");
            int idx = p.indexOf("btn=");
            if (idx >= 0) btn = p.substring(idx + 4).toInt();
            else btn = p.toInt();
        }

        if (btn >= 1 && btn <= 4) {
            Button = (uint8_t)btn;
            UpdateScreen(true);
            server.send(200, "application/json", "{\"ok\":true,\"button\":" + String(btn) + "}");
        } else {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid btn (1-4)\"}");
        }
    });

    // 4. POST /api/weather - Push weather payload: temp,feels,code,hum,wind,pres
    server.on("/api/weather", HTTP_ANY, [this]() {
        recordWifiActivity();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        if (server.method() == HTTP_OPTIONS) {
            server.send(204);
            return;
        }
        server.sendHeader("Connection", "close");
        ResetOTA();

        String body = server.arg("plain");
        if (body.length() == 0 && server.hasArg("data")) {
            body = server.arg("data");
        }

        float tempC = 0, feelsC = 0, windMs = 0;
        unsigned int cond = 0, hum = 0, pres = 1013;
        float elev = 0;
        if (sscanf(body.c_str(), "%f,%f,%u,%u,%f,%u,%f", &tempC, &feelsC, &cond, &hum, &windMs, &pres, &elev) >= 5) {
            bleWeatherPending = false;
            SetWeatherFromBLE(tempC, feelsC, (uint16_t)cond, (uint8_t)hum, windMs * 3.6f, (uint16_t)pres, elev);
            notifyWeatherComplete();
            UpdateScreen(true);
            server.send(200, "application/json", "{\"ok\":true}");
        } else {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid format. Expected: temp,feels,code,hum,wind,pres\"}");
        }
    });

    // 5. POST /api/time - Sync phone time: epoch[,timezone]
    server.on("/api/time", HTTP_ANY, [this]() {
        recordWifiActivity();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        if (server.method() == HTTP_OPTIONS) {
            server.send(204);
            return;
        }
        server.sendHeader("Connection", "close");
        ResetOTA();

        String body = server.arg("plain");
        if (body.length() == 0 && server.hasArg("data")) {
            body = server.arg("data");
        }

        long epoch = 0;
        char tzBuf[48] = {0};
        if (sscanf(body.c_str(), "%ld,%47s", &epoch, tzBuf) >= 1 && epoch > 1700000000) {
            SetTimeFromBLE((time_t)epoch, String(tzBuf));
            UpdateScreen(true);
            server.send(200, "application/json", "{\"ok\":true}");
        } else {
            server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid time format\"}");
        }
    });

    // 6. POST/GET /api/exit - Exit WiFi AP mode immediately
    server.on("/api/exit", HTTP_ANY, [this]() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        if (server.method() == HTTP_OPTIONS) {
            server.send(204);
            return;
        }
        server.sendHeader("Connection", "close");
        server.send(200, "application/json", "{\"ok\":true,\"message\":\"WiFi AP stopping\"}");
        server.client().flush();
        stopWatchyAP();
        CloseMenu();
        UpdateScreen(true);
    });

    // 7. GET /exit - Compatibility exit endpoint
    server.on("/exit", HTTP_ANY, [this]() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        if (server.method() == HTTP_OPTIONS) {
            server.send(204);
            return;
        }
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "OK");
        server.client().flush();
        stopWatchyAP();
        CloseMenu();
        UpdateScreen(true);
    });
}



