#include "PowerStateMachine.h"
#include "OverrideGSR.h"

// Define states
class AwakeState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
};

class SleepState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
};

// State instances
AwakeState awakeState;
SleepState sleepState;

// PowerStateMachine implementation
PowerStateMachine::PowerStateMachine(OverrideGSR* w) : StateMachine(&sleepState), watchy(w) {
}

void PowerStateMachine::handleWakeup() {
    setState(&awakeState);
}

bool PowerStateMachine::shouldSleep() {
    if (currentState == &awakeState) {
        // Here we would check for active flags in watchy
        // For now, if no button was pressed for a while
        // But we need to check if NTP is running, etc.
        
        // This is just a placeholder. Real logic should check ActiveMode flags.
        // return !watchy->IsActive(); // assuming such a method exists or we check flags
        return false; // Stay awake for now while we test
    }
    return false;
}

void PowerStateMachine::goToSleep() {
    setState(&sleepState);
}

void PowerStateMachine::update() {
    StateMachine::update();
}

// AwakeState implementation
void AwakeState::enter(StateMachine* sm) {
    // Initial wakeup logic
}

void AwakeState::execute(StateMachine* sm) {
    PowerStateMachine* psm = (PowerStateMachine*)sm;
    psm->watchy->tick();
}

void AwakeState::exit(StateMachine* sm) {
}

// SleepState implementation
void SleepState::enter(StateMachine* sm) {
    PowerStateMachine* psm = (PowerStateMachine*)sm;
    psm->watchy->deepSleep();
}

void SleepState::execute(StateMachine* sm) {
}

void SleepState::exit(StateMachine* sm) {
}
