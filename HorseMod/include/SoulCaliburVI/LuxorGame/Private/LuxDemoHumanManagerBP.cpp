#include "LuxDemoHumanManagerBP.h"

ALuxDemoHumanManagerBP::ALuxDemoHumanManagerBP(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->DemoPlaySpeed = 1.00f;
    this->bUseStageCollision = false;
    this->BattleSetup = NULL;
}

void ALuxDemoHumanManagerBP::Prepare() {
}


