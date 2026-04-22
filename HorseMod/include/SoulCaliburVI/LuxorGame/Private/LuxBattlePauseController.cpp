#include "LuxBattlePauseController.h"

ALuxBattlePauseController::ALuxBattlePauseController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->StepMode = ELuxBattleStepMode::Select;
}

void ALuxBattlePauseController::OnTickWhenPaused() {
}


