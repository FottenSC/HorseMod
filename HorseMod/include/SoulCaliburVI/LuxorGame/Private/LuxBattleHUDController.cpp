#include "LuxBattleHUDController.h"

ALuxBattleHUDController::ALuxBattleHUDController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->BattleAnnounce = NULL;
    this->DevBattleHUDSetting = NULL;
}

void ALuxBattleHUDController::OnCommandPlayStarted() {
}

void ALuxBattleHUDController::OnCommandPlayEnded() {
}


