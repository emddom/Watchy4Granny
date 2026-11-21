#include <Arduino.h>
#include "OverrideGSR.h"
#include "PowerStateMachine.h"
#include "UiStateMachine.h"
#include "SmallRTC.h"

extern bool ActiveMode;
extern bool UpdateDisp;

OverrideGSR watchy;
PowerStateMachine powerSM(&watchy);
UiStateMachine uiSM(&watchy);

void setup(){
  Serial.begin(115200);
  delay(10);
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.printf("\n--- BOOT / WAKEUP (cause=%d) ---\n", wakeup_reason);

  watchy.init();
  if (!watchy.isBleConnected()) {
      watchy.SuppressWifiWeather();
      watchy.SetWeatherInterval(0);
  }

  if (lastDrawnMinute < 0 || WatchTime.Local.Minute != lastDrawnMinute) {
      WatchTime.NewMinute = true;
      watchy.InsertOnMinute();
      watchy.UpdateScreen(true);
      watchy.showWatchFace();
      WatchTime.NewMinute = false;
      UpdateDisp = false;
  }
  powerSM.handleWakeup();
}

void loop(){
  powerSM.update();
  uiSM.update();

  if (powerSM.shouldSleep()) {
    powerSM.goToSleep();
  }
}
