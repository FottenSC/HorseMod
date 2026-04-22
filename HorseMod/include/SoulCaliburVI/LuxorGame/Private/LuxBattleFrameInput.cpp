#include "LuxBattleFrameInput.h"

ALuxBattleFrameInput::ALuxBattleFrameInput(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->CanUpdateInput = true;
    this->RepeatDelay = 15;
    this->RepeatInterval = 4;
}

void ALuxBattleFrameInput::OnTickWhenPaused() {
}


