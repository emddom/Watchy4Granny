#include <Arduino.h>
#include "OverrideGSR.h"
#include "PowerStateMachine.h"
#include "UiStateMachine.h"

OverrideGSR watchy;
PowerStateMachine powerSM(&watchy);
UiStateMachine uiSM(&watchy);

void setup(){
  watchy.init();
  powerSM.handleWakeup();
}

void loop(){
  powerSM.update();
  uiSM.update();

  if (powerSM.shouldSleep()) {
    powerSM.goToSleep();
  }
}
