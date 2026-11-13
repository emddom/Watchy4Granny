#include "UiStateMachine.h"
#include "OverrideGSR.h"

// State instances
WatchfaceState watchfaceState;
MenuState menuState;
MedInfoState medInfoState;
FindFoneState findFoneState;
SuspendState suspendState;
ShowButtonsState showButtonsState;
WeatherState weatherState;
BleRemoteState bleRemoteState;

// UiStateMachine implementation
UiStateMachine::UiStateMachine(OverrideGSR* w) : StateMachine(&watchfaceState), watchy(w) {
}

void UiStateMachine::update() {
    if (Button) {
        uint8_t b = Button;
        Button = 0;
        
        // Vibrate shortly when a SWx has been pushed
        watchy->VibeTo(true);
        delay(50);
        watchy->VibeTo(false);

        handleEvent(b);
    }

    // Auto-return to watchfaceState if menu was closed by WatchyGSR
    if (currentState == &menuState && watchy->NoMenu()) {
        setState(&watchfaceState);
    }

    StateMachine::update();
}

void UiStateMachine::handleEvent(int event) {
    if (currentState) {
        currentState->handleButton(this, (uint8_t)event);
    }
}

// WatchfaceState implementation
void WatchfaceState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->showWatchFace();
}

void WatchfaceState::execute(StateMachine* sm) {
}

void WatchfaceState::exit(StateMachine* sm) {
}

void WatchfaceState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    bool haptic = false;
    bool refresh = false;
    
    // Call the legacy InsertHandlePressed for now to maintain behavior
    if (usm->watchy->InsertHandlePressed(button, haptic, refresh)) {
        if (haptic) usm->watchy->VibeTo(true);
        // State might have changed inside InsertHandlePressed if we are not careful
        if (isSuspended) usm->setState(&suspendState);
        else if (showingMedInfo) usm->setState(&medInfoState);
        else if (callingForHelp) usm->setState(&findFoneState);
        else if (showingWeather) usm->setState(&weatherState);
        else if (showingBleRemote) usm->setState(&bleRemoteState);
        return;
    }

    // Fallback: pass button to the core WatchyGSR framework
    usm->watchy->handleMenuButton(button);
    
    // If the framework successfully opened the menu, update our state
    if (!usm->watchy->NoMenu()) {
        usm->setState(&menuState);
    }
}

// MenuState implementation
void MenuState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->ShowDefaultMenu();
}

void MenuState::execute(StateMachine* sm) {
}

void MenuState::exit(StateMachine* sm) {
}

void MenuState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    // Call the legacy menu handling logic
    usm->watchy->handleMenuButton(button);
}

// MedInfoState implementation
void MedInfoState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    showingMedInfo = true;
    usm->watchy->UpdateScreen();
}

void MedInfoState::execute(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->KeepDisplayAwake();
}

void MedInfoState::exit(StateMachine* sm) {
    showingMedInfo = false;
}

void MedInfoState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    if (button == 2) { // Back
        usm->setState(&watchfaceState);
    }
}

// FindFoneState implementation
void FindFoneState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    callingForHelp = true;
    usm->watchy->AskForWiFi();
}

void FindFoneState::execute(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->KeepDisplayAwake();
}

void FindFoneState::exit(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    callingForHelp = false;
    usm->watchy->endWiFi();
}

void FindFoneState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    if (button == 2) { // Back
        usm->setState(&watchfaceState);
    }
}

// SuspendState implementation
void SuspendState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    isSuspended = true;
    usm->watchy->UpdateScreen();
}

void SuspendState::execute(StateMachine* sm) {
}

void SuspendState::exit(StateMachine* sm) {
    isSuspended = false;
}

void SuspendState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    bool haptic = false;
    bool refresh = false;
    if (usm->watchy->InsertHandlePressed(button, haptic, refresh)) {
        if (haptic) usm->watchy->VibeTo(true);
        if (!isSuspended) {
            usm->setState(&watchfaceState);
        }
    }
}

// ShowButtonsState implementation
unsigned long showButtonsStartTime = 0;

void ShowButtonsState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    showingButtons = true;
    showButtonsStartTime = millis();
    usm->watchy->UpdateScreen();
}

void ShowButtonsState::execute(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    if (usm->watchy->getButtonPins() > 0) {
        showButtonsStartTime = millis(); // Reset timer if a button is still held
    }

    usm->watchy->KeepDisplayAwake();

    if (millis() - showButtonsStartTime >= 10000) {
        usm->setState(&watchfaceState);
    }
}

void ShowButtonsState::exit(StateMachine* sm) {
    showingButtons = false;
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->UpdateScreen();
}

void ShowButtonsState::handleButton(StateMachine* sm, uint8_t button) {
    // Ignore button presses for the first 1000ms to prevent bouncing from the menu selection exiting the state immediately
    if (millis() - showButtonsStartTime < 1000) return;

    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->setState(&watchfaceState);
}

// WeatherState implementation
void WeatherState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    showingWeather = true;
    if (!usm->watchy->IsWeatherAvailable()) {
        usm->watchy->StartWeather();
    }
    usm->watchy->UpdateScreen();
}

void WeatherState::execute(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->KeepDisplayAwake();
}

void WeatherState::exit(StateMachine* sm) {
    showingWeather = false;
}

void WeatherState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    if (button == 2) { // Back
        usm->setState(&watchfaceState);
    } else if (button == 3) { // Up -> manual refresh
        usm->watchy->StartWeather();
        usm->watchy->UpdateScreen();
    }
}

// BleRemoteState implementation
static unsigned long bleRemoteEnterTime = 0;

void BleRemoteState::enter(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    showingBleRemote = true;
    bleRemoteEnterTime = millis();
    usm->watchy->startBleHid();
    usm->watchy->UpdateScreen();
}

void BleRemoteState::execute(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->watchy->KeepDisplayAwake();
}

void BleRemoteState::exit(StateMachine* sm) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    showingBleRemote = false;
    usm->watchy->stopBleHid();
}

void BleRemoteState::handleButton(StateMachine* sm, uint8_t button) {
    UiStateMachine* usm = (UiStateMachine*)sm;
    if (button == 2) { // Back / SW2 -> Exit BLE Remote
        while (usm->watchy->getButtonPins() != 0) { delay(10); }
        usm->watchy->SetTurbo();
        Button = 0;
        usm->setState(&watchfaceState);
        return;
    } else if (button == 3) { // Up / SW3 -> Volume Up
        usm->watchy->sendBleMediaKey(0x01, "VOLUME UP");
    } else if (button == 4) { // Down / SW4 -> Volume Down
        usm->watchy->sendBleMediaKey(0x02, "VOLUME DOWN");
    } else if (button == 1 && (millis() - bleRemoteEnterTime > 500)) { // Menu / SW1 -> Play / Pause
        usm->watchy->sendBleMediaKey(0x04, "PLAY / PAUSE");
    }

    // Wait for button release so the background KeysCheck task cannot queue a duplicate press during e-paper refresh
    while (usm->watchy->getButtonPins() != 0) {
        delay(10);
    }
    usm->watchy->SetTurbo();
    Button = 0;
}
