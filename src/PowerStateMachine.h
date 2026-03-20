#ifndef POWERSTATEMACHINE_H
#define POWERSTATEMACHINE_H

#include "StateMachine.h"

class OverrideGSR; // Forward declaration

enum PowerStateEnum {
    PS_AWAKE,
    PS_SLEEP
};

class PowerStateMachine : public StateMachine {
public:
    PowerStateMachine(OverrideGSR* watchy);
    void handleWakeup();
    bool shouldSleep();
    void goToSleep();
    void update() override;

    OverrideGSR* watchy;
};

#endif // POWERSTATEMACHINE_H
