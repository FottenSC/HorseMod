#include "AnimNode_StateMachine.h"

FAnimNode_StateMachine::FAnimNode_StateMachine() {
    this->StateMachineIndexInClass = 0;
    this->MaxTransitionsPerFrame = 0;
    this->bSkipFirstUpdateTransition = false;
    this->CurrentState = 0;
    this->ElapsedTime = 0.00f;
}

