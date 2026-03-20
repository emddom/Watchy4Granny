#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include <Arduino.h>

class StateMachine;

class State {
public:
    virtual void enter(StateMachine* sm) = 0;
    virtual void execute(StateMachine* sm) = 0;
    virtual void exit(StateMachine* sm) = 0;
    virtual ~State() {}
};

class StateMachine {
public:
    StateMachine(State* initialState) : currentState(initialState) {}
    void setState(State* newState) {
        if (currentState) {
            currentState->exit(this);
        }
        currentState = newState;
        if (currentState) {
            currentState->enter(this);
        }
    }
    virtual void update() {
        if (currentState) {
            currentState->execute(this);
        }
    }
    State* getCurrentState() { return currentState; }
protected:
    State* currentState;
};

#endif // STATEMACHINE_H
