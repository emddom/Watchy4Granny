#ifndef OVERRIDEGSR_H
#define OVERRIDEGSR_H

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

extern RTC_DATA_ATTR bool callingForHelp;
extern RTC_DATA_ATTR bool helpRequested;
extern RTC_DATA_ATTR bool wifiFailed;
extern RTC_DATA_ATTR bool showingMedInfo;
extern RTC_DATA_ATTR bool isSuspended;
extern RTC_DATA_ATTR bool showingButtons;
extern RTC_DATA_ATTR bool showingWeather;
extern bool showingBleRemote;
extern bool bleMirrorMode;
extern volatile uint8_t pendingBleMode;
extern RTC_DATA_ATTR time_t suspendTime;



class OverrideGSR : public WatchyGSR {
  public:
    OverrideGSR() : WatchyGSR() {}

    void InsertDefaults() override;
    void InsertInitWatchStyle(uint8_t StyleID) override;
    void InsertWiFi() override;
    void InsertWiFiEnding() override;
    void InsertOnMinute() override;
    void drawWatchFace() override;
    void InsertPostDraw() override;
    bool InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh) override;
    bool InsertNeedAwake(bool GoingAsleep) override;
    void InsertHandleShowButtons() override;
    void InsertHandleFindPhone() override;
    void InsertHandleMedInfo() override;
    void InsertHandleWeather() override;
    void InsertHandleBleRemote() override;

    void startBleHid();
    void stopBleHid();
    void sendBleMediaKey(uint8_t mask, const char* label);
};

#endif // OVERRIDEGSR_H
