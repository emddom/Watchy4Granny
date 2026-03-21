#include "UiStateMachine.h"
#include "OverrideGSR.h"

// State instances
WatchfaceState watchfaceState;
MenuState menuState;
MedInfoState medInfoState;
FindFoneState findFoneState;
SuspendState suspendState;
ShowButtonsState showButtonsState;

// UiStateMachine implementation
UiStateMachine::UiStateMachine(OverrideGSR* w) : StateMachine(&watchfaceState), watchy(w) {
}

void UiStateMachine::update() {
    if (Button) {
        uint8_t b = Button;
        Button = 0;
        handleEvent(b);
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
        return;
    }

    if (button == 1) { // Menu
        usm->setState(&menuState);
    } else if (button == 2) { // Back
        usm->setState(&showButtonsState);
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
    UiStateMachine* usm = (UiStateMachine*)sm;
    usm->setState(&watchfaceState);
}
