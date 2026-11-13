#ifndef UISTATEMACHINE_H
#define UISTATEMACHINE_H

#include "StateMachine.h"

class OverrideGSR; // Forward declaration

enum UiStateEnum {
    US_WATCHFACE,
    US_MENU,
    US_MEDINFO,
    US_FINDFONE,
    US_SUSPEND,
    US_SHOW_BUTTONS,
    US_WEATHER,
    US_BLE_REMOTE
};

class WatchfaceState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class MenuState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class MedInfoState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class FindFoneState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class SuspendState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class ShowButtonsState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class WeatherState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class BleRemoteState : public State {
public:
    void enter(StateMachine* sm) override;
    void execute(StateMachine* sm) override;
    void exit(StateMachine* sm) override;
    void handleButton(StateMachine* sm, uint8_t button) override;
};

class UiStateMachine : public StateMachine {
public:
    UiStateMachine(OverrideGSR* watchy);
    void handleEvent(int event);
    void update() override;

    OverrideGSR* watchy;
};

extern UiStateMachine uiSM;
extern WatchfaceState watchfaceState;
extern MenuState menuState;
extern MedInfoState medInfoState;
extern FindFoneState findFoneState;
extern SuspendState suspendState;
extern ShowButtonsState showButtonsState;
extern WeatherState weatherState;
extern BleRemoteState bleRemoteState;

#endif // UISTATEMACHINE_H
