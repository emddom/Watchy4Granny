#include "OverrideGSR.h"
#include "UiStateMachine.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

RTC_DATA_ATTR bool callingForHelp = false;
RTC_DATA_ATTR bool helpRequested = false;
RTC_DATA_ATTR bool wifiFailed = false;
RTC_DATA_ATTR bool showingMedInfo = false;
RTC_DATA_ATTR bool isSuspended = false;
RTC_DATA_ATTR bool showingButtons = false;
RTC_DATA_ATTR bool showingWeather = false;
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
static uint32_t screenFrameSeq = 1;
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
    if (!uiSM.watchy) return "0,0.00,0,0,0,0";
    float v = WatchyGSR::getBatteryVoltage();
    int pct = 0;
    if (v >= 4.20f) pct = 100;
    else if (v <= 3.30f) pct = 0;
    else pct = (int)((v - 3.30f) / (4.20f - 3.30f) * 100.0f);
    uint32_t steps = uiSM.watchy->CurrentStepCount();
    int chg = (uiSM.watchy->GetBatteryState() == 3) ? 1 : 0;
    int mode = showingBleRemote ? 1 : 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d,%.2f,%lu,%d,%lu,%d",
             pct, v, (unsigned long)steps, chg, (unsigned long)screenFrameSeq, mode);
    return String(buf);
}

static void setCharString(NimBLECharacteristic* pChar, const String& s) {
    if (pChar) {
        pChar->setValue((const uint8_t*)s.c_str(), s.length());
    }
}

class WatchyTwinCharCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pChar) {
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
            if (sscanf(val.c_str(), "%f,%f,%u,%u,%f,%u", &tempC, &feelsC, &cond, &hum, &windMs, &pres) >= 5) {
                uiSM.watchy->SetWeatherFromBLE(tempC, feelsC, (uint16_t)cond, (uint8_t)hum, windMs * 3.6f, (uint16_t)pres);
                setCharString(pChar, formatTwinWeather());
                bleLastAction = "Weather Sync";
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
        if ((showingBleRemote || bleMirrorMode) && pServer->getConnectedCount() < 3) {
            NimBLEDevice::startAdvertising();
        }
        if (uiSM.watchy) uiSM.watchy->UpdateScreen();
    }
    void onDisconnect(NimBLEServer* pServer) {
        bleConnected = (pServer->getConnectedCount() > 0);
        bleLastAction = bleConnected ? "Connected" : "Disconnected";
        if (!bleConnected && bleMirrorMode) {
            bleMirrorMode = false;
        }
        if (showingBleRemote) {
            NimBLEDevice::startAdvertising();
        }
        if (uiSM.watchy) uiSM.watchy->UpdateScreen();
    }
};

void OverrideGSR::InsertDefaults() {
    SetWeatherScale(true); // Default to Metric (Celsius, m/s)
}

void OverrideGSR::InsertInitWatchStyle(uint8_t StyleID) {
    WantWeather(true);
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

  // Clear the messages when the minute changes so the normal clock returns
  if (helpRequested || wifiFailed || showingMedInfo || showingButtons || (showingWeather && !IsWeatherLoading())) {
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
    if (showingBleRemote || bleMirrorMode) {
        KeepDisplayAwake();
        return true;
    }
    if (showingWeather && IsWeatherLoading()) return true;
    if (isSuspended && GetBatteryState() == 3) {
        return true; // Keep awake when charging to animate blinking character
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

        String title = IsWeatherLoading() ? "WEATHER *" : "WEATHER";
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((200 - w) / 2, 24);
        display.print(title);

        if (!IsWeatherReady()) {
            display.setFont(&aAntiCorona12pt7b);
            if (IsWeatherLoading()) {
                display.setCursor(20, 95);
                display.print("FETCHING...");
                display.setCursor(15, 130);
                display.print("PLEASE WAIT");
            } else {
                display.setCursor(20, 85);
                display.print("NO DATA YET");
                display.setFont(&aAntiCorona10pt7b);
                display.setCursor(15, 120);
                display.print("Check WiFi or");
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
            display.setCursor(64, 60);
            display.print(tempStr);

            auto getConditionLabel = [](uint16_t id) -> String {
                if (id == 0 || id == 800) return "Clear";
                if (id == 1 || id == 2 || id == 801) return "Partly Cloudy";
                if (id == 3 || id > 801) return "Overcast";
                if (id == 45 || id == 48 || (id >= 700 && id < 800)) return "Fog / Mist";
                if ((id >= 51 && id <= 57) || (id >= 300 && id < 400)) return "Drizzle";
                if ((id >= 61 && id <= 65) || (id >= 80 && id <= 82) || (id >= 500 && id < 600)) return "Rain";
                if ((id >= 66 && id <= 77) || id == 85 || id == 86 || (id >= 600 && id < 700)) return "Snow";
                if (id >= 95 || (id >= 200 && id < 300)) return "Thunderstorm";
                return "Code " + String(id);
            };

            display.setFont(&aAntiCorona10pt7b);
            display.setCursor(5, 90);
            display.print("Sky: " + getConditionLabel(cond));

            display.setCursor(5, 115);
            display.print("Feels Like: " + String(GetWeatherTemperatureFeelsLike()) + (IsMetric() ? " C" : " F"));

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

        display.setCursor(5, 162);
        display.print("SW3: Vol+  SW4: Vol-");
        display.setCursor(5, 188);
        display.print("SW1: Play  SW2: Exit");
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
    }
}

void OverrideGSR::InsertPostDraw() {
    screenFrameSeq++;
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
      if (showingWeather && SwitchNumber == 3) { // Up / SW3 -> Refresh weather
          StartWeather();
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
    screenReadMode = false;
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

void OverrideGSR::sendBleMediaKey(uint8_t mask, const char* label) {
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


