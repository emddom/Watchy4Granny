#ifndef UISTATEMACHINE_H
#define UISTATEMACHINE_H

#include "StateMachine.h"

class OverrideGSR; // Forward declaration

enum UiStateEnum {
    US_WATCHFACE,
    US_MENU,
    US_MEDINFO,
    US_FINDFONE
};

class UiStateMachine : public StateMachine {
public:
    UiStateMachine(OverrideGSR* watchy);
    void handleEvent(int event);
    void update() override;

    OverrideGSR* watchy;
};

#endif // UISTATEMACHINE_H
