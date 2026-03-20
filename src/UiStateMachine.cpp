#include "UiStateMachine.h"
#include "OverrideGSR.h"

// Forward declarations of states
class WatchfaceState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(UiStateMachine* usm, uint8_t button);
};

class MenuState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(UiStateMachine* usm, uint8_t button);
};

class MedInfoState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(UiStateMachine* usm, uint8_t button);
};

class FindFoneState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(UiStateMachine* usm, uint8_t button);
};

// State instances
WatchfaceState watchfaceState;
MenuState menuState;
MedInfoState medInfoState;
FindFoneState findFoneState;

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
    // Cast current state to handle button
    // This is a bit hacky, but works for now. 
    // A better way would be to have a virtual handleButton in State base class.
    if (currentState == &watchfaceState) watchfaceState.handleButton(this, (uint8_t)event);
    else if (currentState == &menuState) menuState.handleButton(this, (uint8_t)event);
    else if (currentState == &medInfoState) medInfoState.handleButton(this, (uint8_t)event);
    else if (currentState == &findFoneState) findFoneState.handleButton(this, (uint8_t)event);
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

void WatchfaceState::handleButton(UiStateMachine* usm, uint8_t button) {
    bool haptic = false;
    bool refresh = false;
    
    // Call the legacy InsertHandlePressed for now to maintain behavior
    if (usm->watchy->InsertHandlePressed(button, haptic, refresh)) {
        if (haptic) usm->watchy->VibeTo(true);
        // State might have changed inside InsertHandlePressed if we are not careful
        // but for now we manually check flags
        if (showingMedInfo) usm->setState(&medInfoState);
        else if (callingForHelp) usm->setState(&findFoneState);
        return;
    }

    if (button == 1) { // Menu
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

void MenuState::handleButton(UiStateMachine* usm, uint8_t button) {
    // Call the legacy menu handling logic
    usm->watchy->handleMenuButton(button);
    
    // Check if we exited the menu (GuiMode changed)
    // This is a bit of a bridge until GuiMode itself is part of the SM state
    // We can't easily access GuiMode here as it's a static in Watchy_GSR.cpp
    // but the WatchyGSR class has it as a member (actually it's a global in the .cpp)
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

void MedInfoState::handleButton(UiStateMachine* usm, uint8_t button) {
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

void FindFoneState::handleButton(UiStateMachine* usm, uint8_t button) {
    if (button == 2) { // Back
        usm->setState(&watchfaceState);
    }
}
