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


class OverrideGSR : public WatchyGSR {
  public:
    OverrideGSR() : WatchyGSR() {}

    void InsertWiFi() override;
    void InsertWiFiEnding() override;
    void InsertOnMinute() override;
    void drawWatchFace() override;
    bool InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh) override;
};

#endif // OVERRIDEGSR_H
