#include "UIFsmMachine.h"

UUIFsmMachine::UUIFsmMachine() {
    this->CurrentState = NULL;
    this->PreviousState = NULL;
}

UUIFsmState* UUIFsmMachine::GetPreviousState() const {
    return NULL;
}

UUIFsmState* UUIFsmMachine::GetCurrentState() const {
    return NULL;
}

void UUIFsmMachine::ChangeState(const FString& StateCode) {
}


