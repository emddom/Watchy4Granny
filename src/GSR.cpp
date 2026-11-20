#include <Arduino.h>
#include "OverrideGSR.h"
#include "PowerStateMachine.h"
#include "UiStateMachine.h"

OverrideGSR watchy;
PowerStateMachine powerSM(&watchy);
UiStateMachine uiSM(&watchy);

void setup(){
  watchy.init();
  if (!watchy.isBleConnected()) {
      watchy.SuppressWifiWeather();
      watchy.SetWeatherInterval(0);
  }
  if (lastDrawnMinute < 0 || WatchTime.Local.Minute != lastDrawnMinute) {
      WatchTime.NewMinute = true;
      watchy.InsertOnMinute();
      bool doFull = (lastDrawnMinute < 0 || WatchTime.Local.Minute == 0);
      watchy.UpdateScreen(doFull);
      watchy.showWatchFace();
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
