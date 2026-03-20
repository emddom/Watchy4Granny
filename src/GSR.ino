#include "Watchy_GSR.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#if __has_include("GSRWatchFaceBallsy.h")
#include "GSRWatchFaceBallsy.h"
#endif
#if __has_include("GSRWatchFaceLCD.h")
#include "GSRWatchFaceLCD.h"
#endif
#if __has_include("WeatherIcons.h")
#include "WeatherIcons.h"
#if __has_include("WatchyClassicsAddOn.h")
#include "WatchyClassicsAddOn.h"
#endif
#endif

// Place all of your data and variables here.

RTC_DATA_ATTR bool callingForHelp = false;
RTC_DATA_ATTR bool helpRequested = false;
RTC_DATA_ATTR bool wifiFailed = false;
RTC_DATA_ATTR bool showingMedInfo = false;

class OverrideGSR : public WatchyGSR {
  public:
    OverrideGSR() : WatchyGSR() {}

    void InsertWiFi() override {
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

    void InsertWiFiEnding() override {
      if (callingForHelp) {
        // This is called if WiFi fails to connect
        callingForHelp = false;
        wifiFailed = true;
        UpdateScreen(); // Force screen redraw to show error
      }
    }

    void InsertOnMinute() override {
      // Clear the messages when the minute changes so the normal clock returns
      if (helpRequested || wifiFailed || showingMedInfo) {
         helpRequested = false;
         wifiFailed = false;
         showingMedInfo = false;
         UpdateScreen();
      }
    }

    void drawWatchFace() override {
        // First, call the base drawing logic so the watch renders normally underneath
        WatchyGSR::drawWatchFace();
        
        // Overlay our custom screens based on state
        if (showingMedInfo) {
            display.fillScreen(BackColor());
            display.setFont(&aAntiCorona16pt7b);
            display.setTextColor(ForeColor());
            display.setCursor(10, 30);
            display.print("MEDICAL INFO");
            
            display.setFont(&aAntiCorona11pt7b);
            display.setCursor(10, 70);
            display.print("Name: Jane Doe");
            display.setCursor(10, 100);
            display.print("ICE: 555-123-4567");
            display.setCursor(10, 130);
            display.print("Allergies: Penicillin");
            display.setCursor(10, 160);
            display.print("Meds: Aspirin daily");
            display.setCursor(10, 190);
            display.print("Blood Type: O-");
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

    bool InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh) override {
      if (SwitchNumber == 3) { // Up / SW3
          showingMedInfo = !showingMedInfo; // Toggle med info
          Haptic = true;
          Refresh = true;
          return true;
      } else if (SwitchNumber == 4) { // Down / SW4
          callingForHelp = true;
          helpRequested = false;
          wifiFailed = false;
          showingMedInfo = false;
          Haptic = true;
          Refresh = true;
          AskForWiFi(); // This asks WatchyGSR to connect to WiFi and call InsertWiFi()
          return true;
      }
      return false;
    }
};

// Do not edit anything below this, leave all of your code above.
OverrideGSR watchy;

void setup(){
  watchy.init();
}

void loop(){}
