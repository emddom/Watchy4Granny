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

extern bool ActiveMode;

bool PowerStateMachine::shouldSleep() {
    if (currentState == &awakeState) {
        return !ActiveMode;
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
