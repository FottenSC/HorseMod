#include "LuxBattleCommonInput.h"

ALuxBattleCommonInput::ALuxBattleCommonInput(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->CanUpdateInput = true;
    this->RepeatDelay = 15;
    this->RepeatInterval = 4;
}

void ALuxBattleCommonInput::OnTickWhenPaused() {
}


