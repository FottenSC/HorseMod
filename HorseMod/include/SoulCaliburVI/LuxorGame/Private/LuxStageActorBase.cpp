#include "LuxStageActorBase.h"

ALuxStageActorBase::ALuxStageActorBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->PS4 = true;
    this->PS4Pro = true;
    this->XBoxOne = true;
    this->XboxOneX = true;
}

void ALuxStageActorBase::SetTimeDilation(float inTimeDilation) {
}


void ALuxStageActorBase::Initialize() {
}


